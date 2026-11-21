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

/* Read statistics in the log while usbloadernx!debug is on: what the game asks for, what the files on the drive cost,
 * and what the USB transfers cost, summed over a few seconds of activity. */
namespace ams::mitm::usbl::perf {

    enum class Layer : u8 {
        Game, /* A RomFS or add-on content read as the client sees it: decryption and hashing included. */
        File, /* A read of a file on the drive: waiting for the volume lock, FatFs and USB. */
        Disk, /* Sectors FatFs reads from the device: USB only. */
        Count,
    };

    /* Reads usbloadernx!debug once; call after InitializeLog. */
    void Initialize();
    bool IsEnabled();

    void Record(Layer layer, size_t size, TimeSpan elapsed, TimeSpan lock_wait = 0);

    /* Logs how much of ams.mitm's heap is free ("heap <when>: ..."), to see what the index and LayeredFS leave. */
    void LogHeap(const char *when);

    /* Wraps a storage handed to a client so its reads are counted as Layer::Game. */
    std::shared_ptr<ams::fs::IStorage> MeasureGameReads(std::shared_ptr<ams::fs::IStorage> &&storage);

    /* Times a step (indexing, registration) and logs "drive <n>: <what> in <ms> ms", without the drive when it is negative. */
    class ScopedStep {
        NON_COPYABLE(ScopedStep);
        NON_MOVEABLE(ScopedStep);
        private:
            const char *m_what;
            s32 m_drive;
            os::Tick m_start;
        public:
            ScopedStep(const char *what, s32 drive) : m_what(what), m_drive(drive), m_start(os::GetSystemTick()) { /* ... */ }
            ~ScopedStep();
    };

}
