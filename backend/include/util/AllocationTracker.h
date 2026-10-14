#pragma once
// Measures heap memory allocated by C++ code between two points.
//
// The counters live in scx_core, but they only move if the executable also links
// src/memory/AllocationHooks.cpp, which replaces global operator new/delete and reports every
// allocation here. Executables without the hooks (most unit tests) simply see isActive() == false,
// and callers must then report memory as "not measured" instead of 0.
//
// Scope of the number: peak bytes requested through operator new (std::vector, std::string, ...)
// while the scope was open, relative to the level when it started. It excludes stack memory and
// allocator overhead, and it is only meaningful while no other thread allocates (the API serialises
// algorithm runs behind a mutex for this reason).

#include <cstddef>

namespace scx {

class AllocationTracker {
public:
    static void recordAllocation(std::size_t bytes) noexcept;   // called by the hooks
    static void recordRelease(std::size_t bytes) noexcept;      // called by the hooks
    static void markHooksInstalled() noexcept;
    static bool isActive() noexcept;
    static std::size_t currentBytes() noexcept;
    static std::size_t peakBytes() noexcept;
    static void resetPeakToCurrent() noexcept;
};

// RAII measurement: peakBytesSinceStart() = highest live heap during the scope minus the live heap at its start.
class AllocationScope {
public:
    AllocationScope() noexcept;
    std::size_t peakBytesSinceStart() const noexcept;
private:
    std::size_t baseline_;
};

}  // namespace scx
