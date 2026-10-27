#include "player_reader.h"
#include "cs2_offsets.h"
#include "esp_item_offsets.h"
#include <Windows.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <limits>

namespace {
namespace off = cs2_offsets;
namespace item = esp_item_offsets;
EspLogCallback logger = nullptr;
std::uintptr_t client = 0, lastSystem = 0, lastLocal = 0, lastPawn = 0;
bool verified = false;
#ifdef CS2_PLAYER_READER_CHECKS
bool testRuntime = false;
#endif
const char* status = "awaiting client module";
ULONGLONG lastCapture = 0, lastReport = 0, lastModuleTry = 0;
unsigned generation = 0;
std::array<EspPlayerSnapshot, 128> players{};
std::array<EspBombSnapshot, 64> bombs{};
std::array<std::uint32_t, 128> bombCarriers{};
std::size_t bombCarrierCount = 0;
std::array<EspVec3, 128> positions{};
std::array<std::uint8_t, 512 * 0x70> bucketData{};
struct RejectedPawn { std::uint32_t handle; char designer[64]; };
std::array<RejectedPawn, 128> rejectedPawns{};
std::size_t rejectedCount = 0;
EspFrameSnapshot frame{};
struct Counters {
    unsigned slots = 0, occupied = 0, controllers = 0, missingPawn = 0;
    unsigned stale = 0, dormant = 0, dead = 0, badBounds = 0, failures = 0;
    unsigned truncated = 0, wrongPawnClass = 0;
    int highest = 0;
    double sampleMs = 0;
} counts;

void Log(const char* format, ...) {
    if (!logger) return;
    char message[1536]{};
    va_list args; va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args); logger(message);
}

// ReadProcessMemory on this process copies through the kernel; stale/unmapped
// source pages become a failed read rather than a game access violation.
bool Copy(std::uintptr_t address, void* output, std::size_t bytes) {
    if (address < 0x10000 || address > 0x00007fffffffffffULL - bytes) {
        ++counts.failures; return false;
    }
    SIZE_T read = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), output, bytes, &read) || read != bytes) {
        ++counts.failures; return false;
    }
    return true;
}
template<class T> bool Read(std::uintptr_t address, T& output) { return Copy(address, &output, sizeof(output)); }
bool VecValid(const EspVec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) &&
        std::abs(value.x) < 1000000 && std::abs(value.y) < 1000000 && std::abs(value.z) < 1000000;
}
void SetStatus(const char* next) {
    if (std::strcmp(status, next) != 0) Log("ESP_STATE generation=%u status=%s", generation, next);
    status = next;
}

bool ModuleReady() {
#ifdef CS2_PLAYER_READER_CHECKS
    if (testRuntime) return client != 0 && verified;
#endif
    const auto module = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
    if (module != client) {
        PlayerReaderInvalidate("client module identity changed"); client = module; verified = false; lastModuleTry = 0;
    }
    if (verified) return true;
    if (!client) { SetStatus("awaiting client module"); return false; }
    const auto now = GetTickCount64();
    if (lastModuleTry && now - lastModuleTry < 1000) return false;
    lastModuleTry = now;
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    if (!Read(client, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew > 0x100000 ||
        !Read(client + dos.e_lfanew, nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        SetStatus("invalid client PE headers"); return false;
    }
    if (nt.FileHeader.TimeDateStamp != off::ClientTimestamp || nt.OptionalHeader.SizeOfImage != off::ClientImageSize) {
        SetStatus("client build differs; reverify offsets");
        Log("ESP_BUILD_MISMATCH timestamp=0x%lx image_size=0x%lx expected=0x%lx/0x%lx",
            nt.FileHeader.TimeDateStamp, nt.OptionalHeader.SizeOfImage,
            static_cast<unsigned long>(off::ClientTimestamp), static_cast<unsigned long>(off::ClientImageSize));
        return false;
    }
    verified = true;
    Log("ESP_BUILD_VERIFIED client=%p timestamp=0x%lx image_size=0x%lx identity_stride=0x%zx; offsets verified against installed-client static schema/code",
        reinterpret_cast<void*>(client), nt.FileHeader.TimeDateStamp, nt.OptionalHeader.SizeOfImage, off::entity_layout::IdentityStride);
    return true;
}

struct Identity { std::uintptr_t entity = 0, address = 0; std::uint32_t handle = UINT32_MAX; };
bool Resolve(std::uintptr_t table, std::uint32_t handle, Identity& identity) {
    if (handle == UINT32_MAX) return false;
    const auto index = handle & off::entity_layout::IndexMask;
    std::uintptr_t bucket = 0;
    if (!Read(table + (index >> off::entity_layout::BucketShift) * sizeof(std::uintptr_t), bucket) || !bucket) return false;
    identity.address = bucket + (index & off::entity_layout::BucketMask) * off::entity_layout::IdentityStride;
    if (!Read(identity.address + off::entity_layout::EntityPointerOffset, identity.entity) || !identity.entity ||
        !Read(identity.address + off::entity_layout::HandleOffset, identity.handle) || identity.handle != handle) return false;
    std::uintptr_t back = 0;
    return Read(identity.entity + off::CEntityInstance::m_pEntity, back) && back == identity.address;
}
bool StillSame(std::uintptr_t table, const Identity& identity) {
    Identity current{};
    return Resolve(table, identity.handle, current) && current.entity == identity.entity && current.address == identity.address;
}
bool ReadDesigner(std::uintptr_t identity, char (&name)[64]) {
    std::uintptr_t pointer = 0;
    std::memset(name, 0, sizeof(name));
    return Read(identity + off::CEntityIdentity::m_designerName, pointer) && pointer &&
        Copy(pointer, name, sizeof(name) - 1);
}
bool Designer(std::uintptr_t identity, const char* expected) {
    char name[64]{};
    return ReadDesigner(identity, name) && std::strcmp(name, expected) == 0;
}
bool Origin(std::uintptr_t pawn, EspVec3& origin, std::uintptr_t& scene) {
    std::uint8_t dormant = 0;
    if (!Read(pawn + off::C_BaseEntity::m_pGameSceneNode, scene) || !scene ||
        !Read(scene + off::CGameSceneNode::m_bDormant, dormant) || dormant ||
        !Read(scene + off::CGameSceneNode::m_vecAbsOrigin, origin)) return false;
    return VecValid(origin);
}
std::uint16_t HeldWeapon(std::uintptr_t table, const Identity& pawn) {
    std::uintptr_t services = 0, afterServices = 0;
    std::uint32_t handle = UINT32_MAX, afterHandle = UINT32_MAX, owner = UINT32_MAX;
    Identity weapon{};
    std::uint16_t definition = 0, afterDefinition = 0;
    if (!Read(pawn.entity + item::WeaponServices, services) || !services ||
        !Read(services + item::ActiveWeapon, handle) || !Resolve(table, handle, weapon) ||
        !Read(weapon.entity + item::OwnerHandle, owner) || owner != pawn.handle ||
        !Read(weapon.entity + item::Item + item::DefinitionIndex, definition) ||
        !Read(pawn.entity + item::WeaponServices, afterServices) || afterServices != services ||
        !Read(services + item::ActiveWeapon, afterHandle) || afterHandle != handle ||
        !Read(weapon.entity + item::OwnerHandle, owner) || owner != pawn.handle ||
        !Read(weapon.entity + item::Item + item::DefinitionIndex, afterDefinition) || afterDefinition != definition ||
        !StillSame(table, weapon) || !StillSame(table, pawn)) return 0;
    return definition;
}
// Called for exact RTTI-matched C4 classes only. The item ownership handle
// identifies carriers even while another weapon is selected or C4 has no scene.
void CaptureBomb(std::uintptr_t table, const Identity& id, std::uintptr_t vtable) {
    if (!StillSame(table, id)) return;
    const bool planted = vtable == client + item::PlantedC4Vtable;
    std::uint8_t ticking = 0, exploded = 1, defused = 1, placed = 1;
    std::uint32_t owner = UINT32_MAX;
    Identity holder{};
    if (planted) {
        if (!Read(id.entity + item::BombTicking, ticking) || ticking != 1 ||
            !Read(id.entity + item::BombExploded, exploded) || exploded != 0 ||
            !Read(id.entity + item::BombDefused, defused) || defused != 0) return;
    } else {
        if (!Read(id.entity + item::C4BombPlanted, placed) || placed != 0 ||
            !Read(id.entity + item::OwnerHandle, owner)) return;
        if (owner != UINT32_MAX) {
            // An unresolved owner is unknown, never evidence of a dropped item.
            std::uint32_t afterOwner = UINT32_MAX;
            std::uintptr_t afterTable = 0;
            if (bombCarrierCount < bombCarriers.size() && Resolve(table, owner, holder) &&
                Read(id.entity + item::C4BombPlanted, placed) && placed == 0 &&
                Read(id.entity + item::OwnerHandle, afterOwner) && afterOwner == owner &&
                Read(id.entity, afterTable) && afterTable == vtable &&
                StillSame(table, id) && StillSame(table, holder))
                bombCarriers[bombCarrierCount++] = owner;
            return;
        }
    }
    if (frame.bombCount >= bombs.size()) return;
    EspVec3 origin{}; std::uintptr_t scene = 0, afterScene = 0, afterTable = 0;
    if (!Origin(id.entity, origin, scene)) return;
    if (planted) {
        if (!Read(id.entity + item::BombTicking, ticking) || ticking != 1 ||
            !Read(id.entity + item::BombExploded, exploded) || exploded != 0 ||
            !Read(id.entity + item::BombDefused, defused) || defused != 0) return;
    } else if (!Read(id.entity + item::OwnerHandle, owner) || owner != UINT32_MAX ||
               !Read(id.entity + item::C4BombPlanted, placed) || placed != 0) return;
    if (!StillSame(table, id) || !Read(id.entity, afterTable) || afterTable != vtable ||
        !Read(id.entity + off::C_BaseEntity::m_pGameSceneNode, afterScene) || afterScene != scene) return;
    bombs[frame.bombCount++] = {id.handle, origin, planted ? EspBombKind::Planted : EspBombKind::Dropped};
}
// Source's pitch/yaw/roll Euler convention (AngleMatrix). All values are
// copied from schema fields; no engine method is called on the Present thread.
EspVec3 Transform(const EspVec3& value, const EspVec3& angles, float scale, const EspVec3& origin) {
    constexpr float radians = 0.01745329251994329577f;
    const float sp = std::sin(angles.x * radians), cp = std::cos(angles.x * radians);
    const float sy = std::sin(angles.y * radians), cy = std::cos(angles.y * radians);
    const float sr = std::sin(angles.z * radians), cr = std::cos(angles.z * radians);
    return {
        origin.x + scale * (cp * cy * value.x + (sp * sr * cy - cr * sy) * value.y + (sp * cr * cy + sr * sy) * value.z),
        origin.y + scale * (cp * sy * value.x + (sp * sr * sy + cr * cy) * value.y + (sp * cr * sy - sr * cy) * value.z),
        origin.z + scale * (-sp * value.x + sr * cp * value.y + cr * cp * value.z)
    };
}
void Matrix() {
    frame.matrixValid = false;
    if (!verified || !Read(client + off::globals::dwViewMatrix, frame.worldToClip)) return;
    float rowNorm[4]{};
    for (int row = 0; row < 4; ++row) for (float value : frame.worldToClip.m[row]) {
        if (!std::isfinite(value)) return;
        rowNorm[row] += std::abs(value);
    }
    frame.matrixValid = rowNorm[0] > 0.0001f && rowNorm[1] > 0.0001f && rowNorm[3] > 0.0001f;
}
} // namespace

void PlayerReaderSetLogCallback(EspLogCallback callback) noexcept { logger = callback; }
const char* PlayerReaderStatus() noexcept { return status; }
void PlayerReaderInvalidate(const char* reason) noexcept {
    frame = {}; lastCapture = 0; lastSystem = 0; lastLocal = 0; lastPawn = 0; ++generation;
    Log("ESP_INVALIDATE generation=%u reason=%s", generation, reason);
}

#ifdef CS2_PLAYER_READER_CHECKS
void PlayerReaderTestConfigure(std::uintptr_t module) noexcept {
    PlayerReaderInvalidate("synthetic reader fixture");
    client = module; verified = module != 0; testRuntime = true;
}
#endif

EspFrameSnapshot PlayerReaderCapture() {
    if (!ModuleReady()) { frame = {}; return frame; }
    const auto now = GetTickCount64();
    // Fresh entity values and projection matrix on every selected Present.
    // Logging remains throttled independently of acquisition/render frequency.
    LARGE_INTEGER start{}, finish{}, frequency{};
    QueryPerformanceCounter(&start); QueryPerformanceFrequency(&frequency);
    lastCapture = now; counts = {}; rejectedCount = 0; bombCarrierCount = 0;
    frame = {}; frame.players = players.data(); frame.bombs = bombs.data();
    std::uintptr_t system = 0, table = 0, localController = 0, localPawn = 0;
    if (!Read(client + off::globals::dwEntityList, system) || !system ||
        
        !Read(client + off::globals::dwLocalPlayerController, localController) ||
        !Read(client + off::globals::dwLocalPlayerPawn, localPawn)) {
        SetStatus("entity system unavailable"); return frame;
    }
    table = system + off::entity_layout::TableOffset;
    if (system != lastSystem || localController != lastLocal || localPawn != lastPawn) {
        ++generation; lastSystem = system; lastLocal = localController; lastPawn = localPawn;
        Log("ESP_SOURCE generation=%u system=%p table=%p local_controller=%p local_pawn=%p",
            generation, reinterpret_cast<void*>(system), reinterpret_cast<void*>(table),
            reinterpret_cast<void*>(localController), reinterpret_cast<void*>(localPawn));
    }
    // The dumper's highest-index label was observed below live pawn indices.
    // Retain it as a diagnostic only; enumerate all 64 verified table buckets.
    if (!Read(system + off::globals::dwGameEntitySystem_highestEntityIndex, counts.highest)) counts.highest = -1;
    EspVec3 localOrigin{}; std::uintptr_t localScene = 0;
    std::uint8_t localTeam = 0, localLife = 1; int localHealth = 0;
    // Validate the local global against the same identity table before using it.
    Identity localIdentity{}; std::uintptr_t localIdentityAddress = 0; std::uint32_t localHandle = UINT32_MAX;
    const bool localValid = localPawn && Read(localPawn + off::CEntityInstance::m_pEntity, localIdentityAddress) &&
        Read(localIdentityAddress + off::entity_layout::HandleOffset, localHandle) && Resolve(table, localHandle, localIdentity) &&
        localIdentity.entity == localPawn && Origin(localPawn, localOrigin, localScene) &&
        Read(localPawn + off::C_BaseEntity::m_iTeamNum, localTeam) &&
        Read(localPawn + off::C_BaseEntity::m_iHealth, localHealth) && localHealth > 0 &&
        Read(localPawn + off::C_BaseEntity::m_lifeState, localLife) && localLife == 0 && StillSame(table, localIdentity);
    frame.distanceAvailable = localValid; frame.localTeam = localValid ? localTeam : 0;
    for (unsigned bucketIndex = 0; bucketIndex < off::entity_layout::BucketCount; ++bucketIndex) {
        std::uintptr_t bucket = 0;
        if (!Read(table + bucketIndex * sizeof(std::uintptr_t), bucket) || !bucket) continue;
        const unsigned baseIndex = bucketIndex << off::entity_layout::BucketShift;
        constexpr unsigned entries = off::entity_layout::BucketMask + 1;
        if (!Copy(bucket, bucketData.data(), entries * off::entity_layout::IdentityStride)) continue;
        for (unsigned index = 0; index < entries; ++index) {
            ++counts.slots;
            Identity controller{}; controller.address = bucket + index * off::entity_layout::IdentityStride;
            std::memcpy(&controller.entity, bucketData.data() + index * off::entity_layout::IdentityStride + off::entity_layout::EntityPointerOffset, sizeof(controller.entity));
            std::memcpy(&controller.handle, bucketData.data() + index * off::entity_layout::IdentityStride + off::entity_layout::HandleOffset, sizeof(controller.handle));
            if (!controller.entity || controller.handle == UINT32_MAX) continue;
            ++counts.occupied;
            if ((controller.handle & off::entity_layout::IndexMask) != baseIndex + index) { ++counts.stale; continue; }
            std::uintptr_t vtable = 0;
            if (Read(controller.entity, vtable) &&
                (vtable == client + item::C4Vtable || vtable == client + item::PlantedC4Vtable)) {
                CaptureBomb(table, controller, vtable);
                continue;
            }
            if (!Designer(controller.address, "cs_player_controller")) continue;
            ++counts.controllers;
            if (!StillSame(table, controller)) { ++counts.stale; continue; }
            std::uint32_t pawnHandle = UINT32_MAX; Identity pawn{};
            if (!Read(controller.entity + off::CCSPlayerController::m_hPlayerPawn, pawnHandle) || !Resolve(table, pawnHandle, pawn)) {
                ++counts.missingPawn; continue;
            }
            char pawnDesigner[64]{};
            if (!ReadDesigner(pawn.address, pawnDesigner) || std::strcmp(pawnDesigner, "c_cs_player_for_precache") != 0) {
                ++counts.wrongPawnClass;
                if (rejectedCount < rejectedPawns.size()) {
                    auto& rejected = rejectedPawns[rejectedCount++]; rejected.handle = pawnHandle;
                    std::memcpy(rejected.designer, pawnDesigner, sizeof(pawnDesigner));
                }
                continue;
            }
            EspPlayerSnapshot player{}; player.handle = pawnHandle;
            player.localPlayer = controller.entity == localController || pawn.entity == localPawn;
            std::uint8_t team = 0, life = 1;
            if (!Read(pawn.entity + off::C_BaseEntity::m_iHealth, player.health) ||
                !Read(pawn.entity + off::C_BaseEntity::m_lifeState, life) ||
                !Read(pawn.entity + off::C_BaseEntity::m_iTeamNum, team)) continue;
            if (player.health <= 0 || life != 0) { ++counts.dead; continue; }
            if (player.health > 1000 || (team != 2 && team != 3)) { ++counts.stale; continue; }
            player.team = team;
            // A failed name/armor read must not erase otherwise valid geometry.
            Copy(controller.entity + off::CBasePlayerController::m_iszPlayerName, player.name, sizeof(player.name) - 1);
            for (char& ch : player.name) if (static_cast<unsigned char>(ch) < 32 && ch != '\0') ch = ' ';
            Read(pawn.entity + off::C_CSPlayerPawn::m_ArmorValue, player.armor);
            player.weaponDefinition = HeldWeapon(table, pawn);
            EspVec3 origin{}, angles{}, mins{}, maxs{}; float scale = 0; std::uintptr_t scene = 0, collision = 0;
            if (!Origin(pawn.entity, origin, scene)) { ++counts.dormant; continue; }
            if (!Read(pawn.entity + off::C_BaseEntity::m_pCollision, collision) || !collision ||
                !Read(collision + off::CCollisionProperty::m_vecMins, mins) ||
                !Read(collision + off::CCollisionProperty::m_vecMaxs, maxs) ||
                !Read(scene + off::CGameSceneNode::m_angAbsRotation, angles) ||
                !Read(scene + off::CGameSceneNode::m_flAbsScale, scale) ||
                !VecValid(mins) || !VecValid(maxs) || !VecValid(angles) || !std::isfinite(scale) || scale <= 0 || scale > 100 ||
                maxs.x <= mins.x || maxs.y <= mins.y || maxs.z <= mins.z ||
                maxs.x - mins.x > 1024 || maxs.y - mins.y > 1024 || maxs.z - mins.z > 1024) {
                ++counts.badBounds; continue;
            }
            for (unsigned corner = 0; corner < 8; ++corner) player.worldCorners[corner] = Transform(
                {corner & 1 ? maxs.x : mins.x, corner & 2 ? maxs.y : mins.y, corner & 4 ? maxs.z : mins.z}, angles, scale, origin);
            if (localValid) {
                const float x = origin.x - localOrigin.x, y = origin.y - localOrigin.y, z = origin.z - localOrigin.z;
                player.distanceMeters = std::sqrt(x*x + y*y + z*z) * 0.0254f;
            }
            std::uintptr_t sceneAfter = 0, collisionAfter = 0;
            if (!StillSame(table, controller) || !StillSame(table, pawn) ||
                !Read(controller.entity + off::CCSPlayerController::m_hPlayerPawn, pawnHandle) || pawnHandle != pawn.handle ||
                !Read(pawn.entity + off::C_BaseEntity::m_pGameSceneNode, sceneAfter) || sceneAfter != scene ||
                !Read(pawn.entity + off::C_BaseEntity::m_pCollision, collisionAfter) || collisionAfter != collision) {
                ++counts.stale; continue;
            }
            if (frame.playerCount >= players.size()) { ++counts.truncated; continue; }
            positions[frame.playerCount] = origin; players[frame.playerCount++] = player;
        }
    }
    for (std::size_t i = 0; i < frame.playerCount; ++i)
        for (std::size_t carrier = 0; carrier < bombCarrierCount; ++carrier)
            if (players[i].handle == bombCarriers[carrier]) { players[i].carriesC4 = true; break; }
    std::uintptr_t systemAfter = 0, localAfter = 0, localPawnAfter = 0;
    if (!Read(client + off::globals::dwEntityList, systemAfter) || systemAfter != system ||
        systemAfter + off::entity_layout::TableOffset != table ||
        !Read(client + off::globals::dwLocalPlayerController, localAfter) || localAfter != localController ||
        !Read(client + off::globals::dwLocalPlayerPawn, localPawnAfter) || localPawnAfter != localPawn ||
        (localValid && !StillSame(table, localIdentity))) {
        frame = {}; ++counts.stale; SetStatus("source changed during capture"); return frame;
    }
    frame.valid = true; Matrix();
    SetStatus(frame.playerCount ? "reading live players" : (counts.controllers ? "controllers found; no live pawns" : "no player controllers"));
    QueryPerformanceCounter(&finish);
    counts.sampleMs = 1000.0 * static_cast<double>(finish.QuadPart - start.QuadPart) / static_cast<double>(frequency.QuadPart);
    return frame;
}

void PlayerReaderReportDraw(const EspDrawStats& draw, int espVertices, int totalVertices, float width, float height) {
    const auto now = GetTickCount64();
    if (lastReport && now - lastReport < 2000) return;
    lastReport = now;
    Log("ESP_SCAN generation=%u status=\"%s\" valid=%d matrix=%d distance=%d highest=%d slots=%u occupied=%u controllers=%u players=%zu missing_pawn=%u wrong_pawn_class=%u dead=%u dormant=%u stale=%u bad_bounds=%u read_failures=%u truncated=%u sample_ms=%.3f age_ms=%llu bombs=%zu c4_owners=%zu",
        generation, status, frame.valid, frame.matrixValid, frame.distanceAvailable, counts.highest, counts.slots,
        counts.occupied, counts.controllers, frame.playerCount, counts.missingPawn, counts.wrongPawnClass, counts.dead, counts.dormant,
        counts.stale, counts.badBounds, counts.failures, counts.truncated, counts.sampleMs, lastCapture ? now-lastCapture : 0,
        frame.bombCount, bombCarrierCount);
    Log("ESP_DRAW enabled=%d considered=%zu local=%zu dead=%zu teammates=%zu bad_distance=%zu bad_projection=%zu behind=%zu offscreen=%zu degenerate=%zu projected=%zu drawn=%zu esp_vertices=%d total_vertices=%d weapon_icons=%zu carriers=%zu bombs=%zu",
        draw.enabled, draw.considered, draw.local, draw.dead, draw.teammates,
        draw.invalidDistance, draw.invalidProjection, draw.behindCamera, draw.offscreen, draw.degenerate,
        draw.projected, draw.drawn, espVertices, totalVertices, draw.weaponIcons, draw.carriers, draw.bombs);
    for (std::size_t i = 0; i < rejectedCount; ++i)
        Log("ESP_REJECT handle=0x%08x designer=\"%s\" reason=pawn_class", rejectedPawns[i].handle, rejectedPawns[i].designer);
    if (frame.valid) for (std::size_t i = 0; i < frame.playerCount; ++i) {
        const auto& player = players[i]; EspScreenRect box{};
        const auto projection = frame.matrixValid ? EspProjectBox(player.worldCorners, frame.worldToClip,
            width, height, box) : EspProjectionResult::InvalidInput;
        // Raw positions prove acquisition independently of the display.
        
        Log("ESP_PLAYER handle=0x%08x name=\"%s\" local=%d team=%d health=%d armor=%d distance_m=%.2f origin=(%.2f,%.2f,%.2f) corner0=(%.2f,%.2f,%.2f) corner7=(%.2f,%.2f,%.2f) projection=%d screen=(%.1f,%.1f,%.1f,%.1f) weapon_definition=%u carries_c4=%d",
            player.handle, player.name, player.localPlayer, player.team, player.health, player.armor, player.distanceMeters,
            positions[i].x, positions[i].y, positions[i].z, player.worldCorners[0].x, player.worldCorners[0].y,
            player.worldCorners[0].z, player.worldCorners[7].x, player.worldCorners[7].y, player.worldCorners[7].z,
            static_cast<int>(projection), box.left, box.top, box.right, box.bottom,
            static_cast<unsigned>(player.weaponDefinition), player.carriesC4);
    }
    if (frame.valid) for (std::size_t i = 0; i < frame.bombCount; ++i) {
        const auto& bomb = bombs[i];
        Log("ESP_C4 handle=0x%08x kind=%s origin=(%.2f,%.2f,%.2f)", bomb.handle,
            bomb.kind == EspBombKind::Planted ? "planted" : "dropped", bomb.origin.x, bomb.origin.y, bomb.origin.z);
    }
}
