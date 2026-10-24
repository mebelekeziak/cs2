#pragma once

using SkinChangerLogCallback = void (*)(const char*);
// Register outside DllMain, before shared client-frame hook activation. Native
// item/model functions execute only after the original stage-8 owner callback.
bool SkinChangerPrepare(SkinChangerLogCallback logger) noexcept;
void SkinChangerActivate() noexcept;
// Cooperatively restore on subsequent owner frames. Callbacks and code remain
// resident until normal process exit; stopping never physically unloads a DLL.
void SkinChangerStop(const char* reason) noexcept;
void SkinChangerFlushTelemetry() noexcept;

#ifdef CS2_SKIN_CHANGER_CHECKS
#include <cstdint>
using SkinChangerAttributeFn = void (*)(void*, const char*, float);
using SkinChangerRemoveFn = void (*)(void*, std::uint16_t);
using SkinChangerRefreshFn = void (*)(void*);
using SkinChangerModelFn = void (*)(void*, const char*);
using SkinChangerLoadFn = bool (*)(const char*);
void SkinChangerTestConfigure(std::uintptr_t client, void (*frame)(void*, int),
    SkinChangerAttributeFn attribute, SkinChangerRemoveFn remove,
    SkinChangerRefreshFn refresh, SkinChangerModelFn model,
    SkinChangerLoadFn load) noexcept;
void SkinChangerTestFrame(void* owner, int stage) noexcept;
#endif
