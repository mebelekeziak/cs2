#pragma once

#include "esp_draw.h"

// Present-thread owned. Reads copied memory only; never calls engine methods.
void PlayerReaderSetLogCallback(EspLogCallback callback) noexcept;
EspFrameSnapshot PlayerReaderCapture();
void PlayerReaderReportDraw(const EspDrawStats& stats, int espVertices, int totalVertices, float width, float height);
const char* PlayerReaderStatus() noexcept;
void PlayerReaderInvalidate(const char* reason) noexcept;
