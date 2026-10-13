#include "movement_steering.h"
#include <array>
#include <chrono>
#include <cstdio>
#ifdef CS2_STEERING_BENCHMARK
unsigned int SteeringCandidateEvaluationsForBenchmark() noexcept;
#endif

// Deterministic input corpus: ordinary commands, fractional fragments, weak
// acceleration, and projection-clipped impulses. No live game timing claim.
int main() {
    constexpr std::array<AirPhysics, 8> physics{{
        {250, 30, 12, 1, 1.0f / 64}, {250, 30, 12, 0.2f, 0.004f},
        {250, 30, 12, 1, 1.0f / 32}, {250, 3, 100, 1, 0.1f},
        {250, 250, 0.01f, 1, 0.0001f}, {250, 30, 0, 1, 1.0f / 64},
        {250, 250, 100, 1, 0.1f}, {250, 30, 12, 2, 1.0f / 64}
    }};
    constexpr int commands = 300000;
    unsigned int maximumEvaluations = 0;
    for (int trial = 0; trial < 5; ++trial) {
        SteeringState state{};
        double checksum = 0;
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < commands; ++i) {
            const float speed = (i % 3 == 0) ? 30.0f : (i % 3 == 1 ? 250.0f : 800.0f);
            const auto out = PlanAirSteering(state,
                {speed, static_cast<float>(i % 7 - 3), static_cast<float>(i % 360 - 180),
                 i % 3 == 0 ? 1.0f : (i % 3 == 1 ? -1.0f : 0.0f),
                 i % 4 == 0 ? 0.0f : (i % 4 == 1 ? 1.0f : -1.0f),
                 static_cast<std::uint32_t>(i + 1)}, physics[i % physics.size()]);
            checksum += out.predictedVelocityX + out.predictedVelocityY + out.forward + out.left;
#ifdef CS2_STEERING_BENCHMARK
            const unsigned int count = SteeringCandidateEvaluationsForBenchmark();
            if (count > maximumEvaluations) maximumEvaluations = count;
#endif
        }
        const double microseconds = std::chrono::duration<double, std::micro>(
            std::chrono::steady_clock::now() - start).count();
        std::printf("trial=%d commands=%d us_per_command=%.6f checksum=%.9f\n",
            trial + 1, commands, microseconds / commands, checksum);
    }
#ifdef CS2_STEERING_BENCHMARK
    // The cost bound is deterministic; elapsed time is reported without flaky
    // hardware/load-dependent pass thresholds. Projection refinement has a
    // separate fixed scalar bound; full candidate simulation remains <=2.
    std::printf("maximum_candidate_evaluations=%u bound=2\n", maximumEvaluations);
    return maximumEvaluations > 2 ? 1 : 0;
#else
    (void)maximumEvaluations;
#endif
}
