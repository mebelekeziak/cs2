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

template<class T> bool MovementReadCvar(std::uintptr_t wrapper, T& value) noexcept {
    std::uintptr_t storage = 0;
    return MovementCopy(wrapper + movement_offsets::CvarValuePointer, &storage, sizeof(storage)) &&
        storage >= 0x10000 && storage <= 0x00007fffffffffffULL - movement_offsets::CvarValue &&
        MovementCopy(storage + movement_offsets::CvarValue, &value, sizeof(value));
}
