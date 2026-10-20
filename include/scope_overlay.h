#pragma once
#include <cstdint>
using ScopeOverlayLogCallback = void (*)(const char*);
bool ScopeOverlayPrepare(ScopeOverlayLogCallback logger) noexcept;
void ScopeOverlayActivate() noexcept;
void ScopeOverlayStop() noexcept;
const char* ScopeOverlayStatus() noexcept;

#ifdef CS2_SCOPE_OVERLAY_CHECKS
using ScopeOverlayNativeFn = void (*)(void*, void*);
using ScopeOverlayIronSightFn = bool (*)(void*);
void ScopeOverlayTestConfigure(std::uintptr_t client, ScopeOverlayNativeFn native,
    ScopeOverlayIronSightFn ironSight) noexcept;
void ScopeOverlayTestCall(void* pawn, void* output, std::uintptr_t caller) noexcept;
bool ScopeOverlayTestIronSight(void* controller, std::uintptr_t caller) noexcept;
#endif
