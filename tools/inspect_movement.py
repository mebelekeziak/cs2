"""Verify movement evidence against the installed PE, without opening a process.

No game files are changed. A different build requires a new investigation, not
automatic substitution of internet offsets. Standard-library Python only.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

EXPECTED_HASH = "5a46a09b521bbd607012146364dafae2abfa50364246638391990d4c7dfb9d49"
CHECKS = {
    "command_inline_presence": (0xB66117, "488b7c24304885ff74064883c710eb03488bfa834f1001488b4730"),
    "command_viewangles_pointer": (0xCECFB1, "488b4a40488d7a40"),
    "base_viewangles_pointer": (0xCECFCA, "488b4840"),
    "command_native_buttons": (0xCED0A7, "488b4b60"),
    "base_forward_presence": (0xB662A1, "83481040f30f117058"),
    "base_left_presence": (0xB66338, "81481080000000f30f11705c"),
    "modern_usable_landing_stamps": (0x8C3610, "f30f10571c488d4d678b5718e80fa7d200f30f105724488d4de78b5720"),
    "modern_jump_observer": (0x8C34F0, "405553565741564157488d6c24d14881ec880000008b9ad40000004c8bf2"),
    "modern_jump_call": (0x8DF74B, "488d8bc8060000e8993dfeff"),
    "modern_jump_services": (0x8C3522, "488b4908"),
    "modern_jump_fraction": (0x8C3516, "f30f10b2dc000000"),
    "modern_jump_live_ground": (0x8DF766, "488b4338488bd7488bcbf680f403000001"),
    "modern_jump_velocity_xy": (0x8E8223, "8b47288946388b472c89463c"),
    "modern_jump_velocity_z": (0x8E84CA, "f30f590b450fb6cf498bcef30f110b"),
    "command_finalizer": (0xCECF90, "48895c2408488974241048897c24184c8974242055488d6c24e04881ec200100"),
    "command_finalizer_call": (0xB66968, "498b07498bcf488b542430ff5030"),
    "effective_maxspeed": (0x8CDEF0, "48895c24084889742420574883ec30488bf9"),
    "float_cvar_getter": (0x160B10, "488b4108f30f104058c3"),
    "surface_speed_row": (0x9E3560, "48634110488b0d95abb60148c1e00548034128c3"),
    "command_ingestion": (0xA77390, "48895424105355565741544155415641574883ec68"),
    "subtick_allocator": (0x4F74A0, "48895c2410574883ec20"),
    "subtick_append": (0x1233010, "48895c2408574883ec20"),
    "controller_simulation_tick": (0x9442A0, "8b81b8060000c3"),
    "input_constructor_object": (0xEE574, "488d0dc5d04802e860d8a600"),
    "input_constructor_vtable": (0xEE58C, "488d058df5ba0148890d4edc48024889059fd04802"),
    "command_native_sequence": (0xB66113, "44897008"),
    "command_base_presence": (0xB6612A, "834f1001488b4730"),
    "base_client_tick": (0xB66151, "83481020897054"),
    "subtick_pool_reuse": (0xB665F7, "488b4828488d70184885c97415486356083b117d0d488b4cd1088d4201894608eb16"),
    "subtick_allocation_abi": (0xB66619, "488b0ee87f0e99ff488bd0488bcee8e4c96c00488bc8"),
    "subtick_generated_payloads": (0xB6662F, "8b5110f30f1047f083ca04895110f30f114124488b47f84885c0741883ca0189511083ca02488941180fb607895110884120eb2df30f10070f2ec77a02740b83ca08895110f30f114128f30f1047040f2ec77a02740b83ca10895110f30f11412c4584ff"),
    "subtick_camera_deltas": (0xB666C8, "83491020f30f114130f30f104708f30f11442448f30f10470cf30f5c44244c0f2ec77a02741483491040f30f114134"),
    "base_protobuf_constructor": (0x4F8260, "4533c9488bc24883c8024584c0480f44c248894108488d059c53630148890133c048894110488d057c0a1002488951184c8949204c8949284c8949384c8949404c8949484c8949504c8949584c8949604c8949684c8949704c89497844898980000000c78184000000ffffff0048894130488bc1c3"),
    "subtick_constructor": (0x4F8580, "4533c9488bc24883c8024584c0480f44c248894108488d053c4f630148890133c048894110884120488bc14c8949184c8949244c89492c44894934c3"),
    "subtick_append_container": (0x1233010, "48895c2408574883ec20488bd9488bfa488b49104885c974078b013b430c7513ba01000000488bcbe843010000488b4b108b01ffc0890148634b08488b431048897cc808488bc7ff4308488b5c24304883c4205fc3"),
    "command_axis_ingestion": (0xA7762A, "498b4d40488bc24885c9480f45c18b40588986a0010000488bc2498b4d404885c9480f45c18b405c8986a4010000"),
    "historical_button_state": (0xA77409, "488b46584c8d7638"),
    "surface_data_operand": (0x8C45D5, "488b8f78020000"),
    "stamina_scaling": (0x8C46C8, "f30f108f940600000f2ff9774d0f2fcf7648f30f5e0db6192001"),
    "effective_speed_services": (0x8CDFCC, "488b87301300000f57c9f30f11442448f30f1090ac010000"),
    "effective_speed_caller": (0x8A2131, "488b4948c745670000803fe8afbd0200488d5567f30f11456f"),
    "ingestion_forward_thunk": (0x8E9510, "e97bde1800"),
    "button_state_setter": (0x4C10A0, "48895c2408488bd941ba0000000041f6c001458bca4c8bda4c0f45ca49f7d3498bc3482341084c0bc8488b43104c89490841f6c002418bca480f45ca4923c3480bc8488b431841f6c00448894b104c0f45d24923c34c0bd04c895318488b5c2408c3cccccccccccccccccccccccccccc48895c2408574883ec40488b01488bf9"),
    "jump_actual_state": (0x8C3588, "f30f105714488d4dcf8b5710"),
}

# These are instruction sites, not copies of the source offsets: resolving
# their operands independently catches wrapper-start/value-pointer confusion.
CVAR_REGISTRATIONS = {
    "LegacyJump": (0xB2377, 0xB2370, "sv_legacy_jump"),
    "BhopWindow": (0xB1B12, 0xB1B06, "sv_bhop_time_window"),
    "AutoBunnyhop": (0xB1A77, 0xB1A70, "sv_autobunnyhopping"),
    "JumpPenalty": (0xB202B, 0xB2022, "sv_jump_spam_penalty_time"),
    "JumpPrecision": (0xB1FE7, 0xB1FE0, "sv_jump_precision_enable"),
    "AirCap": (0xB19C8, 0xB1962, "sv_air_max_wishspeed"),
    "AirAccelerate": (0xD8568, 0xD8502, "sv_airaccelerate"),
}


def rip_target(image, rva, opcode):
    if bytes(image[rva:rva + 3]) != bytes.fromhex(opcode):
        raise SystemExit(f"Cvar operand evidence differs at 0x{rva:X}")
    return rva + 7 + struct.unpack_from("<i", image, rva + 3)[0]


def verify_cvar_offsets(image, header_path):
    header = header_path.read_text(encoding="utf-8-sig")
    header = re.sub(r"//[^\n]*|/\*.*?\*/", "", header, flags=re.S)
    values = {}
    for name in (*CVAR_REGISTRATIONS, "CvarValuePointer", "CvarValue"):
        matches = re.findall(
            rf"\bptrdiff_t\s+{name}\s*=\s*(0[xX][0-9a-fA-F]+|[0-9]+)\s*;", header)
        if len(matches) != 1:
            raise SystemExit(f"Expected one literal source offset {name} in {header_path}")
        values[name] = int(matches[0], 16 if matches[0].lower().startswith("0x") else 10)

    getter = bytes(image[0x160B10:0x160B1A])
    if getter[:3] != bytes.fromhex("488b41") or getter[4:8] != bytes.fromhex("f30f1040") or getter[9] != 0xC3:
        raise SystemExit("Cvar getter addressing evidence differs")
    for name, observed in (("CvarValuePointer", getter[3]), ("CvarValue", getter[8])):
        if values[name] != observed:
            raise SystemExit(f"Source offset mismatch: {name}=0x{values[name]:X}; getter requires 0x{observed:X}")

    verified = {}
    for name, (wrapper_site, name_site, cvar_name) in CVAR_REGISTRATIONS.items():
        wrapper = rip_target(image, wrapper_site, "488d0d")  # LEA RCX
        name_rva = rip_target(image, name_site, "488d15")  # LEA RDX
        expected_name = cvar_name.encode("ascii") + b"\0"
        if bytes(image[name_rva:name_rva + len(expected_name)]) != expected_name:
            raise SystemExit(f"Registration name differs: {name} at 0x{name_rva:X}")
        if values[name] != wrapper:
            raise SystemExit(
                f"Source offset mismatch: {name}=0x{values[name]:X}; "
                f"{cvar_name} registration requires wrapper start 0x{wrapper:X}")
        verified[name] = {"name": cvar_name, "wrapper_start": hex(wrapper)}

    # Both selectors load the pointer slot directly, unlike Cvar(), which
    # receives the wrapper start and then adds the getter's pointer offset.
    for load_site, compare_site in ((0x8DF72D, 0x8DF737), (0x8C0336, 0x8C0340)):
        slot = rip_target(image, load_site, "488b05")  # MOV RAX
        if slot != values["LegacyJump"] + values["CvarValuePointer"]:
            raise SystemExit(f"Legacy selector pointer slot differs at 0x{load_site:X}")
        if bytes(image[compare_site:compare_site + 4]) != bytes((0x80, 0x78, values["CvarValue"], 0)):
            raise SystemExit(f"Legacy selector value addressing differs at 0x{compare_site:X}")
    return verified


def verify_jump_trace_offsets(image, header_path):
    header = re.sub(r"//[^\n]*|/\*.*?\*/", "", header_path.read_text(encoding="utf-8-sig"), flags=re.S)

    def literal(name, scope=None):
        source = header if scope is None else re.search(rf"namespace\s+{scope}\s*\{{([^}}]*)\}}", header, re.S).group(1)
        match = re.search(rf"\bptrdiff_t\s+{name}\s*=\s*(0[xX][0-9a-fA-F]+|[0-9]+)\s*;", source)
        if not match:
            raise SystemExit(f"Missing literal jump trace source offset: {scope or ''}::{name}")
        return int(match.group(1), 0)

    # CALL rel32 + LEA RCX[services+disp32] independently identify the hook and receiver.
    target = 0x8DF752 + 5 + struct.unpack_from("<i", image, 0x8DF753)[0]
    state_offset = struct.unpack_from("<i", image, 0x8DF74E)[0]
    observed = {
        "ModernJump": (literal("ModernJump"), target),
        "services::ModernJumpState": (literal("ModernJumpState", "services"), state_offset),
        "services::Pawn": (literal("Pawn", "services"), image[0x8DF769]),
        "move_data::Tick": (literal("Tick", "move_data"), struct.unpack_from("<i", image, 0x8C3507)[0]),
        "move_data::Fraction": (literal("Fraction", "move_data"), struct.unpack_from("<i", image, 0x8C351A)[0]),
        "move_data::Velocity": (literal("Velocity", "move_data"), image[0x8E8228]),
    }
    for name, (source, decoded) in observed.items():
        if source != decoded:
            raise SystemExit(f"Jump trace source mismatch: {name}=0x{source:X}; machine operand requires 0x{decoded:X}")
    prefix = re.search(r"ModernJumpBytes\[\]\s*\{([^}]*)\}", header, re.S)
    if not prefix:
        raise SystemExit("Missing ModernJumpBytes source prefix")
    expected = bytes(int(token.strip(), 0) for token in prefix.group(1).split(",") if token.strip())
    if bytes(image[target:target + len(expected)]) != expected:
        raise SystemExit("ModernJumpBytes source prefix differs from installed function")
    return {name: hex(decoded) for name, (_, decoded) in observed.items()}


# Frozen source layouts are independently checked against the local schema
# templates and code windows below. Never populate these from the source header.
LAYOUTS = {
    "pawn": {"Flags": 0x3F4, "Velocity": 0x3F8, "ActualMoveType": 0x526,
             "WaterLevel": 0x528, "MovementServices": 0x1330},
    "controller": {"TickBase": 0x6B8},
    "services": {"Pawn": 0x38, "ModernJumpState": 0x6C8, "MaxSpeed": 0x1AC,
        "Friction": 0x26C, "SurfaceData": 0x278, "Stamina": 0x694,
        "Buttons": 0x58, "CommandForward": 0x1A0, "CommandLeft": 0x1A4,
        "ModernActual": 0x6D8, "ModernUsable": 0x6E0, "ModernLanded": 0x6E8},
    "move_data": {"Velocity": 0x38, "Tick": 0xD4, "Fraction": 0xDC},
    "command": {"Sequence": 0x8, "Presence": 0x20, "Base": 0x40, "Buttons": 0x60},
    "base": {"Presence": 0x10, "SubtickSize": 0x20, "SubtickCapacity": 0x24,
        "SubtickRep": 0x28, "Angles": 0x40, "Tick": 0x54,
        "Forward": 0x58, "Left": 0x5C, "ForwardPresent": 0x40, "LeftPresent": 0x80},
}
SCHEMA_CLASSES = {
    "C_BaseEntity": (0x21F4BE8, 0x21F32C0, 82, {
        "m_fFlags": ("pawn", "Flags"), "m_vecAbsVelocity": ("pawn", "Velocity"),
        "m_nActualMoveType": ("pawn", "ActualMoveType"), "m_flWaterLevel": ("pawn", "WaterLevel")}),
    "C_BasePlayerPawn": (0x21F4588, 0x21F41F0, 28, {
        "m_pMovementServices": ("pawn", "MovementServices")}),
    "CBasePlayerController": (0x2223A38, 0x2223800, 17, {
        "m_nTickBase": ("controller", "TickBase")}),
    "CPlayer_MovementServices": (0x222AD38, 0x222AAE0, 18, {
        "m_flMaxspeed": ("services", "MaxSpeed")}),
    "CPlayer_MovementServices_Humanoid": (0x222B2D8, 0x222B200, 6, {
        "m_flSurfaceFriction": ("services", "Friction")}),
    "CCSPlayer_MovementServices": (0x2221E88, 0x22217E0, 49, {
        "m_flStamina": ("services", "Stamina")}),
}


def source_literal(header, name, scope=None):
    source = re.sub(r"namespace\s+(?:pawn|controller|services|move_data|command|base)\s*\{[^}]*\}", "", header, flags=re.S) if scope is None else re.search(
        rf"namespace\s+{scope}\s*\{{([^}}]*)\}}", header, re.S).group(1)
    matches = re.findall(rf"\b{name}\s*=\s*(0[xX][0-9a-fA-F]+|[0-9]+)\s*[,;]", source)
    if len(matches) != 1:
        raise SystemExit(f"Expected one literal source value {scope or ''}::{name}")
    return int(matches[0], 0)


def verify_source_offsets(image, image_base, header_path):
    header = re.sub(r"//[^\n]*|/\*.*?\*/", "", header_path.read_text(encoding="utf-8-sig"), flags=re.S)
    def compare(name, observed, scope=None):
        value = source_literal(header, name, scope)
        if value != observed:
            raise SystemExit(f"Source mismatch: {scope or ''}::{name}=0x{value:X}; independent evidence requires 0x{observed:X}")
        return observed
    def rel32(site, opcode=0xE8):
        if image[site] != opcode:
            raise SystemExit(f"Relative call/thunk evidence differs at 0x{site:X}")
        return site + 5 + struct.unpack_from("<i", image, site + 1)[0]
    decoded = {
        "Input": rip_target(image, 0xEE574, "488d0d"),
        "InputVtable": rip_target(image, 0xEE58C, "488d05"),
        "ModernJump": rel32(0x8DF752),
        "EffectiveMaxSpeed": rel32(0x8A213C),
        "Ingest": rel32(0x8E9510, 0xE9),
        "AllocateSubtick": rel32(0xB6661C),
        "AddAllocatedSubtick": rel32(0xB66627),
        "SurfaceManager": rip_target(image, 0x9E3564, "488b0d"),
    }
    decoded["Finalize"] = struct.unpack_from("<Q", image, decoded["InputVtable"] + 6 * 8)[0] - image_base
    for name, site, opcode in (("TickIntervalConstant", 0xA55F99, "f30f5915"),
                               ("StaminaScaleConstant", 0x8C46DA, "f30f5e0d")):
        if image[site:site + 4] != bytes.fromhex(opcode):
            raise SystemExit(f"Engine constant addressing differs at 0x{site:X}")
        decoded[name] = site + 8 + struct.unpack_from("<i", image, site + 4)[0]
    # Constructor writes its vtable to the embedded object itself.
    if rip_target(image, 0xEE59A, "488905") != decoded["Input"]:
        raise SystemExit("CCSGOInput constructor no longer initializes the embedded object")
    # RTTI locator/type strings bind this vtable to the installed input class.
    if struct.unpack_from("<Q", image, decoded["InputVtable"] - 8)[0] - image_base != 0x1F236C8:
        raise SystemExit("Input vtable RTTI differs")
    if struct.unpack_from("<I", image, 0x1F236C8 + 12)[0] != 0x23283D0 or bytes(image[0x23283E0:0x23283F1]) != b".?AVCCSGOInput@@\0":
        raise SystemExit("Input RTTI class differs")
    for name, value in decoded.items():
        compare(name, value)
    prefixes = {
        "FinalizeBytes": ("Finalize", "command_finalizer"),
        "ModernJumpBytes": ("ModernJump", "modern_jump_observer"),
        "MaxSpeedBytes": ("EffectiveMaxSpeed", "effective_maxspeed"),
        "IngestBytes": ("Ingest", "command_ingestion"),
        "AllocateBytes": ("AllocateSubtick", "subtick_allocator"),
        "AddAllocatedBytes": ("AddAllocatedSubtick", "subtick_append"),
    }
    for array, (target, frozen_check) in prefixes.items():
        match = re.search(rf"\b{array}\[\]\s*\{{([^}}]*)\}}", header, re.S)
        if match is None:
            raise SystemExit(f"Missing source fingerprint {array}")
        prefix = bytes(int(token.strip(), 0) for token in match.group(1).split(",") if token.strip())
        site = decoded[target]
        if prefix != bytes.fromhex(CHECKS[frozen_check][1]) or image[site:site + len(prefix)] != prefix:
            raise SystemExit(f"Source fingerprint {array} differs from frozen installed code")
    for scope, fields in LAYOUTS.items():
        for name, expected in fields.items():
            compare(name, expected, scope)
    # Actual machine operands support historical axes, which intentionally differ
    # from the schema's m_flForwardMove/m_flLeftMove at 0x1C0/0x1C4.
    operands = {
        ("services", "SurfaceData"): (0x8C45D8, "<I"),
        ("services", "CommandForward"): (0xA7763D, "<I"),
        ("services", "CommandLeft"): (0xA77654, "<I"),
        ("services", "Buttons"): (0xA7740C, "<B"),
        ("command", "Base"): (0xA7762D, "<B"),
        ("command", "Sequence"): (0xB66116, "<B"),
        ("command", "Buttons"): (0xCED0AA, "<B"),
        ("base", "Presence"): (0x4F8284, "<B"),
        ("base", "SubtickRep"): (0xB665FA, "<B"),
        ("base", "Angles"): (0xCECFCD, "<B"),
        ("base", "ForwardPresent"): (0xB662A4, "<B"),
        ("base", "LeftPresent"): (0xB6633B, "<I"),
        ("base", "Forward"): (0xA7763A, "<B"),
        ("base", "Left"): (0xA77651, "<B"),
        ("base", "Tick"): (0xB66157, "<B"),
    }
    for (scope, name), (site, fmt) in operands.items():
        compare(name, struct.unpack_from(fmt, image, site)[0], scope)
    # Nested objects: command contains its protobuf at +0x10; the repeated
    # container starts at base+0x18. Decode both operands rather than mistaking
    # a container-relative offset for an absolute command/base field.
    compare("Presence", image[0xB66124] + image[0xB6612C], "command")
    repeated = image[0xB665FE]
    compare("SubtickSize", repeated + image[0xB66607], "base")
    compare("SubtickCapacity", repeated + image[0x123302D], "base")
    modern_state = struct.unpack_from("<I", image, 0x8DF74E)[0]
    for name, operand in (("ModernActual", 0x8C3593), ("ModernUsable", 0x8C361B),
                          ("ModernLanded", 0x8C362C)):
        compare(name, modern_state + image[operand], "services")
    for name, expected in {"OnGround": 1, "WaterJump": 4, "Frozen": 32, "Walk": 2,
        "Jump": 2, "Forward": 8, "Back": 0x10, "Left": 0x200, "Right": 0x400}.items():
        compare(name, expected)
    return {"globals_and_helpers": {name: hex(value) for name, value in decoded.items()},
            "layout_constants": sum(map(len, LAYOUTS.values())), "runtime_fingerprints": len(prefixes)}


def verify_schema_offsets(image, image_base, header_path):
    header = re.sub(r"//[^\n]*|/\*.*?\*/", "", header_path.read_text(encoding="utf-8-sig"), flags=re.S)
    def pointer(site):
        return struct.unpack_from("<Q", image, site)[0] - image_base
    def string(site):
        if not 0 <= site < len(image):
            raise SystemExit("Invalid static schema name pointer")
        end = image.find(0, site)
        return bytes(image[site:end]).decode("ascii")
    result = {}
    for name, (template, fields, count, wanted) in SCHEMA_CLASSES.items():
        if string(pointer(template)) != name or pointer(template + 0x28) != fields or struct.unpack_from("<H", image, template + 0x1C)[0] != count:
            raise SystemExit(f"Static schema template differs: {name}")
        observed = {string(pointer(fields + index * 32)): struct.unpack_from("<I", image, fields + index * 32 + 16)[0]
                    for index in range(count)}
        result[name] = {}
        for field, (scope, source_name) in wanted.items():
            value = observed.get(field)
            if value != source_literal(header, source_name, scope):
                raise SystemExit(f"Source/schema mismatch: {name}::{field}")
            result[name][field] = hex(value)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, default=Path(
        r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll"))
    parser.add_argument("--header", type=Path,
        default=Path(__file__).resolve().parents[1] / "include" / "movement_offsets.h",
        help="Source offsets to compare with independent installed registration evidence")
    args = parser.parse_args()
    raw = args.client.read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    if digest != EXPECTED_HASH:
        raise SystemExit("Client changed: re-investigate movement before using these layouts.")
    nt = struct.unpack_from("<I", raw, 0x3C)[0]
    machine, sections = struct.unpack_from("<HH", raw, nt + 4)
    timestamp = struct.unpack_from("<I", raw, nt + 8)[0]
    optional_size = struct.unpack_from("<H", raw, nt + 20)[0]
    optional = nt + 24
    image_base = struct.unpack_from("<Q", raw, optional + 24)[0]
    image_size = struct.unpack_from("<I", raw, optional + 56)[0]
    image = bytearray(image_size)
    for index in range(sections):
        section = optional + optional_size + index * 40
        rva, size, start = struct.unpack_from("<III", raw, section + 12)
        image[rva:rva + size] = raw[start:start + size]
    assert (machine, timestamp, image_size) == (0x8664, 0x6AD7B41C, 0x299D000)
    for name, (rva, expected) in CHECKS.items():
        want = bytes.fromhex(expected)
        if bytes(image[rva:rva + len(want)]) != want:
            raise SystemExit(f"Evidence differs: {name} at 0x{rva:X}")
    verified_cvars = verify_cvar_offsets(image, args.header)
    verified_jump_trace = verify_jump_trace_offsets(image, args.header)
    verified_source = verify_source_offsets(image, image_base, args.header)
    verified_schema = verify_schema_offsets(image, image_base, args.header)
    assert struct.unpack_from("<Q", image, 0x1C9DB20 + 6 * 8)[0] - image_base == 0xCECF90
    interval = struct.unpack_from("<f", image, 0x1AC6064)[0]
    assert interval > 0
    assert struct.unpack_from("<f", image, 0x1AC6098)[0] == 100.0
    # Embedded installed descriptors contain the actual field names. They do
    # not establish C++ offsets by themselves; reflection/machine code does.
    descriptor = bytes(image[0x1B2CEB0:0x1B2CEB0 + 1400])
    for field in (b"CSubtickMoveStep", b"analog_forward_delta", b"analog_left_delta", b"CBaseUserCmdPB"):
        assert field in descriptor
    print(json.dumps({"path": str(args.client), "sha256": digest,
        "machine": hex(machine), "timestamp": hex(timestamp), "image_size": hex(image_size),
        "input": "embedded object RVA 0x257B640", "finalize_slot": 6,
        "tick_quantum_from_engine_seconds": interval, "verified_code": list(CHECKS),
        "source_offsets": str(args.header), "verified_cvars": verified_cvars,
        "verified_jump_trace": verified_jump_trace,
        "verified_source": verified_source, "verified_schema": verified_schema,
        "live_observation": False}, indent=2))

if __name__ == "__main__":
    main()
