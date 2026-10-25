"""Read-only exact-build native shot-construction observer verification.

No client/server process or native callback is invoked. The observer copies
client construction evidence and does not establish authoritative firing.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from verify_sky_draw import mapped, primary_vtable

ROOT = Path(__file__).resolve().parents[1]
CLIENT = Path(r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll")
CLIENT_HASH = "ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5"
WINDOWS = {
    "Builder": (0x80e530, 0xce6, "02c24fe6a9d0abcc060d5c8d539b0afd3a783cc7ab295f4444407a4b820ab2eb"),
    "Wrapper": (0x804ea0, 0x1c7, "bc0d8fcf11f58a3c427b17fd02273675c8743a932124f6dc3c4b164bc813a06b"),
    "OrdinaryFire": (0x804660, 0x6dd, "f076d6b82fd1e9ebb0f267a241ea87ee6a19ea84f2e5ad163ad87ffe9b4cd437"),
    "WeaponOwner": (0x942dd0, 0x5f, "6144ca111a3c037856972e9a169cbf0181c2cc3b3c3a01c9758bdccdd0c9a40f"),
    "PawnController": (0x75dda0, 0x5f, "e197b61fe477ec562c94d857231d0c3226c8b63ffc3d116f5243c769f1652964"),
    "SetCurrentCommand": (0x964490, 0x11, "fb1cb471c353216fb9454f218b7600a66bef32aa4a6127c9da45b275887833f1"),
    "PredictionCommandScope": (0xbefc60, 0x862, "083f517f16838ca16e3d4a6a99b0f21f8bd4759a7b4d327808f240f8e5185bb3"),
    "PrimaryAttackSelector": (0x8235d4, 0x15, "9846a83b0a65481d760ff19b183fb059b790710dc52c30a90996b914755cecc5"),
    "FinalAccuracyInputs": (0x804b6b, 223, "12326217c2839b9ce3e2c2fac780b95b999126d59fe94f54b133d53acbfc2059"),
    "SpreadGetter": (0x80e2c0, 43, "25fe2c8f08c83dfa604e353a2324870533fb2f5ca497ba181eb48215cbe42561"),
    "InaccuracyGetter": (0x80cfd0, 796, "1054ae1b18c90cab613e7536aaed31318e24e381c2ce5084d3c36fef421cfa9e"),
}


def verify(client, header):
    raw, image, base, nt = mapped(client)
    if hashlib.sha256(raw).hexdigest() != CLIENT_HASH:
        raise SystemExit("Changed client: repeat native shot observer investigation")
    for name, (at, size, digest) in WINDOWS.items():
        if hashlib.sha256(image[at:at + size]).hexdigest() != digest:
            raise SystemExit("Changed native shot observer window: " + name)

    def u32(at):
        return struct.unpack_from("<I", image, at)[0]

    def call(at):
        if image[at] != 0xe8:
            raise SystemExit(f"Expected direct native call at {at:#x}")
        return at + 5 + struct.unpack_from("<i", image, at + 1)[0]

    def pointer(at):
        return struct.unpack_from("<Q", image, at)[0] - base

    def virtual_slot(at):
        if bytes(image[at:at + 2]) != b"\xff\x90":
            raise SystemExit(f"Expected native six-byte virtual call at {at:#x}")
        return u32(at + 2)

    def string(at):
        end = image.find(0, at, min(at + 512, len(image)))
        if at < 0 or end < at:
            raise SystemExit("Invalid native schema string")
        return bytes(image[at:end]).decode("ascii")

    # The complete builder consumer independently connects the exact native
    # controller-owned command to the already-frozen history index/array fields.
    current_command = u32(0x80e66e)
    if call(0x80e56e) != WINDOWS["WeaponOwner"][0] or call(0x80e65d) != WINDOWS["PawnController"][0]:
        raise SystemExit("Changed native weapon/pawn/controller owner chain")
    if current_command != u32(0x964496) or call(0xbefe33) != 0x964490 or call(0xbf0256) != 0x964490:
        raise SystemExit("Native command producer/consumer disagree")
    if bytes(image[0xbefe30:0xbefe33]) != b"\x48\x8b\xd6" or bytes(image[0xbf0254:0xbf0256]) != b"\x33\xd2":
        raise SystemExit("Prediction command is not installed/cleared at the verified scope")
    if u32(0x75dda2) != 0x14bc or image[0x80e68f] != 0x4c or image[0x80e698] != 0x30 or image[0x80e788] != 0x38:
        raise SystemExit("Changed controller handle or native indexed command consumption")
    if u32(0x8235d7) != 0x1a00 or bytes(image[0x8235db:0x8235de]) != b"\x45\x33\xc9" or call(0x8235e4) != 0x804660:
        raise SystemExit("Scoped weapon mode is no longer separate from the primary attack selector")

    # Independently named schema corroborates weapon owner full-handle width.
    registration = 0x21f6e78
    if string(pointer(registration)) != "C_BaseEntity":
        raise SystemExit("Changed C_BaseEntity schema registration")
    array = pointer(registration + 0x28)
    count = struct.unpack_from("<H", image, registration + 0x1c)[0]
    fields = {string(pointer(array + index * 0x20)): u32(array + index * 0x20 + 0x10)
              for index in range(count)}
    owner = u32(0x942dd2)
    if fields.get("m_hOwnerEntity") != owner:
        raise SystemExit("Weapon owner getter disagrees with named entity schema")

    # The ordinary caller copies all four 16-byte blocks from the builder's
    # caller-owned result before sampling native accuracy and effective seed.
    block_loads = [(0x804a68, "0f284590"), (0x804a70, "0f284da0"),
                   (0x804a86, "0f2845b0"), (0x804a8e, "0f284dc0")]
    for at, wanted in block_loads:
        if bytes(image[at:at + 4]).hex() != wanted:
            raise SystemExit("Changed native shot result extent")
    # These are the actual ordinary-fire accuracy invocations after builder
    # completion. Decode their virtual slots, then resolve targets from the
    # independently named weapon RTTI table rather than copied source RVAs.
    weapon_table = primary_vtable(image, base, b".?AVC_AK47@@")
    spread_call, inaccuracy_call = 0x804b71, 0x804b85
    spread_slot, inaccuracy_slot = virtual_slot(spread_call), virtual_slot(inaccuracy_call)
    spread, inaccuracy = pointer(weapon_table + spread_slot), pointer(weapon_table + inaccuracy_slot)
    if spread != WINDOWS["SpreadGetter"][0] or inaccuracy != WINDOWS["InaccuracyGetter"][0]:
        raise SystemExit("Named weapon virtual slots disagree with the pinned native accuracy getters")
    if bytes(image[0x804b6b:spread_call]) != bytes.fromhex("488b06488bce") or bytes(image[spread_call + 6:inaccuracy_call]) != bytes.fromhex("488b064533c033d2488bce0f28f8"):
        raise SystemExit("Changed ordinary accuracy weapon/optional-output argument setup")
    expected = {
        "Builder": call(0x804eda), "BuilderCall": 0x804eda,
        "BuilderReturn": 0x804eda + 5, "CurrentCommand": current_command,
        "OwnerHandle": owner, "NativeShotSize": len(block_loads) * 16,
        "Spread": spread, "SpreadCall": spread_call, "SpreadReturn": spread_call + 6,
        "Inaccuracy": inaccuracy, "InaccuracyCall": inaccuracy_call, "InaccuracyReturn": inaccuracy_call + 6,
    }
    source = re.sub(r"//[^\n]*|/\*.*?\*/", "", header.read_text(encoding="utf-8-sig"), flags=re.S)
    actual = {name: int(value, 0) for name, value in re.findall(
        r"\bptrdiff_t\s+(\w+)\s*=\s*(0[xX][\da-fA-F]+|\d+)\s*;", source)}
    if actual != expected:
        raise SystemExit("Observer source constants differ from independently decoded native contracts")
    for name, at, size in [("BuilderBytes", expected["Builder"], 20), ("BuilderCallBytes", expected["BuilderCall"], 5),
                           ("SpreadBytes", spread, 43), ("InaccuracyBytes", inaccuracy, 33),
                           ("SpreadCallBytes", spread_call, 6), ("InaccuracyCallBytes", inaccuracy_call, 6)]:
        match = re.search(r"\b" + name + r"\[\]\s*\{([^}]*)\}", source)
        value = bytes(int(part.strip(), 0) for part in match[1].split(",") if part.strip()) if match else b""
        if value != image[at:at + size]:
            raise SystemExit("Changed observer runtime entry fingerprint: " + name)
    return {
        "client": str(client), "sha256": CLIENT_HASH,
        "machine": hex(struct.unpack_from("<H", raw, nt + 4)[0]),
        "constants": {name: hex(value) for name, value in expected.items()},
        "native_windows": {name: {"rva": hex(at), "size": size, "sha256": digest}
                           for name, (at, size, digest) in WINDOWS.items()},
        "abi": "void* Builder(void* weapon, void* callerOwned64ByteOutput, int attackSelector); RAX=output; selector0=primary, distinct from scoped weapon mode",
        "accuracy_abi": ["float Spread(void* weapon); XMM0=float return", "float Inaccuracy(void* weapon,float* firstOptional,float* secondOptional); actual engine call supplies null RDX/R8; no attack selector argument"],
        "accuracy_virtual_slots": {"named_weapon_table": hex(weapon_table), "spread": hex(spread_slot), "inaccuracy": hex(inaccuracy_slot)},
        "limits": "Copies actual invoked client construction/accuracy only; each original native body always executes exactly once; no extra getter calls, attack-selector inference or server firing acknowledgement",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, default=CLIENT)
    parser.add_argument("--header", type=Path, default=ROOT / "include/aim_fire_observer_offsets.h")
    args = parser.parse_args()
    print(json.dumps(verify(args.client, args.header), indent=2))


if __name__ == "__main__":
    main()
