// Link this file into an executable to make AllocationTracker live. It replaces the global
// allocation functions; each block gets a 16-byte header holding its size (16 keeps the usual
// alignment guarantee of operator new).
#include <cstdlib>
#include <new>

#include "util/AllocationTracker.h"

namespace {
constexpr std::size_t kHeader = 16;

struct Installer {
    Installer() { scx::AllocationTracker::markHooksInstalled(); }
} installer;

void* allocate(std::size_t size) {
    void* raw = std::malloc(size + kHeader);
    if (!raw) return nullptr;
    *static_cast<std::size_t*>(raw) = size;
    scx::AllocationTracker::recordAllocation(size);
    return static_cast<char*>(raw) + kHeader;
}

void release(void* pointer) noexcept {
    if (!pointer) return;
    void* raw = static_cast<char*>(pointer) - kHeader;
    scx::AllocationTracker::recordRelease(*static_cast<std::size_t*>(raw));
    std::free(raw);
}
}  // namespace

void* operator new(std::size_t size) {
    if (void* pointer = allocate(size)) return pointer;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) {
    if (void* pointer = allocate(size)) return pointer;
    throw std::bad_alloc();
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return allocate(size); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return allocate(size); }
void operator delete(void* pointer) noexcept { release(pointer); }
void operator delete[](void* pointer) noexcept { release(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { release(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { release(pointer); }
void operator delete(void* pointer, const std::nothrow_t&) noexcept { release(pointer); }
void operator delete[](void* pointer, const std::nothrow_t&) noexcept { release(pointer); }
