#pragma once
#include <cstdint>

using CosmeticRecoilLogCallback = void (*)(const char*);
bool CosmeticRecoilPrepare(CosmeticRecoilLogCallback logger) noexcept;
void CosmeticRecoilActivate() noexcept;
void CosmeticRecoilStop() noexcept;
const char* CosmeticRecoilStatus() noexcept;
void CosmeticRecoilFlushTelemetry() noexcept;

#ifdef CS2_COSMETIC_RECOIL_CHECKS
using CosmeticRecoilNativeFn = void* (*)(void*, void*);
using CosmeticRecoilCameraFn = void (*)(void*, void*, void*, float*);
using CosmeticRecoilAngleAddFn = void (*)(const float*, const float*, float*);
void CosmeticRecoilTestConfigure(std::uintptr_t client, CosmeticRecoilNativeFn native) noexcept;
void* CosmeticRecoilTestCall(void* service, void* output, std::uintptr_t caller) noexcept;
void CosmeticRecoilTestSetLogger(CosmeticRecoilLogCallback logger) noexcept;
void CosmeticRecoilTestConfigureCamera(CosmeticRecoilCameraFn base, CosmeticRecoilCameraFn normal,
    CosmeticRecoilAngleAddFn add) noexcept;
void CosmeticRecoilTestCamera(void* camera, void* origin, void* angles, float* fov, bool base = false) noexcept;
void CosmeticRecoilTestAngleAdd(const float* left, const float* right, float* output, std::uintptr_t caller) noexcept;
#endif
