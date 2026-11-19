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
#include <string>
#include <unordered_map>

/* The JSON the Manager reads (languages, themes): nested objects of strings, flattened to "Section.Key". */
namespace json {

    using Table = std::unordered_map<std::string, std::string>;

    /* False when the file is missing or is not that kind of JSON; `out` is then left as it was. */
    bool ReadFlatFile(Table *out, const std::string &path);

}
