#include "movement_steering.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#ifdef CS2_STEERING_BENCHMARK
unsigned int SteeringCandidateEvaluationsForBenchmark() noexcept;
unsigned int SteeringCommandEvaluationsForBenchmark() noexcept;
#endif

// One shared deterministic corpus covers direct planner and complete command
// costs. Elapsed time is reported without hardware-dependent thresholds.
int main() {
    constexpr std::array<AirPhysics, 8> physics{{
        {250, 30, 12, 1, 1.0f / 64}, {250, 30, 12, 0.2f, 0.004f},
        {250, 30, 12, 1, 1.0f / 32}, {250, 3, 100, 1, 0.1f},
        {250, 250, 0.01f, 1, 0.0001f}, {250, 30, 0, 1, 1.0f / 64},
        {250, 250, 100, 1, 0.1f}, {250, 30, 12, 2, 1.0f / 64}
    }};
    constexpr int commands = 100000;
    constexpr std::array<float, 9> speeds{{0, 9.99f, 10.01f, 30, 79.99f, 80, 80.01f, 250, 800}};
    unsigned int maximumFragmentEvaluations = 0, maximumCommandEvaluations = 0;
    bool validCounts = true;
    for (bool whole : {false, true}) {
        for (int trial = 0; trial < 5; ++trial) {
            SteeringState state{};
            double checksum = 0;
            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < commands; ++i) {
                const float speed = speeds[i % speeds.size()];
                const StrafeInput input{
                    speed, static_cast<float>(i % 7 - 3), static_cast<float>(i % 360 - 180),
                    i % 3 == 0 ? 1.0f : (i % 3 == 1 ? -1.0f : 0.0f),
                    i % 4 == 0 ? 0.0f : (i % 4 == 1 ? 1.0f : -1.0f),
                    static_cast<std::uint32_t>(i + 1)};
                const bool hasIntent = input.forward != 0 || input.left != 0;
                SteeringOutput output{};
                if (whole) {
                    const auto command = PlanAirSteeringCommand(state, input, physics[i % physics.size()]);
                    validCounts &= hasIntent ? command.valid && command.count == AirSteeringSliceCount : !command.valid && !command.count;
                    output = command.summary;
#ifdef CS2_STEERING_BENCHMARK
                    const unsigned int count = SteeringCommandEvaluationsForBenchmark();
                    maximumCommandEvaluations = std::max(maximumCommandEvaluations, count);
                    validCounts &= hasIntent ? count == AirSteeringSliceCount : count == 0;
#endif
                } else {
                    output = PlanAirSteering(state, input, physics[i % physics.size()]);
#ifdef CS2_STEERING_BENCHMARK
                    const unsigned int count = SteeringCandidateEvaluationsForBenchmark();
                    maximumFragmentEvaluations = std::max(maximumFragmentEvaluations, count);
                    validCounts &= hasIntent ? count == 1 : count == 0;
#endif
                }
                checksum += output.predictedVelocityX + output.predictedVelocityY + output.forward + output.left;
            }
            const double microseconds = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
            std::printf("path=%s trial=%d commands=%d us_per_command=%.6f checksum=%.9f\n",
                        whole ? "projection_timed_command" : "projection_direct_planner", trial + 1,
                        commands, microseconds / commands, checksum);
        }
    }
#ifdef CS2_STEERING_BENCHMARK
    std::printf("maximum_fragment_candidate_evaluations=%u bound=1 maximum_command_candidate_evaluations=%u bound=32 exact_counts=%s\n",
                maximumFragmentEvaluations, maximumCommandEvaluations, validCounts ? "pass" : "fail");
    return !validCounts || maximumFragmentEvaluations > 1 || maximumCommandEvaluations > AirSteeringSliceCount ? 1 : 0;
#else
    (void)maximumFragmentEvaluations; (void)maximumCommandEvaluations;
    return validCounts ? 0 : 1;
#endif
}
