"""Run all frozen CS2 file verifiers and save a dated investigation bundle.

Reads installed files only; never starts/opens CS2 or updates offsets. Each
component runs separately so one failure cannot hide the other results.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_GAME = Path(r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game")
MODULES = (
    "bin/win64/cs2.exe", "csgo/bin/win64/client.dll", "csgo/bin/win64/server.dll", "bin/win64/engine2.dll",
    "bin/win64/scenesystem.dll", "bin/win64/worldrenderer.dll",
    "bin/win64/materialsystem2.dll", "bin/win64/rendersystemdx11.dll",
    "bin/win64/animationsystem.dll", "bin/win64/tier0.dll",
    "csgo/bin/win64/host.dll", "bin/win64/resourcesystem.dll",
)


def inventory(path):
    result = {"path": str(path)}
    try:
        raw = path.read_bytes()
        result["sha256"] = hashlib.sha256(raw).hexdigest()
        pe = struct.unpack_from("<I", raw, 0x3C)[0]
        if raw[:2] != b"MZ" or raw[pe:pe + 4] != b"PE\0\0":
            raise ValueError("Expected PE image")
        result.update({
            "machine": hex(struct.unpack_from("<H", raw, pe + 4)[0]),
            "timestamp": hex(struct.unpack_from("<I", raw, pe + 8)[0]),
            "optional_magic": hex(struct.unpack_from("<H", raw, pe + 24)[0]),
            "image_size": hex(struct.unpack_from("<I", raw, pe + 24 + 56)[0]),
        })
    except (OSError, ValueError, struct.error) as exc:
        result["error"] = str(exc)
    return result


def component_commands(game):
    client = str(game / "csgo/bin/win64/client.dll")
    return (
        ("esp", "inspect_cs2.py", ["--client", client]),
        ("grenade_areas", "inspect_grenade_areas.py", ["--game", str(game)]),
        ("grenade_effects", "inspect_grenade_effects.py", ["--client", client]),
        ("movement", "inspect_movement.py", ["--client", client]),
        ("jumpbug", "inspect_jumpbug.py", ["--client", client]),
        ("silent_aim", "inspect_silent_aim.py", ["--client", client]),
        ("anti_aim", "inspect_anti_aim.py", ["--client", client]),
        ("third_person", "inspect_third_person.py", ["--client", client]),
        ("scoped_zoom", "inspect_zoom.py", ["--client", client]),
        ("scope_overlay", "inspect_scope_overlay.py", ["--client", client]),
        ("model_swap", "inspect_model_swap.py", ["--game", str(game)]),
        ("player_chams", "verify_player_chams.py", ["--game", str(game)]),
        ("aim_targets", "inspect_silent_aim_targets.py", ["--client", client]),
        ("aim_hitboxes", "inspect_silent_aim_hitboxes.py", ["--client", client]),
        ("aim_geometry", "inspect_aim_geometry.py", ["--client", client,
            "--animation", str(game / "bin/win64/animationsystem.dll"), "--vpk", str(game / "csgo/pak01_dir.vpk")]),
        ("aim_weapon", "inspect_aim_weapon.py", ["--client", client]),
        ("aim_spread", "inspect_aim_spread.py", ["--game", str(game)]),
        ("aim_shoot_origin", "inspect_aim_shoot_origin.py", ["--client", client]),
        ("aim_damage", "inspect_aim_damage.py", ["--game", str(game)]),
        ("autowall", "inspect_autowall.py", ["--game", str(game)]),
        ("autofire", "inspect_autofire.py", ["--client", client]),
        ("aim_occlusion", "inspect_aim_occlusion.py", ["--client", client]),
        ("aim_console", "inspect_aim_console.py", ["--game", str(game)]),
        ("console_control", "inspect_console_control.py", ["--game", str(game)]),
        ("shot_events", "inspect_shot_events.py", ["--client", client,
            "--server", str(game / "csgo/bin/win64/server.dll"), "--vpk", str(game / "csgo/pak01_dir.vpk"),
            "--core-vpk", str(game / "core/pak01_dir.vpk")]),
        ("sky", "verify_sky_draw.py", ["--game", str(game)]),
        ("materials", "verify_world_materials.py", ["--installation", str(game / "bin/win64")]),
        ("local_lighting", "verify_world_lighting.py", ["--game", str(game)]),
        ("light_switches", "verify_lighting_switches.py", ["--game", str(game)]),
        ("environment_lighting", "verify_environment_lighting.py", ["--game", str(game)]),
    )


def run_component(name, script, arguments, output):
    command = [sys.executable, "-E", str(ROOT / "tools" / script), *arguments]
    try:
        process = subprocess.run(command, cwd=ROOT, capture_output=True,
                                 text=True, encoding="utf-8", errors="replace", timeout=60)
        result = {"returncode": process.returncode,
                  "stdout": process.stdout, "stderr": process.stderr}
    except (OSError, subprocess.TimeoutExpired) as exc:
        result = {"returncode": None, "error": str(exc)}
    result.update({"component": name, "command": command,
                   "passed": result["returncode"] == 0})
    report_file = output / f"{name}.json"
    report_file.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    failure = result.get("error") or result.get("stderr", "").strip() or result.get("stdout", "").strip()
    # The last line gives the useful reason even for a verifier traceback.
    reason = "" if result["passed"] else failure.splitlines()[-1] if failure else "No verifier diagnostics"
    print(f"{'PASS' if result['passed'] else 'FAIL'} {name}" + (f": {reason}" if reason else ""))
    return {"component": name, "passed": result["passed"],
            "returncode": result["returncode"], "report": report_file.name, "reason": reason}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=DEFAULT_GAME,
                        help="CS2 game directory containing bin/ and csgo/")
    parser.add_argument("--output", type=Path,
                        help="Report directory; default is a new UTC-dated folder under logs/compatibility/")
    args = parser.parse_args()
    game = args.game.resolve()
    now = datetime.now(timezone.utc)
    output = (args.output or ROOT / "logs/compatibility" / now.strftime("%Y%m%dT%H%M%S%fZ")).resolve()
    output.mkdir(parents=True, exist_ok=True)
    snapshot = {"created_utc": now.isoformat(), "game": str(game),
                "modules": {name: inventory(game / name) for name in MODULES}}
    try:
        text = (game / "csgo/steam.inf").read_text(encoding="utf-8-sig")
        snapshot["steam_inf"] = dict(line.split("=", 1) for line in text.splitlines() if "=" in line)
    except OSError as exc:
        snapshot["steam_inf_error"] = str(exc)
    # Steam's manifest is informational; custom installations may not have one.
    manifest = game.parents[2] / "appmanifest_730.acf" if len(game.parents) > 2 else None
    if manifest is not None and manifest.is_file():
        text = manifest.read_text(encoding="utf-8-sig")
        match = re.search(r'"buildid"\s+"(\d+)"', text)
        snapshot["steam_build_id"] = match[1] if match else None
    source_files = [*sorted((ROOT / "include").glob("*offsets.h")),
                    *sorted((ROOT / "src").glob("aim_*.cpp")),
                    ROOT / "src/silent_aim.cpp", ROOT / "src/silent_aim_command.cpp", ROOT / "src/silent_aim_target.cpp",
                    ROOT / "src/shot_events.cpp", ROOT / "src/shot_log.cpp", ROOT / "src/shot_logging.cpp",
                    ROOT / "src/model_swap.cpp", ROOT / "src/model_catalog.cpp", ROOT / "src/model_resource_validation.cpp",
                    ROOT / "src/player_chams.cpp", ROOT / "src/player_chams_material.cpp",
                    ROOT / "src/client_frame.cpp",
                    ROOT / "src/scope_overlay.cpp", ROOT / "src/scoped_crosshair.cpp",
                    ROOT / "tools/verify_player_chams_materials.py",
                    ROOT / "tools/aim_autowall_contract.json",
                    ROOT / "tools/jumpbug_contract.json",
                    ROOT / "src/movement_jumpbug.cpp",
                    *(ROOT / "tools" / script for _, script, _ in component_commands(game)),
                    ROOT / "tools/check_compatibility.py"]
    snapshot["source_sha256"] = {
        str(path.relative_to(ROOT)).replace("\\", "/"): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in source_files
    }
    results = [run_component(name, script, arguments, output)
               for name, script, arguments in component_commands(game)]
    snapshot["components"] = results
    snapshot["all_passed"] = all(result["passed"] for result in results)
    snapshot["live_observation"] = False
    (output / "summary.json").write_text(json.dumps(snapshot, indent=2) + "\n", encoding="utf-8")
    print(f"Reports: {output}")
    return 0 if snapshot["all_passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
