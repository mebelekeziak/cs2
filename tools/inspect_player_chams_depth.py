"""Re-extract installed player shader depth-state expressions read-only.

Uses the already-provenanced Source2Viewer-CLI 20.0 DATA reader; no client
launch, process access, installed-file writes, or shader recompilation.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
from inspect_surface_tint import extract, objects

ROOT = Path(__file__).resolve().parents[1]
GAME = Path(r'C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game')
CLI_HASH = 'a4bb4f9945db985efb2cad90a90d06867de5ba7e0447a2a4a69a07dace781028'
SHADERS = {
    'csgo_unlitgeneric': ('11ef90cb61aa0e6cf47811a828937fc610eaec7adece2a264aa60efe6529fb3c', 1),
    'csgo_character': ('18394bbc4562dd2bf00e1acb73b95a9ea334c2f96fc8686288f7126f884a8e40', 3),
}


def inspect(game, research):
    cli = ROOT/'logs/research/vrf20/Source2Viewer-CLI.exe'
    if hashlib.sha256(cli.read_bytes()).hexdigest() != CLI_HASH:
        raise SystemExit('Research CLI identity changed')
    research.mkdir(parents=True, exist_ok=True)
    result = {}
    for shader, (expected, index) in SHADERS.items():
        asset = f'shaders/vfx/{shader}_pc_50_features.vcs'
        data = extract(game/'csgo/shaders_pc_dir.vpk', asset)
        if hashlib.sha256(data).hexdigest() != expected:
            raise SystemExit(f'Changed installed depth shader: {asset}')
        path = research/(shader+'_features.vcs')
        path.write_bytes(data)
        metadata = subprocess.run([str(cli), '-i', str(path), '-b', 'DATA'],
                                  capture_output=True, text=True, check=True).stdout
        (research/(shader+'_metadata.txt')).write_text(metadata)
        features_section = metadata[metadata.index('m_staticComboArray'):metadata.index('m_staticComboRuleArray')]
        features = []
        for block in objects(features_section):
            name = re.search(r'm_szName = "([^"]+)"', block)
            if name:
                features.append(name[1])
        if features[index] != 'F_DISABLE_Z_BUFFERING' or features[index+1] != 'F_DISABLE_Z_PREPASS':
            raise SystemExit('Changed native depth feature index')
        section = metadata[metadata.index('m_variableDescriptionArray'):metadata.index('m_textureChannelProcessorArray')]
        inverse = bytes([0x1a, index])+bytes.fromhex('04 07 00 0F 00 07 00 00 00 00 02 14 00 07 00 00 80 3F 00')
        states = {}
        for block in objects(section):
            name = re.search(r'm_szName = "([^"]+)"', block)
            if not name or name[1] not in ('DepthEnable', 'DepthWriteEnable'):
                continue
            source = int(re.search(r'm_sourceType = (\d+)', block)[1])
            register = int(re.search(r'm_registerType = (\d+)', block)[1])
            identifier = int(re.search(r'm_nTypeSpecificBits = (\d+)', block)[1])
            if register != 5 or identifier != (0xffff0010 if name[1]=='DepthEnable' else 0xffff0011):
                raise SystemExit('Changed native depth render-state binding')
            expression = re.search(r'm_pCompiledExpression = #\[([^]]+)\]', block)
            if source == 6 and expression:
                encoded = bytes.fromhex(expression[1])
                if encoded != inverse:
                    raise SystemExit('Changed native depth conditional bytecode')
                states.setdefault(name[1], []).append({'source': 'feature expression',
                    'expression': 'F_DISABLE_Z_BUFFERING ? 0 : 1', 'bytecode': encoded.hex(),
                    'enabled_feature_result': 0})
            elif source == 5 and name[1]=='DepthWriteEnable' and 'm_intDefault = [ 0, 0, 0, 0 ]' in block:
                states.setdefault(name[1], []).append({'source': 'render-state default', 'value': 0})
            else:
                raise SystemExit('Unverified native depth render-state source')
        if set(states) != {'DepthEnable', 'DepthWriteEnable'}:
            raise SystemExit('Missing native depth state')
        result[shader] = {'sha256': expected, 'feature_index': index, 'states': states,
                          'depth_test_when_disabled': False, 'depth_write_when_disabled': False}
    return {'verified': True, 'shaders': result,
            'expression_provenance': 'ValveResourceFormat 20.0 Serialization/VfxEval.cs and Tests/VfxEvalTests.cs',
            'live_observation': False}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, default=GAME)
    parser.add_argument('--research', type=Path, default=ROOT/'logs/research/chams-depth')
    args = parser.parse_args()
    print(json.dumps(inspect(args.game, args.research), indent=2))
