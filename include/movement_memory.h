#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include "movement_offsets.h"

// In-process copied snapshots on the engine callback owner. Unmapped sources
// fail through SEH instead of issuing a kernel read for each scalar field.
inline bool MovementCopy(std::uintptr_t address, void* output, std::size_t bytes) noexcept {
    constexpr std::uintptr_t maximum = 0x00007fffffffffffULL;
    if (!output || bytes > maximum || address < 0x10000 || address > maximum - bytes) return false;
    __try {
        std::memcpy(output, reinterpret_cast<const void*>(address), bytes);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Only for command/container bytes owned by the current engine callback. A
// guarded write-back of each byte proves accessibility without a kernel page
// query. This preserves bytes, requires exclusive ownership, and never caches
// permissions across commands or allocation lifecycles.
inline bool MovementOwnedWritable(std::uintptr_t address, std::size_t bytes) noexcept {
    constexpr std::uintptr_t maximum = 0x00007fffffffffffULL;
    if (!bytes || bytes > maximum || address < 0x10000 || address > maximum - bytes) return false;
    __try {
        auto* memory = reinterpret_cast<volatile unsigned char*>(address);
        for (std::size_t i = 0; i < bytes; ++i) {
            const unsigned char value = memory[i];
            memory[i] = value;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

template<class T> bool MovementReadCvar(std::uintptr_t wrapper, T& value) noexcept {
    std::uintptr_t storage = 0;
    return MovementCopy(wrapper + movement_offsets::CvarValuePointer, &storage, sizeof(storage)) &&
        storage >= 0x10000 && storage <= 0x00007fffffffffffULL - movement_offsets::CvarValue &&
        MovementCopy(storage + movement_offsets::CvarValue, &value, sizeof(value));
}
