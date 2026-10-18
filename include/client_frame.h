#pragma once

using ClientFrameCallback = void (*)(void*, int) noexcept;
using ClientFrameLogCallback = void (*)(const char*);
// Startup registration only. The shared native owner forwards every stage and
// dispatches subscribers after the outer stage-8 callback, on its engine thread.
// Subscribers retain their own activation/stop barriers; callbacks stay resident.
bool ClientFrameSubscribe(ClientFrameCallback callback,
    ClientFrameLogCallback logger = nullptr) noexcept;
// Allows cooperative stop pumped by a blocking native owner call to defer
// feature restoration without re-acquiring its own outer frame lock.
bool ClientFrameInCallback() noexcept;
#ifdef CS2_CLIENT_FRAME_CHECKS
#include <cstdint>
void ClientFrameTestConfigure(std::uintptr_t base, void (*original)(void*, int)) noexcept;
void ClientFrameTestDispatch(void* owner, int stage) noexcept;
#endif
