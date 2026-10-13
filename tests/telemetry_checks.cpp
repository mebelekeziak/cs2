#include "movement_telemetry.h"
#include <atomic>
#include <cstdio>
#include <cstring>

namespace {
unsigned calls = 0, failures = 0;
void Sink(const char* message) {
    ++calls;
    if (!std::strstr(message, "created_ms=") || !std::strstr(message, "MOVE_TEST")) ++failures;
    // Reentrant producer must not deadlock while a sink does work.
    if (calls == 1) MovementTelemetryPush("MOVE_TEST reentrant");
}
void Expect(bool ok, const char* text) { printf("%s %s\n", ok ? "PASS" : "FAIL", text); failures += !ok; }
}
int main() {
    MovementTelemetrySetCallback(Sink);
    MovementTelemetryPush("MOVE_TEST one");
    Expect(calls == 0, "producer never invokes file callback");
    MovementTelemetryDrain(1);
    Expect(calls == 1, "consumer drains only its bounded budget without holding producer lock");
    MovementTelemetryDrain(1);
    Expect(calls == 2, "callback can enqueue safely for a later drain");
    for (unsigned i = 0; i < 300; ++i) MovementTelemetryPush("MOVE_TEST overflow");
    Expect(MovementTelemetryDropped() == 172 && calls == 2, "full queue drops records instead of blocking producer on I/O");
    MovementTelemetryDrain(1000);
    Expect(calls == 130, "queued records drain exactly once");
    MovementTelemetrySetCallback(nullptr);
    MovementTelemetryPush("MOVE_TEST disabled");
    MovementTelemetryDrain(1000);
    Expect(calls == 130, "inactive sink produces no callback work");
    return failures ? 1 : 0;
}
