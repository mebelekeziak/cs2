#pragma once
#include <cstdint>

using AntiAimNativeReplay = void (*)(void*, int);
using AntiAimReplaySourceValid = bool (*)(void*);
// Calls native exactly once. Preserve the fresh physical view and advance
// input's previous directions to its current physical directions.
// Native action buttons and changed/tap resets remain native. The owner keeps
// all fields alive; validation after native rejects source/lifecycle changes.
bool AntiAimReplayPreservingInput(std::uintptr_t angles, std::uintptr_t currentButtons,
    std::uintptr_t previousButtons, bool preserve,
    AntiAimNativeReplay native, void* input, int slot,
    AntiAimReplaySourceValid valid, void* context) noexcept;
