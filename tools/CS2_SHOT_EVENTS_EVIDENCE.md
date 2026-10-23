# Native shot-event observations

## Current recertification — October 23, 2026

Installed AMD64 client **1.41.9.2 / 2000935 / Steam 26144732**. Client SHA-256
`ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5`,
timestamp `0x6ADA7340`, size `0x29A0000`. Static server emission image SHA-256
`2a229dd46c8af0b52c1f49a3e343b99d9ffe946c102c56102a45b00f6a52349c`,
timestamp `0x6ADA7343`, size `0x257F000`. No server was run or contacted.

Scratch evidence is in `logs/research/26144732/core/`: named RTTI independently
discovers manager vtable `0x1C337A8`, event vtable `0x1C334F0`, two-method listener
vtable `0x1ACD678` (destructor `0x190680`, purecall `0x19FD244`). Registration
for exact `weapon_fire` at `0xC60A52` loads manager slot `0x254DC00`; destructor
comparison/clear at `0x9C5283` agrees. Add/Find/Fire methods are now
`0x9CAB60`/`0x9E2A20`/`0x9E3790`. Typed getters are independently selected by
their RTTI table slots, resolving otherwise structurally similar getter bodies.
Fifteen frozen windows now cover all relocated native paths and enum records.
All complete code bodies retain instruction/layout/ownership contracts, with
decoded relative targets refreshed. HitGroup_t registration `0x21D19D8` points
at fourteen enum records at `0x1AFABC0`; named values remain unchanged.

Without an old raw server file, comparison used preserved complete disassembly
and independent named event-string references. Full new producer windows are
decoded and refrozen: weapon_fire `0xA151AC`, bullet_impact `0xA7DEF3`, hurt
`0xA9B474`, combined controller/pawn setter `0xC2F2B0`. All normalized
instructions agree except one native hurt producer member load `+0x150C` to
`+0x1514`; this is native-only emission evidence, not a runtime adapter field.
Creation slot +0x30, setter keys/serialized handle normalization, field widths,
native dispatch/free ownership and broadcast path remain as decoded. The three
VPK event assets retain exact hashes and schemas; events still have no shot ID
or native event tick. Earlier address tables below are historical.

`inspect_shot_events.py` passes. Four bad headers (manager, AddListener, getter
slot, entry bytes) reject (`negative.json`). Exact unknown-build hash rejection
remains. No callback was registered or observed live; build/test results are
separate in `docs/CS2_UPDATE_26144732.md`.

## Current recertification — October 21, 2026

Installed Windows AMD64 client: patch **1.41.9.1**, client/server **2000934**,
Steam build **26087496**, Source revision **11150591**, version date
October 20, 2026, 11:41:22. Installation and executable paths remain those
listed below. Current identities:

| Image | SHA-256 | PE timestamp | Image size |
| --- | --- | --- | --- |
| Client | `5a46a09b521bbd607012146364dafae2abfa50364246638391990d4c7dfb9d49` | `0x6AD7B41C` | `0x299D000` |
| Server, static emission evidence only | `05e6f04972bb2983f463c08824c3133626e0703e881443589288b68e200acc4d` | `0x6AD7B41C` | `0x2572000` |

The separate scratch investigation under
`logs/research/26087496/events_console/` rediscovered the three exact code
anchors in the new client and the manager, event and listener primary vtables
from named RTTI. All **15 client windows and four server emission windows**
retain their exact previous SHA-256, including full decoded relative operands.
Manager global registration/destruction agree; native listener dispatch,
external-listener ownership/reset, typed getters, serialized pawn-handle
normalization and Murmur2 key contracts remain as documented below. All header
constants and entry bytes match the independently resolved vtables/operands.
The three installed VPK event assets and the named HitGroup_t registration
also pass unchanged. No address, slot, key, layout or runtime code changed.

`discover.py` preserves searches, RTTI results and full window disassembly in
`discovery.json` (Capstone 5.0.9); `probe.py` ran all original checks with the
candidate image identities in memory before changing frozen pins. Only the
verifier's exact client/server hashes and PE timestamps were then updated.
Unknown builds continue to reject. The identities in the original investigation
below are historical.

`python tools/inspect_shot_events.py` passes; its report is
`logs/research/26087496/events_console/events-final.json`. Temporary changes to
the manager global, handle getter RVA, AddListener slot and AddListener entry
bytes all reject, as does a deliberately unfamiliar client hash; diagnostics
are in `negative.json`. No game/server process was launched or opened and no
installed file was changed. Native event delivery and shot outcomes remain
unobserved. Repository build/CTest results belong to the combined update,
separate from these installed-file checks.

## Original October 18 investigation

Read-only installed-file investigation, October 18, 2026. Target is the AMD64
CS2 client, with all 17 prior compatibility components passing before research
in `logs/compatibility/20261018T083320319050Z/`. No game/server process was
launched or accessed, and no installed file was changed. This is a client event
observer; it adds no dedicated-server plugin or server configuration.

Installed paths are under
`C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game`:

| Image | Path relative to game | SHA256 |
| --- | --- | --- |
| Client | `csgo/bin/win64/client.dll` | `659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0` |
| Server, static emission evidence only | `csgo/bin/win64/server.dll` | `09e8c04c5f44040155d04fa79a3f43491d542a386f45807ed77785729e5925a9` |

Client PE timestamp/image size are `0x6AD01314` / `0x299D000`; server values
are `0x6ACFCD38` / `0x2572000`. No server interaction is needed for verification.

## Current native listener and event ownership

Exact RTTI identifies `CGameEventManager` primary vtable `0x1C31878` and
`CGameEvent` primary vtable `0x1C315C0`. The current manager pointer slot is
client `0x254BC08`: native player registration loads it at `0xC5FC07`, and
manager destructor `0x9C4453` compares/clears the same slot.
The named `IGameEventListener2` interface's primary vtable at `0x1ACB668`
has destructor `0x1906A0` and its Fire virtual slot pointing to native purecall
`0x19FAFE4`; the next table's RTTI boundary follows these two methods.

| Manager method | Vtable byte slot | Function | Verified ABI |
| --- | --- | --- | --- |
| Add listener | `0x18` | `0x9C9D30` | `int(manager*, listener*, const char* name, bool serverSide)` |
| Find registered listener | `0x20` | `0x9E1BF0` | `bool(manager*, listener*, const char* name)` |
| Client-side dispatch | `0x40` | `0x9E2960` | `bool(manager*, event*)` |
| Remove listener, evidence only | `0x28` | `0xA01540` | `void(manager*, listener*)` |

Native player registration `0xC5FC00..0xC5FC1A` passes event name
`weapon_fire`, a player-owned listener pointer and serverSide **false** to
slot `0x18`. Add-listener returns an event index: **zero is successful**,
and -1 is failure. Its inner method `0x9C99B0` stores the supplied listener
pointer in its own 16-byte wrapper, marks the client listener as type 1, and
sets manager `+0xEC` to request a native listen-interest update. This matters
for `bullet_impact`, for which no exact built-in client listener name was found.
Observing a dispatch hook alone would not establish that the server delivers
an event for which this client has no interest.

Client dispatcher `0x9E2AB0..0x9E2AD2` selects client listeners and invokes
listener byte slot **8**, passing `(listener*, event*)`, synchronously.
At `0x9E2AFD..0x9E2B07`, it calls manager byte slot `0x50` to free the
event before returning. The event and all returned string pointers are borrowed
only for the listener callback. The observer copies fields and invokes its
value callback before returning; it retains no event pointer or borrowed string.
The normal native dispatcher continues unchanged afterward.

Remove-listener `0xA01540..0xA0171F` removes registration references and frees
the manager's wrapper; it does not delete the externally supplied listener.
Manager reset `0xA04868..0xA0489C` likewise frees only those 16-byte wrappers
directly through the native allocator, with no listener virtual/destructor call.
The observer therefore uses a plain, statically initialized callback table
containing its verified slot-8 callback, without a CRT constructor/destructor.
Registration occurs only in `ShotEventsPump` on the native command owner.
Preparation on the loader worker only verifies contracts. Native registration
calls run without the adapter callback barrier held, avoiding the reverse lock
order of native dispatch holding its own manager mutex while invoking listeners.
Each owner pump validates the current manager identity. Membership lookup and
partial registration retry are limited to once per second for the same manager;
a replaced manager registers immediately. A same-manager native reset therefore
has at most the membership refresh interval before interests are restored.
Partial registration never reports ready.

Stop disarms the observer and drains copied callbacks. Its listener and callback
code remain registered, inert and resident until normal process exit. No native
manager method is called from the external stop thread; **stopped and resident
is not physically unloaded**. The module must remain resident while native code
can call its retained listener table.

## Field access uses current native key and getter contracts

Keys are exactly 16 bytes: `{uint32 hash, int32 token=-1, const char* name}`.
Native helper `0x166050` computes lowercase-ASCII Murmur2 with multiplier
`0x5BD1E995` and initial state `length XOR 0x31415926`. Native server producers
independently embed these hashes: `userid=0x3E7B804B`,
`userid_pawn=0x79261AD5`, `attacker_pawn=0xA8F40039`,
`x=0xF38610FF`, `y=0x3225AF29`, `z=0x2BF4F9C5`.

| Event method | Byte slot | Function / arguments after event |
| --- | --- | --- |
| Name | `0x08` | `0x9E7E50`, returns borrowed string |
| Empty/missing field | `0x28` | `0x9F1770`, `(const Key*)`, true for absent/empty value |
| Bool | `0x30` | `0x9E3E60`, `(const Key*, bool default)` |
| Int | `0x38` | `0x9E7120`, `(const Key*, int default)` |
| Float | `0x48` | `0x9E6D40`, `(const Key*, float default)`; default in XMM2 |
| String | `0x50` | `0x9E9550`, `(const Key*, const char* default)` |
| Serialized handle | `0x60` | `0x9E4620`, `(uint32* output, const Key*, uint32 default)`, returns output pointer |

Scalar wrappers copy the key and forward to the current event's variant
collection at `event+0x58`; missing fields remain explicitly unknown.
Get-handle calls the integer getter, then applies native serialized-handle
normalization. For index `<0x4000` it preserves index and serial bits with
`(value & 0x01FF8000) | (value & 0x7FFF)`; otherwise it preserves the raw result,
including `UINT32_MAX`. The observer keeps the full engine-canonical serialized
handle, preserving its serial bits.

## Identity is the generated pawn field, not a current player slot

Current event schemas explicitly declare `userid` and `attacker` as
`player_controller_and_pawn` for weapon fire, bullet impact, hurt and death.
Server combined setter `0xC21E10` stores a 16-bit controller/player-slot value
and separately generates the `<field>_pawn` serialized handle. Client combined
setter `0xA13350` follows the same pattern. The numeric field is not a persistent
legacy UserID or an established connection-generation identifier.

The native convenience pawn getter at event slot `0x88` first tries the generated
pawn field but falls back to a **current** controller/pawn when it is absent.
Controller getter `0x80` resolves the current entity from the low eight player-slot
bits. Those fallbacks can attribute an older event to a respawned/reconnected
player. The observer intentionally reads only the named `userid_pawn` and
`attacker_pawn` fields through the verified serialized-handle getter.

It retains a valid event pawn handle even after the victim despawns. Current
controller/name/local enrichment requires that exact serialized pawn identity
still resolves and its reciprocal current controller points to the same handle.
There is no health/alive requirement for enrichment. If the pawn no longer
resolves or a slot has been recycled, its controller/name remain unknown.
If a generated pawn field is absent, identity remains unknown; no current-slot
fallback is used. `player_disconnect` declares only `player_controller`, so it
is treated as a conservative lifecycle signal without guessing an actor pawn.

## Event declarations, emission and limits

The verifier reads the original VPK resources without extracting installed files:

| Resource | VPK | SHA256 |
| --- | --- | --- |
| `resource/mod.gameevents` | `csgo/pak01_dir.vpk` | `6beca96ba82e2362491f1172a18201beb3cf6ad34856cea825fc9adfd2ae3220` |
| `resource/game.gameevents` | `csgo/pak01_dir.vpk` | `03b665315a0dca6ac542d81d0348331da86a59c090fdee2ee42f2d90aabef5c6` |
| `resource/core.gameevents` | `core/pak01_dir.vpk` | `5cebb645cd1a1790ebc4c2a8a3116d55cacbe06f982778d2ef0f2a087dde8765` |

`weapon_fire` has actor, weapon string and silenced bool. `bullet_impact` has
actor plus float x/y/z. `player_hurt` has actor/victim, health/armor bytes,
weapon string, health damage short, armor damage byte and hitgroup byte.
The observer also registers death, spawn, disconnect, round start/end/prestart,
new-map and `cs_game_disconnected` lifecycle signals.

Native server firearm path `0xA07CFC..0xA07E52` creates `weapon_fire`, fills
its actor/weapon/silenced fields and broadcasts through manager slot `0x38`.
Bullet-impact producer `0xA70A43..0xA70B90` embeds the generated pawn-field and
coordinate hashes and broadcasts the event. Hurt producer
`0xA8DFC4..0xA8E3E4` supplies damage and hitgroup fields. These establish native
emission and field ownership; they do not establish delivery in every mode.
No predicted `weapon_fire` producer was found in the examined ordinary client
shot path or the client name references. This is not proof of globally
duplicate-free delivery. Every listener callback receives its own adapter
delivery number, even if timestamps and payloads are identical. No same-ms
heuristic collapses distinct native fire observations.

These declarations have no shot ID and no event tick. `receiptMs` is local
delivery time; `deliveryId` identifies one copied callback, not one server shot.
Fire/impact/hurt correlation must remain conservative. A missing hurt event alone
does not establish spread, obstruction, resolver error, prediction rejection,
or another invented miss reason. Local snapshots and event receipts do not
establish an exact server trajectory or exhaustive hit registration.

Hitgroup names are independently verified from named `HitGroup_t` registration
`0x21CF818`, 14 records at `0x1AF8B90`, stride `0x20`: generic 0, head 1,
chest 2, stomach 3, left/right arm 4/5, left/right leg 6/7, neck 8; other values
retain their numeric/unknown treatment.

## Reproduction

`python tools/inspect_shot_events.py` verifies both installed image identities,
15 client windows, four server producer windows, three code anchors, exact
RTTI/vtables, all source constants/entry bytes, three VPK resource hashes,
declared event field kinds and the native hitgroup enumeration. It uses only
Python's standard library and the existing read-only VPK helper. `--header`
supports deliberately incorrect source contracts without rewriting offsets.

`tests/shot_events_checks.cpp` uses native-method substitutes to check key
hashes/ABI, zero-valued registration success, owner registration/re-registration,
partial readiness, copied native fields/strings, exact serialized actor identity,
despawn/recycled slots, missing fields, malformed coordinates, distinct deliveries
and cooperative stop. No live event delivery, console visibility or shot outcome
observation is claimed by these standalone checks.
