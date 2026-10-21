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

namespace ams::mitm::usbl::nca {

    void InitializeCrypto();

    const fssystem::NcaCryptoConfiguration &GetCryptoConfiguration();

    /* Returns whether any key derivation failed since the last call, and clears the flag. */
    bool ConsumeKeyGenerationFailure();

    /* Returns whether a header whose signature 1 is not Nintendo's was accepted since the last call, and clears the flag. */
    bool ConsumeResignedHeaderAcceptance();

}
