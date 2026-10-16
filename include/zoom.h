#pragma once

using ZoomLogCallback = void (*)(const char*);
// Setup/stop outside DllMain. Stop drains edits; forwarding hooks remain resident.
bool ZoomPrepare(ZoomLogCallback logger) noexcept;
void ZoomActivate() noexcept;
void ZoomStop(const char* reason) noexcept;
const char* ZoomStatus() noexcept;
void ZoomFlushTelemetry() noexcept;

// Strength is 0..200 percent: zero is baseline, 100 preserves native FOV.
bool ZoomComputeFov(float baseline, float native, float strength, float& output) noexcept;

#ifdef CS2_ZOOM_CHECKS
#include <cstdint>
using ZoomViewFn = void (*)(void*, void*);
using ZoomBaselineFn = float (*)(void*);
void ZoomTestConfigure(std::uintptr_t base, ZoomViewFn original, ZoomBaselineFn baseline) noexcept;
void ZoomTestView(void* mode, void* view) noexcept;
#endif
