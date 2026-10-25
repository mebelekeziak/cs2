#include "client_frame.h"
#include "model_swap_offsets.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <thread>

namespace {
std::atomic<unsigned> nativeCalls{0}, firstCalls{0}, secondCalls{0}, failures{0};
std::atomic<unsigned> extraCalls{0};
std::atomic<bool> nested{false}, mutateOwner{false};
constexpr std::uintptr_t base = 0x10000000;
void Expect(bool condition, const char* label) {
    if (!condition) { ++failures; std::printf("FAIL %s\n", label); }
}
void Native(void* owner, int stage) {
    ++nativeCalls;
    if (nested.exchange(false)) ClientFrameTestDispatch(owner, stage);
    if (mutateOwner) *static_cast<std::uintptr_t*>(owner) = 0;
}
void First(void*, int stage) noexcept {
    if (stage != model_swap_offsets::ApplyStage || !nativeCalls || !ClientFrameInCallback()) ++failures;
    ++firstCalls;
}
void Second(void*, int) noexcept { ++secondCalls; }
void Third(void*, int) noexcept { extraCalls.fetch_add(3); }
void Fourth(void*, int) noexcept { extraCalls.fetch_add(4); }
void Fifth(void*, int) noexcept { extraCalls.fetch_add(5); }
void Sixth(void*, int) noexcept { extraCalls.fetch_add(6); }
void Seventh(void*, int) noexcept { extraCalls.fetch_add(7); }
void Eighth(void*, int) noexcept { extraCalls.fetch_add(8); }
void Ninth(void*, int) noexcept { extraCalls.fetch_add(9); }
}
int main() {
    ClientFrameTestConfigure(base, &Native);
    Expect(!ClientFrameInCallback(), "owner callback scope starts clear");
    Expect(!ClientFrameSubscribe(nullptr), "null subscriber rejected");
    Expect(ClientFrameSubscribe(&First) && ClientFrameSubscribe(&First) && ClientFrameSubscribe(&Second),
        "distinct features register; repeated subscription is idempotent");
    std::uintptr_t owner = base + model_swap_offsets::FrameVtable;
    for (int stage = 0; stage < 10; ++stage) ClientFrameTestDispatch(&owner, stage);
    Expect(nativeCalls == 10 && firstCalls == 1 && secondCalls == 1, "all native stages forwarded, only owner stage dispatched once per subscriber");
    nested = true;
    ClientFrameTestDispatch(&owner, model_swap_offsets::ApplyStage);
    Expect(nativeCalls == 12 && firstCalls == 2 && secondCalls == 2, "nested native callbacks forwarded without duplicate subscriber execution");
    owner = 0;
    ClientFrameTestDispatch(&owner, model_swap_offsets::ApplyStage);
    Expect(nativeCalls == 13 && firstCalls == 2, "other native receiver forwarded without subscriber execution");
    owner = base + model_swap_offsets::FrameVtable;
    mutateOwner = true;
    ClientFrameTestDispatch(&owner, model_swap_offsets::ApplyStage);
    Expect(nativeCalls == 14 && firstCalls == 2, "receiver checked after native lifecycle changes");
    mutateOwner = false;
    ClientFrameTestDispatch(nullptr, model_swap_offsets::ApplyStage);
    Expect(nativeCalls == 15 && firstCalls == 2, "unreadable receiver forwarded without dispatch");
    Expect(ClientFrameSubscribe(&Third) && ClientFrameSubscribe(&Fourth) && ClientFrameSubscribe(&Fifth) &&
        ClientFrameSubscribe(&Sixth) && ClientFrameSubscribe(&Seventh) && ClientFrameSubscribe(&Eighth) && !ClientFrameSubscribe(&Ninth),
        "fixed subscriber capacity rejects excess without corrupting registrations");
    owner = base + model_swap_offsets::FrameVtable;
    std::array<std::thread, 4> workers;
    for (auto& worker : workers) worker = std::thread([&] {
        for (unsigned i = 0; i < 100; ++i) ClientFrameTestDispatch(&owner, model_swap_offsets::ApplyStage);
    });
    for (auto& worker : workers) worker.join();
    Expect(nativeCalls == 415 && firstCalls == 402 && secondCalls == 402 && extraCalls == 13200,
        "thread-local nesting state isolates simultaneous owner callbacks");
    Expect(!ClientFrameInCallback(), "owner callback scope clears on return");
    std::printf("Client frame failures: %u\n", failures.load());
    return failures ? 1 : 0;
}
