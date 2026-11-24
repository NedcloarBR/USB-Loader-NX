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
#include <switch.h>
#include <string>
#include <vector>

/* GameTDB (gametdb.com) knows Switch games by their product code ("AURVE"), not by title id: a game is matched by
 * its names against the index built from GameTDB's database (tools/gametdb/build_index.py, romfs:/gametdb/index.bin). */
namespace gametdb {

    struct Entry {
        std::string id;
        /* GameTDB's regions ("USA", "EUR,JPN", "ALL"). */
        std::string regions;
        std::string developer, publisher, year;
        /* GameTDB's English names ("action", "role-playing"), the main one first. */
        std::vector<std::string> genres;
        /* "1", "1-4"... as GameTDB has it; empty when it does not say. */
        std::string players;
        bool tv = false, handheld = false, tabletop = false;
        /* "ESRB" -> "M", "PEGI" -> "18", "CERO" -> "D". */
        std::vector<std::pair<std::string, std::string>> ratings;
    };

    /* Lowercase ASCII, Latin marks folded, punctuation as single spaces (the same as build_index.py). */
    std::string Normalize(const std::string &name);

    /* The entry for a game with these names (any language), preferring the console's region; the player's own
     * choice in data/gametdb.ini wins. False when no entry has any of the names. */
    bool Find(u64 application_id, const std::vector<std::string> &names, Entry *out);

    /* The art regions to try for an entry, best first ("US", "EN", "JA"...). */
    std::vector<std::string> ArtRegions(const Entry &entry);

}
