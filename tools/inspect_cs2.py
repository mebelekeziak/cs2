"""Read-only installed-client evidence collection and build-specific header generation.

Input schema/global JSON must be generated locally by a2x/cs2-dumper while the
installed client is running. This tool does not launch, inject, or modify CS2.
Optional --pid reads process memory with PROCESS_VM_READ only.
"""
import argparse
import ctypes
import hashlib
import json
import pathlib
import struct

ROOT = pathlib.Path(__file__).resolve().parents[1]
VERIFIED_CLIENT_SHA256 = "44af2aa0d2c64f90d273444cb393b8b679094f0b02136dbdb89d4ec82a560008"
VERIFIED_DUMP_HASHES = {
    "offsets.json": "d8f824a6007381d3c484914ba6450e226a17483e5f2c83880115f2f4c059f2b6",
    "client_dll.json": "8916976681b167a052160289fc37de99ced6beef3fbf2f63b02f0f251e33c4c7",
}
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
    parser.add_argument("--dump", type=pathlib.Path, default=ROOT / "logs/research/cs2-dumper/generated")
    parser.add_argument("--pid", type=int)
    parser.add_argument("--scan-all", action="store_true", help="Inspect all 64 entity buckets (the default); retained for explicit command documentation.")
    parser.add_argument("--reported-bound", action="store_true", help="Diagnostic only: use the upstream reported highest-index bound, known to miss live pawns in this build.")
    parser.add_argument("--modules", type=pathlib.Path, default=ROOT / "logs/research/modules.json")
    parser.add_argument("--header", type=pathlib.Path)
    args = parser.parse_args()
    raw = args.client.read_bytes()
    client_hash = hashlib.sha256(raw).hexdigest()
    if (args.pid or args.header) and client_hash != VERIFIED_CLIENT_SHA256:
        raise SystemExit("Installed client changed: re-verify binary entity layout and local schema/global dump before reading entities or generating a header.")
    dump_hashes = {name:hashlib.sha256((args.dump/name).read_bytes()).hexdigest() for name in VERIFIED_DUMP_HASHES}
    if (args.pid or args.header) and dump_hashes != VERIFIED_DUMP_HASHES:
        raise SystemExit("Input dump changed: re-verify its association with this installed client before reading entities or generating a header.")
    pe = struct.unpack_from("<I", raw, 0x3C)[0]
    machine, = struct.unpack_from("<H", raw, pe+4)
    timestamp, = struct.unpack_from("<I", raw, pe+8)
    image_size, = struct.unpack_from("<I", raw, pe+24+56)
    globals_ = json.loads((args.dump / "offsets.json").read_text())["client.dll"]
    classes = json.loads((args.dump / "client_dll.json").read_text())["client.dll"]["classes"]
    fields = {name: {field: classes[name]["fields"][field] for field in wanted} for name, wanted in FIELDS.items()}
    evidence = {"client_path": str(args.client), "sha256": client_hash, "machine": hex(machine), "pe_timestamp": hex(timestamp), "size_of_image": hex(image_size), "dump_hashes": dump_hashes, "globals": {k:hex(v) for k,v in globals_.items()}, "schema": {k:{f:hex(v) for f,v in fs.items()} for k,fs in fields.items()}}
    if args.pid:
        modules = json.loads(args.modules.read_text(encoding="utf-8-sig"))
        base = next(m["Base"] for m in modules if m["ModuleName"] == "client.dll")
        mem = Reader(args.pid)
        system = mem.u64(base+globals_["dwGameEntitySystem"])
        table = mem.u64(base+0x2366770)
        evidence["live"] = {"pid": args.pid, "base": hex(base), "entity_system": hex(system), "table_global": hex(table), "system_bytes": mem.read(system, 0x40).hex(), "highest_index":mem.u32(system+globals_["dwGameEntitySystem_highestEntityIndex"]), "local_controller":hex(mem.u64(base+globals_["dwLocalPlayerController"])), "local_pawn":hex(mem.u64(base+globals_["dwLocalPlayerPawn"])), "view_matrix":struct.unpack("<16f", mem.read(base+globals_["dwViewMatrix"],64))}
        entities = []
        evidence["live"]["system_tail_ints"] = struct.unpack("<32I",mem.read(system+0x20E0,128))
        for index in range(min(evidence["live"]["highest_index"]+1, 32768) if args.reported_bound and not args.scan_all else 32768):
            try:
                bucket = mem.u64(table+(index>>9)*8)
                identity = bucket+(index&511)*0x70
                entity = mem.u64(identity)
                handle = mem.u32(identity+0x10)
                if not entity or (handle&0x7fff)!=index:
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
    if args.header:
        header = ["#pragma once", "#include <cstddef>", "#include <cstdint>", "", "// Generated from the locally running installed CS2 client, October 13, 2026.", "// a2x/cs2-dumper release 0.1.3; source reference 4116de000e085d62bbd42334c67b35bda37bda4f.", "// Dumper SHA256: 501368ffb8f252b3cfb70fee6177b6ab4bbb21cad480724af17720d74ce44bbb.", "// client.dll SHA256: " + evidence["sha256"], "// Entity layout separately verified in installed-client disassembly; see logs/research.", "namespace cs2_offsets {", f"inline constexpr std::uint32_t ClientTimestamp = 0x{timestamp:X};", f"inline constexpr std::uint32_t ClientImageSize = 0x{image_size:X};"]
        header.append("namespace globals {")
        for name in ["dwEntityList","dwGameEntitySystem","dwGameEntitySystem_highestEntityIndex","dwLocalPlayerController","dwLocalPlayerPawn","dwViewMatrix"]:
            header.append(f"inline constexpr std::ptrdiff_t {name} = 0x{globals_[name]:X};")
        header.append("}")
        header.extend(["namespace entity_layout {", "inline constexpr std::ptrdiff_t TableOffset = 0x10;", "inline constexpr std::ptrdiff_t IdentityStride = 0x70;", "inline constexpr std::ptrdiff_t EntityPointerOffset = 0;", "inline constexpr std::ptrdiff_t HandleOffset = 0x10;", "inline constexpr std::uint32_t IndexMask = 0x7FFF;", "inline constexpr std::uint32_t BucketShift = 9;", "inline constexpr std::uint32_t BucketMask = 0x1FF;", "inline constexpr std::uint32_t BucketCount = 64;", "}"])
        for name, members in fields.items():
            header.append(f"namespace {name} {{")
            header.extend(f"inline constexpr std::ptrdiff_t {field} = 0x{offset:X};" for field,offset in members.items())
            header.append("}")
        header.append("}")
        args.header.write_text("\n".join(header)+"\n")
    print(json.dumps(evidence, indent=2))

if __name__ == "__main__":
    main()
