#pragma once
#include "grenade_draw.h"
using GrenadeVisualsLogCallback = void (*)(const char*);
bool GrenadeVisualsPrepare(GrenadeVisualsLogCallback logger) noexcept;
void GrenadeVisualsActivate() noexcept;
void GrenadeVisualsStop() noexcept;
const char* GrenadeVisualsStatus() noexcept;
void GrenadeVisualsDraw(const EspMatrix4x4& matrix, bool matrixValid, float width, float height);

#ifdef CS2_GRENADE_VISUALS_CHECKS
#include <cstdint>
bool GrenadeVisualsTestConfigure(std::uintptr_t client) noexcept;
void GrenadeVisualsTestFrame() noexcept;
std::vector<GrenadeArea> GrenadeVisualsTestCapture();
#endif
