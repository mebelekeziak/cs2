#include "anti_aim_boundary.h"
#include "anti_aim_command.h"
#include "movement_memory.h"
#include <cmath>
#include <cstring>

namespace {
namespace off = movement_offsets;
struct Container {
    std::uintptr_t command = 0, base = 0, rep = 0;
    int sequence = 0, count = 0, capacity = 0, allocated = 0;
};
template<class T> bool Read(std::uintptr_t address, T& value) noexcept {
    return MovementCopy(address, &value, sizeof(value));
}
bool Pointer(std::uintptr_t address, std::size_t bytes) noexcept {
    return address >= 0x10000 && address <= 0x00007fffffffffffULL - bytes;
}
bool Overlap(std::uintptr_t a, std::size_t an, std::uintptr_t b, std::size_t bn) noexcept {
    return a < b + bn && b < a + an;
}
bool SemanticEqual(const MovementSubtick& a, const MovementSubtick& b) noexcept {
    // Serialization may update cachedSize; all ownership and event semantics
    // must still match. Padding has no gameplay or ownership meaning.
    return a.vtable == b.vtable && a.arena == b.arena && a.presence == b.presence &&
        a.button == b.button && a.pressed == b.pressed &&
        std::memcmp(&a.when, &b.when, sizeof(float) * 5) == 0;
}
bool Snapshot(std::uintptr_t command, Container& c) noexcept {
    c.command = command;
    if (!Pointer(command, 0x78) || !Read(command + off::command::Sequence, c.sequence) || c.sequence <= 0 ||
        !Read(command + off::command::Base, c.base) || !Pointer(c.base, 0x60) ||
        Overlap(command, 0x78, c.base, 0x60) ||
        !Read(c.base + off::base::SubtickSize, c.count) || c.count < 0 || c.count > 128 ||
        !Read(c.base + off::base::SubtickCapacity, c.capacity) || c.capacity < c.count || c.capacity > 4096 ||
        !Read(c.base + off::base::SubtickRep, c.rep)) return false;
    if (!c.rep) return c.count == 0 && c.capacity == 0;
    const auto bytes = 8 + static_cast<std::size_t>(c.capacity) * 8;
    return Pointer(c.rep, bytes) && !Overlap(c.rep, bytes, c.command, 0x78) &&
        !Overlap(c.rep, bytes, c.base, 0x60) && Read(c.rep, c.allocated) && c.allocated >= c.count && c.allocated <= c.capacity;
}
bool SameSource(const Container& c, const AntiAimBoundaryPatch& p) noexcept {
    return c.command == p.command && c.sequence == p.sequence && c.base == p.base;
}
bool OriginalSteps(const Container& c, const AntiAimBoundaryPatch& p, int shift) noexcept {
    for (int i = 0; i < p.originalCount; ++i) {
        std::uintptr_t address = 0; MovementSubtick step{};
        if (!Read(c.rep + 8 + static_cast<std::size_t>(i + shift) * 8, address) ||
            address != p.originals[static_cast<std::size_t>(i)].address || !Read(address, step) ||
            !SemanticEqual(step, p.originals[static_cast<std::size_t>(i)].value)) return false;
    }
    return true;
}
bool GeneratedSafe(std::uintptr_t address, const Container& c, const AntiAimBoundaryPatch& p) noexcept {
    if (!Pointer(address, sizeof(MovementSubtick)) ||
        Overlap(address, sizeof(MovementSubtick), c.command, 0x78) ||
        Overlap(address, sizeof(MovementSubtick), c.base, 0x60) ||
        Overlap(address, sizeof(MovementSubtick), c.rep, 8 + static_cast<std::size_t>(c.capacity) * 8)) return false;
    for (int i = 0; i < p.originalCount; ++i)
        if (Overlap(address, sizeof(MovementSubtick), p.originals[static_cast<std::size_t>(i)].address, sizeof(MovementSubtick))) return false;
    return MovementOwnedWritable(address + 0x10, sizeof(MovementSubtick) - 0x10);
}
std::uintptr_t Allocate(std::uintptr_t function, std::uintptr_t arena) noexcept {
    __try { return reinterpret_cast<std::uintptr_t>(reinterpret_cast<void* (*)(void*)>(function)(reinterpret_cast<void*>(arena))); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
std::uintptr_t Append(std::uintptr_t function, std::uintptr_t repeated, std::uintptr_t step) noexcept {
    __try { return reinterpret_cast<std::uintptr_t>(reinterpret_cast<void* (*)(void*, void*)>(function)(reinterpret_cast<void*>(repeated), reinterpret_cast<void*>(step))); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
void Payload(std::uintptr_t address, const MovementSubtick& value) noexcept {
    // Preserve vtable, arena, serializer cache and padding.
    std::memcpy(reinterpret_cast<void*>(address + 0x10), &value.presence, sizeof(value.presence));
    std::memcpy(reinterpret_cast<void*>(address + 0x18), &value.button, sizeof(value.button));
    std::memcpy(reinterpret_cast<void*>(address + 0x20), &value.pressed, sizeof(value.pressed));
    std::memcpy(reinterpret_cast<void*>(address + 0x24), &value.when, sizeof(float) * 5);
}
bool Insert(const AntiAimBoundaryPatch& p) noexcept {
    __try {
        Payload(p.generated, p.editedGenerated);
        auto* pointers = reinterpret_cast<std::uintptr_t*>(p.rep + 8);
        std::memmove(pointers + 1, pointers, static_cast<std::size_t>(p.originalCount) * 8);
        pointers[0] = p.generated;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool RollbackAppend(const AntiAimBoundaryPatch& p) noexcept {
    Container now{};
    if (!Snapshot(p.command, now) || !SameSource(now, p) ||
        (now.count != p.originalCount && now.count != p.originalCount + 1) ||
        !OriginalSteps(now, p, 0) || !MovementOwnedWritable(now.base + off::base::SubtickSize, sizeof(int))) return false;
    if (now.count == p.originalCount + 1) {
        std::uintptr_t tail = 0;
        if (!Read(now.rep + 8 + static_cast<std::size_t>(p.originalCount) * 8, tail) || tail != p.generated) return false;
    }
    __try { std::memcpy(reinterpret_cast<void*>(now.base + off::base::SubtickSize), &p.originalCount, sizeof(int)); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}

bool AntiAimAppendBoundary(std::uintptr_t command, float deltaF, float deltaL,
    std::uintptr_t allocator, std::uintptr_t append, AntiAimBoundaryPatch& patch, const char** reason) noexcept {
    patch = {};
    const auto fail = [&](const char* why) noexcept { if (reason) *reason = why; return false; };
    if (!std::isfinite(deltaF) || !std::isfinite(deltaL)) return fail("boundary_nonfinite");
    Container before{};
    if (!Snapshot(command, before) || before.count > 127) return fail("boundary_layout");
    patch.command = command; patch.sequence = before.sequence; patch.base = before.base; patch.originalCount = before.count;
    if (!MovementOwnedWritable(before.base + 0x18, 0x18)) return fail("boundary_container_readonly");
    if (before.rep && !MovementOwnedWritable(before.rep + 8, static_cast<std::size_t>(before.capacity) * 8))
        return fail("boundary_order_readonly");
    float previous = 0;
    for (int i = 0; i < before.count; ++i) {
        auto& original = patch.originals[static_cast<std::size_t>(i)];
        if (!Read(before.rep + 8 + static_cast<std::size_t>(i) * 8, original.address) ||
            !Pointer(original.address, sizeof(MovementSubtick)) || !Read(original.address, original.value) ||
            !(original.value.presence & 4) || !std::isfinite(original.value.when) || original.value.when < previous || original.value.when > 1 ||
            Overlap(original.address, sizeof(MovementSubtick), before.command, 0x78) ||
            Overlap(original.address, sizeof(MovementSubtick), before.base, 0x60) ||
            Overlap(original.address, sizeof(MovementSubtick), before.rep, 8 + static_cast<std::size_t>(before.capacity) * 8))
            return fail("boundary_original_step");
        previous = original.value.when;
        for (int j = 0; j < i; ++j)
            if (Overlap(original.address, sizeof(MovementSubtick), patch.originals[static_cast<std::size_t>(j)].address, sizeof(MovementSubtick)))
                return fail("boundary_step_alias");
    }
    if (deltaF == 0 && deltaL == 0) { patch.valid = true; if (reason) *reason = "boundary_not_needed"; return true; }
    if (before.allocated > before.count) {
        if (!Read(before.rep + 8 + static_cast<std::size_t>(before.count) * 8, patch.generated) ||
            !GeneratedSafe(patch.generated, before, patch) || !Read(patch.generated, patch.originalGenerated))
            return fail("boundary_pool_step");
        const int size = before.count + 1;
        std::memcpy(reinterpret_cast<void*>(before.base + off::base::SubtickSize), &size, sizeof(size));
    } else {
        unsigned char code = 0;
        std::uintptr_t arena = 0;
        if (!Read(allocator, code) || !Read(append, code) || !Read(before.base + 0x18, arena)) return fail("boundary_helpers");
        patch.generated = Allocate(allocator, arena);
        if (!patch.generated) return fail("boundary_allocation_failed");
        if (!GeneratedSafe(patch.generated, before, patch) || !Read(patch.generated, patch.originalGenerated)) return fail("boundary_allocation_invalid");
        const auto result = Append(append, before.base + 0x18, patch.generated);
        if (result != patch.generated) { RollbackAppend(patch); return fail("boundary_append_failed"); }
    }
    Container after{};
    std::uintptr_t tail = 0;
    if (!Snapshot(command, after) || !SameSource(after, patch) || after.count != before.count + 1 ||
        !OriginalSteps(after, patch, 0) ||
        !Read(after.rep + 8 + static_cast<std::size_t>(before.count) * 8, tail) || tail != patch.generated ||
        !GeneratedSafe(patch.generated, after, patch) ||
        !MovementOwnedWritable(after.rep + 8, static_cast<std::size_t>(after.count) * 8)) {
        RollbackAppend(patch); return fail("boundary_append_layout");
    }
    patch.rep = after.rep; patch.capacity = after.capacity; patch.allocated = after.allocated;
    patch.editedGenerated = patch.originalGenerated;
    patch.editedGenerated.presence = 0x1c; patch.editedGenerated.button = 0; patch.editedGenerated.pressed = false;
    patch.editedGenerated.when = 0; patch.editedGenerated.forwardDelta = deltaF; patch.editedGenerated.leftDelta = deltaL;
    patch.editedGenerated.pitchDelta = 0; patch.editedGenerated.yawDelta = 0;
    if (!Insert(patch)) { Payload(patch.generated, patch.originalGenerated); RollbackAppend(patch); return fail("boundary_insert_failed"); }
    patch.valid = patch.appended = true;
    if (reason) *reason = "boundary_applied";
    return true;
}

bool AntiAimRestoreBoundary(const AntiAimBoundaryPatch& patch) noexcept {
    if (!patch.valid) return false;
    Container now{};
    if (!Snapshot(patch.command, now) || !SameSource(now, patch)) return false;
    if (!patch.appended) return true;
    std::uintptr_t first = 0; MovementSubtick generated{};
    if (now.rep != patch.rep || now.count != patch.originalCount + 1 || now.capacity != patch.capacity || now.allocated != patch.allocated ||
        !Read(now.rep + 8, first) || first != patch.generated || !OriginalSteps(now, patch, 1) ||
        !Read(first, generated) || !SemanticEqual(generated, patch.editedGenerated) ||
        !GeneratedSafe(first, now, patch) || !MovementOwnedWritable(now.rep + 8, static_cast<std::size_t>(now.count) * 8) ||
        !MovementOwnedWritable(now.base + off::base::SubtickSize, sizeof(int))) return false;
    __try {
        auto* pointers = reinterpret_cast<std::uintptr_t*>(now.rep + 8);
        std::memmove(pointers, pointers + 1, static_cast<std::size_t>(patch.originalCount) * 8);
        pointers[patch.originalCount] = patch.generated;
        Payload(patch.generated, patch.originalGenerated);
        std::memcpy(reinterpret_cast<void*>(now.base + off::base::SubtickSize), &patch.originalCount, sizeof(int));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool AntiAimRebindCommandContainer(const AntiAimBoundaryPatch& boundary, AntiAimCommandPatch& command) noexcept {
    Container now{};
    if (!command.valid || command.command != boundary.command || command.base != boundary.base ||
        command.sequence != boundary.sequence || command.count != boundary.originalCount ||
        !Snapshot(boundary.command, now) || !SameSource(now, boundary)) return false;
    int shift = 0;
    if (boundary.valid && boundary.appended && now.count == boundary.originalCount + 1) {
        std::uintptr_t first = 0; MovementSubtick value{};
        if (!Read(now.rep + 8, first) || first != boundary.generated || !Read(first, value) ||
            !SemanticEqual(value, boundary.editedGenerated)) return false;
        shift = 1;
    } else if (now.count != boundary.originalCount) return false;
    if (!OriginalSteps(now, boundary, shift)) return false;
    command.rep = now.rep; command.capacity = now.capacity; command.allocated = now.allocated;
    return true;
}
