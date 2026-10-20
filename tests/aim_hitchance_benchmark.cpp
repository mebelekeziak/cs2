#include "aim_hitchance.h"
#include "aim_hitchance_reference.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
std::uint64_t sink = 0;
struct Measurement { double median = 0, p95 = 0; };
template<class Work> Measurement Measure(Work work) {
    std::array<double, 41> times{};
    for (unsigned iteration = 0; iteration < times.size() + 4; ++iteration) {
        const auto start = Clock::now();
        sink += work();
        const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        if (iteration >= 4) times[iteration - 4] = elapsed;
    }
    std::sort(times.begin(), times.end());
    return {times[times.size() / 2], times[(times.size() - 1) * 95 / 100]};
}
std::vector<AimHitchanceInput> Scene(unsigned enemies, unsigned samples, bool zeroSpread) {
    std::vector<AimHitchanceInput> result;
    for (unsigned enemy = 0; enemy < enemies; ++enemy) for (unsigned region = 0; region < 6; ++region) {
        AimHitchanceInput input{};
        input.eye = {17, -29, 64};
        const float distance = 700.0f + 275.0f * enemy;
        const float yaw = static_cast<float>(enemy * 17 + region);
        const float radians = yaw * .01745329251994329577f;
        const AimVector center{input.eye.x + distance * std::cos(radians),
            input.eye.y + distance * std::sin(radians), input.eye.z};
        const float height = region == 0 ? 2.0f : 4.0f;
        input.angles = {0, yaw, 0};
        input.capsule = {{center.x, center.y, center.z - height},
            {center.x, center.y, center.z + height}, region == 0 ? 2.8f : 5.0f};
        input.inaccuracy = zeroSpread ? 0 : .012f; input.spread = zeroSpread ? 0 : .004f;
        input.range = 8192; input.samples = samples;
        result.push_back(input);
    }
    return result;
}
}

int main() {
    std::puts("Synthetic copied-geometry benchmark; no native reads or traces; preparation included per decision.");
    std::puts("enemies,regions,samples,zero_spread,reference_median_ms,scalar_median_ms,prepared_median_ms,reference_p95_ms,prepared_p95_ms,speedup");
    for (bool zeroSpread : {false, true}) for (unsigned samples : {256u, 1024u}) for (unsigned enemies : {1u, 3u, 5u}) {
        const auto scene = Scene(enemies, samples, zeroSpread);
        const auto reference = Measure([&] {
            unsigned hits = 0;
            for (const auto& input : scene) hits += aim_hitchance_reference::AimEstimateHitchance(input).hits;
            return hits;
        });
        const auto scalar = Measure([&] {
            unsigned hits = 0;
            for (const auto& input : scene) hits += AimEstimateHitchance(input).hits;
            return hits;
        });
        const auto prepared = Measure([&] {
            AimSpreadPattern pattern{};
            AimPrepareSpreadPattern(scene.front(), pattern);
            unsigned hits = 0;
            for (const auto& input : scene) hits += AimEstimateHitchance(input, pattern).hits;
            return hits;
        });
        std::printf("%u,6,%u,%u,%.6f,%.6f,%.6f,%.6f,%.6f,%.2f\n", enemies, samples, zeroSpread,
            reference.median, scalar.median, prepared.median, reference.p95, prepared.p95,
            reference.median / prepared.median);
    }
    std::printf("checksum=%llu\n", static_cast<unsigned long long>(sink));
}
