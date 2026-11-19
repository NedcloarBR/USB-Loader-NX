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
#include <cmath>
#include <switch.h>

namespace ui {

    /* Seconds since boot, for looping effects (floating, pulsing, particles). */
    inline double Seconds() {
        return static_cast<double>(armTicksToNs(armGetSystemTick())) / 1e9;
    }

    /* Set by the theme: how fast transitions run (1 = as designed) and how much covers and icons float (0 = still). */
    inline double MotionSpeed = 1.0;
    inline double FloatAmount = 1.0;

    /* The gentle up-and-down of floating things, `amplitude` design px at full float. */
    inline double Float(double amplitude) {
        return std::sin(Seconds() * 2.4) * amplitude * FloatAmount;
    }

    /* Set by the theme: seconds for the focus ring to go to FocusAlt and back; 0 keeps it still. */
    inline double FocusPeriod = 1.6;

    /* 0..1..0 once every `period` seconds. */
    inline double Pulse(double period) {
        return 0.5 - 0.5 * std::cos(Seconds() * 2 * M_PI / period);
    }

    /* Where the focus ring is between Focus (0) and FocusAlt (1). */
    inline double FocusPulse() {
        return FocusPeriod > 0 ? 0.5 - 0.5 * std::cos(Seconds() * 2 * M_PI / FocusPeriod) : 0.0;
    }

    /* A short transition driven by the system tick, independent of the frame rate. */
    class Motion {
        private:
            double seconds;
            u64 start_tick = 0;

        public:
            explicit constexpr Motion(double seconds) : seconds(seconds) {}

            void Start() {
                this->start_tick = armGetSystemTick();
            }

            /* 0 at the start, 1 once done (or never started), easing out. */
            double Progress() const {
                if (this->start_tick == 0) {
                    return 1.0;
                }
                const double t = static_cast<double>(armTicksToNs(armGetSystemTick() - this->start_tick)) / 1e9 / (this->seconds / MotionSpeed);
                if (t >= 1.0) {
                    return 1.0;
                }
                const double rest = 1.0 - t;
                return 1.0 - rest * rest * rest;
            }

            /* From `from` towards `to` as the motion runs. */
            double Between(double from, double to) const {
                return from + (to - from) * this->Progress();
            }
    };

}
