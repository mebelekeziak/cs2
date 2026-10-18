# Scoped third-person crosshair — October 18, 2026

This is a client D3D11 overlay feature using the existing installed Windows
AMD64 contracts. Installation:
`C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game`,
executable `bin/win64/cs2.exe`, client `csgo/bin/win64/client.dll`. The static
compatibility inventory records patch 1.41.9.1, client/server 2000933 and Steam
build 25966348. MSVC C++20 x64 builds use the existing static CRT configuration;
live device verification remains in the existing D3D11 renderer.

No new native addresses, signatures, offsets or hooks were introduced.
`tools/check_compatibility.py` passed all 21 components in
`logs/compatibility/20261018T141620956448Z/` before integration. The client hash
and exact spread/inaccuracy signatures, slot contracts and native-owner
requirements remain in [weapon evidence](CS2_AIM_WEAPON_EVIDENCE.md),
`aim_weapon_offsets.h` and `inspect_aim_weapon.py`. Scope fields and the six
supported AUG/AWP/G3SG1/SCAR20/SG556/SSG08 vtables are the existing contracts in
[zoom evidence](CS2_ZOOM_EVIDENCE.md), `zoom_offsets.h` and `inspect_zoom.py`.

`AimWeaponReadSpread` reuses the verified weapon source reader but calls only
the native spread and inaccuracy getters. It checks finite output in [0,1],
weapon identity, active handle, subclass data, services and mode around those
calls. It does not call punch, readiness, time or accuracy-update methods.
The existing command-finalization callback invokes the new capture before
movement or aim edits, independently of their feature switches.

`scoped_crosshair.cpp` validates the current alive local pawn, serialized pawn,
controller and weapon identities, controller ownership, weapon services owner,
supported scoped class, pawn scoped flag and zoom stage 1/2. It revalidates
after native accuracy reads. The render side makes memory reads only and
compares this full source to the published sample. Samples expire after 250ms.
`ThirdPersonViewActive` independently requires the existing successful primary
camera-origin publication, matching current pawn/controller/identity/handle,
enabled/active camera and a publication at most 250ms old. A switch alone never
qualifies. Failed/native views and cooperative camera stop clear publication.

Capture and publication use an SRW lock; command capture uses try-acquisition
to avoid recursion from a native getter. Stop disarms capture before waiting
for in-flight reads and clears copied state. All existing forwarding hooks and
mapped code retain their established stopped/resident lifecycle.

Drawing uses copied scalar input only. Four white tapered cardinal arms and a
one-pixel center dot have a low-alpha layered halo. At 1080p the outer white
radius is 18 pixels and the inner gap is `2 + min(3, 30*(spread+inaccuracy))`
pixels. Exponential smoothing has a 90ms time constant. Hidden/invalid inputs,
new pawn/weapon source keys and renderer recreation reset smoothing. There is
no spread-cone or ballistic accuracy claim and no third-person aim/parallax
correction. The graphic is centered on the current screen view.

`scoped_crosshair_checks` exercises actual source-resolution/publication logic
with substituted native getter and camera query, including stale serialized
sources, both scope stages, unscoping, death, native failure, camera exit,
freshness and stop draining. `aim_weapon_checks` verifies the lean two-getter
path independently. `third_person_checks` verifies actual camera publication
and immediate invalidation. `scoped_crosshair_draw_checks` checks bounded
spread response, smoothing, source reset and actual ImGui geometry. Preview
modes 11/12 render the production drawing with synthetic copied input through
software D3D11, without starting or loading CS2. Live scoped gameplay appearance
and feel remain unobserved.

Final validation: public and diagnostic x64 Release builds succeed, with 44/44
CTest suites passing in each. The new state suite runs 199 checks; drawing runs
22 checks. Production-drawing still/spread WARP previews were inspected under
`logs/scoped-crosshair-{still,spread}.bmp`. Both rebuilt DLLs have AMD64 machine
type and zero static TLS/delay-import directories, recorded in
`logs/scoped-crosshair-payload-checks.json`. No CS2 client/server was launched.
