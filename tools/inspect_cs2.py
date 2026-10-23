"""Verify frozen ESP constants against installed-client static schema and code.

No process is needed. This tool does not launch, inject, or modify CS2.
Optional --pid collects read-only observations after static verification.
"""
import argparse
import ctypes
import hashlib
import json
import pathlib
import re
import struct
from verify_sky_draw import mapped, require_bytes

ROOT = pathlib.Path(__file__).resolve().parents[1]
VERIFIED_CLIENT_SHA256 = "ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5"
FIELDS = {
    "CEntityInstance": ["m_pEntity"],
    "CEntityIdentity": ["m_designerName", "m_flags"],
    "C_BaseEntity": ["m_pGameSceneNode", "m_pCollision", "m_iHealth", "m_iMaxHealth", "m_lifeState", "m_iTeamNum"],
    "CBasePlayerController": ["m_iszPlayerName", "m_bIsLocalPlayerController", "m_hPawn"],
    "CCSPlayerController": ["m_hPlayerPawn", "m_sSanitizedPlayerName", "m_iPawnArmor", "m_iPawnHealth", "m_bPawnIsAlive"],
    "CGameSceneNode": ["m_vecAbsOrigin", "m_angAbsRotation", "m_flAbsScale", "m_bDormant", "m_nodeToWorld"],
    "CCollisionProperty": ["m_vecMins", "m_vecMaxs"],
    "C_CSPlayerPawn": ["m_ArmorValue"],
    "C_BasePlayerPawn": ["m_hController", "m_hDefaultController"],
}

# On-disk schema registration templates: class name+0, size+0x18,
# field count+0x1C, field array+0x28. These precede runtime class binding.
# Array records are 0x20 bytes: field name pointer+0, byte offset+0x10.
SCHEMA = {
    "CEntityInstance": (0x226CF28, 0x226CEC0, 3),
    "CEntityIdentity": (0x226D2F8, 0x226D170, 12),
    "C_BaseEntity": (0x21F6E78, 0x21F5550, 82),
    "CBasePlayerController": (0x2225C98, 0x2225A60, 17),
    "CCSPlayerController": (0x2222498, 0x2221810, 70),
    "CGameSceneNode": (0x21FDA88, 0x21FD640, 34),
    "CCollisionProperty": (0x21CECB8, 0x21CE960, 17),
    "C_CSPlayerPawn": (0x223BAC8, 0x223AD40, 105),
    "C_BasePlayerPawn": (0x21F67E8, 0x21F6450, 28),
}


def static_evidence(image, base):
    checks = {
        "entity_global_store": (0x15BEFA9, "48890d08d91501e99bbfffff"),
        "entity_global_load": (0xA1892B, "488b1d863fd00148891ddf52b3014c63b3"),
        "controller_global": (0x934CB0, "488b0541a2c0014189becc060000"),
        "prediction_address": (0xBF8080, "488d0559f59601c3"),
        "pawn_pointer_slot": (0xBD1465, "4c39b6f800000074114488bef10000004c893f4c89bef8000000"),
        "view_matrix": (0xC14FB3, "488d0d2680950148c1e0064803c1c3"),
        "diagnostic_highest_index": (0x9F8B5D, "ff81202100004885d27456"),
        "table_initialization": (0x15BAFCF, "488d43104533ff48891d9b28e0004885db490f44c7488905856adb00"),
        "table_initializer_argument": (0x15BAF64, "488bd9"),
        "handle_lookup": (0x39C703, "418bd281e2ff7f00008bca48c1e9094d8b0cc84d85c9741b8bca81e1ff010000486bd1704903d1740a44395210490f45d5"),
        "lookup_object_pointer": (0x39C73E, "488b0a"),
    }
    require_bytes(image, checks)
    def u32(p): return struct.unpack_from('<I', image, p)[0]
    def ptr(p): return struct.unpack_from('<Q', image, p)[0] - base
    def rip(p): return p + 7 + struct.unpack_from('<i', image, p + 3)[0]
    def string(p):
        if not 0 < p < len(image):
            raise SystemExit(f"Invalid schema string RVA {p:#x}")
        end = image.find(0, p, min(p + 256, len(image)))
        if end < 0:
            raise SystemExit(f"Unterminated schema string {p:#x}")
        return bytes(image[p:end]).decode('ascii')
    fields, registrations = {}, {}
    for name, (registration, array, count) in SCHEMA.items():
        if (string(ptr(registration)) != name or ptr(registration + 0x28) != array or
                struct.unpack_from('<H', image, registration + 0x1C)[0] != count):
            raise SystemExit(f"Changed static schema registration: {name}")
        records = {}
        for index in range(count):
            record = array + index * 0x20
            field = string(ptr(record))
            if field in records:
                raise SystemExit(f"Duplicate field in {name}: {field}")
            records[field] = u32(record + 0x10)
        fields[name] = {field: records[field] for field in FIELDS[name]}
        registrations[name] = {"registration": hex(registration), "fields": hex(array), "field_count": count}
    system = rip(0x15BEFA9)
    if system != rip(0xA1892B):
        raise SystemExit("Entity-system load/store disagree")
    table_offset = image[0x15BAFD2]
    table_global = rip(0x15BAFE4)
    initializer = 0x15BEFB0 + 5 + struct.unpack_from('<i', image, 0x15BEFB1)[0]
    if initializer != 0x15BAF50 or rip(0x39C6E0) != table_global:
        raise SystemExit("Entity table initialization/lookup disagree")
    layout = {
        "TableOffset": table_offset,
        "IdentityStride": image[0x39C726],
        "EntityPointerOffset": 0,  # MOV RCX,[RDX] checked above, no displacement
        "HandleOffset": image[0x39C72F],
        "IndexMask": u32(0x39C708),
        "BucketShift": image[0x39C711],
        "BucketMask": u32(0x39C71F),
    }
    layout["BucketCount"] = (layout["IndexMask"] >> layout["BucketShift"]) + 1
    if u32(SCHEMA['CEntityIdentity'][0] + 0x18) != layout['IdentityStride']:
        raise SystemExit("Identity schema size differs from lookup stride")
    globals_ = {
        "dwEntityList": system, "dwGameEntitySystem": system,
        "dwGameEntitySystem_highestEntityIndex": u32(0x9F8B5F),
        "dwLocalPlayerController": rip(0x934CB0),
        "dwLocalPlayerPawn": rip(0xBF8080) + u32(0xBD1468),
        "dwViewMatrix": rip(0xC14FB3),
    }
    return globals_, fields, layout, registrations, checks


def verify_header(path, timestamp, image_size, globals_, fields, layout):
    source = re.sub(r'//[^\n]*|/\*.*?\*/', '', path.read_text(encoding='utf-8-sig'), flags=re.S)
    def check(scope, values):
        text = source
        if scope:
            matches = re.findall(rf'namespace\s+{scope}\s*\{{([^}}]*)\}}', source, re.S)
            if len(matches) != 1:
                raise SystemExit(f"Expected one header namespace {scope}")
            text = matches[0]
        for name, value in values.items():
            matches = re.findall(rf'\b{name}\s*=\s*(0[xX][0-9a-fA-F]+|[0-9]+)\s*;', text)
            if len(matches) != 1 or int(matches[0], 0) != value:
                raise SystemExit(f"Header mismatch: {scope or 'cs2_offsets'}::{name}; decoded {value:#x}")
    check(None, {'ClientTimestamp': timestamp, 'ClientImageSize': image_size})
    check('globals', globals_)
    check('entity_layout', layout)
    for name, values in fields.items():
        check(name, values)

class Reader:
    def __init__(self, pid):
        self.k32 = ctypes.WinDLL("kernel32", use_last_error=True)
        self.k32.OpenProcess.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32]
        self.k32.OpenProcess.restype = ctypes.c_void_p
        self.k32.ReadProcessMemory.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
        self.k32.ReadProcessMemory.restype = ctypes.c_int
        self.k32.CloseHandle.argtypes = [ctypes.c_void_p]
        self.handle = self.k32.OpenProcess(0x0010, False, pid)
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())
    def read(self, address, size):
        buf = ctypes.create_string_buffer(size)
        done = ctypes.c_size_t()
        if not self.k32.ReadProcessMemory(self.handle, address, buf, size, ctypes.byref(done)) or done.value != size:
            raise OSError(f"ReadProcessMemory failed at 0x{address:X}, size {size}")
        return buf.raw
    def u64(self, address):
        return struct.unpack("<Q", self.read(address, 8))[0]
    def u32(self, address):
        return struct.unpack("<I", self.read(address, 4))[0]
    def string(self, address, size=128):
        return self.read(address, size).split(b"\0", 1)[0].decode("utf-8", "replace")
    def __del__(self):
        if getattr(self, "handle", None):
            self.k32.CloseHandle(self.handle)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=pathlib.Path, default=pathlib.Path(r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll"))
    parser.add_argument("--pid", type=int)
    parser.add_argument("--scan-all", action="store_true", help="Inspect all 64 entity buckets (the default); retained for explicit command documentation.")
    parser.add_argument("--reported-bound", action="store_true", help="Diagnostic only: use the upstream reported highest-index bound, known to miss live pawns in this build.")
    parser.add_argument("--modules", type=pathlib.Path, default=ROOT / "logs/research/modules.json")
    parser.add_argument("--header", type=pathlib.Path, default=ROOT / "include/cs2_offsets.h", help="Source header to verify; never modified")
    args = parser.parse_args()
    raw, image, image_base, pe = mapped(args.client)
    client_hash = hashlib.sha256(raw).hexdigest()
    if client_hash != VERIFIED_CLIENT_SHA256:
        raise SystemExit("Installed client changed: independently reverify static schema and machine evidence.")
    machine, = struct.unpack_from("<H", raw, pe+4)
    timestamp, = struct.unpack_from("<I", raw, pe+8)
    image_size = len(image)
    globals_, fields, layout, registrations, checks = static_evidence(image, image_base)
    verify_header(args.header, timestamp, image_size, globals_, fields, layout)
    evidence = {"client_path": str(args.client), "sha256": client_hash,
        "machine": hex(machine), "pe_timestamp": hex(timestamp), "size_of_image": hex(image_size),
        "provenance": "installed PE static schema registration templates and decoded machine operands",
        "verified_header": str(args.header), "live_observation": False,
        "globals": {k:hex(v) for k,v in globals_.items()},
        "entity_layout": {k:hex(v) for k,v in layout.items()},
        "schema": {k:{f:hex(v) for f,v in fs.items()} for k,fs in fields.items()},
        "schema_registrations": registrations, "verified_code": list(checks)}
    if args.pid:
        modules = json.loads(args.modules.read_text(encoding="utf-8-sig"))
        base = next(m["Base"] for m in modules if m["ModuleName"] == "client.dll")
        mem = Reader(args.pid)
        system = mem.u64(base+globals_["dwGameEntitySystem"])
        table = system + layout["TableOffset"]
        evidence["live_observation"] = True
        evidence["live"] = {"pid": args.pid, "base": hex(base), "entity_system": hex(system), "embedded_table": hex(table), "system_bytes": mem.read(system, 0x40).hex(), "highest_index":mem.u32(system+globals_["dwGameEntitySystem_highestEntityIndex"]), "local_controller":hex(mem.u64(base+globals_["dwLocalPlayerController"])), "local_pawn":hex(mem.u64(base+globals_["dwLocalPlayerPawn"])), "view_matrix":struct.unpack("<16f", mem.read(base+globals_["dwViewMatrix"],64))}
        entities = []
        capacity = layout["BucketCount"] * (layout["BucketMask"] + 1)
        for index in range(min(evidence["live"]["highest_index"]+1, capacity) if args.reported_bound and not args.scan_all else capacity):
            try:
                bucket = mem.u64(table+(index>>layout["BucketShift"])*8)
                if not bucket:
                    continue
                identity = bucket+(index&layout["BucketMask"])*layout["IdentityStride"]
                entity = mem.u64(identity+layout["EntityPointerOffset"])
                handle = mem.u32(identity+layout["HandleOffset"])
                if not entity or (handle&layout["IndexMask"])!=index:
                    continue
                backref = mem.u64(entity+fields["CEntityInstance"]["m_pEntity"])
                designer = mem.u64(identity+fields["CEntityIdentity"]["m_designerName"])
                entry = {"index":index,"identity":hex(identity),"entity":hex(entity),"handle":hex(handle),"identity_backref":hex(backref),"designer":mem.string(designer,64) if designer else ""}
                if entry["designer"] == "cs_player_controller":
                    entry["name"] = mem.string(entity+fields["CBasePlayerController"]["m_iszPlayerName"])
                    entry["pawn_handle"] = hex(mem.u32(entity+fields["CCSPlayerController"]["m_hPlayerPawn"]))
                if "player" in entry["designer"] and "controller" not in entry["designer"]:
                    entry["health"] = mem.u32(entity+fields["C_BaseEntity"]["m_iHealth"])
                    entry["life_state"] = mem.read(entity+fields["C_BaseEntity"]["m_lifeState"],1)[0]
                    entry["team"] = mem.read(entity+fields["C_BaseEntity"]["m_iTeamNum"],1)[0]
                    entry["controller_handle"] = hex(mem.u32(entity+fields["C_BasePlayerPawn"]["m_hController"]))
                    entry["default_controller_handle"] = hex(mem.u32(entity+fields["C_BasePlayerPawn"]["m_hDefaultController"]))
                    scene = mem.u64(entity+fields["C_BaseEntity"]["m_pGameSceneNode"])
                    collision = mem.u64(entity+fields["C_BaseEntity"]["m_pCollision"])
                    if scene:
                        entry["scene"] = {"ptr":hex(scene),"origin":struct.unpack("<3f",mem.read(scene+fields["CGameSceneNode"]["m_vecAbsOrigin"],12)),"rotation":struct.unpack("<3f",mem.read(scene+fields["CGameSceneNode"]["m_angAbsRotation"],12)),"scale":struct.unpack("<f",mem.read(scene+fields["CGameSceneNode"]["m_flAbsScale"],4))[0],"node_to_world":struct.unpack("<8f",mem.read(scene+fields["CGameSceneNode"]["m_nodeToWorld"],32))}
                    if collision:
                        entry["collision"]={"mins":struct.unpack("<3f",mem.read(collision+fields["CCollisionProperty"]["m_vecMins"],12)),"maxs":struct.unpack("<3f",mem.read(collision+fields["CCollisionProperty"]["m_vecMaxs"],12))}
                entities.append(entry)
            except OSError:
                continue
        evidence["live"]["entities"] = entities
    print(json.dumps(evidence, indent=2))

if __name__ == "__main__":
    main()
