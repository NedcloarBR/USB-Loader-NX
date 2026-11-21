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
#include <stratosphere.hpp>
#include "usbl_perf.hpp"
#include "usbl_log.hpp"

namespace ams::mitm::usbl::perf {

    namespace {

        constexpr TimeSpan ReportInterval = TimeSpan::FromSeconds(5);
        constexpr size_t SizeBucketLimits[] = { 16_KB, 64_KB, 256_KB, 1_MB };
        constexpr size_t SizeBucketCount = util::size(SizeBucketLimits) + 1;
        constexpr const char *LayerNames[] = { "game", "file", "disk" };
        static_assert(util::size(LayerNames) == static_cast<size_t>(Layer::Count));

        struct LayerStats {
            u64 count;
            u64 bytes;
            s64 total_us;
            s64 max_us;
            s64 lock_wait_us;
            u64 sizes[SizeBucketCount];
        };

        constinit bool g_enabled = false;
        constinit os::SdkMutex g_lock;
        constinit LayerStats g_stats[static_cast<size_t>(Layer::Count)] = {};
        constinit os::Tick g_window_start = os::Tick(0);

        size_t GetSizeBucket(size_t size) {
            for (size_t i = 0; i < util::size(SizeBucketLimits); ++i) {
                if (size <= SizeBucketLimits[i]) {
                    return i;
                }
            }
            return SizeBucketCount - 1;
        }

        /* Tenths, for the log's integer-only formatting. */
        u64 ToTenthsOfMegabytes(u64 bytes) { return bytes * 10 / 1_MB; }

        void LogLayer(Layer layer, const LayerStats &stats) {
            if (stats.count == 0) {
                return;
            }

            const u64 mb_tenths = ToTenthsOfMegabytes(stats.bytes);
            const s64 avg_us = stats.total_us / static_cast<s64>(stats.count);
            /* Throughput while this layer was busy: concurrent reads overlap, so it can exceed the drive's speed. */
            const u64 rate_tenths = stats.total_us > 0 ? stats.bytes * 10 * 1'000'000 / static_cast<u64>(stats.total_us) / 1_MB : 0;

            char lock_wait[48] = {};
            if (layer == Layer::File) {
                util::SNPrintf(lock_wait, sizeof(lock_wait), ", lock wait %" PRId64 ".%" PRId64 " ms avg", stats.lock_wait_us / static_cast<s64>(stats.count) / 1000, stats.lock_wait_us / static_cast<s64>(stats.count) / 100 % 10);
            }

            Log("perf %s: %" PRIu64 " reads, %" PRIu64 ".%" PRIu64 " MB, %" PRIu64 " KB avg, %" PRId64 ".%" PRId64 " ms avg, %" PRId64 " ms max, %" PRIu64 ".%" PRIu64 " MB/s busy%s | <=16K %" PRIu64 ", <=64K %" PRIu64 ", <=256K %" PRIu64 ", <=1M %" PRIu64 ", >1M %" PRIu64,
                LayerNames[static_cast<size_t>(layer)], stats.count, mb_tenths / 10, mb_tenths % 10, stats.bytes / stats.count / 1_KB,
                avg_us / 1000, avg_us / 100 % 10, stats.max_us / 1000, rate_tenths / 10, rate_tenths % 10, lock_wait,
                stats.sizes[0], stats.sizes[1], stats.sizes[2], stats.sizes[3], stats.sizes[4]);
        }

        class MeasuredStorage final : public ams::fs::IStorage, public ams::fs::impl::Newable {
            private:
                std::shared_ptr<ams::fs::IStorage> m_base;
            public:
                explicit MeasuredStorage(std::shared_ptr<ams::fs::IStorage> &&base) : m_base(std::move(base)) { /* ... */ }

                virtual Result Read(s64 offset, void *buffer, size_t size) override {
                    const os::Tick start = os::GetSystemTick();
                    const Result result = m_base->Read(offset, buffer, size);
                    if (R_SUCCEEDED(result)) {
                        Record(Layer::Game, size, (os::GetSystemTick() - start).ToTimeSpan());
                    }
                    R_RETURN(result);
                }

                virtual Result Write(s64 offset, const void *buffer, size_t size) override { R_RETURN(m_base->Write(offset, buffer, size)); }
                virtual Result Flush() override { R_RETURN(m_base->Flush()); }
                virtual Result SetSize(s64 size) override { R_RETURN(m_base->SetSize(size)); }
                virtual Result GetSize(s64 *out) override { R_RETURN(m_base->GetSize(out)); }

                virtual Result OperateRange(void *dst, size_t dst_size, ams::fs::OperationId op_id, s64 offset, s64 size, const void *src, size_t src_size) override {
                    R_RETURN(m_base->OperateRange(dst, dst_size, op_id, offset, size, src, src_size));
                }
        };

    }

    void Initialize() {
        g_enabled = IsDebugEnabled();
        if (g_enabled) {
            Log("perf: read statistics every %" PRId64 " s of activity", ReportInterval.GetSeconds());
        }
    }

    bool IsEnabled() {
        return g_enabled;
    }

    void Record(Layer layer, size_t size, TimeSpan elapsed, TimeSpan lock_wait) {
        if (!g_enabled) {
            return;
        }

        LayerStats snapshot[static_cast<size_t>(Layer::Count)];
        s64 window_ms;
        {
            std::scoped_lock lk(g_lock);

            const os::Tick now = os::GetSystemTick();
            if (g_window_start.GetInt64Value() == 0) {
                g_window_start = now;
            }

            LayerStats &stats = g_stats[static_cast<size_t>(layer)];
            const s64 elapsed_us = elapsed.GetMicroSeconds();
            stats.count        += 1;
            stats.bytes        += size;
            stats.total_us     += elapsed_us;
            stats.max_us        = std::max(stats.max_us, elapsed_us);
            stats.lock_wait_us += lock_wait.GetMicroSeconds();
            stats.sizes[GetSizeBucket(size)] += 1;

            window_ms = (now - g_window_start).ToTimeSpan().GetMilliSeconds();
            if (window_ms < ReportInterval.GetMilliSeconds()) {
                return;
            }

            std::memcpy(snapshot, g_stats, sizeof(snapshot));
            std::memset(g_stats, 0, sizeof(g_stats));
            g_window_start = os::Tick(0);
        }

        /* Logged outside the lock: the log writes to the SD card. */
        Log("perf: %" PRId64 ".%" PRId64 " s window", window_ms / 1000, window_ms / 100 % 10);
        for (size_t i = 0; i < static_cast<size_t>(Layer::Count); ++i) {
            LogLayer(static_cast<Layer>(i), snapshot[i]);
        }
        /* While a game reads (LayeredFS built, buffers in use): the heap the index has to share. */
        LogHeap("now");
    }

    std::shared_ptr<ams::fs::IStorage> MeasureGameReads(std::shared_ptr<ams::fs::IStorage> &&storage) {
        if (!g_enabled) {
            return std::move(storage);
        }
        return std::make_shared<MeasuredStorage>(std::move(storage));
    }

    ScopedStep::~ScopedStep() {
        const s64 ms = (os::GetSystemTick() - m_start).ToTimeSpan().GetMilliSeconds();
        if (m_drive < 0) {
            Log("%s in %" PRId64 " ms", m_what, ms);
        } else {
            Log("drive %d: %s in %" PRId64 " ms", m_drive, m_what, ms);
        }
    }

    void LogHeap(const char *when) {
        if (!IsEnabled()) {
            return;
        }
        if (const auto *allocator = ams::init::GetAllocator(); allocator != nullptr) {
            Log("heap %s: %zu KB free, largest block %zu KB", when, allocator->GetTotalFreeSize() / 1_KB, allocator->GetAllocatableSize() / 1_KB);
        }
    }

}
