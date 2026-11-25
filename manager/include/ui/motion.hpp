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
#include <algorithm>
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

    /* Set by the theme: the curve of every transition, from linear time t (0..1) to progress; 1 at t = 1. Curves
     * that overshoot (out-back, out-elastic) go past 1 on the way. */
    enum class Easing {
        OutCubic,
        Linear,
        OutQuad,
        InOutSine,
        OutBack,
        OutElastic,
    };
    inline Easing TransitionEasing = Easing::OutCubic;

    inline double Ease(double t) {
        switch (TransitionEasing) {
            case Easing::Linear:
                return t;
            case Easing::OutQuad:
                return 1.0 - (1.0 - t) * (1.0 - t);
            case Easing::InOutSine:
                return 0.5 - 0.5 * std::cos(t * M_PI);
            case Easing::OutBack: {
                constexpr double c1 = 1.70158, c3 = c1 + 1.0;
                const double u = t - 1.0;
                return 1.0 + c3 * u * u * u + c1 * u * u;
            }
            case Easing::OutElastic:
                return t >= 1.0 ? 1.0 : std::pow(2.0, -10.0 * t) * std::sin((t * 10.0 - 0.75) * (2.0 * M_PI / 3.0)) + 1.0;
            case Easing::OutCubic:
            default: {
                const double rest = 1.0 - t;
                return 1.0 - rest * rest * rest;
            }
        }
    }

    /* A short transition driven by the system tick, independent of the frame rate. */
    class Motion {
        private:
            double seconds;
            u64 start_tick = 0;

            /* Linear time, 0 at the start and 1 once done (or never started). */
            double Time() const {
                if (this->start_tick == 0) {
                    return 1.0;
                }
                const double t = static_cast<double>(armTicksToNs(armGetSystemTick() - this->start_tick)) / 1e9 / (this->seconds / MotionSpeed);
                return t >= 1.0 ? 1.0 : t;
            }
        public:
            explicit constexpr Motion(double seconds) : seconds(seconds) {}

            void Start() {
                this->start_tick = armGetSystemTick();
            }

            /* 0 at the start, 1 once done, along the theme's curve but kept within 0..1 (for opacities and fades). */
            double Progress() const {
                const double t = this->Time();
                return t >= 1.0 ? 1.0 : std::clamp(Ease(t), 0.0, 1.0);
            }

            /* From `from` towards `to` along the theme's curve, overshooting `to` when the curve does. */
            double Between(double from, double to) const {
                const double t = this->Time();
                return from + (to - from) * (t >= 1.0 ? 1.0 : Ease(t));
            }
    };

}
