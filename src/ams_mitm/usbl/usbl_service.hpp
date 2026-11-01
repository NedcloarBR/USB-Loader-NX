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
#pragma once
#include <stratosphere.hpp>
#include "usbl_config.hpp"
#include "usbl_usb_volume_manager.hpp"
#include "xci/usbl_registration.hpp"

namespace ams::mitm::usbl {

    /* Wire format shared with the ncm client. */
    struct ServedContentInfo {
        s64 size;
        u64 program_id;
        bool is_served;
        u8 key_generation;
        u8 reserved[6];
        fs::RightsId rights_id;
    };
    static_assert(sizeof(ServedContentInfo) == 0x28);
    static_assert(util::is_pod<ServedContentInfo>::value);

    /* ncm's request: query is the ncm::ExternalContentQuery asking; every query gets the same answer. */
    struct ContentQuery {
        ncm::ContentId content_id;
        u32 query;
        u32 reserved;
    };
    static_assert(sizeof(ContentQuery) == 0x18);
    static_assert(util::is_pod<ContentQuery>::value);

    struct ContentReadRequest {
        ncm::ContentId content_id;
        s64 offset;
    };
    static_assert(sizeof(ContentReadRequest) == 0x18);
    static_assert(util::is_pod<ContentReadRequest>::value);


}

/* "usbl": used by loader and ncm; fs_mitm calls usbl_content_api.hpp directly. Command ids 1-3 and 7 are retired. */
#define AMS_USBL_I_SERVICE_INTERFACE_INFO(C, H)                                                                                                                                                                  \
    AMS_SF_METHOD_INFO(C, H, 0, Result, IsServedProgram,         (sf::Out<bool> out, ncm::ProgramId program_id),                                                           (out, program_id))                         \
    AMS_SF_METHOD_INFO(C, H, 4, Result, GetContentInfoForNcm,    (sf::Out<ams::mitm::usbl::ServedContentInfo> out, ams::mitm::usbl::ContentQuery request),                (out, request))                            \
    AMS_SF_METHOD_INFO(C, H, 5, Result, ReadNcaContent,          (const sf::OutBuffer &out, ams::mitm::usbl::ContentReadRequest request),                                   (out, request))                            \
    AMS_SF_METHOD_INFO(C, H, 6, Result, OpenCodeFileSystem,      (sf::Out<sf::SharedPointer<fssrv::sf::IFileSystem>> out, const sf::OutBuffer &out_verification_data, ncm::ProgramId program_id), (out, out_verification_data, program_id)) \
    AMS_SF_METHOD_INFO(C, H, 8, Result, ForgetContentForNcm,     (sf::Out<bool> out, ncm::ContentId content_id),                                                            (out, content_id))  \
    AMS_SF_METHOD_INFO(C, H, 9, Result, ReloadConfig,            (),                                                                                                        ())                 \
    AMS_SF_METHOD_INFO(C, H, 10, Result, GetConfigValue,         (sf::Out<ams::mitm::usbl::ConfigValue> out, u8 key),                                                       (out, key))       \
    AMS_SF_METHOD_INFO(C, H, 11, Result, SetConfigValue,         (const ::usbl::protocol::SetConfigValueInput &input),                                                      (input))          \
    AMS_SF_METHOD_INFO(C, H, 12, Result, ListDrives,             (sf::Out<s32> out_count, const sf::OutArray<ams::mitm::usbl::DriveStatus> &out),                           (out_count, out)) \
    AMS_SF_METHOD_INFO(C, H, 13, Result, ListGames,              (sf::Out<s32> out_count, const sf::OutArray<ams::mitm::usbl::xci::GameStatus> &out),                       (out_count, out)) \
    AMS_SF_METHOD_INFO(C, H, 14, Result, SetGameHidden,          (const ::usbl::protocol::SetGameHiddenInput &input),                                                       (input))          \
    AMS_SF_METHOD_INFO(C, H, 15, Result, ShowGameForLaunch,      (ncm::ApplicationId application_id),                                                                       (application_id)) \
    AMS_SF_METHOD_INFO(C, H, 16, Result, ApplyHomeVisibility,    (),                                                                                                        ())               \
    AMS_SF_METHOD_INFO(C, H, 17, Result, GetGameIcon,            (sf::Out<u32> out_size, const sf::OutBuffer &out, ncm::ApplicationId application_id),                      (out_size, out, application_id)) \
    AMS_SF_METHOD_INFO(C, H, 18, Result, GetGameDisplayVersion,  (sf::Out<::usbl::protocol::DisplayVersion> out, ncm::ApplicationId application_id),                       (out, application_id))

AMS_SF_DEFINE_INTERFACE(ams::mitm::usbl::impl, IService, AMS_USBL_I_SERVICE_INTERFACE_INFO, 0x00000000)

namespace ams::mitm::usbl {

    class Service {
        public:
            Result IsServedProgram(sf::Out<bool> out, ncm::ProgramId program_id);
            Result GetContentInfoForNcm(sf::Out<ServedContentInfo> out, ContentQuery request);
            Result ReadNcaContent(const sf::OutBuffer &out, ContentReadRequest request);
            Result OpenCodeFileSystem(sf::Out<sf::SharedPointer<fssrv::sf::IFileSystem>> out, const sf::OutBuffer &out_verification_data, ncm::ProgramId program_id);
            Result ForgetContentForNcm(sf::Out<bool> out, ncm::ContentId content_id);
            /* For the Manager: config.ini was rewritten, and the effective value of each option with where it comes from. */
            Result ReloadConfig();
            Result GetConfigValue(sf::Out<ConfigValue> out, u8 key);
            Result SetConfigValue(const ::usbl::protocol::SetConfigValueInput &input);
            Result ListDrives(sf::Out<s32> out_count, const sf::OutArray<DriveStatus> &out);
            Result ListGames(sf::Out<s32> out_count, const sf::OutArray<xci::GameStatus> &out);
            Result SetGameHidden(const ::usbl::protocol::SetGameHiddenInput &input);
            Result ShowGameForLaunch(ncm::ApplicationId application_id);
            Result ApplyHomeVisibility();
            Result GetGameIcon(sf::Out<u32> out_size, const sf::OutBuffer &out, ncm::ApplicationId application_id);
            Result GetGameDisplayVersion(sf::Out<::usbl::protocol::DisplayVersion> out, ncm::ApplicationId application_id);
        private:
            ServedContentInfo FindContent(ncm::ContentId content_id);
            Result ReadCachedContent(ncm::ContentId content_id, s64 offset, void *dst, size_t size);
        private:
            /* ns verification reads a whole NCA through ncm in 16 KB requests; reopening the file each time made it crawl. */
            os::SdkMutex m_read_cache_lock;
            ncm::ContentId m_read_cache_content_id = {};
            std::shared_ptr<fs::IStorage> m_read_cache_storage;
    };
    static_assert(impl::IsIService<Service>);

}
