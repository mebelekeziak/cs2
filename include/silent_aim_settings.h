#pragma once
#include <cstdint>

enum class AimHitbox {
    Head, Chest, Stomach, Pelvis,
    LeftUpperArm, RightUpperArm, LeftForearm, RightForearm, LeftHand, RightHand,
    LeftThigh, RightThigh, LeftCalf, RightCalf, LeftFoot, RightFoot, LowerChest, MiddleChest, Count
};
enum class AimHitgroup : std::uint32_t {
    Head = 1u << 0, Chest = 1u << 1, Stomach = 1u << 2, Pelvis = 1u << 3,
    Arms = 1u << 4, Hands = 1u << 5, Legs = 1u << 6, Feet = 1u << 7
};
constexpr std::uint32_t AimHitgroupBit(AimHitgroup group) noexcept {
    return static_cast<std::uint32_t>(group);
}
inline constexpr std::uint32_t AimAllHitgroups = 0xffu;

struct SilentAimSettings {
    bool enabled = false;
    float fov = 5;
    AimHitbox hitbox = AimHitbox::Chest; // Legacy preset field; targeting uses hitgroups.
    bool autofire = false;
    float hitchance = 50;
    int samples = 256;
    bool quickstop = false;
    int minDamage = 1;
    std::uint32_t hitgroups = AimHitgroupBit(AimHitgroup::Chest);

    bool operator==(const SilentAimSettings&) const = default;
};

// Render owner publishes one atomic snapshot; the command owner consumes it.
SilentAimSettings SilentAimGetSettings() noexcept;
void SilentAimSetSettings(const SilentAimSettings& settings) noexcept;
void SilentAimPollControls(bool foreground) noexcept;
