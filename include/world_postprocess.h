#pragma once
#include "world_settings.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
struct ID3D11RenderTargetView;
using WorldPostprocessLogCallback = void (*)(const char* message);

bool WorldPostprocessIsActive(const WorldSettings& settings) noexcept;
// Render, Release and Status belong to the serialized Present/Resize owner.
// True includes disabled/neutral no-ops; false means the frame is untouched.
bool WorldPostprocessRender(ID3D11Device* device, ID3D11DeviceContext* context,
    ID3D11Texture2D* sourceBackbuffer, ID3D11RenderTargetView* target,
    const WorldSettings& settings) noexcept;
void WorldPostprocessRelease() noexcept;
const char* WorldPostprocessStatus() noexcept;
void WorldPostprocessSetLogCallback(WorldPostprocessLogCallback callback) noexcept;

