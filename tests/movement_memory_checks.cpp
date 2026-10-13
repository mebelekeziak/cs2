#include "movement_memory.h"
#include <array>
#include <cstdio>
int main() {
    std::array<unsigned, 8> source{1,2,3,4,5,6,7,8}, result{};
    int failures = 0;
    auto expect = [&](bool ok, const char* label) { printf("%s %s\n", ok ? "PASS" : "FAIL", label); failures += !ok; };
    expect(MovementCopy(reinterpret_cast<std::uintptr_t>(source.data()), result.data(), sizeof(result)) && source == result,
           "guarded in-process copy preserves the snapshot");
    expect(MovementOwnedWritable(reinterpret_cast<std::uintptr_t>(source.data()), sizeof(source)) && source == result,
           "owned writable probe preserves every command byte");
    expect(!MovementOwnedWritable(0, sizeof(source)) && !MovementOwnedWritable(UINTPTR_MAX, sizeof(source)),
           "owned writable probe rejects null and noncanonical ranges");
    expect(!MovementCopy(0, result.data(), sizeof(result)), "null source rejected");
    expect(!MovementCopy(UINTPTR_MAX, result.data(), sizeof(result)), "overflow and noncanonical source rejected");
    void* inaccessible = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_NOACCESS);
    expect(inaccessible && !MovementCopy(reinterpret_cast<std::uintptr_t>(inaccessible), result.data(), sizeof(result)),
           "inaccessible source fails without an escaping access violation");
    expect(inaccessible && !MovementOwnedWritable(reinterpret_cast<std::uintptr_t>(inaccessible), sizeof(result)),
           "owned writable probe rejects inaccessible storage without an escaping exception");
    if (inaccessible) VirtualFree(inaccessible, 0, MEM_RELEASE);
    SYSTEM_INFO system{}; GetSystemInfo(&system);
    const auto pageSize = system.dwPageSize;
    auto* pages = static_cast<unsigned char*>(VirtualAlloc(nullptr, 2 * pageSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    DWORD previous = 0;
    const bool protectedPage = pages && VirtualProtect(pages + pageSize, pageSize, PAGE_READONLY, &previous);
    expect(protectedPage && !MovementOwnedWritable(reinterpret_cast<std::uintptr_t>(pages + pageSize), sizeof(result)),
           "owned writable probe rejects a readable but immutable page");
    if (pages) pages[pageSize - 1] = 73;
    expect(protectedPage && !MovementOwnedWritable(reinterpret_cast<std::uintptr_t>(pages + pageSize - 1), 2) &&
           pages[pageSize - 1] == 73 && pages[pageSize] == 0,
           "cross-page writable probe fails while preserving the readable prefix and immutable page");
    if (pages) VirtualFree(pages, 0, MEM_RELEASE);
    std::array<std::byte, 0x60> storage{};
    std::uint8_t mode = 1, observed = 0;
    std::memcpy(storage.data() + 0x58, &mode, sizeof(mode));
    const std::array<std::uintptr_t, 3> wrapper{0x1234, reinterpret_cast<std::uintptr_t>(storage.data()), 0x4321};
    const auto wrapperAddress = reinterpret_cast<std::uintptr_t>(wrapper.data());
    expect(MovementReadCvar(wrapperAddress, observed) && observed == mode,
           "Cvar reader receives wrapper start and follows its value pointer");
    expect(!MovementReadCvar(wrapperAddress + 8, observed),
           "passing the value-pointer slot as a wrapper fails instead of inventing jump rules");
    LARGE_INTEGER frequency{}, begin{}, end{};
    QueryPerformanceFrequency(&frequency);
    constexpr int iterations = 200000;
    QueryPerformanceCounter(&begin);
    for (int i = 0; i < iterations; ++i) {
        SIZE_T copied = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), source.data(), result.data(), sizeof(result), &copied)) return 2;
    }
    QueryPerformanceCounter(&end);
    const double kernelUs = 1000000.0 * (end.QuadPart - begin.QuadPart) / frequency.QuadPart / iterations;
    QueryPerformanceCounter(&begin);
    volatile unsigned checksum = 0;
    for (int i = 0; i < iterations; ++i) {
        if (!MovementCopy(reinterpret_cast<std::uintptr_t>(source.data()), result.data(), sizeof(result))) return 2;
        checksum = result[static_cast<std::size_t>(i) % result.size()];
    }
    QueryPerformanceCounter(&end);
    const double directUs = 1000000.0 * (end.QuadPart - begin.QuadPart) / frequency.QuadPart / iterations;
    printf("COPY_BENCHMARK kernel_us=%.4f guarded_us=%.4f ratio=%.2f checksum=%u\n", kernelUs, directUs, kernelUs / directUs, checksum);
    return failures ? 1 : 0;
}
