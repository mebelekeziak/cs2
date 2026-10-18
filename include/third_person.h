#pragma once
#include <cstdint>

using ThirdPersonLogCallback = void (*)(const char*);
// Setup/stop outside DllMain. Hooks remain resident and forward after stop.
bool ThirdPersonPrepare(ThirdPersonLogCallback logger) noexcept;
void ThirdPersonActivate() noexcept;
void ThirdPersonStop(const char* reason) noexcept;
void ThirdPersonPollControls(bool foreground) noexcept;
const char* ThirdPersonStatus() noexcept;
void ThirdPersonFlushTelemetry() noexcept;
// True only for a recent successfully translated primary view of this live
// serialized pawn. Memory reads only; never invokes engine code on the renderer.
bool ThirdPersonViewActive(std::uintptr_t pawn, std::uintptr_t identity,
    std::uintptr_t controller, std::uint32_t handle) noexcept;

#ifdef CS2_THIRD_PERSON_CHECKS
#include <cstdint>
using CameraViewFn = void (*)(void*, void*);
using CameraQueryFn = int (*)(void*, int);
using CameraShapeFn = void (*)(void*, const float*);
using CameraResultFn = void (*)(void*);
using CameraFilterFn = void (*)(void*, void*, std::uint64_t, std::uint8_t, std::uint8_t);
using CameraTraceFn = bool (*)(void*, void*, const void*, const void*, void*, void*);
void ThirdPersonTestConfigure(std::uintptr_t base, CameraViewFn view, CameraQueryFn query,
    CameraShapeFn shape, CameraResultFn result, CameraFilterFn filter, CameraTraceFn trace) noexcept;
void ThirdPersonTestView(void* mode, void* view) noexcept;
int ThirdPersonTestQuery(void* input, int slot, std::uintptr_t caller = 0) noexcept;
#endif
