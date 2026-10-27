#pragma once
#include "grenade_prediction_draw.h"
#include <array>
#include <cstdint>
#include <vector>

// Engine-owner state containing copied values only. The runtime owns locking,
// complete entity scans, association with a prepared local throw, and clearing
// on source/lifecycle changes. A live observed projectile has no time expiry.
class GrenadePredictionTracker {
public:
    static constexpr std::size_t MaxEntries = 64;
    static constexpr std::size_t MaxNodes = 4096;
    void BeginFrame() noexcept;
    // Record physical discovery before thrower/owner data is ready. Neutral
    // entries preserve admission order without publishing a trajectory.
    bool MarkLive(std::uint32_t handle, GrenadeKind kind, std::uint64_t firstFrame = 0);
    bool Observe(std::uint32_t handle, GrenadeKind kind, bool enemy, std::uint64_t firstFrame = 0);
    // First complete path wins. A prepared local path can supersede an ordinary
    // path once; a retained complete prepared path is never shortened/replaced.
    bool SetPath(std::uint32_t handle, GrenadePath&& path, bool preferPrepared = false);
    void End(std::uint32_t handle) noexcept;
    // Call only after a complete scan, including projectiles not recomputed
    // because the frame's native tracing budget has been exhausted.
    void EndFrame() noexcept;
    std::vector<GrenadePredictionVisual> Capture(bool local = true, bool enemies = true) const;
    bool Lookup(std::uint32_t handle, GrenadeKind& kind, bool& enemy) const noexcept;
    bool FirstObservedFrame(std::uint32_t handle, std::uint64_t& frame) const noexcept;
    bool Complete(std::uint32_t handle) const noexcept;
    void Clear() noexcept;
    std::size_t Size() const noexcept;

private:
    struct Entry {
        std::uint32_t handle = UINT32_MAX;
        std::uint64_t firstFrame = 0;
        GrenadeKind kind = GrenadeKind::HE;
        bool enemy = false, associated = false, seen = false, hasPath = false, prepared = false;
        GrenadePath path;
    };
    std::vector<Entry> entries_;
    std::array<std::uint32_t, MaxEntries> ended_{};
    std::size_t endedCount_ = 0;
    bool frameOpen_ = false, saturated_ = false;
};
