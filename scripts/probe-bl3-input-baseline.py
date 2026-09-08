"""Read-only BL3 install fingerprint; does not inject or claim temporal inputs.

Writes only the explicitly named new report outside the game directory. Do not
publish the private report without reviewing paths. No game process is launched.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--game-root', type=Path, required=True)
    ap.add_argument('--steam-manifest', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    root = args.game_root.resolve(strict=True)
    output = args.output.resolve()
    if output == root or root in output.parents:
        ap.error('Report must be outside the game directory')
    exe = root / 'OakGame/Binaries/Win64/Borderlands3.exe'
    manifest = args.steam_manifest.read_text(encoding='utf-8')
    def field(name):
        match = re.search(r'"' + re.escape(name) + r'"\s*"([^"]*)"', manifest)
        if not match:
            raise ValueError('Missing Steam manifest field: ' + name)
        return match.group(1)
    if field('appid') != '397540' or field('installdir') != root.name:
        ap.error('Manifest does not identify this Borderlands 3 installation')
    with exe.open('rb') as stream:
        executable_hash = hashlib.file_digest(stream, 'sha256').hexdigest()
    candidates = sorted(str(p.relative_to(root)) for p in root.rglob('*')
                        if p.is_file() and p.suffix.lower() == '.dll' and
                        re.search(r'nvngx|dlss|fsr|fidelityfx|xess', p.name, re.I))
    report = {
        'kind': 'readonly-install-baseline', 'steam_appid': field('appid'),
        'steam_buildid': field('buildid'), 'game_root_private': str(root),
        'executable_sha256': executable_hash, 'executable_bytes': exe.stat().st_size,
        'temporal_dll_name_candidates': candidates,
        'dll_scan_scope': 'names in install tree; absence does not exclude static/dynamic integration',
        'luma_source_commit': '91babfe12abe5f7de0c4890f0efc321d8277887f',
        'runtime_api_observed': None, 'taa_shader_hash_observed': None,
        'temporal_inputs_observed': False, 'shader_registers_observed': None,
        'game_started': False, 'game_files_modified': False,
        'supported_game_claim': False,
    }
    with output.open('x', encoding='utf-8', newline='\n') as stream:
        json.dump(report, stream, indent=2)
        stream.write('\n')
    print('Install fingerprint recorded; API/TAA/color/depth/MV/jitter/reset remain unobserved.')


if __name__ == '__main__':
    main()
