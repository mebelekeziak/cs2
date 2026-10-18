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

// Strength is 0..100 percent: zero is unscoped, 100 preserves native sensitivity.
bool ZoomBlendSensitivity(float baseline, float native, float strength, float& output) noexcept;

#ifdef CS2_ZOOM_CHECKS
#include <cstdint>
using ZoomViewFn = void (*)(void*, void*);
using ZoomBaselineFn = float (*)(void*);
using ZoomMouseFn = void (*)(int, float, float, float, float*, float*, int*, int*);
using ZoomMouseAlternateFn = void (*)(int, float, float, float, float, float*, float*);
void ZoomTestConfigure(std::uintptr_t base, ZoomViewFn original, ZoomBaselineFn baseline) noexcept;
void ZoomTestView(void* mode, void* view) noexcept;
void ZoomTestConfigureMouse(ZoomMouseFn normal, ZoomMouseAlternateFn alternate) noexcept;
void ZoomTestMouse(int slot, float dx, float dy, float scale, float* yaw, float* pitch, int* mouseDx, int* mouseDy) noexcept;
void ZoomTestMouseAlternate(int slot, float dx, float dy, float scale, float deltaTime, float* yaw, float* pitch) noexcept;
#endif
