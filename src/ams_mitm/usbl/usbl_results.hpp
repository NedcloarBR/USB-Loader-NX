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

R_DEFINE_NAMESPACE_RESULT_MODULE(ams::mitm::usbl, 505);

namespace ams::mitm::usbl {

    R_DEFINE_ERROR_RESULT(NoBulkEndpoints,      1);
    R_DEFINE_ERROR_RESULT(InvalidCommandStatus, 2);
    R_DEFINE_ERROR_RESULT(CommandFailed,        3);
    R_DEFINE_ERROR_RESULT(PhaseError,           4);
    R_DEFINE_ERROR_RESULT(ShortTransfer,        5);
    R_DEFINE_ERROR_RESULT(UnitNotReady,         6);
    R_DEFINE_ERROR_RESULT(UnsupportedBlockSize, 7);
    R_DEFINE_ERROR_RESULT(OutOfRange,           8);
    R_DEFINE_ERROR_RESULT(FatFsError,           9);
    R_DEFINE_ERROR_RESULT(InvalidXciHeader,     10);
    R_DEFINE_ERROR_RESULT(InvalidHfs0Header,    11);
    R_DEFINE_ERROR_RESULT(HashMismatch,         12);
    R_DEFINE_ERROR_RESULT(PartitionNotFound,    13);
    R_DEFINE_ERROR_RESULT(KeyGenerationUnavailable, 14);
    R_DEFINE_ERROR_RESULT(DriveDetached,        15);
    R_DEFINE_ERROR_RESULT(ContentNotFound,      16);
    R_DEFINE_ERROR_RESULT(IndexFull,            17);
    R_DEFINE_ERROR_RESULT(DriveNotConnected,    18);
    R_DEFINE_ERROR_RESULT(TransferTimedOut,     19);

}
