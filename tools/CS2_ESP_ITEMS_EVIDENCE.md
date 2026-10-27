# Held weapon and C4 ESP contracts — October 27, 2026

Target: installed Windows AMD64 CS2 **client 2000935 / 1.41.9.2 / Steam
26144732**. Client:
`C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll`.
SHA-256:
`ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5`.
The existing Present-owned reader targets the project's D3D11 overlay. Static
installed-file inspection does not establish a running process's graphics API.

Header: `include/esp_item_offsets.h`. Frozen read-only verifier:
`python tools/inspect_esp_items.py`; `--client PATH` selects another installed
file and `--header PATH` supports source-header rejection tests. It rejects an
unfamiliar file hash before trusting native sites. It checks eight named schema
registrations, five exact native instruction windows and both primary RTTI
vtables. No live process, client launch, server interaction or installed-file
write is involved.

## Sources and identity

The existing complete serialized handle lookup is retained: 64 buckets,
512 slots per bucket, 0x70-byte identity records, handle at +0x10 and entity
back pointer at +0x10. See `tools/CS2_ENTITY_EVIDENCE.md` and
`tools/inspect_cs2.py`. Every optional item read is separate from player
geometry: a missing item never suppresses a valid player box.

| Contract | Installed value | Independent source |
| --- | --- | --- |
| Pawn weapon services | +0x12F0 | `C_BasePlayerPawn` registration 0x21F67E8 / array 0x21F6450 / 28 fields |
| Active weapon handle | services +0x60 | `CPlayer_WeaponServices` registration 0x222DDC8 / array 0x222DD30 / four fields |
| Embedded item | weapon +0x12E0 | `C_EconEntity::m_AttributeManager` +0x1290 plus `C_AttributeContainer::m_Item` +0x50 |
| Item definition | item +0x1BA, uint16 | `C_EconItemView` registration 0x2256918 / array 0x2256520 / 31 fields; existing skins contract |
| Owner handle | entity +0x520 | `C_BaseEntity` registration 0x21F6E78 / array 0x21F5550 / 82 fields |
| C4 class | primary vtable 0x1BDD1D0 | Complete-object RTTI `.?AVC_C4@@` |
| Planted C4 class | primary vtable 0x1C9A9B0 | Complete-object RTTI `.?AVC_PlantedC4@@` |

Active weapon capture resolves the full weapon handle, requires its owner to
equal the full pawn handle, then copies the definition. It rechecks the pawn's
services pointer, active handle, item definition, owner and both identities.
The item definition/ownership chain was already verified by
`tools/inspect_skins.py` and `tools/inspect_aim_weapon.py`.

## C4 state and retirement

`C_C4`: registration **0x221DCB8**, field array **0x221DB60**, ten fields,
schema size 0x1F60. `m_bBombPlanted` is byte **+0x1F5B**. Native read
at **0x80A34C** compares that byte against zero; native planting writes one
at **0x823347**, followed by a pawn weapon-services load. The old C4 weapon
is suppressed once this latch is true.

`C_PlantedC4`: registration **0x223E778**, field array **0x223E3B0**, 29
fields, schema size 0x1958. Relevant byte fields:

| Field | Offset | Native consumer |
| --- | --- | --- |
| `m_bBombTicking` | +0x1288 | compare at 0x82A34B, branch out if false |
| `m_bHasExploded` | +0x12BD | compare at 0x82A358, branch out if true |
| `m_bBombDefused` | +0x12DC | compare at 0xCBE62F, branch out if true |

The defuse handler at **0x8043AD–0x8043D1** clears ticking and sets defused;
the paired predicate at **0xCBE62F/0xCBE63C** checks defused/exploded. The
verifier independently decodes each flag operand and checks exact instruction
windows, establishing byte width and predicate direction in addition to schema
names. A world planted marker requires ticking=1, exploded=0 and defused=0.
Malformed boolean values reject.

The entity scan identifies C4 classes by exact verified primary vtable, without
guessing designer names or using a global planted-bomb pointer chain. Owned
`C_C4` associates its full owner handle with a captured player's full pawn
handle. This works while another weapon is selected and without requiring
an inventory C4 scene. A **dropped** marker requires the owner handle to be
exactly `UINT32_MAX`; an unresolved owner never means a dropped item.

World origins use the existing verified non-dormant scene `m_vecAbsOrigin`
chain. Entity identity, C4 state and scene source are rechecked before copying.
The scan captures up to 64 world markers independently of player rendering
toggles. Each capture starts with empty player/bomb counts; module, entity-system
or local-source changes invalidate both. Rendering receives copied values only.
No callbacks or engine writes are introduced.

`tests/player_reader_checks.cpp` exercises real reader captures against
synthetic memory: held rifle/C4, ownership changes, carrier with rifle selected,
missing weapon services preserving players, stale active-handle serials, carrier
without a C4 scene, drop/pickup/plant transitions, defuse/explosion, malformed
flags, dormant/invalid origins, wrong class, stale identity and source loss. Its compile-time configure
hook is excluded from production. These fixtures and static contracts do not
claim a live in-game result; offline/private appearance still requires observation.

Diagnostics include copied `weapon_definition`, `carries_c4`, `ESP_C4` identity
and world position, plus acquired and drawn item counts to separate source
acquisition from projection/rendering failures.
