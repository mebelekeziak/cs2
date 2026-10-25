#pragma once
#include <cstdint>

using ViewmodelLogCallback = void (*)(const char*);
bool ViewmodelPrepare(ViewmodelLogCallback logger) noexcept;
void ViewmodelActivate() noexcept;
void ViewmodelStop() noexcept;
const char* ViewmodelStatus() noexcept;
void ViewmodelFlushTelemetry() noexcept;
// Called on the native view owner with a current local pawn. No retained state.
bool ViewmodelUnzoomedFov(std::uintptr_t pawn, float& fov) noexcept;

#ifdef CS2_VIEWMODEL_CHECKS
using ViewmodelParametersFn = void (*)(void*, void*, float*);
using ViewmodelViewFn = void (*)(void*, void*);
void ViewmodelTestConfigure(std::uintptr_t client, ViewmodelParametersFn native) noexcept;
void ViewmodelTestParameters(void* pawn, void* xyz, float* fov) noexcept;
void ViewmodelTestConfigureView(ViewmodelViewFn native) noexcept;
void ViewmodelTestView(void* mode, void* view) noexcept;
#endif
