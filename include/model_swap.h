#pragma once

using ModelSwapLogCallback = void (*)(const char*);
// Setup outside DllMain. Entity methods run only on the verified native frame
// callback. Stop disarms replacements and requests restoration on that owner.
// Hooks, trampolines and stopped code stay resident until normal process exit.
bool ModelSwapPrepare(ModelSwapLogCallback logger) noexcept;
void ModelSwapActivate() noexcept;
void ModelSwapStop(const char* reason) noexcept;
void ModelSwapFlushTelemetry() noexcept;

#ifdef CS2_MODEL_SWAP_CHECKS
#include <cstdint>
using ModelSwapFrameFn = void (*)(void*, int);
using ModelSwapSetFn = void (*)(void*, const char*);
using ModelSwapLoadFn = bool (*)(const char*);
void ModelSwapTestConfigure(std::uintptr_t client, ModelSwapFrameFn frame, ModelSwapSetFn setter,
    ModelSwapLoadFn loader = nullptr) noexcept;
void ModelSwapTestFrame(void* owner, int stage) noexcept;
void ModelSwapTestSetAvailable(bool value) noexcept;
#endif
