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
#include <sound.hpp>
#include <algorithm>
#include <array>
#include <string>
#include <pu/Plutonium>
#include <manager_settings.hpp>
#include <ui/theme.hpp>

namespace sound {

    namespace {

        constexpr const char *FileNames[] = { "move", "select", "back", "whoosh", "toggle_on", "toggle_off", "launch", "error", "plug", "unplug", "open" };
        static_assert(std::size(FileNames) == static_cast<size_t>(Effect::Count));

        /* "sounds = 0" turns them off; anything else, or no setting, keeps them on. */
        constexpr const char SoundsKey[] = "sounds";
        constexpr const char MusicKey[] = "music";
        constexpr const char MusicVolumeKey[] = "music_volume";
        /* Percent of full volume; 25 keeps it under the effects. */
        constexpr s32 DefaultMusicVolume = 25;

        s32 SavedMusicVolume() {
            return std::clamp(settings::GetInt(MusicVolumeKey, DefaultMusicVolume), 0, 100);
        }

        bool g_is_open = false;
        bool g_is_enabled = true;
        std::array<pu::audio::Sfx, static_cast<size_t>(Effect::Count)> g_effects = {};
        Mix_Music *g_music = nullptr;

        void StartMusic() {
            const std::string path = ui::theme::MusicPath();
            if (path.empty() || settings::GetInt(MusicKey, 1) == 0) {
                return;
            }
            g_music = Mix_LoadMUS(path.c_str());
            if (g_music != nullptr) {
                Mix_VolumeMusic(MIX_MAX_VOLUME * SavedMusicVolume() / 100);
                Mix_PlayMusic(g_music, -1);
            }
        }

    }

    void Initialize() {
        g_is_enabled = settings::GetInt(SoundsKey, 1) != 0;
        /* Decoders for theme music; WAV needs none. */
        Mix_Init(MIX_INIT_OGG | MIX_INIT_MP3);
        /* A 4096-sample buffer (85 ms): with less, decoding the music while the Manager redraws and refreshes its list
         * underruns now and then, which crackles. */
        if (Mix_OpenAudio(48000, AUDIO_S16SYS, 2, 4096) != 0) {
            return;
        }
        Mix_AllocateChannels(8);
        g_is_open = true;
        for (size_t i = 0; i < g_effects.size(); ++i) {
            const std::string themed = ui::theme::FindAsset(std::string("sfx/") + FileNames[i] + ".wav");
            g_effects[i] = pu::audio::LoadSfx(!themed.empty() ? themed : std::string("romfs:/sfx/") + FileNames[i] + ".wav");
        }
        StartMusic();
    }

    void Exit() {
        if (!g_is_open) {
            return;
        }
        if (g_music != nullptr) {
            Mix_HaltMusic();
            Mix_FreeMusic(g_music);
            g_music = nullptr;
        }
        for (auto &effect : g_effects) {
            if (effect != nullptr) {
                pu::audio::DestroySfx(effect);
            }
        }
        Mix_CloseAudio();
        g_is_open = false;
    }

    void Reload() {
        Exit();
        Initialize();
    }

    void Play(Effect effect) {
        if (const auto sfx = g_effects[static_cast<size_t>(effect)]; g_is_open && g_is_enabled && sfx != nullptr) {
            pu::audio::PlaySfx(sfx);
        }
    }

    bool IsMusicEnabled() {
        return settings::GetInt(MusicKey, 1) != 0;
    }

    bool SetMusicEnabled(bool enabled) {
        if (!settings::SetInt(MusicKey, enabled ? 1 : 0)) {
            return false;
        }
        Reload();
        return true;
    }

    s32 GetMusicVolume() {
        return SavedMusicVolume();
    }

    bool SetMusicVolume(s32 percent) {
        percent = std::clamp(percent, 0, 100);
        if (!settings::SetInt(MusicVolumeKey, percent)) {
            return false;
        }
        if (g_music != nullptr) {
            Mix_VolumeMusic(MIX_MAX_VOLUME * percent / 100);
        }
        return true;
    }

    bool IsEnabled() {
        return g_is_enabled;
    }

    bool SetEnabled(bool enabled) {
        if (!settings::SetInt(SoundsKey, enabled ? 1 : 0)) {
            return false;
        }
        g_is_enabled = enabled;
        return true;
    }

}
