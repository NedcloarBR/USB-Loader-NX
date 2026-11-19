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

/* The Manager's sound effects (romfs:/sfx, made by tools/assets/generate_sounds.py). Silent if audio cannot open. */
namespace sound {

    enum class Effect {
        Move,
        Select,
        Back,
        Whoosh,
        ToggleOn,
        ToggleOff,
        Launch,
        Error,
        Plug,
        Unplug,
        Open,

        Count,
    };

    /* After the renderer, which initializes SDL. */
    void Initialize();
    void Exit();
    /* After a theme change: its sfx/<effect>.wav replace the built-in ones. */
    void Reload();

    void Play(Effect effect);

    /* The Manager's own option, "sounds" in manager.ini (on by default). */
    bool IsEnabled();
    bool SetEnabled(bool enabled);

    /* The theme's background music, looped quietly; "music" in manager.ini (on by default). */
    bool IsMusicEnabled();
    bool SetMusicEnabled(bool enabled);

    /* The music's volume in percent of full (0 to 100, 25 by default), "music_volume" in manager.ini; a new value
     * applies right away. */
    s32 GetMusicVolume();
    bool SetMusicVolume(s32 percent);

}
