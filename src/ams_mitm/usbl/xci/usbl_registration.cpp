/*
 * Copyright (c) 2026 NedcloarBR
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stratosphere.hpp>
#include <atomic>
#include "usbl_registration.hpp"
#include "usbl_content_index.hpp"
#include "usbl_content_meta.hpp"
#include "usbl_known_contents.hpp"
#include "usbl_es_shim.h"
#include "usbl_ns_shim.h"
#include "../storage/usbl_volume.hpp"
#include "usbl_hidden_games.hpp"
#include "../usbl_config.hpp"
#include "../usbl_log.hpp"
#include "../usbl_results.hpp"

namespace ams::mitm::usbl::xci {

    namespace {

        constexpr s32 MaxApplicationCount = 32;

        /* With the drive already attached at boot, indexing finishes before ns activates the SD card storage. */
        constexpr s32 DatabaseOpenAttempts = 60;
        constexpr TimeSpan DatabaseOpenRetryInterval = TimeSpan::FromSeconds(1);

        /* Value installers push for installed content (Awoo Installer: 0x3 Installed, 0x5 GamecardMissing, 0xB Archived). */
        constexpr u8 ApplicationRecordInstalled = 3;

        struct ContentStorageRecord {
            ncm::ContentMetaKey key;
            u8 storage_id;
            u8 padding[7];
        };
        static_assert(sizeof(ContentStorageRecord) == 0x18);

        constexpr size_t MaxListedTickets = 0x800;

        /* Rights ids are only listed, never read: the ticket itself stays inside es. */
        bool HasTicket(::Service &es, const fs::RightsId &rights_id) {
            static constinit fs::RightsId s_rights_ids[MaxListedTickets];

            for (const auto list : { ::esListCommonTicketRightsIds, ::esListPersonalizedTicketRightsIds }) {
                s32 count = 0;
                if (R_FAILED(list(std::addressof(es), std::addressof(count), s_rights_ids, sizeof(s_rights_ids)))) {
                    continue;
                }
                if (std::find(s_rights_ids, s_rights_ids + std::min<s32>(count, MaxListedTickets), rights_id) != s_rights_ids + std::min<s32>(count, MaxListedTickets)) {
                    return true;
                }
            }
            return false;
        }

        /* eShop content needs its ticket in es (on the emuMMC when one is active), as an installer would import it. */
        Result ImportTicketIfMissing(const IndexedContent &content, const char *owner) {
            R_SUCCEED_IF(content.rights_id == fs::InvalidRightsId);

            os::NativeHandle handle;
            R_TRY(sm::GetServiceHandle(std::addressof(handle), sm::ServiceName::Encode("es")));
            ::Service es;
            ::serviceCreate(std::addressof(es), handle);
            ON_SCOPE_EXIT { ::serviceClose(std::addressof(es)); };

            if (HasTicket(es, content.rights_id)) {
                Log("register %016" PRIx64 ": %s already in es", content.program_id.value, owner);
                R_SUCCEED();
            }

            static constinit TicketData s_ticket = {};
            R_TRY(ReadContentTicket(std::addressof(s_ticket), content.content_id));
            R_TRY(::esImportTicket(std::addressof(es), s_ticket.ticket, s_ticket.ticket_size, s_ticket.cert, s_ticket.cert_size));

            Log("register %016" PRIx64 ": %s imported", content.program_id.value, owner);
            R_SUCCEED();
        }

        bool IsAutoRegisterEnabled() {
            return IsConfigEnabled(ConfigKey::AutoRegister);
        }

        template<typename Predicate>
        s32 CollectMetaContents(IndexedContent *out, s32 max_count, Predicate predicate) {
            std::scoped_lock lk(GetVolumeLock());

            s32 count = 0;
            for (s32 i = 0; i < GetIndexedContentCount() && count < max_count; ++i) {
                IndexedContent content;
                if (R_SUCCEEDED(GetIndexedContent(std::addressof(content), i)) && content.content_type == NcaContentType::Meta && predicate(content.program_id)) {
                    out[count++] = content;
                }
            }
            return count;
        }

        /* Registering a title with a missing NCA would leave a "corrupted data" entry on the HOME menu. With `drive`, every
         * content must also be on that drive: a game's update and add-on contents are only used from the game's own drive. */
        bool AreAllContentsIndexed(const ncm::PackagedContentMetaReader &reader, s32 drive = -1) {
            std::scoped_lock lk(GetVolumeLock());

            for (size_t i = 0; i < reader.GetContentCount(); ++i) {
                IndexedContent content;
                const char *xci_path;
                /* An update's delta fragments only serve to build it from the previous version; ncm drops them from the meta. */
                const auto *info = reader.GetContentInfo(i);
                if (info->GetType() == ncm::ContentType::DeltaFragment) {
                    continue;
                }
                if (R_FAILED(FindIndexedContent(std::addressof(content), std::addressof(xci_path), info->GetId())) || (drive >= 0 && content.drive != drive)) {
                    return false;
                }
            }
            return true;
        }

        /* Returns the record's last event, or nullopt when ns has no record for the application. Requires nsInitialize. */
        util::optional<u8> FindApplicationRecordEvent(ncm::ApplicationId application_id) {
            constexpr s32 PageSize = 32;
            ::NsApplicationRecord records[PageSize];

            for (s32 offset = 0; ; offset += PageSize) {
                s32 count = 0;
                if (R_FAILED(::nsListApplicationRecord(records, PageSize, offset, std::addressof(count))) || count <= 0) {
                    return util::nullopt;
                }

                for (s32 i = 0; i < count; ++i) {
                    if (records[i].application_id == application_id.value) {
                        return records[i].last_event;
                    }
                }
            }
        }

        Result OpenSdCardContentMetaDatabase(ncm::ContentMetaDatabase *out) {
            for (s32 attempt = 1; ; ++attempt) {
                const Result result = ncm::OpenContentMetaDatabase(out, ncm::StorageId::SdCard);
                if (R_SUCCEEDED(result) || !ncm::ResultSdCardContentMetaDatabaseNotActive::Includes(result) || attempt == DatabaseOpenAttempts) {
                    R_RETURN(result);
                }
                os::SleepThread(DatabaseOpenRetryInterval);
            }
        }

        /* Applications whose record was pushed since boot; ns keeps them "present" across later detach/attach cycles. */
        constinit ncm::ApplicationId g_pushed_applications[MaxApplicationCount] = {};
        constinit s32 g_pushed_count = 0;

        bool WasPushedSinceBoot(ncm::ApplicationId application_id) {
            return std::find(g_pushed_applications, g_pushed_applications + g_pushed_count, application_id) != g_pushed_applications + g_pushed_count;
        }

        void RememberPushed(ncm::ApplicationId application_id) {
            if (!WasPushedSinceBoot(application_id) && g_pushed_count < MaxApplicationCount) {
                g_pushed_applications[g_pushed_count++] = application_id;
            }
        }

        void ForgetPushed(ncm::ApplicationId application_id) {
            g_pushed_count = static_cast<s32>(std::remove(g_pushed_applications, g_pushed_applications + g_pushed_count, application_id) - g_pushed_applications);
        }

        Result PushApplicationRecord(ncm::ApplicationId application_id, const ContentStorageRecord *records, s32 record_count) {
            R_TRY(::nsInitialize());
            ON_SCOPE_EXIT { ::nsExit(); };

            /* Pushed even when the record already says Installed: ns computes "all contents present" at boot */
            /* and only recomputes it on a push, so a drive connected after boot would otherwise stay "corrupted". */
            if (const auto event = FindApplicationRecordEvent(application_id); event.has_value()) {
                Log("register %016" PRIx64 ": ns record found (last event %u), pushing again", application_id.value, *event);
            }

            ::Service app_manager;
            R_TRY(::nsGetApplicationManagerInterface(std::addressof(app_manager)));
            ON_SCOPE_EXIT { ::serviceClose(std::addressof(app_manager)); };

            /* The HOME menu keeps icons in ns' control cache; drop it so the icon from fs_mitm (badged or not) is read. Before
             * the push, because HOME reloads the icon as soon as the record changes; after it too, in case ns cached it
             * again meanwhile. */
            const auto invalidate = [&]() {
                if (const Result result = ::nsamInvalidateApplicationControlCache(std::addressof(app_manager), application_id.value); R_FAILED(result)) {
                    Log("register %016" PRIx64 ": control cache not invalidated 2%03d-%04d", application_id.value, result.GetModule(), result.GetDescription());
                }
            };
            invalidate();
            R_TRY(::nsamPushApplicationRecord(std::addressof(app_manager), application_id.value, ApplicationRecordInstalled, records, record_count * sizeof(*records)));
            invalidate();
            RememberPushed(application_id);
            R_SUCCEED();
        }

        struct RequiredVersions {
            u32 system;
            u32 application;

            constexpr bool operator==(const RequiredVersions &) const = default;
        };

        template<typename Reader>
        RequiredVersions GetRequiredVersions(const Reader &reader, ncm::ContentMetaType type) {
            switch (type) {
                case ncm::ContentMetaType::Application: {
                    const auto *header = reader.template GetExtendedHeader<ncm::ApplicationMetaExtendedHeader>();
                    return { header->required_system_version, header->required_application_version };
                }
                case ncm::ContentMetaType::Patch:
                    return { reader.template GetExtendedHeader<ncm::PatchMetaExtendedHeader>()->required_system_version, 0 };
                default:
                    return {};
            }
        }

        /* Like DBI, the registered meta drops the firmware the game was built for: ns would otherwise ask for a system */
        /* update (2016-0170) although Atmosphère has every key the game needs. The game's own version requirement stays. */
        RequiredVersions GetRegisteredRequiredVersions(const ncm::PackagedContentMetaReader &reader) {
            return { 0, GetRequiredVersions(reader, reader.GetKey().type).application };
        }

        /* nullopt when the database has no readable meta for the key. */
        util::optional<RequiredVersions> GetStoredRequiredVersions(ncm::ContentMetaDatabase &db, const ncm::ContentMetaKey &key) {
            constexpr size_t MaxMetaSize = 16_KB;
            static constinit u8 s_meta[MaxMetaSize];

            size_t size = 0;
            if (R_FAILED(db.Get(std::addressof(size), s_meta, sizeof(s_meta), key)) || size < sizeof(ncm::ContentMetaHeader)) {
                return util::nullopt;
            }

            const ncm::ContentMetaReader stored(s_meta, size);
            return GetRequiredVersions(stored, key.type);
        }

        void ClearRequiredSystemVersion(void *meta, size_t size, ncm::ContentMetaType type) {
            const ncm::ContentMetaReader converted(meta, size);
            switch (type) {
                case ncm::ContentMetaType::Application: const_cast<ncm::ApplicationMetaExtendedHeader *>(converted.GetExtendedHeader<ncm::ApplicationMetaExtendedHeader>())->required_system_version = 0; break;
                case ncm::ContentMetaType::Patch:       const_cast<ncm::PatchMetaExtendedHeader *>(converted.GetExtendedHeader<ncm::PatchMetaExtendedHeader>())->required_system_version = 0;             break;
                default:                                break;
            }
        }

        Result WriteContentMeta(ncm::ContentMetaDatabase &db, const ncm::ContentMetaKey &key, ncm::PackagedContentMetaReader &reader, const IndexedContent &meta_content) {
            /* The packaged meta does not list its own NCA; installers append it as the Meta content. */
            const auto meta_info = ncm::ContentInfo::Make(meta_content.content_id, meta_content.size, fs::ContentAttributes_None, ncm::ContentType::Meta);

            const size_t size = reader.CalculateConvertContentMetaSize();
            std::unique_ptr<u8[]> buffer(new (std::nothrow) u8[size]);
            R_UNLESS(buffer != nullptr, ncm::ResultAllocationFailed());
            reader.ConvertToContentMeta(buffer.get(), size, meta_info);
            ClearRequiredSystemVersion(buffer.get(), size, key.type);

            R_TRY(db.Set(key, buffer.get(), size));
            R_RETURN(db.Commit());
        }

        /* Writes one meta (the application's or its update's) unless the SD card database already has it unchanged. */
        Result EnsureContentMeta(bool *out_written, ncm::ContentMetaDatabase &db, ncm::PackagedContentMetaReader &reader, const IndexedContent &meta_content) {
            const auto key = reader.GetKey();

            bool registered = false;
            R_TRY(db.Has(std::addressof(registered), key));
            if (registered && GetStoredRequiredVersions(db, key) == GetRegisteredRequiredVersions(reader)) {
                Log("register %016" PRIx64 ": content meta v%u already in SD ncm database", key.id, key.version);
                *out_written = false;
                R_SUCCEED();
            }

            R_TRY(WriteContentMeta(db, key, reader, meta_content));
            const u32 game_required_system_version = GetRequiredVersions(reader, key.type).system;
            Log("register %016" PRIx64 ": content meta v%u %s SD ncm database (required firmware %u.%u.%u dropped)", key.id, key.version, registered ? "rewritten in" : "written to",
                game_required_system_version >> 26, (game_required_system_version >> 20) & 0x3F, (game_required_system_version >> 16) & 0xF);
            *out_written = true;
            R_SUCCEED();
        }

        Result EnsureTicket(const IndexedContent &content, const char *owner) {
            if (const Result result = ImportTicketIfMissing(content, owner); R_FAILED(result)) {
                Log("register %016" PRIx64 ": %s not imported 2%03d-%04d", content.program_id.value, owner, result.GetModule(), result.GetDescription());
                R_THROW(result);
            }
            R_SUCCEED();
        }

        Result EnsureProgramTicket(ncm::ProgramId program_id, ContentLayer layer) {
            IndexedContent program;
            {
                std::scoped_lock lk(GetVolumeLock());
                const char *path;
                R_TRY(FindIndexedContent(std::addressof(program), std::addressof(path), program_id, NcaContentType::Program, layer));
            }
            R_RETURN(EnsureTicket(program, program.is_patch ? "update ticket" : "ticket"));
        }

        /* An add-on content's ticket rides with whichever of its NCAs carries a rights id. */
        Result EnsureAddOnContentTicket(const ncm::PackagedContentMetaReader &reader) {
            for (size_t i = 0; i < reader.GetContentCount(); ++i) {
                IndexedContent content;
                {
                    std::scoped_lock lk(GetVolumeLock());
                    const char *path;
                    R_TRY(FindIndexedContent(std::addressof(content), std::addressof(path), reader.GetContentInfo(i)->GetId()));
                }
                if (content.rights_id != fs::InvalidRightsId) {
                    R_RETURN(EnsureTicket(content, "add-on content ticket"));
                }
            }
            R_SUCCEED();
        }

        /* An update that cannot be registered must not be served either: the base game would get its code and RomFS. */
        void DropPatch(ncm::ProgramId application_id) {
            {
                std::scoped_lock lk(GetVolumeLock());
                RemoveIndexedPatch(application_id);
            }
            ForgetKnownPatch(application_id);
        }

        constexpr ContentStorageRecord MakeSdCardRecord(const ncm::ContentMetaKey &key) {
            return { .key = key, .storage_id = static_cast<u8>(ncm::StorageId::SdCard), .padding = {} };
        }

        /* The update's meta, when the update is indexed with every content it lists. */
        Result RegisterPatch(util::optional<ncm::ContentMetaKey> *out_key, bool *out_written, ncm::ContentMetaDatabase &db, ncm::ProgramId application_id, s32 drive) {
            const ncm::ProgramId patch_id = GetPatchId(application_id);

            IndexedContent meta_content;
            {
                std::scoped_lock lk(GetVolumeLock());
                const char *path;
                R_SUCCEED_IF(R_FAILED(FindIndexedContent(std::addressof(meta_content), std::addressof(path), patch_id, NcaContentType::Meta, ContentLayer::Current)));
            }
            if (meta_content.drive != drive) {
                Log("register %016" PRIx64 ": update not used, it is on drive %d and the game on drive %d", patch_id.value, meta_content.drive, drive);
                R_SUCCEED();
            }

            ncm::AutoBuffer meta;
            R_TRY(ReadPackagedContentMeta(std::addressof(meta), patch_id));

            ncm::PackagedContentMetaReader reader(meta.Get(), meta.GetSize());
            if (reader.GetKey().type != ncm::ContentMetaType::Patch || !AreAllContentsIndexed(reader, drive)) {
                Log("register %016" PRIx64 ": update skipped, not every content passed validation", patch_id.value);
                DropPatch(application_id);
                R_SUCCEED();
            }

            if (R_FAILED(EnsureProgramTicket(application_id, ContentLayer::Current))) {
                Log("register %016" PRIx64 ": update skipped, no ticket", patch_id.value);
                DropPatch(application_id);
                R_SUCCEED();
            }

            R_TRY(EnsureContentMeta(out_written, db, reader, meta_content));
            *out_key = reader.GetKey();
            R_SUCCEED();
        }

        constexpr s32 MaxAddOnContentCount = 32;

        /* Each add-on content's meta names its application; those with every content and their ticket go into the application's record. */
        void RegisterAddOnContents(ContentStorageRecord *records, s32 *record_count, s32 max_records, bool *out_written, ncm::ContentMetaDatabase &db, ncm::ProgramId application_id, s32 drive) {
            IndexedContent metas[MaxAddOnContentCount];
            const s32 meta_count = CollectMetaContents(metas, MaxAddOnContentCount, IsAddOnContentTitleId);

            for (s32 i = 0; i < meta_count && *record_count < max_records; ++i) {
                ncm::AutoBuffer meta;
                if (const Result result = ReadPackagedContentMeta(std::addressof(meta), metas[i].program_id); R_FAILED(result)) {
                    Log("register %016" PRIx64 ": add-on content meta unreadable 2%03d-%04d", metas[i].program_id.value, result.GetModule(), result.GetDescription());
                    continue;
                }

                ncm::PackagedContentMetaReader reader(meta.Get(), meta.GetSize());
                const auto key = reader.GetKey();
                if (key.type != ncm::ContentMetaType::AddOnContent || reader.GetExtendedHeader<ncm::AddOnContentMetaExtendedHeader>()->application_id.value != application_id.value) {
                    continue;
                }

                if (metas[i].drive != drive) {
                    Log("register %016" PRIx64 ": add-on content not used, it is on drive %d and the game on drive %d", key.id, metas[i].drive, drive);
                    continue;
                }
                if (!AreAllContentsIndexed(reader, drive)) {
                    Log("register %016" PRIx64 ": add-on content skipped, not every content passed validation", key.id);
                    continue;
                }
                if (R_FAILED(EnsureAddOnContentTicket(reader))) {
                    Log("register %016" PRIx64 ": add-on content skipped, no ticket", key.id);
                    continue;
                }

                bool written = false;
                if (const Result result = EnsureContentMeta(std::addressof(written), db, reader, metas[i]); R_FAILED(result)) {
                    Log("register %016" PRIx64 ": add-on content meta not written 2%03d-%04d", key.id, result.GetModule(), result.GetDescription());
                    continue;
                }

                *out_written |= written;
                records[(*record_count)++] = MakeSdCardRecord(key);
            }
        }

        /* The update and add-on content metas registered for an application that came from USB: those whose content is a
         * known USB content. Anything really installed on the SD card is not ours and is never removed. Taken before
         * registering, since a failed update is forgotten on the way. */
        struct OwnedParts {
            ncm::ContentMetaKey keys[1 + MaxAddOnContentCount];
            s32 count;
        };

        void CollectOwnedParts(OwnedParts *out, ncm::ContentMetaDatabase &db, ncm::ProgramId application_id) {
            out->count = 0;
            const auto add_if_ours = [&](const ncm::ContentMetaKey &key, ncm::ContentType main_type) {
                ncm::ContentId content_id;
                KnownContent known;
                if (out->count < static_cast<s32>(util::size(out->keys)) && R_SUCCEEDED(db.GetContentIdByType(std::addressof(content_id), key, main_type)) && FindKnownContent(std::addressof(known), content_id)) {
                    out->keys[out->count++] = key;
                }
            };

            ncm::ContentMetaKey key;
            if (R_SUCCEEDED(db.GetLatest(std::addressof(key), GetPatchId(application_id).value))) {
                add_if_ours(key, ncm::ContentType::Program);
            }
            ncm::ContentMetaKey add_ons[MaxAddOnContentCount];
            const auto listed = db.ListContentMeta(add_ons, MaxAddOnContentCount, ncm::ContentMetaType::AddOnContent, ncm::ApplicationId{ application_id.value });
            for (s32 i = 0; i < std::min<s32>(listed.written, MaxAddOnContentCount); ++i) {
                add_if_ours(add_ons[i], ncm::ContentType::Data);
            }
        }

        /* Our parts that did not make it into the record (deleted from the game's drive, moved to another drive, or no
         * longer valid) leave the SD card database, so ns sees the game as it now is. Returns whether any was removed. */
        bool RemoveUnusedParts(ncm::ContentMetaDatabase &db, const OwnedParts &owned, const ContentStorageRecord *records, s32 record_count) {
            bool removed = false;
            for (s32 i = 0; i < owned.count; ++i) {
                const auto &key = owned.keys[i];
                if (std::any_of(records, records + record_count, [&](const ContentStorageRecord &record) { return record.key == key; })) {
                    continue;
                }
                const char *what = key.type == ncm::ContentMetaType::Patch ? "update" : "add-on content";
                if (const Result result = db.Remove(key); R_FAILED(result)) {
                    Log("register %016" PRIx64 ": %s v%u not removed 2%03d-%04d", key.id, what, key.version, result.GetModule(), result.GetDescription());
                    continue;
                }
                Log("register %016" PRIx64 ": %s v%u removed, no longer on the game's drive", key.id, what, key.version);
                removed = true;
            }
            if (removed) {
                R_DISCARD(db.Commit());
            }
            return removed;
        }

        /* Registration runs on the USB thread and, for the Manager, on a service thread. */
        constinit os::SdkRecursiveMutex g_registration_lock;

        Result DeleteApplicationRecord(ncm::ApplicationId application_id);

        /* The game ShowGameForLaunch is registering: pushed even if hidden in the Manager. */
        constinit ncm::ApplicationId g_launching_application = {};

        Result RegisterApplication(ncm::ContentMetaDatabase &db, const IndexedContent &meta_content) {
            ncm::AutoBuffer meta;
            R_TRY(ReadPackagedContentMeta(std::addressof(meta), meta_content.program_id));

            ncm::PackagedContentMetaReader reader(meta.Get(), meta.GetSize());
            const auto key = reader.GetKey();
            if (key.type != ncm::ContentMetaType::Application) {
                Log("register %016" PRIx64 ": skipped, meta type 0x%02x", key.id, static_cast<u8>(key.type));
                R_SUCCEED();
            }

            if (!AreAllContentsIndexed(reader)) {
                Log("register %016" PRIx64 ": skipped, not every content passed validation", key.id);
                R_SUCCEED();
            }

            bool meta_written = false;
            R_TRY(EnsureContentMeta(std::addressof(meta_written), db, reader, meta_content));
            R_DISCARD(EnsureProgramTicket(meta_content.program_id, ContentLayer::Base));

            ContentStorageRecord records[2 + MaxAddOnContentCount] = { MakeSdCardRecord(key) };
            s32 record_count = 1;
            u32 version = key.version;

            OwnedParts owned;
            CollectOwnedParts(std::addressof(owned), db, meta_content.program_id);

            util::optional<ncm::ContentMetaKey> patch_key;
            bool patch_written = false;
            if (const Result result = RegisterPatch(std::addressof(patch_key), std::addressof(patch_written), db, meta_content.program_id, meta_content.drive); R_FAILED(result)) {
                Log("register %016" PRIx64 ": update not registered 2%03d-%04d", key.id, result.GetModule(), result.GetDescription());
                DropPatch(meta_content.program_id);
            }
            if (patch_key.has_value()) {
                records[record_count++] = MakeSdCardRecord(*patch_key);
                version = patch_key->version;
            }

            bool add_on_content_written = false;
            const s32 application_record_count = record_count;
            RegisterAddOnContents(records, std::addressof(record_count), util::size(records), std::addressof(add_on_content_written), db, meta_content.program_id, meta_content.drive);
            add_on_content_written |= RemoveUnusedParts(db, owned, records, record_count);

            /* Every push makes HOME reload its whole grid; once per boot is enough unless a meta changed. */
            if (!meta_written && !patch_written && !add_on_content_written && WasPushedSinceBoot({ key.id })) {
                Log("register %016" PRIx64 ": ns record already pushed since boot", key.id);
                R_SUCCEED();
            }

            /* Metas and tickets stay in place, so showing the game again only takes a push. */
            if (IsHiddenGame({ key.id }) && g_launching_application.value != key.id) {
                R_TRY(DeleteApplicationRecord({ key.id }));
                Log("register %016" PRIx64 ": hidden in the Manager, not shown on HOME", key.id);
                R_SUCCEED();
            }

            R_TRY(PushApplicationRecord({ key.id }, records, record_count));
            Log("register %016" PRIx64 ": ns application record ok (Installed, v%u%s, %d add-on contents)", key.id, version, patch_key.has_value() ? " with update" : "", record_count - application_record_count);
            R_SUCCEED();
        }

    }

    namespace {

        void HideKnownApplicationsLocked();

    }

    void RegisterIndexedApplicationsIfEnabled() {
        AMS_ASSERT(!GetVolumeLock().IsLockedByCurrentThread());
        std::scoped_lock lk(g_registration_lock);

        if (!IsAutoRegisterEnabled()) {
            Log("auto register disabled (usbloadernx!auto_register)");
            HideKnownApplicationsLocked();
            return;
        }

        IndexedContent metas[MaxApplicationCount];
        const s32 count = CollectMetaContents(metas, MaxApplicationCount, IsApplicationTitleId);

        ncm::ContentMetaDatabase db;
        if (const Result result = OpenSdCardContentMetaDatabase(std::addressof(db)); R_FAILED(result)) {
            Log("register: cannot open SD ncm database 2%03d-%04d", result.GetModule(), result.GetDescription());
            return;
        }

        for (s32 i = 0; i < count; ++i) {
            if (const Result result = RegisterApplication(db, metas[i]); R_FAILED(result)) {
                Log("register %016" PRIx64 ": failed 2%03d-%04d", metas[i].program_id.value, result.GetModule(), result.GetDescription());
            }
        }
    }

    namespace {

        /* ncm paths look like "@SdCardContent://registered/000000E8/<id>.nca"; the content storage filesystem wants "/registered/...". */
        const char *ToContentStoragePath(const char *path) {
            const char *p = std::strchr(path, ':');
            if (p == nullptr) {
                return path;
            }
            for (++p; p[0] == '/' && p[1] == '/'; ++p) { /* ... */ }
            return p;
        }

        /* Our own registration also writes SD card content meta, so a real SD install is told apart by its Program NCA being a file. */
        bool IsInstalledOnSdCard(ncm::ContentMetaDatabase &db, ncm::ContentStorage &storage, ::FsFileSystem &content_fs, ncm::ApplicationId application_id) {
            ncm::ContentMetaKey key;
            if (R_FAILED(db.GetLatest(std::addressof(key), application_id.value))) {
                return false;
            }

            ncm::ContentId program_content_id;
            if (R_FAILED(db.GetContentIdByType(std::addressof(program_content_id), key, ncm::ContentType::Program))) {
                return false;
            }

            ncm::Path path;
            storage.GetPath(std::addressof(path), program_content_id);

            ::FsDirEntryType type;
            return R_SUCCEEDED(::fsFsGetEntryType(std::addressof(content_fs), ToContentStoragePath(path.str), std::addressof(type))) && type == FsDirEntryType_File;
        }

        bool IsInstalledOnConsole(ncm::ContentMetaDatabase &db, ncm::ApplicationId application_id) {
            ncm::ContentMetaKey key;
            return R_SUCCEEDED(db.GetLatest(std::addressof(key), application_id.value));
        }

    }

    void ExcludeInstalledApplications() {
        AMS_ASSERT(!GetVolumeLock().IsLockedByCurrentThread());

        /* Known titles too: one installed while its drive was unplugged must not be claimed by loader. */
        ncm::ProgramId applications[MaxApplicationCount * 2];
        s32 count = GetKnownApplications(applications, MaxApplicationCount);
        {
            /* Program NCAs, not metas: an update alone on the drive also carries the application's program id. */
            std::scoped_lock lk(GetVolumeLock());
            for (s32 i = 0; i < GetIndexedContentCount() && count < MaxApplicationCount * 2; ++i) {
                IndexedContent content;
                if (R_SUCCEEDED(GetIndexedContent(std::addressof(content), i)) && content.content_type == NcaContentType::Program &&
                    std::find(applications, applications + count, content.program_id) == applications + count) {
                    applications[count++] = content.program_id;
                }
            }
        }
        if (count == 0) {
            return;
        }

        ncm::ContentMetaDatabase console_db;
        const bool has_console_db = R_SUCCEEDED(ncm::OpenContentMetaDatabase(std::addressof(console_db), ncm::StorageId::BuiltInUser));

        ncm::ContentMetaDatabase sd_db;
        ncm::ContentStorage sd_storage;
        ::FsFileSystem sd_content_fs;
        const bool has_sd = R_SUCCEEDED(OpenSdCardContentMetaDatabase(std::addressof(sd_db))) &&
                            R_SUCCEEDED(ncm::OpenContentStorage(std::addressof(sd_storage), ncm::StorageId::SdCard)) &&
                            R_SUCCEEDED(::fsOpenContentStorageFileSystem(std::addressof(sd_content_fs), FsContentStorageId_SdCard));
        ON_SCOPE_EXIT { if (has_sd) { ::fsFsClose(std::addressof(sd_content_fs)); } };

        for (s32 i = 0; i < count; ++i) {
            const ncm::ApplicationId application_id = { applications[i].value };

            const char *location = nullptr;
            if (has_console_db && IsInstalledOnConsole(console_db, application_id)) {
                location = "console";
            } else if (has_sd && IsInstalledOnSdCard(sd_db, sd_storage, sd_content_fs, application_id)) {
                location = "SD card";
            }
            if (location == nullptr) {
                continue;
            }

            {
                std::scoped_lock lk(GetVolumeLock());
                RemoveIndexedProgram(applications[i]);
            }
            ForgetKnownProgram(applications[i]);
            Log("%016" PRIx64 ": installed on the %s, USB copy ignored", application_id.value, location);
        }
    }

    namespace {

        bool IsHideWhenRemovedEnabled() {
            return IsConfigEnabled(ConfigKey::HideWhenRemoved);
        }

        /* Only the ns record goes: the content meta, the ticket, save data and play time all stay. */
        Result DeleteApplicationRecord(ncm::ApplicationId application_id) {
            R_TRY(::nsInitialize());
            ON_SCOPE_EXIT { ::nsExit(); };

            R_SUCCEED_IF(!FindApplicationRecordEvent(application_id).has_value());

            ::Service app_manager;
            R_TRY(::nsGetApplicationManagerInterface(std::addressof(app_manager)));
            ON_SCOPE_EXIT { ::serviceClose(std::addressof(app_manager)); };

            R_TRY(::nsamDeleteApplicationRecord(std::addressof(app_manager), application_id.value));
            ForgetPushed(application_id);
            R_SUCCEED();
        }

    }

    namespace {

        /* auto_register off: no USB game stays on HOME, plugged in or not. */
        void HideKnownApplicationsLocked() {
            ncm::ProgramId applications[MaxApplicationCount];
            const s32 count = GetKnownApplications(applications, MaxApplicationCount);
            for (s32 i = 0; i < count; ++i) {
                if (const Result result = DeleteApplicationRecord({ applications[i].value }); R_FAILED(result)) {
                    Log("hide %016" PRIx64 ": ns record not removed 2%03d-%04d", applications[i].value, result.GetModule(), result.GetDescription());
                }
            }
            Log("hide: %d known games off HOME (auto register disabled)", count);
        }

    }

    namespace {

        bool IsApplicationRunning(u64 application_id);

    }

    void HideUnpluggedApplications(const ncm::ProgramId *applications, s32 count) {
        AMS_ASSERT(!GetVolumeLock().IsLockedByCurrentThread());
        std::scoped_lock lk(g_registration_lock);

        if (count == 0 || !IsHideWhenRemovedEnabled()) {
            return;
        }

        for (s32 i = 0; i < count; ++i) {
            /* Still served by another drive, or no longer ours (installed on the console or SD card since). */
            {
                std::scoped_lock lk(GetVolumeLock());
                if (IsIndexedProgram(applications[i])) {
                    continue;
                }
            }
            if (!IsKnownProgram(applications[i])) {
                continue;
            }

            /* ns must not lose the record of the running game: it goes once the game closes (with the error its reads get). */
            if (IsApplicationRunning(applications[i].value)) {
                Log("hide %016" PRIx64 ": running, hidden once it closes", applications[i].value);
                WatchLaunch({ applications[i].value });
                continue;
            }

            if (const Result result = DeleteApplicationRecord({ applications[i].value }); R_FAILED(result)) {
                Log("hide %016" PRIx64 ": ns record not removed 2%03d-%04d", applications[i].value, result.GetModule(), result.GetDescription());
            } else {
                Log("hide %016" PRIx64 ": hidden until its drive is plugged in", applications[i].value);
            }
        }
    }

    namespace {

        bool HasApplicationRecord(ncm::ApplicationId application_id) {
            if (R_FAILED(::nsInitialize())) {
                return false;
            }
            ON_SCOPE_EXIT { ::nsExit(); };
            return FindApplicationRecordEvent(application_id).has_value();
        }

        /* The record an unplugged game had while its drive was in, rebuilt from the metas left in the SD card ncm database. */
        Result PushRecordFromDatabase(ncm::ContentMetaDatabase &db, ncm::ApplicationId application_id) {
            ContentStorageRecord records[2 + MaxAddOnContentCount];
            s32 record_count = 0;

            ncm::ContentMetaKey key;
            R_TRY(db.GetLatest(std::addressof(key), application_id.value));
            records[record_count++] = MakeSdCardRecord(key);
            if (R_SUCCEEDED(db.GetLatest(std::addressof(key), GetPatchId({ application_id.value }).value))) {
                records[record_count++] = MakeSdCardRecord(key);
            }

            ncm::ContentMetaKey add_ons[MaxAddOnContentCount];
            const auto listed = db.ListContentMeta(add_ons, MaxAddOnContentCount, ncm::ContentMetaType::AddOnContent, application_id);
            for (s32 i = 0; i < std::min<s32>(listed.written, MaxAddOnContentCount); ++i) {
                records[record_count++] = MakeSdCardRecord(add_ons[i]);
            }

            R_RETURN(PushApplicationRecord(application_id, records, record_count));
        }

    }

    namespace {

        /* Whether the options want this game on HOME right now: shown, not hidden in the Manager, and plugged in or
         * kept while unplugged. */
        bool IsWantedOnHome(ncm::ApplicationId application_id) {
            if (!IsAutoRegisterEnabled() || IsHiddenGame(application_id)) {
                return false;
            }
            std::scoped_lock lk(GetVolumeLock());
            return IsIndexedProgram({ application_id.value }) || !IsHideWhenRemovedEnabled();
        }

        /* Takes a game ShowGameForLaunch put on HOME off it again when the options do not want it there. */
        void RemoveLaunchRecordLocked(ncm::ApplicationId application_id) {
            if (IsWantedOnHome(application_id)) {
                return;
            }
            if (const Result result = DeleteApplicationRecord(application_id); R_FAILED(result)) {
                Log("launch %016" PRIx64 ": ns record not removed 2%03d-%04d", application_id.value, result.GetModule(), result.GetDescription());
            } else {
                Log("launch %016" PRIx64 ": off HOME again", application_id.value);
            }
        }

    }

    void ApplyHomeVisibility() {
        AMS_ASSERT(!GetVolumeLock().IsLockedByCurrentThread());
        std::scoped_lock lk(g_registration_lock);

        if (!IsAutoRegisterEnabled()) {
            HideKnownApplicationsLocked();
            return;
        }

        /* Plugged-in games: registered and pushed (or kept off HOME when hidden in the Manager). */
        RegisterIndexedApplicationsIfEnabled();

        /* Unplugged games: off HOME with hide_when_removed, otherwise on HOME as when their drive was in. */
        const bool hide_unplugged = IsHideWhenRemovedEnabled();
        ncm::ContentMetaDatabase db;
        if (const Result result = OpenSdCardContentMetaDatabase(std::addressof(db)); R_FAILED(result) && !hide_unplugged) {
            Log("visibility: cannot open SD ncm database 2%03d-%04d", result.GetModule(), result.GetDescription());
            return;
        }

        ncm::ProgramId applications[MaxApplicationCount];
        const s32 count = GetKnownApplications(applications, MaxApplicationCount);
        for (s32 i = 0; i < count; ++i) {
            const ncm::ApplicationId application_id = { applications[i].value };
            bool is_indexed;
            {
                std::scoped_lock lk(GetVolumeLock());
                is_indexed = IsIndexedProgram(applications[i]);
            }
            if (is_indexed) {
                /* Registering skips a hidden game but leaves the record a launch from the Manager pushed. */
                if (IsHiddenGame(application_id)) {
                    RemoveLaunchRecordLocked(application_id);
                }
                continue;
            }

            if (hide_unplugged || IsHiddenGame(application_id)) {
                if (const Result result = DeleteApplicationRecord(application_id); R_FAILED(result)) {
                    Log("visibility %016" PRIx64 ": ns record not removed 2%03d-%04d", application_id.value, result.GetModule(), result.GetDescription());
                } else {
                    Log("visibility %016" PRIx64 ": off HOME until its drive is plugged in", application_id.value);
                }
                continue;
            }

            /* Already on HOME: pushing again would only make HOME reload its grid. */
            if (HasApplicationRecord(application_id)) {
                continue;
            }
            if (const Result result = PushRecordFromDatabase(db, application_id); R_FAILED(result)) {
                Log("visibility %016" PRIx64 ": ns record not pushed 2%03d-%04d", application_id.value, result.GetModule(), result.GetDescription());
            } else {
                Log("visibility %016" PRIx64 ": on HOME while its drive is unplugged", application_id.value);
            }
        }
    }

    namespace {

        /* Games taken off HOME by RefreshApplicationRecords, waiting to come back once HOME is in front again. */
        constinit ncm::ApplicationId g_refresh_pending[MaxApplicationCount] = {};
        constinit s32 g_refresh_pending_count = 0;
        /* The thread is waiting for the Manager to close (a second badge change must not count as that signal). */
        constinit bool g_refresh_armed = false;

        constexpr size_t HomeRefreshStackSize = 32_KB;
        alignas(os::ThreadStackAlignment) constinit u8 g_home_refresh_stack[HomeRefreshStackSize];
        constinit os::ThreadType g_home_refresh_thread;
        constinit os::EventType g_home_refresh_event;

        constexpr TimeSpan HomeRefreshFallback = TimeSpan::FromMinutes(5);
        /* Time for the Manager to close and HOME to come to the front. */
        constexpr TimeSpan HomeRefreshDelay = TimeSpan::FromMilliSeconds(1500);

        void BringBackRefreshedGames() {
            std::scoped_lock lk(g_registration_lock);
            if (g_refresh_pending_count == 0) {
                g_refresh_armed = false;
                return;
            }

            /* Plugged-in games come back through registration. */
            g_pushed_count = 0;
            RegisterIndexedApplicationsIfEnabled();

            /* Unplugged games come back from their metas on the SD card. */
            ncm::ContentMetaDatabase db;
            const bool has_db = R_SUCCEEDED(OpenSdCardContentMetaDatabase(std::addressof(db)));
            s32 unplugged = 0;
            for (s32 i = 0; i < g_refresh_pending_count && has_db; ++i) {
                {
                    std::scoped_lock vlk(GetVolumeLock());
                    if (IsIndexedProgram({ g_refresh_pending[i].value })) {
                        continue;
                    }
                }
                if (HasApplicationRecord(g_refresh_pending[i])) {
                    continue;
                }
                if (const Result result = PushRecordFromDatabase(db, g_refresh_pending[i]); R_FAILED(result)) {
                    Log("refresh %016" PRIx64 ": ns record not pushed 2%03d-%04d", g_refresh_pending[i].value, result.GetModule(), result.GetDescription());
                } else {
                    ++unplugged;
                }
            }
            Log("refresh: %d games back on HOME, %d of them unplugged", g_refresh_pending_count, unplugged);
            g_refresh_pending_count = 0;
            g_refresh_armed = false;
        }

        /* The Manager runs as homebrew: in the album applet (hbloader in place of it) or in place of a game. */
        bool IsManagerRunning() {
            constexpr ncm::ProgramId AlbumAppletId = { 0x010000000000100Dull };
            os::ProcessId process_id;
            if (R_SUCCEEDED(::ams::pm::info::GetProcessId(std::addressof(process_id), AlbumAppletId))) {
                return true;
            }
            bool is_hbl = false;
            return R_SUCCEEDED(::ams::pm::dmnt::GetApplicationProcessId(std::addressof(process_id))) &&
                   R_SUCCEEDED(::ams::pm::info::IsHblProcessId(std::addressof(is_hbl), process_id)) && is_hbl;
        }

        void HomeRefreshThreadFunction(void *) {
            while (true) {
                os::WaitEvent(std::addressof(g_home_refresh_event));

                /* Back once the Manager says it is closing, or is no longer running (closed with HOME it may not say),
                 * or after the fallback. */
                const os::Tick deadline = os::GetSystemTick() + os::ConvertToTick(HomeRefreshFallback);
                while (true) {
                    if (os::TimedWaitEvent(std::addressof(g_home_refresh_event), TimeSpan::FromSeconds(1))) {
                        break;
                    }
                    if (!IsManagerRunning()) {
                        Log("refresh: the Manager is no longer running");
                        break;
                    }
                    if (os::GetSystemTick() >= deadline) {
                        Log("refresh: the Manager did not close in time, bringing the games back anyway");
                        break;
                    }
                }
                os::SleepThread(HomeRefreshDelay);
                BringBackRefreshedGames();
            }
        }

    }

    void RefreshApplicationRecords() {
        AMS_ASSERT(!GetVolumeLock().IsLockedByCurrentThread());
        std::scoped_lock lk(g_registration_lock);

        /* HOME's grid keeps its own copy of each icon while the game stays in it, and reconciles only when it comes to
         * the front: taking a game off and back while the Manager is open changes nothing it can see. So the games stay
         * off HOME until the Manager closes, and come back once HOME is in front. */
        ncm::ProgramId applications[MaxApplicationCount];
        const s32 count = GetKnownApplications(applications, MaxApplicationCount);
        for (s32 i = 0; i < count; ++i) {
            const ncm::ApplicationId application_id = { applications[i].value };
            if (!HasApplicationRecord(application_id)) {
                continue;
            }
            if (const Result result = DeleteApplicationRecord(application_id); R_FAILED(result)) {
                Log("refresh %016" PRIx64 ": ns record not removed 2%03d-%04d", application_id.value, result.GetModule(), result.GetDescription());
                continue;
            }
            if (std::find(g_refresh_pending, g_refresh_pending + g_refresh_pending_count, application_id) == g_refresh_pending + g_refresh_pending_count &&
                g_refresh_pending_count < MaxApplicationCount) {
                g_refresh_pending[g_refresh_pending_count++] = application_id;
            }
        }
        Log("refresh: %d games off HOME until the Manager closes", g_refresh_pending_count);
        /* Arms the fallback; FinishHomeRefresh is the second signal. */
        if (!g_refresh_armed) {
            g_refresh_armed = true;
            os::SignalEvent(std::addressof(g_home_refresh_event));
        }
    }

    void FinishHomeRefresh() {
        bool pending;
        {
            std::scoped_lock lk(g_registration_lock);
            pending = g_refresh_armed;
        }
        if (pending) {
            Log("refresh: the Manager is closing");
            os::SignalEvent(std::addressof(g_home_refresh_event));
        }
    }

    void StartHomeRefreshThread() {
        os::InitializeEvent(std::addressof(g_home_refresh_event), false, os::EventClearMode_AutoClear);
        R_ABORT_UNLESS(os::CreateThread(std::addressof(g_home_refresh_thread), HomeRefreshThreadFunction, nullptr, g_home_refresh_stack, sizeof(g_home_refresh_stack), AMS_GET_SYSTEM_THREAD_PRIORITY(mitm_usbl, IpcServer)));
        os::SetThreadNamePointer(std::addressof(g_home_refresh_thread), AMS_GET_SYSTEM_THREAD_NAME(mitm_usbl, IpcServer));
        os::StartThread(std::addressof(g_home_refresh_thread));
    }

    Result ShowGameForLaunch(ncm::ApplicationId application_id) {
        AMS_ASSERT(!GetVolumeLock().IsLockedByCurrentThread());
        std::scoped_lock lk(g_registration_lock);

        IndexedContent meta;
        R_UNLESS(CollectMetaContents(std::addressof(meta), 1, [application_id](ncm::ProgramId id) { return id.value == application_id.value; }) == 1, usbl::ResultContentNotFound());

        ncm::ContentMetaDatabase db;
        R_TRY(OpenSdCardContentMetaDatabase(std::addressof(db)));

        g_launching_application = application_id;
        ON_SCOPE_EXIT { g_launching_application = {}; };
        R_TRY(RegisterApplication(db, meta));

        Log("launch %016" PRIx64 ": shown on HOME for a launch from the Manager", application_id.value);
        WatchLaunch(application_id);
        R_SUCCEED();
    }

    Result SetGameHidden(ncm::ApplicationId application_id, bool hidden) {
        AMS_ASSERT(!GetVolumeLock().IsLockedByCurrentThread());
        std::scoped_lock lk(g_registration_lock);

        R_UNLESS(IsKnownProgram({ application_id.value }), usbl::ResultContentNotFound());
        R_TRY(SetHiddenGame(application_id, hidden));

        if (hidden) {
            R_TRY(DeleteApplicationRecord(application_id));
            Log("hide %016" PRIx64 ": hidden in the Manager", application_id.value);
        } else {
            /* Pushes it again when its drive is plugged in; otherwise the next attach does. */
            Log("hide %016" PRIx64 ": shown again by the Manager", application_id.value);
            RegisterIndexedApplicationsIfEnabled();
        }
        R_SUCCEED();
    }

    namespace {

        s32 GetContainerFragments(const IndexedContent &content) {
            ContainerInfo info;
            return R_SUCCEEDED(GetContainerInfo(std::addressof(info), content.xci_index)) ? info.fragments : 1;
        }

        /* Worst of two counts: any unmappable file (negative) wins, then the most pieces. */
        s32 WorseFragments(s32 a, s32 b) {
            return (a < 0 || b < 0) ? -1 : std::max(a, b);
        }

        void FillIndexedStatus(GameStatus *out, ncm::ProgramId program_id) {
            std::scoped_lock lk(GetVolumeLock());

            IndexedContent base;
            const char *path = nullptr;
            if (R_FAILED(FindIndexedContent(std::addressof(base), std::addressof(path), program_id, NcaContentType::Program, ContentLayer::Base))) {
                return;
            }

            out->is_connected = true;
            out->drive        = base.drive;
            out->fragments    = GetContainerFragments(base);

            const char *name = std::strrchr(path, '/');
            util::Strlcpy(out->file_name, name != nullptr ? name + 1 : path, sizeof(out->file_name));

            IndexedContent patch;
            const char *patch_path;
            if (R_SUCCEEDED(FindIndexedContent(std::addressof(patch), std::addressof(patch_path), program_id, NcaContentType::Program, ContentLayer::Current)) && patch.is_patch && patch.drive == base.drive) {
                out->fragments = WorseFragments(out->fragments, GetContainerFragments(patch));
            }

            /* Parts on another drive are not used; the Manager tells the user to move them next to the game. */
            const ncm::ProgramId patch_id = GetPatchId(program_id);
            for (s32 i = 0; i < GetIndexedContentCount(); ++i) {
                IndexedContent content;
                if (R_FAILED(GetIndexedContent(std::addressof(content), i)) || content.content_type != NcaContentType::Meta || content.drive == base.drive) {
                    continue;
                }
                const bool is_add_on_of_game = IsAddOnContentTitleId(content.program_id) && ((content.program_id.value - 0x1000) & ~TitleIdKindMask) == program_id.value;
                if (content.program_id == patch_id || is_add_on_of_game) {
                    out->has_parts_elsewhere = true;
                    break;
                }
            }
        }

        void FillRegisteredStatus(GameStatus *out, ncm::ContentMetaDatabase &db) {
            ncm::ContentMetaKey key;
            if (R_SUCCEEDED(db.GetLatest(std::addressof(key), out->application_id))) {
                out->version = key.version;
            }
            if (R_SUCCEEDED(db.GetLatest(std::addressof(key), GetPatchId({ out->application_id }).value))) {
                out->has_update     = true;
                out->update_version = key.version;
            }

            constexpr size_t MaxAddOnContents = 64;
            ncm::ContentMetaKey add_ons[MaxAddOnContents];
            const auto listed = db.ListContentMeta(add_ons, MaxAddOnContents, ncm::ContentMetaType::AddOnContent, { out->application_id });
            out->add_on_count = static_cast<u8>(std::min<s32>(listed.written, std::numeric_limits<u8>::max()));
        }

    }

    s32 GetGameStatuses(GameStatus *out, s32 max_count) {
        AMS_ASSERT(!GetVolumeLock().IsLockedByCurrentThread());

        ncm::ProgramId applications[MaxApplicationCount];
        const s32 count = std::min(GetKnownApplications(applications, MaxApplicationCount), max_count);

        ncm::ContentMetaDatabase db;
        const bool has_db = R_SUCCEEDED(ncm::OpenContentMetaDatabase(std::addressof(db), ncm::StorageId::SdCard));

        for (s32 i = 0; i < count; ++i) {
            GameStatus &game = out[i];
            game = { .application_id = applications[i].value, .size = static_cast<u64>(GetKnownApplicationSize(applications[i])), .drive = -1, .fragments = 1 };

            KnownContent program;
            if (FindKnownProgramContent(std::addressof(program), applications[i], static_cast<u8>(NcaContentType::Program), ContentLayer::Base)) {
                game.source = program.source;
            }
            game.is_hidden = IsHiddenGame({ applications[i].value });

            FillIndexedStatus(std::addressof(game), applications[i]);
            if (has_db) {
                FillRegisteredStatus(std::addressof(game), db);
            }
        }
        return count;
    }

    namespace {

        /* The game ShowGameForLaunch put on HOME: watched until it is closed, then taken off HOME again. */
        constexpr size_t LaunchWatchStackSize = 32_KB;
        alignas(os::ThreadStackAlignment) constinit u8 g_launch_watch_stack[LaunchWatchStackSize];
        constinit os::ThreadType g_launch_watch_thread;
        constinit os::EventType g_launch_event;
        constinit std::atomic<u64> g_watched_application = 0;

        constexpr TimeSpan LaunchStartTimeout = TimeSpan::FromSeconds(60);
        constexpr TimeSpan LaunchPollInterval = TimeSpan::FromSeconds(2);

        bool IsApplicationRunning(u64 application_id) {
            os::ProcessId process_id;
            ncm::ProgramId program_id;
            return R_SUCCEEDED(::ams::pm::dmnt::GetApplicationProcessId(std::addressof(process_id))) &&
                   R_SUCCEEDED(::ams::pm::info::GetProgramId(std::addressof(program_id), process_id)) && program_id.value == application_id;
        }

        void LaunchWatchThreadFunction(void *) {
            while (true) {
                os::WaitEvent(std::addressof(g_launch_event));
                const u64 application_id = g_watched_application.load();

                /* ns may take a moment to start it; a launch that never starts is undone after the timeout. */
                const os::Tick deadline = os::GetSystemTick() + os::ConvertToTick(LaunchStartTimeout);
                while (!IsApplicationRunning(application_id) && os::GetSystemTick() < deadline && g_watched_application.load() == application_id) {
                    os::SleepThread(LaunchPollInterval);
                }
                while (IsApplicationRunning(application_id)) {
                    os::SleepThread(LaunchPollInterval);
                }
                /* A newer launch replaced it: that one is watched now. */
                if (g_watched_application.load() != application_id) {
                    continue;
                }

                Log("launch %016" PRIx64 ": closed", application_id);
                std::scoped_lock lk(g_registration_lock);
                RemoveLaunchRecordLocked({ application_id });
                g_watched_application = 0;
            }
        }

    }

    void StartLaunchWatch() {
        os::InitializeEvent(std::addressof(g_launch_event), false, os::EventClearMode_AutoClear);
        R_ABORT_UNLESS(os::CreateThread(std::addressof(g_launch_watch_thread), LaunchWatchThreadFunction, nullptr, g_launch_watch_stack, sizeof(g_launch_watch_stack), AMS_GET_SYSTEM_THREAD_PRIORITY(mitm_usbl, IpcServer)));
        os::SetThreadNamePointer(std::addressof(g_launch_watch_thread), AMS_GET_SYSTEM_THREAD_NAME(mitm_usbl, IpcServer));
        os::StartThread(std::addressof(g_launch_watch_thread));
    }

    void WatchLaunch(ncm::ApplicationId application_id) {
        g_watched_application = application_id.value;
        os::SignalEvent(std::addressof(g_launch_event));
    }

}
