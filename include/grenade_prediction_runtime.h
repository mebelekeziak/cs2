#pragma once
#include "grenade_prediction_draw.h"
using GrenadePredictionLogCallback = void (*)(const char*);
bool GrenadePredictionPrepare(GrenadePredictionLogCallback logger) noexcept;
void GrenadePredictionActivate() noexcept;
void GrenadePredictionStop() noexcept;
const char* GrenadePredictionStatus() noexcept;
void GrenadePredictionRender(const EspMatrix4x4&, bool valid, float width, float height);
#ifdef CS2_GRENADE_PREDICTION_CHECKS
#include <cstdint>
struct GrenadePredictionTestNative {
    bool (*launch)(void*,void*,EspVec3*,EspVec3*,bool)=nullptr;
    float* (*time)(float*,int)=nullptr;
    void (*shape)(void*,const float*)=nullptr;
    void (*result)(void*)=nullptr;
    void (*filter)(void*,void*,std::uint64_t,std::uint8_t,std::uint8_t)=nullptr;
    bool (*trace)(void*,void*,const void*,const void*,void*,void*)=nullptr;
    EspVec3* (*velocity)(void*,EspVec3*)=nullptr;
    std::uint64_t (*budgetNowUs)()=nullptr;
    bool (*heldPrediction)(std::uintptr_t,const GrenadeState&,GrenadePath&)=nullptr;
};
void GrenadePredictionTestConfigure(std::uintptr_t,const GrenadePredictionTestNative&) noexcept;
void GrenadePredictionTestFrame() noexcept;
std::vector<GrenadePredictionVisual> GrenadePredictionTestCapture();
#endif
