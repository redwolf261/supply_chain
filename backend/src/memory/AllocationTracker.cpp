#include "util/AllocationTracker.h"

#include <atomic>

namespace scx {

namespace {
std::atomic<std::size_t> liveBytes{0};
std::atomic<std::size_t> highWaterMark{0};
std::atomic<bool> hooksInstalled{false};
}  // namespace

void AllocationTracker::recordAllocation(std::size_t bytes) noexcept {
    const std::size_t now = liveBytes.fetch_add(bytes, std::memory_order_relaxed) + bytes;
    std::size_t peak = highWaterMark.load(std::memory_order_relaxed);
    while (now > peak && !highWaterMark.compare_exchange_weak(peak, now, std::memory_order_relaxed)) {}
}

void AllocationTracker::recordRelease(std::size_t bytes) noexcept { liveBytes.fetch_sub(bytes, std::memory_order_relaxed); }
void AllocationTracker::markHooksInstalled() noexcept { hooksInstalled.store(true); }
bool AllocationTracker::isActive() noexcept { return hooksInstalled.load(); }
std::size_t AllocationTracker::currentBytes() noexcept { return liveBytes.load(std::memory_order_relaxed); }
std::size_t AllocationTracker::peakBytes() noexcept { return highWaterMark.load(std::memory_order_relaxed); }
void AllocationTracker::resetPeakToCurrent() noexcept { highWaterMark.store(liveBytes.load(std::memory_order_relaxed)); }

AllocationScope::AllocationScope() noexcept : baseline_(AllocationTracker::currentBytes()) {
    AllocationTracker::resetPeakToCurrent();
}

std::size_t AllocationScope::peakBytesSinceStart() const noexcept {
    const std::size_t peak = AllocationTracker::peakBytes();
    return peak > baseline_ ? peak - baseline_ : 0;
}

}  // namespace scx
