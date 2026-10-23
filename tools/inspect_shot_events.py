"""Read-only installed native client-event listener and field verification.

Uses only the standard library and existing VPK reader. No process is launched,
no listener is registered, and no installed/source file is changed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from verify_sky_draw import mapped, primary_vtable, require_bytes
from inspect_surface_tint import extract

ROOT = Path(__file__).resolve().parents[1]
CLIENT = Path(r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll")
CLIENT_HASH = "ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5"
SERVER_HASH = "2a229dd46c8af0b52c1f49a3e343b99d9ffe946c102c56102a45b00f6a52349c"
SERVER_WINDOWS = {'server_weapon_fire_broadcast': (10572204,
                                  347,
                                  '47d2c92e521aa22de89f66b095d7322666411f84d905f58f87c1a117656c186c'),
 'server_bullet_impact_pawn_and_xyz': (11001587,
                                       333,
                                       '48872524761b13fc558c06f861f38676383f4b5ff035343d7456bc5e0b6abe42'),
 'server_hurt_fields': (11121780,
                        1056,
                        '7a302e1a469c18640d1ee2e0bbff4fdbc5011c5137600160d4680c02cf1c80ec'),
 'server_combined_controller_pawn_setter': (12776112,
                                            336,
                                            'cc737d473a8951406a45f2cce1f337fb6af7f18b0e0e2dea06f7661faf293f53')}
WINDOWS = {'manager_add': (10267488,
                 284,
                 '2df332c8fa5d386979eb162e022b646061504a5f854d040060da2ec2ee5ea4a2'),
 'manager_find': (10365472,
                  343,
                  'fbd2c1c1a78b453a76d764cf0a37e963d06e4a29d1ecf31fc2adc9897116f16e'),
 'manager_client_dispatch': (10368912,
                             583,
                             '99af2b0a12aa6f31cada5ecff5a199f06557744d4fb5ff1ad76d797f38c262d1'),
 'manager_register': (10266592,
                      894,
                      'ae172ed312b7217ff8ef07769f9797f6587e062180654cd69b5c8e6b34e3beca'),
 'manager_remove': (10494832,
                    479,
                    '8534146c5a11ea51d0aa5c498fc347a61cb264843df799c7da0276e85d760bc5'),
 'manager_reset_external_listener_wrappers': (10507928,
                                              52,
                                              'f49f840e5d57f3b557d620084ad0af672c8897a277111ad0373c76be2ded32a6'),
 'event_name': (10390656,
                34,
                'e16f0afd98e397445b0c33f9ebd16e2a24bea686276eefaaccdd1e9aea8f89a8'),
 'event_empty': (10429856,
                 86,
                 'dbc31320a57ecb0b64fdbb1071e687441b22e5bb8581daaf3364be09c2f79ea3'),
 'event_bool': (10374288,
                45,
                '5d27611234a7a5226c75976cb99291ee044a90f712a7d412de40f1c216897125'),
 'event_int': (10387280,
               45,
               'd5670e6c8ecd057dcdf7583378f8412ac590d8b61386ab81d1d5098f75c8377b'),
 'event_float': (10386288,
                 45,
                 'a5637c33e06bb7758a9f614bbe2dc5f2fc4cf9409879073e42331969a374ea5d'),
 'event_string': (10396544,
                  45,
                  '883db578ab4fe9dc33aebddc47408923671d9568b161c218b808da8698ade3cf'),
 'event_handle': (10376272,
                  85,
                  '2085d254151931aaf3d106e2acd990b2ef20bdd54a7e3223728c390ab5766066'),
 'key_hash': (1466416,
              294,
              'fc102547d3248e607c9e2ca0d949e171aefcd880c1c97375b30f47283d89f9e6'),
 'hitgroups': (28289984,
               448,
               '290eea176a98ab6899c966c84e4038b2c5ab2121932156665805ad36645a7f81')}
CHECKS = {'global_manager_load': (12978770,
                         '4c8d05cfe10201488b0da0d18e014533c9488bd6488b01ff5018'),
 'global_manager_clear': (10244739, '48391d7689b801750748892d6d89b801'),
 'event_ctor': (10231875, '488d05a61427014d8bf8488901488bda33c04c8bf1')}
ASSETS = {
    "resource/mod.gameevents": "6beca96ba82e2362491f1172a18201beb3cf6ad34856cea825fc9adfd2ae3220",
    "resource/game.gameevents": "03b665315a0dca6ac542d81d0348331da86a59c090fdee2ee42f2d90aabef5c6",
    "resource/core.gameevents": "5cebb645cd1a1790ebc4c2a8a3116d55cacbe06f982778d2ef0f2a087dde8765",
}


def verify_source(image, base, header):
    def rip(site): return site + 7 + struct.unpack_from("<i", image, site + 3)[0]
    manager = primary_vtable(image, base, b".?AVCGameEventManager@@")
    event = primary_vtable(image, base, b".?AVCGameEvent@@")
    listener = primary_vtable(image, base, b".?AVIGameEventListener2@@")
    if listener != 0x1ACD678 or struct.unpack_from("<QQ", image, listener) != (base + 0x190680, base + 0x19FD244):
        raise SystemExit("Native two-method listener interface RTTI/vtable differs")
    expected = {"Manager": rip(0xC60A59), "ManagerVtable": manager, "EventVtable": event}
    if expected["Manager"] != rip(0x9C5283):
        raise SystemExit("Native manager global registration/destructor disagree")
    for name, slot, table in (("AddListener", 0x18, manager), ("FindListener", 0x20, manager),
        ("FireClient", 0x40, manager), ("Name", 8, event), ("Empty", 0x28, event),
        ("Boolean", 0x30, event), ("Integer", 0x38, event), ("Float", 0x48, event),
        ("String", 0x50, event), ("Handle", 0x60, event)):
        expected[name] = struct.unpack_from("<Q", image, table + slot)[0] - base
        expected[name + "Slot"] = slot
    source = re.sub(r"//[^\n]*|/\*.*?\*/", "", header.read_text(encoding="utf-8-sig"), flags=re.S)
    values = {}
    for name, literal in re.findall(r"\bptrdiff_t\s+(\w+)\s*=\s*(0[xX][0-9A-Fa-f]+|[0-9]+)\s*;", source):
        if name in values:
            raise SystemExit(f"Duplicate source constant: {name}")
        values[name] = int(literal, 0)
    if set(values) != set(expected):
        raise SystemExit(f"Source constant inventory differs: {set(values) ^ set(expected)}")
    for name, value in expected.items():
        if values[name] != value:
            raise SystemExit(f"Source contract mismatch: {name}={values[name]:#x}; native requires {value:#x}")
    prefixes = {
        "AddListenerBytes": "48895c241048896c2418565741564883",
        "FindListenerBytes": "48895c241048896c2418488974242057",
        "FireClientBytes": "40535641544883ec30488bf2488d99e0",
    }
    for name, expected_bytes in prefixes.items():
        match = re.search(rf"\b{name}\[\]\s*\{{([^}}]*)\}}", source, re.S)
        actual = bytes(int(value.strip(), 0) for value in match.group(1).split(",") if value.strip()) if match else b""
        if actual != bytes.fromhex(expected_bytes):
            raise SystemExit(f"Source entry bytes mismatch: {name}")
    return expected


def schemas(vpk, core_vpk):
    events = {}
    for asset, digest in ASSETS.items():
        data = extract(core_vpk if asset.endswith("core.gameevents") else vpk, asset)
        if hashlib.sha256(data).hexdigest() != digest:
            raise SystemExit(f"Installed event schema changed: {asset}")
        text = re.sub(r"//[^\n]*", "", data.decode("utf-8-sig"))
        for name, body in re.findall(r'"([^"\n]+)"\s*\{([^{}]*)\}', text, re.S):
            events.setdefault(name, []).append(dict(re.findall(r'"([^"\n]+)"\s*"([^"\n]+)"', body)))
    expected = {
        "weapon_fire": {"userid": "player_controller_and_pawn", "weapon": "string", "silenced": "bool"},
        "bullet_impact": {"userid": "player_controller_and_pawn", "x": "float", "y": "float", "z": "float"},
        "player_hurt": {"userid": "player_controller_and_pawn", "attacker": "player_controller_and_pawn", "health": "byte",
            "armor": "byte", "weapon": "string", "dmg_health": "short", "dmg_armor": "byte", "hitgroup": "byte"},
        "player_death": {"userid": "player_controller_and_pawn", "attacker": "player_controller_and_pawn", "weapon": "string"},
        "player_spawn": {"userid": "player_controller_and_pawn"},
        "player_disconnect": {"userid": "player_controller"},
        "round_start": {}, "round_end": {}, "game_newmap": {}, "round_prestart": {}, "cs_game_disconnected": {},
    }
    selected = {}
    for name, fields in expected.items():
        candidates = [declaration for declaration in events.get(name, []) if all(declaration.get(key) == value for key, value in fields.items())]
        if not candidates:
            raise SystemExit(f"Native event field declaration differs: {name}")
        # Core declares a subset of some game-specific events. Verify the
        # complete matching declaration without inferring loader precedence.
        selected[name] = max(candidates, key=len)
        if "tick" in selected[name] or "shot_id" in selected[name]:
            raise SystemExit(f"Event time/shot identity contract changed: {name}")
    return selected


def hitgroups(image, base):
    def string(address): return bytes(image[address:image.index(0, address)]).decode("ascii")
    registration = 0x21D19D8
    if (string(struct.unpack_from("<Q", image, registration)[0] - base) != "HitGroup_t" or
        struct.unpack_from("<I", image, registration + 0x14)[0] != 14 or
        struct.unpack_from("<Q", image, registration + 0x18)[0] - base != 0x1AFABC0):
        raise SystemExit("Named HitGroup_t registration differs")
    values = {}
    for index in range(14):
        record = 0x1AFABC0 + index * 0x20
        values[string(struct.unpack_from("<Q", image, record)[0] - base)] = struct.unpack_from("<q", image, record + 8)[0]
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, default=CLIENT)
    parser.add_argument("--server", type=Path, default=CLIENT.with_name("server.dll"))
    parser.add_argument("--header", type=Path, default=ROOT / "include/shot_events_offsets.h")
    parser.add_argument("--vpk", type=Path, default=CLIENT.parents[2] / "pak01_dir.vpk")
    parser.add_argument("--core-vpk", type=Path, default=CLIENT.parents[3] / "core/pak01_dir.vpk")
    args = parser.parse_args()
    raw, image, base, nt = mapped(args.client)
    if hashlib.sha256(raw).hexdigest() != CLIENT_HASH or struct.unpack_from("<I", raw, nt + 8)[0] != 0x6ADA7340 or len(image) != 0x29A0000:
        raise SystemExit("Client changed: repeat native listener/event-field investigation")
    require_bytes(image, CHECKS)
    for name, (site, size, digest) in WINDOWS.items():
        if hashlib.sha256(image[site:site + size]).hexdigest() != digest:
            raise SystemExit(f"Native event window changed: {name}")
    server_raw, server, _, server_nt = mapped(args.server)
    if hashlib.sha256(server_raw).hexdigest() != SERVER_HASH or struct.unpack_from("<I", server_raw, server_nt + 8)[0] != 0x6ADA7343 or len(server) != 0x257F000:
        raise SystemExit("Installed server changed: repeat event emission/identity investigation")
    for name, (site, size, digest) in SERVER_WINDOWS.items():
        if hashlib.sha256(server[site:site + size]).hexdigest() != digest:
            raise SystemExit(f"Native event producer window changed: {name}")
    constants = verify_source(image, base, args.header)
    fields = schemas(args.vpk, args.core_vpk)
    print(json.dumps({"client": str(args.client), "client_sha256": CLIENT_HASH,
        "server": str(args.server), "server_sha256": SERVER_HASH, "verified_server_windows": list(SERVER_WINDOWS),
        "verified_windows": list(WINDOWS), "verified_code": list(CHECKS),
        "source_constants": {name: hex(value) for name, value in constants.items()},
        "event_schemas": fields, "asset_sha256": ASSETS, "hitgroups": hitgroups(image, base),
        "identity": "engine-canonical serialized *_pawn field; never userid/current-slot fallback",
        "event_lifetime": "copy synchronously inside externally-owned listener; manager frees event after dispatch",
        "native_shot_id": None, "native_event_tick": None, "live_observation": False}, indent=2))


if __name__ == "__main__":
    main()
