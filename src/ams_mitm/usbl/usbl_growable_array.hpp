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

namespace ams::mitm::usbl {

    /* ams.mitm's heap (12 MB) is shared with LayeredFS and every USB buffer: lists sized by what is on the drives never
     * take the last of it. */
    constexpr size_t HeapReserve = 3_MB;

    inline bool HasHeapRoomFor(size_t bytes) {
        const auto *allocator = ams::init::GetAllocator();
        return allocator == nullptr || allocator->GetAllocatableSize() >= bytes + HeapReserve;
    }

    /* A list that starts empty, grows (doubling from 64) as items are found, and fails instead of aborting when the heap
     * has no room left. No limit on the number of items besides that. */
    template<typename T>
    class GrowableArray {
        NON_COPYABLE(GrowableArray);
        NON_MOVEABLE(GrowableArray);
        private:
            static constexpr size_t InitialCapacity = 64;

            std::unique_ptr<T[]> m_items;
            size_t m_count = 0;
            size_t m_capacity = 0;
        public:
            constexpr GrowableArray() = default;

            size_t size() const { return m_count; }
            bool empty() const { return m_count == 0; }

            T &operator[](size_t index) { AMS_ASSERT(index < m_count); return m_items[index]; }
            const T &operator[](size_t index) const { AMS_ASSERT(index < m_count); return m_items[index]; }

            T *begin() { return m_items.get(); }
            T *end() { return m_items.get() + m_count; }
            const T *begin() const { return m_items.get(); }
            const T *end() const { return m_items.get() + m_count; }

            /* False when there was no room to grow. */
            bool Reserve(size_t capacity) {
                if (capacity <= m_capacity) {
                    return true;
                }
                if (!HasHeapRoomFor(capacity * sizeof(T))) {
                    return false;
                }
                std::unique_ptr<T[]> items(new (std::nothrow) T[capacity]);
                if (items == nullptr) {
                    return false;
                }
                for (size_t i = 0; i < m_count; ++i) {
                    items[i] = std::move(m_items[i]);
                }
                m_items = std::move(items);
                m_capacity = capacity;
                return true;
            }

            bool PushBack(T item) {
                if (m_count == m_capacity && !this->Reserve(std::max(InitialCapacity, m_capacity * 2))) {
                    return false;
                }
                m_items[m_count++] = std::move(item);
                return true;
            }

            template<typename Predicate>
            void RemoveIf(Predicate predicate) {
                m_count = static_cast<size_t>(std::remove_if(this->begin(), this->end(), predicate) - this->begin());
            }

            void Clear() {
                m_count = 0;
            }

            /* Within what Reserve made room for: the caller fills the new items (a file read straight into begin()). */
            bool SetCount(size_t count) {
                if (count > m_capacity) {
                    return false;
                }
                m_count = count;
                return true;
            }

            /* Gives the memory back. */
            void Release() {
                m_items.reset();
                m_count = 0;
                m_capacity = 0;
            }
    };

    /* A scratch array on the heap, for lists too large for a thread's stack. Empty (Size() == 0) when there was no room. */
    template<typename T>
    class TemporaryArray {
        NON_COPYABLE(TemporaryArray);
        NON_MOVEABLE(TemporaryArray);
        private:
            std::unique_ptr<T[]> m_items;
            size_t m_size;
        public:
            explicit TemporaryArray(size_t size)
                : m_items(size > 0 && HasHeapRoomFor(size * sizeof(T)) ? new (std::nothrow) T[size]() : nullptr), m_size(m_items != nullptr ? size : 0) { /* ... */ }

            T *Get() { return m_items.get(); }
            size_t Size() const { return m_size; }
            s32 Count() const { return static_cast<s32>(m_size); }
            T &operator[](size_t index) { AMS_ASSERT(index < m_size); return m_items[index]; }
    };

}
