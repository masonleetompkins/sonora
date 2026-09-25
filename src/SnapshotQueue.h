#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace sonora
{
// Single message-thread producer, single audio-thread consumer. Full queues
// reject writes; the UI retains the latest state and retries on its timer.
template <typename T, std::size_t Size = 8>
class SnapshotQueue
{
    static_assert(Size > 1 && std::is_trivially_copyable_v<T>);
    static_assert(std::atomic<std::size_t>::is_always_lock_free);
public:
    bool push(const T& value)
    {
        const auto current = write.load(std::memory_order_relaxed);
        const auto next = (current + 1) % Size;
        if (next == read.load(std::memory_order_acquire))
            return false;
        slots[current] = value;
        write.store(next, std::memory_order_release);
        return true;
    }
    bool pop(T& value)
    {
        const auto current = read.load(std::memory_order_relaxed);
        if (current == write.load(std::memory_order_acquire))
            return false;
        value = slots[current];
        read.store((current + 1) % Size, std::memory_order_release);
        return true;
    }
private:
    std::array<T, Size> slots {};
    std::atomic<std::size_t> read { 0 }, write { 0 };
};
}
