#pragma once

#include <Windows.h>

// The mapper calls the PE CRT entrypoint first, then this export on its remote
// bootstrap thread, outside the Windows loader lock. OverlayStart must copy the
// supplied UTF-16 log path before returning if another thread will use it.
using OverlayStartFunction = DWORD(WINAPI*)(const wchar_t* logPath);
using OverlayRequestStopFunction = DWORD(WINAPI*)(void*);

inline constexpr const char* kOverlayStartExport = "OverlayStart";
inline constexpr const char* kOverlayStopExport = "OverlayRequestStop";

// Stop is cooperative. This mapper never calls DLL_PROCESS_DETACH, removes an
// unwind table, frees a started image, or physically unloads stopped code.
