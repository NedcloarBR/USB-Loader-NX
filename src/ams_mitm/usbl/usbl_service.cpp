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
#include <stratosphere/fssrv/fssrv_interface_adapters.hpp>
#include "usbl_service.hpp"
#include "nca/usbl_external_keys.hpp"
#include "nca/usbl_nca_filesystem.hpp"
#include "content/usbl_content_index.hpp"
#include "content/usbl_known_contents.hpp"
#include "content/usbl_registration.hpp"
#include "storage/usbl_volume.hpp"
#include "usbl_content_api.hpp"
#include "i18n/usbl_i18n.hpp"
#include "usbl_results.hpp"
#include "usbl_log.hpp"
#include "usbl_notifications.hpp"
#include "content/usbl_hidden_games.hpp"
#include <usbl/version.hpp>

namespace ams::mitm::usbl {

    Result Service::IsServedProgram(sf::Out<bool> out, ncm::ProgramId program_id) {
        bool served;
        {
            std::scoped_lock lk(GetVolumeLock());
            served = IsIndexedProgram(program_id);
        }

        /* A known title whose drive is unplugged is still ours: loader must fail with DriveNotConnected, not fall back to FS. */
        served = served || IsKnownProgram(program_id);
        out.SetValue(served);
        R_SUCCEED();
    }



    ServedContentInfo Service::FindContent(ncm::ContentId content_id) {
        /* Known contents answer even when their drive is unplugged, so ns never sees them as missing. */
        KnownContent known;
        if (!FindKnownContent(std::addressof(known), content_id)) {
            return {};
        }

        return {
            .size           = known.size,
            .program_id     = known.program_id.value,
            .is_served      = true,
            .key_generation = known.key_generation,
            .reserved       = {},
            .rights_id      = known.rights_id,
        };
    }


    Result Service::GetContentInfoForNcm(sf::Out<ServedContentInfo> out, ContentQuery request) {
        const auto info = this->FindContent(request.content_id);

        out.SetValue(info);
        R_SUCCEED();
    }


    Result Service::ForgetContentForNcm(sf::Out<bool> out, ncm::ContentId content_id) {
        const bool was_known = ForgetKnownContent(content_id);
        if (was_known) {
            Log("ncm: %s deleted, forgotten", ncm::GetContentIdString(content_id).data);
        }
        out.SetValue(was_known);
        R_SUCCEED();
    }


    Result Service::ReloadConfig() {
        LoadConfig();
        char language[sizeof(::usbl::protocol::Language::locale)];
        usbl::GetLanguage(language, sizeof(language));
        i18n::Initialize(language);
        R_SUCCEED();
    }

    Result Service::GetLanguage(sf::Out<::usbl::protocol::Language> out) {
        ::usbl::protocol::Language language = {};
        usbl::GetLanguage(language.locale, sizeof(language.locale));
        out.SetValue(language);
        R_SUCCEED();
    }

    Result Service::SetLanguage(const ::usbl::protocol::SetLanguageInput &input) {
        char locale[sizeof(input.language.locale) + 1] = {};
        std::memcpy(locale, input.language.locale, sizeof(input.language.locale));
        R_TRY(usbl::SetLanguage(locale, input.overwrite));
        i18n::Initialize(locale);
        R_SUCCEED();
    }

    Result Service::GetConfigValue(sf::Out<ConfigValue> out, u8 key) {
        R_UNLESS(key < static_cast<u8>(ConfigKey::Count), usbl::ResultOutOfRange());
        out.SetValue(usbl::GetConfigValue(static_cast<ConfigKey>(key)));
        R_SUCCEED();
    }


    Result Service::SetConfigValue(const ::usbl::protocol::SetConfigValueInput &input) {
        R_UNLESS(input.key < ConfigKey::Count, usbl::ResultOutOfRange());
        R_TRY(usbl::SetConfigValue(input.key, input.enabled, input.overwrite));

        /* HOME follows the new value right away instead of at the next boot or plug. Notifications are read at each one. */
        switch (input.key) {
            case ConfigKey::AutoRegister:
            case ConfigKey::HideWhenRemoved:
                content::ApplyHomeVisibility();
                break;
            case ConfigKey::Badge:
                content::RefreshApplicationRecords();
                break;
            default:
                break;
        }
        R_SUCCEED();
    }

    Result Service::ListDrives(sf::Out<s32> out_count, const sf::OutArray<DriveStatus> &out) {
        out_count.SetValue(GetDriveStatuses(out.GetPointer(), static_cast<s32>(out.GetSize())));
        R_SUCCEED();
    }

    Result Service::ListLeftOutFiles(sf::Out<s32> out_count, const sf::OutArray<::usbl::protocol::LeftOutFile> &out) {
        out_count.SetValue(GetLeftOutFiles(out.GetPointer(), static_cast<s32>(out.GetSize())));
        R_SUCCEED();
    }

    Result Service::ListGames(sf::Out<s32> out_count, const sf::OutArray<content::GameStatus> &out) {
        out_count.SetValue(content::GetGameStatuses(out.GetPointer(), static_cast<s32>(out.GetSize())));
        R_SUCCEED();
    }

    Result Service::SetGameHidden(const ::usbl::protocol::SetGameHiddenInput &input) {
        R_RETURN(content::SetGameHidden({ input.application_id }, input.hidden));
    }

    Result Service::FinishHomeRefresh() {
        content::FinishHomeRefresh();
        R_SUCCEED();
    }

    Result Service::ShowGameForLaunch(ncm::ApplicationId application_id) {
        R_RETURN(content::ShowGameForLaunch(application_id));
    }

    Result Service::ApplyHomeVisibility() {
        content::ApplyHomeVisibility();
        R_SUCCEED();
    }

    Result Service::GetGameIcon(sf::Out<u32> out_size, const sf::OutBuffer &out, ncm::ApplicationId application_id) {
        size_t size = 0;
        R_TRY(ReadServedProgramIcon(std::addressof(size), out.GetPointer(), out.GetSize(), { application_id.value }));
        out_size.SetValue(static_cast<u32>(size));
        R_SUCCEED();
    }

    Result Service::GetGameDisplayVersion(sf::Out<::usbl::protocol::DisplayVersion> out, ncm::ApplicationId application_id) {
        ::usbl::protocol::DisplayVersion version = {};
        R_TRY(ReadServedProgramDisplayVersion(version.text, sizeof(version.text), { application_id.value }));
        out.SetValue(version);
        R_SUCCEED();
    }


    Result Service::GetVersion(sf::Out<::usbl::protocol::VersionInfo> out) {
        ::usbl::protocol::VersionInfo info = { .major = ::usbl::protocol::VersionMajor, .minor = ::usbl::protocol::VersionMinor, .flags = 0, .module_version = {} };
        if (IsConfigFromNewerVersion()) {
            info.flags |= ::usbl::protocol::VersionFlag_ConfigFromNewerVersion;
        }
        if (IsKnownContentsFromNewerVersion() || IsHiddenGamesFromNewerVersion()) {
            info.flags |= ::usbl::protocol::VersionFlag_DataFromNewerVersion;
        }
        util::Strlcpy(info.module_version, ::usbl::Version, sizeof(info.module_version));
        out.SetValue(info);
        R_SUCCEED();
    }

    Result Service::ReadCachedContent(ncm::ContentId content_id, s64 offset, void *dst, size_t size) {
        std::scoped_lock lk(m_read_cache_lock);

        if (m_read_cache_storage == nullptr || m_read_cache_content_id != content_id) {
            m_read_cache_storage.reset();
            R_TRY(OpenServedContentStorage(std::addressof(m_read_cache_storage), content_id));
            m_read_cache_content_id = content_id;
        }

        /* The drive may have been replugged since the file was opened: reopen once. */
        const Result result = m_read_cache_storage->Read(offset, dst, size);
        if (usbl::ResultDriveDetached::Includes(result)) {
            m_read_cache_storage.reset();
            R_TRY(OpenServedContentStorage(std::addressof(m_read_cache_storage), content_id));
            R_RETURN(m_read_cache_storage->Read(offset, dst, size));
        }
        R_RETURN(result);
    }

    Result Service::ReadNcaContent(const sf::OutBuffer &out, ContentReadRequest request) {
        const Result result = this->ReadCachedContent(request.content_id, request.offset, out.GetPointer(), out.GetSize());
        if (R_FAILED(result)) {
            Log("ncm: read of %s at 0x%" PRIx64 " (0x%zx bytes) failed: 2%03d-%04d", ncm::GetContentIdString(request.content_id).data, request.offset, out.GetSize(), result.GetModule(), result.GetDescription());
        }
        R_RETURN(result);
    }

    Result Service::OpenCodeFileSystem(sf::Out<sf::SharedPointer<fssrv::sf::IFileSystem>> out, const sf::OutBuffer &out_verification_data, ncm::ProgramId program_id) {
        constexpr s32 CodeSectionIndex = 0;

        R_UNLESS(out_verification_data.GetSize() >= sizeof(fs::CodeVerificationData), fs::ResultInvalidSize());

        const Result result = [&]() -> Result {
            std::shared_ptr<fs::IStorage> raw_nca;
            R_TRY(OpenServedProgramContentStorage(std::addressof(raw_nca), program_id, NcaContentType::Program));

            std::shared_ptr<fssystem::NcaReader> reader;
            R_TRY(nca::OpenReader(std::addressof(reader), std::move(raw_nca)));
            R_TRY(nca::ApplyExternalKey(*reader));

            /* What FS returns for loader's ACID check: header signature 2 and the hash it covers. */
            auto *verification = reinterpret_cast<fs::CodeVerificationData *>(out_verification_data.GetPointer());
            std::memset(verification, 0, sizeof(*verification));
            reader->GetHeaderSign2(verification->signature, sizeof(verification->signature));
            reader->GetHeaderSign2TargetHash(verification->target_hash, sizeof(verification->target_hash));
            verification->has_data = true;

            std::shared_ptr<fs::fsa::IFileSystem> code_fs;
            R_TRY(nca::OpenSectionFileSystem(std::addressof(code_fs), std::move(reader), CodeSectionIndex));

            out.SetValue(sf::CreateSharedObjectEmplaced<fssrv::sf::IFileSystem, fssrv::impl::FileSystemInterfaceAdapter>(std::move(code_fs), false));
            R_SUCCEED();
        }();

        if (usbl::ResultDriveNotConnected::Includes(result)) {
            NotifyDriveRequired();
        } else if (R_FAILED(result)) {
            Log("loader: cannot open code of %016" PRIx64 ": 2%03d-%04d", program_id.value, result.GetModule(), result.GetDescription());
        }
        R_RETURN(result);
    }


}
