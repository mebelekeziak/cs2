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
CLIENT_HASH = "659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0"
SERVER_HASH = "09e8c04c5f44040155d04fa79a3f43491d542a386f45807ed77785729e5925a9"
SERVER_WINDOWS = {
    "server_weapon_fire_broadcast": (0xA07CFC, 0x15B, "b4228042c133a8e75719cfac2c66cc333d1e966594efed93ce1cfc9e0bd11940"),
    "server_bullet_impact_pawn_and_xyz": (0xA70A43, 0x14D, "76205d54b3192ab3e1203539f9b08532e269685d6b0cbd3dc65c1e6cb9193468"),
    "server_hurt_fields": (0xA8DFC4, 0x420, "a0091188cfd475127402931d8001424c8d9b3609f02cfdf80480c2ece30d760c"),
    "server_combined_controller_pawn_setter": (0xC21E10, 0x150, "872dd3157f111b2cbef299c28425058f0b296a4d805ea696cb6065433bbe9796"),
}
WINDOWS = {
    "manager_add": (0x9C9D30, 0x11C, "7772f8d29812a3126d7e60c34395d48498a93ec7ccb39c1d7dc16ecdfd6fad92"),
    "manager_find": (0x9E1BF0, 0x157, "79f917cf9ec401788d54684461e3e368d71127bce971aa31046a509c6784ef93"),
    "manager_client_dispatch": (0x9E2960, 0x247, "7f54d677f668ee08ad8b9e4a7ff6a80db648c78b9688c9805b27158f477a0589"),
    "manager_register": (0x9C99B0, 0x37E, "506aabfdc0ba27d96a248f474f7abae32342fbaaf4ca3ed09e29b09ac2ebb3db"),
    "manager_remove": (0xA01540, 0x1DF, "7be1273350ec316fe20942272a41ad3790880cad328da2e3c56112a32726d466"),
    "manager_reset_external_listener_wrappers": (0xA04868, 0x34, "a7e498329a01db8ec00825ad044ce9bec4aa8766811f39400b50fc52ec4213fd"),
    "event_name": (0x9E7E50, 0x22, "ec2655f13c7114e54d211c757a4fdda8d80f782294cad531e8bcec0a5ed4b363"),
    "event_empty": (0x9F1770, 0x56, "d906d92c014040e48afe8647a03ac03fe37b62f48be328698cf62f7cfe215394"),
    "event_bool": (0x9E3E60, 0x2D, "9d955b7b8eb7c00d4113c35d730d54023772e3da0094a7ea0896ea94ff35dd23"),
    "event_int": (0x9E7120, 0x2D, "6081c5f24a24fd77d72dfded439e815d673d4a6e2461ad7db11ac663c3157dda"),
    "event_float": (0x9E6D40, 0x2D, "a9338414c443efad3987eaf5e2143d658dbf2a5f75a3acb286738b29ada68a07"),
    "event_string": (0x9E9550, 0x2D, "6e6c7144f26ef69575611fdc18deb59171c6ec2157233dd9674e40d3ea751a89"),
    "event_handle": (0x9E4620, 0x55, "2085d254151931aaf3d106e2acd990b2ef20bdd54a7e3223728c390ab5766066"),
    "key_hash": (0x166050, 0x126, "fc102547d3248e607c9e2ca0d949e171aefcd880c1c97375b30f47283d89f9e6"),
    "hitgroups": (0x1AF8B90, 0x1C0, "2775a2a6cdf6b376b7d54918e962ab02682742bd8f5ccd032d63975bea36e301"),
}
CHECKS = {
    "global_manager_load": (0xC5FC00, "4c8d05f1d00201488b0dfabf8e014533c9488bd6488b01ff5018"),
    "global_manager_clear": (0x9C4453, "48391dae77b801750748892da577b801"),
    "event_ctor": (0x9C1213, "488d05a60327014d8bf8488901488bda33c04c8bf1"),
}
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
    if listener != 0x1ACB668 or struct.unpack_from("<QQ", image, listener) != (base + 0x1906A0, base + 0x19FAFE4):
        raise SystemExit("Native two-method listener interface RTTI/vtable differs")
    expected = {"Manager": rip(0xC5FC07), "ManagerVtable": manager, "EventVtable": event}
    if expected["Manager"] != rip(0x9C4453):
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
    registration = 0x21CF818
    if (string(struct.unpack_from("<Q", image, registration)[0] - base) != "HitGroup_t" or
        struct.unpack_from("<I", image, registration + 0x14)[0] != 14 or
        struct.unpack_from("<Q", image, registration + 0x18)[0] - base != 0x1AF8B90):
        raise SystemExit("Named HitGroup_t registration differs")
    values = {}
    for index in range(14):
        record = 0x1AF8B90 + index * 0x20
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
    if hashlib.sha256(raw).hexdigest() != CLIENT_HASH or struct.unpack_from("<I", raw, nt + 8)[0] != 0x6AD01314 or len(image) != 0x299D000:
        raise SystemExit("Client changed: repeat native listener/event-field investigation")
    require_bytes(image, CHECKS)
    for name, (site, size, digest) in WINDOWS.items():
        if hashlib.sha256(image[site:site + size]).hexdigest() != digest:
            raise SystemExit(f"Native event window changed: {name}")
    server_raw, server, _, server_nt = mapped(args.server)
    if hashlib.sha256(server_raw).hexdigest() != SERVER_HASH or struct.unpack_from("<I", server_raw, server_nt + 8)[0] != 0x6ACFCD38 or len(server) != 0x2572000:
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
