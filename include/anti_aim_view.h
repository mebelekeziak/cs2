#pragma once
#include <cstdint>

using AntiAimNativeReplay = void (*)(void*, int);
using AntiAimReplaySourceValid = bool (*)(void*);
// Calls native exactly once. The callback owner must keep angle storage alive;
// validation after native rejects source/lifecycle changes before restoring.
bool AntiAimReplayPreservingView(std::uintptr_t angles, bool preserve,
    AntiAimNativeReplay native, void* input, int slot,
    AntiAimReplaySourceValid valid, void* context) noexcept;
