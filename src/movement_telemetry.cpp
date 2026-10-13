#include "movement_telemetry.h"
#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdio>

namespace {
struct Record { char message[1536]{}; };
std::array<Record, 128> records{};
SRWLOCK queueLock = SRWLOCK_INIT;
unsigned head = 0, count = 0;
std::atomic<unsigned long long> dropped{0};
std::atomic<MovementLogCallback> sink{nullptr};
}

void MovementTelemetrySetCallback(MovementLogCallback callback) noexcept { sink.store(callback, std::memory_order_release); }
void MovementTelemetryPush(const char* message) noexcept {
    if (!message || !sink.load(std::memory_order_acquire)) return;
    AcquireSRWLockExclusive(&queueLock);
    if (count == records.size()) {
        dropped.fetch_add(1, std::memory_order_relaxed);
    } else {
        const auto index = (head + count) % static_cast<unsigned>(records.size());
        snprintf(records[index].message, sizeof(records[index].message), "[move_tid=%lu created_ms=%llu] %s",
                 GetCurrentThreadId(), GetTickCount64(), message);
        ++count;
    }
    ReleaseSRWLockExclusive(&queueLock);
}
void MovementTelemetryDrain(unsigned maximumRecords) noexcept {
    const auto callback = sink.load(std::memory_order_acquire);
    if (!callback) return;
    for (unsigned i = 0; i < maximumRecords; ++i) {
        Record record{};
        AcquireSRWLockExclusive(&queueLock);
        const bool available = count != 0;
        if (available) {
            record = records[head]; head = (head + 1) % static_cast<unsigned>(records.size()); --count;
        }
        ReleaseSRWLockExclusive(&queueLock);
        if (!available) break;
        // Release the queue lock before any possibly slow consumer I/O.
        callback(record.message);
    }
}
unsigned long long MovementTelemetryDropped() noexcept { return dropped.load(std::memory_order_relaxed); }
