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
#include <memory>
#include <string>

namespace ui {

    /* An animated GIF or WebP played in a loop: the file stays compressed in memory and a thread decodes one frame
     * ahead, so a long animation costs the same as a short one. Up to 1280x720, at most 30 frames a second. */
    class AnimatedImage {
        public:
            static constexpr s32 MaxWidth = 1280, MaxHeight = 720;
            static constexpr long MaxFileSize = 32 * 1024 * 1024;

            /* Null when the file is not an animation (one frame), cannot be read or is over the limits. */
            static std::unique_ptr<AnimatedImage> Open(const std::string &path);
            /* The first frame alone, for a preview; null when it cannot be decoded. */
            static SDL_Texture *LoadFirstFrame(const std::string &path);

            /* Whether the file is a GIF or WebP with more than one frame (decodes two frames to tell). */
            static bool IsAnimated(const std::string &path);

            /* While the Manager is not in front, every animation stops decoding. */
            static void SetInFocus(bool in_focus);

            ~AnimatedImage();
            AnimatedImage(const AnimatedImage &) = delete;
            AnimatedImage &operator=(const AnimatedImage &) = delete;

            /* The frame due now, uploaded when it changed; call from the render thread only. */
            SDL_Texture *Frame();

            struct Impl;
        private:
            explicit AnimatedImage(std::unique_ptr<Impl> impl);
            std::unique_ptr<Impl> impl;
    };

}
