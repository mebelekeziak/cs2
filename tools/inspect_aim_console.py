"""Verify the installed native console printf ABI and frozen runtime checks.

Reads only installed files and source; never starts or changes a game process.
Uses only the Python standard library and the project's existing PE mapper.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from verify_sky_draw import DEFAULT, ROOT, mapped, require_bytes

TIER0_HASH = "990ea56bf1626d299ba54b64b423f2e409bd592bb737c7a5edf7972ad5c7143e"
CLIENT_HASH = "659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0"
MSG_RVA = 0x7EE60
PROLOGUE = "48894c240848895424104c894424184c894c2420574881ec40080000"
CHECKS = {
    "msg_varargs_prologue": (MSG_RVA, PROLOGUE),
    "msg_format_from_saved_rcx": (0x7EEA5, "488b9c2450080000488bcb"),
    "msg_varargs_from_saved_rdx": (0x7EEB8, "488db42458080000"),
    "msg_vsnprintf_arguments": (0x7EECA, "4889742428488d54243033f64c8bcb41b8000800004889742420488b084883c901e8f00e2300"),
    "msg_dispatch_formatted_line": (0x7EF11, "8b15dde63200488d44243048897424284533c941b8020000004889442420488bcfe8d98e0a00"),
}
CALLERS = {
    "client_msg_string_1": (0x153C4F, "488bd0488d0d47209701ff15399d9501"),
    "client_msg_string_2": (0x153C72, "488bd0488d0d34209701ff15169d9501"),
}


def string(image, rva):
    if not 0 <= rva < len(image):
        raise SystemExit(f"Invalid string RVA: {rva:#x}")
    end = image.find(0, rva, min(len(image), rva + 256))
    if end < 0:
        raise SystemExit(f"Unterminated string at {rva:#x}")
    return bytes(image[rva:end]).decode("ascii")


def export(image, raw, nt, name):
    rva, size = struct.unpack_from("<II", raw, nt + 24 + 112)
    functions, names, function_array, name_array, ordinal_array = struct.unpack_from("<IIIII", image, rva + 20)
    found = []
    for index in range(names):
        text = struct.unpack_from("<I", image, name_array + index * 4)[0]
        if image[text:text + len(name) + 1] == name.encode("ascii") + b"\0":
            ordinal = struct.unpack_from("<H", image, ordinal_array + index * 2)[0]
            if ordinal >= functions:
                raise SystemExit("Invalid Msg export ordinal")
            address = struct.unpack_from("<I", image, function_array + ordinal * 4)[0]
            if rva <= address < rva + size:
                raise SystemExit("Forwarded Msg export has no verified native body")
            found.append(address)
    if len(found) != 1:
        raise SystemExit(f"Expected exactly one {name} export")
    return found[0]


def import_slot(image, raw, nt, dll, name):
    descriptor, size = struct.unpack_from("<II", raw, nt + 24 + 112 + 8)
    found = []
    for offset in range(0, size, 20):
        original, timestamp, chain, dll_name, first = struct.unpack_from("<5I", image, descriptor + offset)
        if not any((original, timestamp, chain, dll_name, first)):
            break
        if string(image, dll_name).lower() != dll.lower():
            continue
        thunk = original or first
        for index in range(65536):
            entry = struct.unpack_from("<Q", image, thunk + index * 8)[0]
            if not entry:
                break
            if not entry & (1 << 63) and image[entry + 2:entry + 3 + len(name)] == name.encode("ascii") + b"\0":
                found.append(first + index * 8)
    if len(found) != 1:
        raise SystemExit(f"Expected exactly one {dll}!{name} import")
    return found[0]


def verify_source(path, timestamp, size):
    text = re.sub(r"//[^\n]*|/\*.*?\*/", "", path.read_text(encoding="utf-8-sig"), flags=re.S)
    for name, expected in (("Tier0Timestamp", timestamp), ("Tier0ImageSize", size), ("MsgRva", MSG_RVA)):
        values = re.findall(rf"\b{name}\s*=\s*(0[xX][0-9a-fA-F]+|[0-9]+)\s*;", text)
        if len(values) != 1 or int(values[0], 0) != expected:
            raise SystemExit(f"Console source constant mismatch: {name}")
    arrays = re.findall(r"\bMsgPrologue\[\]\s*\{([^}]*)\}", text, flags=re.S)
    if len(arrays) != 1 or bytes(int(value, 0) for value in re.findall(r"0x[0-9a-fA-F]+|\d+", arrays[0])) != bytes.fromhex(PROLOGUE):
        raise SystemExit("Console source MsgPrologue mismatch")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=DEFAULT)
    parser.add_argument("--source", type=Path, default=ROOT / "src/aim_console.cpp")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    tier0_path = args.game / "bin/win64/tier0.dll"
    client_path = args.game / "csgo/bin/win64/client.dll"
    raw, image, _, nt = mapped(tier0_path)
    tier0_hash = hashlib.sha256(raw).hexdigest()
    if tier0_hash != TIER0_HASH:
        raise SystemExit("Installed tier0 hash changed; console ABI needs renewed evidence")
    timestamp = struct.unpack_from("<I", raw, nt + 8)[0]
    size = struct.unpack_from("<I", raw, nt + 24 + 56)[0]
    if export(image, raw, nt, "Msg") != MSG_RVA:
        raise SystemExit("Named Msg export moved")
    require_bytes(image, CHECKS)
    verify_source(args.source, timestamp, size)
    client_raw, client, _, client_nt = mapped(client_path)
    client_hash = hashlib.sha256(client_raw).hexdigest()
    if client_hash != CLIENT_HASH:
        raise SystemExit("Installed client hash changed; native console callers need renewed evidence")
    slot = import_slot(client, client_raw, client_nt, "tier0.dll", "Msg")
    require_bytes(client, CALLERS)
    for lea, call, expected_format in ((0x153C52, 0x153C59, "T     %s\n"), (0x153C75, 0x153C7C, "UWT1  %s\n")):
        format_rva = lea + 7 + struct.unpack_from("<i", client, lea + 3)[0]
        target = call + 6 + struct.unpack_from("<i", client, call + 2)[0]
        if target != slot or string(client, format_rva) != expected_format:
            raise SystemExit("Native Msg caller no longer passes RCX=format, RDX=string")
    result = {
        "status": "pass", "tier0": str(tier0_path), "tier0_sha256": tier0_hash,
        "client": str(client_path), "client_sha256": client_hash,
        "timestamp": hex(timestamp), "image_size": hex(size), "export": "Msg", "export_rva": hex(MSG_RVA),
        "abi": "AMD64 native variadic printf: RCX=const char* format, RDX=first vararg; return value unused",
        "native_format_buffer_bytes": 0x800, "checked_native_paths": list(CHECKS) + list(CALLERS),
        "source": str(args.source), "source_sha256": hashlib.sha256(args.source.read_bytes()).hexdigest(),
        "observed_live_console": False,
    }
    rendered = json.dumps(result, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered + "\n", encoding="utf-8")
    print(rendered)


if __name__ == "__main__":
    main()
