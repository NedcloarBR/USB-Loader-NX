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
#include <gametdb.hpp>
#include <pu/Plutonium>
#include <string>
#include <vector>
#include <usbl_client.hpp>

/* The games USB Loader NX knows, with the name and icon ns reports for them. Requires nsInitialize. */
namespace library {

    /* From the console's play records (pdm); all zero for a game never played. */
    struct PlayStats {
        u64 play_time_ns;
        u32 launches;
        /* POSIX time of the last session, 0 when never played. */
        u64 last_played;
    };

    struct Game {
        usbl::GameStatus status;
        std::string name;
        /* The name in every language the game has, American English first (GameTDB matches any of them). */
        std::vector<std::string> names;
        /* As the game shows it ("1.0.2"); empty when ns has no control data. */
        std::string display_version;
        /* Null when ns has no control data (e.g. the game is hidden and was never shown since boot). */
        pu::sdl2::TextureHandle::Ref icon;
        PlayStats play = {};
        bool is_favorite = false;
        /* The console asks for a newer version than the drive has: the game was played with an update that is gone
         * (avm's launch required version only goes up). HOME would only offer to download it. */
        bool update_missing = false;
        /* What GameTDB knows of the game (developer, year, genres...), from the index built into the app. */
        gametdb::Entry gametdb;
        bool in_gametdb = false;
    };

    Result LoadGames(std::vector<Game> *out);

    /* `update_missing` asked again right now (before a launch: the list can be a moment behind the module). */
    bool IsUpdateMissing(u64 application_id);

    /* A JPEG (the app's own icon) with the same rounded corners as the game icons. */
    pu::sdl2::TextureHandle::Ref LoadRoundedImage(const char *path);

    /* Favorites are the Manager's own list (favorites.txt next to manager.ini) and always come first. */
    bool SetFavorite(u64 application_id, bool favorite);
    /* Replaces the whole list (used when converting the old favorites.txt). */
    bool WriteFavorites(const std::vector<u64> &favorites);
    bool AreFavoritesFromNewerVersion();

    /* Which games the library shows (ZR); a drive is one more filter per drive. */
    enum class Filter : s32 {
        All,
        /* The last few games played, most recent first. */
        ContinuePlaying,
        Favorites,
        Drive,
        Xci,
        Nsp,
        Installed,
        Hidden,
    };
    /* Keeps the games `filter` lets through (`drive` for Filter::Drive). */
    void FilterBy(std::vector<Game> *games, Filter filter, s32 drive);

    enum class SortMode : s32 {
        Name,
        RecentlyPlayed,
        MostPlayed,
        Size,

        Count,
    };

    /* Favorites first, then by `mode`; ties keep alphabetical order. */
    void SortGames(std::vector<Game> *games, SortMode mode);

    /* Games whose name contains `query`, ignoring case (ASCII letters); everything for an empty query. */
    void FilterGames(std::vector<Game> *games, const std::string &query);

    /* A game hidden in the Manager, or any game while auto_register is off, is shown on HOME for the launch (ns only
     * launches recorded games); the module takes it off again once the game is closed. */
    Result LaunchGame(const Game &game);

}
