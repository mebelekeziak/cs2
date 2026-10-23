"""Verify native console listener, clear command and locked command queue; no game launch."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from verify_sky_draw import DEFAULT, ROOT, mapped, primary_vtable, require_bytes
from inspect_aim_console import import_slot, string

ENGINE_HASH = "e8533af07adc3426eac95391cb70804a0a27f8391c03d36f1cfa06f50d901d29"
CHECKS = {'Listener': (2141216,
              '48895c2408574883ec20837a0805498bf8488bda7510488b0d9b154b00488b01ff9098'),
 'Clear': (1802864,
           '48895c2408574883ec20488b01498bf8488bd9ff90b800000084c074614883bbd001000000740c488b03488bcbff900001000048833dedbc570000744183bf38040000007e0c488b8740040000488b08eb07488d0d86db4000488d15706b4500ff152ab73500488b0dbbbc570085c00f94c24533c0488b01ff90c8000000488b5c24304883c4205fc3'),
 'Queue': (474032,
           '48895c24184889742420574883ec40498bf88bda488bf183fa05752d488b0da5056c00488b01ff900001000084c07546'),
 'QueueLock': (474105,
               '48896c24504863eb488d85e2430000488d0440488d1c86ff15c2e849008bd0394308756766ff4304e984000000488b051b499400488d0ddcc364008b58588bd3488b05d0c36400ff90e000000084c075afba5b000000488bcf33dbe88beb43004885c0749b4533c948895c24204533c0488bd7488bcee84c371500488b5c2460488b7424684883c4405fc38b039085c07516b903000000f00fb10b750b66c743040100895308eb09488bcbff15cefa4900f20f10056e1b5b004533c948c7442430000000004533c0f20f11442428488bd74869cdd8860000c6442420014881c1880500004803ceff1582f04900'),
 'QueueUnlock': (474342,
                 '488b6c2450b8ffff000066014304751ec7430800000000b8fdfffffff00fc10383c0fd7409488bcbff154cfa4900488b5c2460488b7424684883c4405fc3cccccccccccccccc'),
 'QueueCaller': (482791, '4c8d05aafb55008bd7488d0d293a900040b601e8b1ddffff'),
 'ClearRegistration': (1791630, '488d05db2b0000c645f8010f104dd04889'),
 'ClearName': (1791682, '488d15ff1e4300'),
 'ListenerRoute': (2141350,
                   '488b0d83c752004885c9740d488b014c8d430c488bd7ff501848833d71c75200007408')}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=DEFAULT)
    parser.add_argument("--header", type=Path, default=ROOT / "include/console_control_offsets.h")
    args = parser.parse_args()
    path = args.game / "bin/win64/engine2.dll"
    raw, image, base, nt = mapped(path)
    if hashlib.sha256(raw).hexdigest() != ENGINE_HASH:
        raise SystemExit("Engine changed; console controls require renewed evidence")
    require_bytes(image, CHECKS)
    header = args.header.read_text()
    for name, (rva, expected) in CHECKS.items():
        found = re.search(r"uintptr_t " + name + r" = (0x[0-9A-Fa-f]+)", header)
        body = re.search(r"unsigned char " + name + r"Bytes\[\]\{([^}]+)\}", header)
        if not found or int(found[1], 16) != rva or not body or bytes(int(x, 16) for x in body[1].split(",")).hex() != expected:
            raise SystemExit("Header mismatch: " + name)
    def value(name):
        return int(re.search(r"(?:unsigned|uintptr_t) " + name + r" = (0x[0-9A-Fa-f]+)", header)[1], 16)
    if value("Timestamp") != struct.unpack_from("<I", raw, nt + 8)[0] or value("ImageSize") != len(image):
        raise SystemExit("Header module identity mismatch")
    # Independent native caller supplies this command-buffer object and function.
    buffer = 0x75df0 + 7 + struct.unpack_from("<i", image, 0x75df3)[0]
    target = 0x75dfa + 5 + struct.unpack_from("<i", image, 0x75dfb)[0]
    if value("Buffer") != buffer or target != CHECKS["Queue"][0]:
        raise SystemExit("Native queue object/callee differs")
    if value("ArgCount") != struct.unpack_from("<i", image, 0x1b82af)[0] or value("ArgValues") != struct.unpack_from("<i", image, 0x1b82b9)[0]:
        raise SystemExit("Native clear argument count/vector layout differs")
    # Queue holds a native recursive lock and passes literal text to AddText.
    add_text = import_slot(image, raw, nt, "tier0.dll", "?AddText@CCommandBuffer@@QEAA_NPEBDHH_NN_K@Z")
    if 0x73ce0 + 6 + struct.unpack_from("<i", image, 0x73ce2)[0] != add_text:
        raise SystemExit("Queue no longer calls native AddText")
    vt = primary_vtable(image, base, b".?AVCEngineConsoleLoggingListener@@")
    if struct.unpack_from("<Q", image, vt)[0] - base != CHECKS["Listener"][0]:
        raise SystemExit("Console logging listener vtable differs")
    clear = 0x1b568e + 7 + struct.unpack_from("<i", image, 0x1b5691)[0]
    clear_name = 0x1b56c2 + 7 + struct.unpack_from("<i", image, 0x1b56c5)[0]
    if clear != CHECKS["Clear"][0] or string(image, clear_name) != "clear":
        raise SystemExit("Native clear registration differs")
    print(json.dumps({"status": "pass", "engine": str(path), "sha256": ENGINE_HASH,
        "listener": hex(CHECKS["Listener"][0]), "listener_vtable": hex(vt),
        "clear": hex(clear), "queue": hex(target), "buffer": hex(buffer),
        "queue_abi": "void(buffer*, int slot, const char*); locked AddText, deferred native execution",
        "clear_abi": "void(gameUIService*, const command context*, const command args*)",
        "listener_abi": "void(listener*, const logging context*, const char*)",
        "live_observation": False}, indent=2))


if __name__ == "__main__":
    main()
