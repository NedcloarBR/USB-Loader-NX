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
#include <pu/Plutonium>
#include <string>
#include <vector>

/* Game art from GameTDB (art.gametdb.com), the Manager's only use of the network, off until the player turns it on
 * (manager.ini `gametdb_art`). A thread downloads what is missing into sdmc:/usbloadernx/cache/art/<kind>/; a file
 * GameTDB does not have is not asked for again for a week. */
namespace art {

    enum class Kind {
        /* The front cover, 352 x 570. */
        Cover,
        /* The case in 3D, with transparency, 300 x 486. */
        Box,
        /* The whole cover opened flat (back, spine, front), 740 x 570: the shelves' spines and the back. */
        Spread,
        /* The game card, with transparency, 240 x 360. */
        Cart,
        Count,
    };

    struct Game {
        u64 application_id;
        /* As the Manager shows it (the art window lists games without art). */
        std::string name;
        /* The game's names in every language it has (any of them can match GameTDB's). */
        std::vector<std::string> names;
    };

    bool IsEnabled();
    bool SetEnabled(bool enabled);

    /* The games on the drives; with art on, what is missing for them is downloaded. */
    void SetGames(std::vector<Game> games);
    /* Downloads again what GameTDB did not have or what failed (otherwise asked again after a week). */
    void Retry();

    /* The download now: files done of those to fetch in this pass. */
    struct Status {
        bool running;
        /* No internet connection the last time it tried. */
        bool offline;
        u32 done, total;
    };
    Status GetStatus();

    /* Why a game has no cover. */
    enum class Missing {
        /* No GameTDB entry has any of its names (data/gametdb.ini can name one). */
        NotOnGameTdb,
        /* Its entry has no cover on art.gametdb.com (asked again after a week). */
        NotOnServer,
        NoInternet,
        Failed,
    };
    struct MissingArt {
        std::string name;
        Missing reason;
    };
    /* After the last pass: how many games have a cover, of how many, and the others. */
    struct Report {
        u32 with_art, total;
        std::vector<MissingArt> missing;
    };
    Report GetReport();

    /* Deletes every downloaded file (art stays on: it comes back with Retry or the next start). */
    void DeleteAll();

    /* The texture of a game's art, kept in a small cache; null when it is not downloaded. Render thread only. */
    SDL_Texture *GetTexture(u64 application_id, Kind kind);

    /* Only the spine of a game's Spread, cut out when loaded (the shelves show many at once). */
    SDL_Texture *GetSpineTexture(u64 application_id);

    /* The parts of a Spread texture: the back and the front, in texture px. */
    SDL_Rect SpreadBack(SDL_Texture *spread);
    SDL_Rect SpreadFront(SDL_Texture *spread);

    /* Stops the download thread and frees the textures, before the renderer goes. */
    void Shutdown();

}
