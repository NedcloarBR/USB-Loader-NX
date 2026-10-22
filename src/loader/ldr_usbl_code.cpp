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
#include "ldr_usbl_code.hpp"
#include "ldr_usbl_shim.h"

namespace ams::ldr {

    namespace {

        constexpr sm::ServiceName UsblServiceName = sm::ServiceName::Encode("usbl");

        /* Never wait on sm: "usbl" is registered by ams.mitm only when enabled, after the SD card is ready. */
        bool OpenUsblSession(::Service *out) {
            bool has_service = false;
            if (R_FAILED(sm::HasService(std::addressof(has_service), UsblServiceName)) || !has_service) {
                return false;
            }

            os::NativeHandle handle;
            if (R_FAILED(sm::GetServiceHandle(std::addressof(handle), UsblServiceName))) {
                return false;
            }

            ::serviceCreate(out, handle);
            return true;
        }

    }

    Result OpenUsblCodeFileSystem(fs::CodeVerificationData *out_verification_data, std::unique_ptr<fs::fsa::IFileSystem> *out, ncm::ProgramId program_id) {
        R_UNLESS(ncm::IsApplicationId(program_id), fs::ResultTargetNotFound());

        /* One session per mount, closed once the ExeFS object is received (that object is a session of its own). */
        ::Service session;
        R_UNLESS(OpenUsblSession(std::addressof(session)), fs::ResultTargetNotFound());
        ON_SCOPE_EXIT { ::serviceClose(std::addressof(session)); };

        bool served = false;
        R_UNLESS(R_SUCCEEDED(::ldrUsblIsServedProgram(std::addressof(session), std::addressof(served), program_id.value)), fs::ResultTargetNotFound());
        R_UNLESS(served, fs::ResultTargetNotFound());

        ::FsFileSystem code_fs;
        R_TRY(::ldrUsblOpenCodeFileSystem(std::addressof(session), std::addressof(code_fs), out_verification_data, sizeof(*out_verification_data), program_id.value));

        auto remote = std::make_unique<fs::RemoteFileSystem>(code_fs);
        R_UNLESS(remote != nullptr, fs::ResultAllocationMemoryFailedNew());

        *out = std::move(remote);
        R_SUCCEED();
    }

}
