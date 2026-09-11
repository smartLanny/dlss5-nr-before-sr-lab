"""Convert the exact validated D13 Chinese acceptance payload to standard DX12 OTA.

Keeps the binary identities frozen. Does not modify games, Manager, or old packages.
The owner deferred independent face protection. This OTA includes Core/chain
and player guides only, without that experiment's extra runtime or model assets.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import struct
import subprocess
import zipfile

from nr_ota_pe import companion_check

ROOT = Path(__file__).resolve().parents[1]
VERSION = 'beta0.5-dline13'
SOURCE = '7abd23569d0a64691caa8cd9adcff3596d6a04a9'
INPUT_SHA = '85a5e0c3e662a6e3bf0b01162c579ca15739f7bedbf8dabb97fd004eb10a673a'
VALIDATION_SHA = 'e3751f49f8fe37574ac40d73b45c7813714b0988d55e04fb654b38f98ce06b76'
CORE = 'DLSS5-AI渲染超分版-beta0.5-dline13-@野生的装机宅-Bilibili.addon64'
CORE_SHA = '3f67cc32536482dd51857df71bc8294f4af3d4e5974c453ca45a94fb9280e296'
CHAIN_SHA = '46041a5ff91ae2fd907e310d132aabc3c4a1ecd48dace511b8672909d5d9c2fb'
IMAGES = {'ui-main.png': '新UI-常用设置.png', 'ui-light.png': '新UI-光影微调.png'}
DOCS = ROOT / 'docs/releases/beta0.5-dline13'


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args], text=True).strip()


def clean_packaging_commit():
    require(not git('status', '--porcelain', '--untracked-files=no'), 'Commit packaging source first.')
    commit = git('rev-parse', 'HEAD')
    git('merge-base', '--is-ancestor', SOURCE, commit)
    for name in git('diff', '--name-only', SOURCE, commit).splitlines():
        require(name.startswith('docs/') or name == 'scripts/package-d13-ota.py',
                f'Runtime/build input changed: {name}')
    return commit


def load_payload(source_package: Path):
    require(sha(source_package.read_bytes()) == INPUT_SHA, 'Wrong frozen input package.')
    with zipfile.ZipFile(source_package) as archive:
        require(archive.testzip() is None, 'Input ZIP CRC failed.')
        names = archive.namelist()
        hashes = json.loads(archive.read('SHA256.json'))
        require(len(names) == len(set(names)) and set(names) == set(hashes) | {'SHA256.json'},
                'Unexpected input files.')
        for name, digest in hashes.items():
            require(sha(archive.read(name)) == digest, f'Input hash mismatch: {name}')
        info = json.loads(archive.read('build-info.json'))
        require(info['source_commit'] == SOURCE and info['validation_sha256'] == VALIDATION_SHA
                and info['language'] == 'zh-CN' and info['version'] == VERSION,
                'Input provenance mismatch.')
        require(info['compile_verified'] is True and info['warp_verified'] is True,
                'Missing source build/WARP checks.')
        selected = {CORE, 'nrchain_nvngx.dll', 'LICENSES.txt'}
        require(selected.issubset(names), 'Missing owned payload or licenses.')
        data = {name: archive.read(name) for name in selected}
    require(sha(data[CORE]) == CORE_SHA and sha(data['nrchain_nvngx.dll']) == CHAIN_SHA,
            'Frozen Core/chain bytes changed.')
    pe = struct.unpack_from('<I', data[CORE], 0x3c)[0]
    require(data[CORE][:2] == b'MZ' and data[CORE][pe:pe+4] == b'PE\0\0'
            and struct.unpack_from('<H', data[CORE], pe+4)[0] == 0x8664
            and VERSION.encode() in data[CORE] and SOURCE.encode() in data[CORE],
            'Core PE identity mismatch.')
    companion_check(data['nrchain_nvngx.dll'])
    return data


def read_images(receipt_path: Path):
    receipt = json.loads(receipt_path.read_text(encoding='utf-8-sig'))
    require(receipt['source_commit'] == SOURCE and receipt['core_sha256'] == CORE_SHA
            and receipt['visual_reviewed'] is True, 'UI must be reviewed from final Core.')
    rows = receipt['images']
    require(len(rows) == len(IMAGES) and {r['file'] for r in rows} == set(IMAGES), 'Unexpected UI image set.')
    images = {}
    for row in rows:
        data = (receipt_path.parent / row['file']).read_bytes()
        require(sha(data) == row['sha256'] and data[:8] == b'\x89PNG\r\n\x1a\n', 'UI PNG hash mismatch.')
        width, height = struct.unpack('>II', data[16:24])
        require(400 <= width <= 800 and 400 <= height <= 2400, 'Unexpected UI capture size.')
        images[IMAGES[row['file']]] = data
    return images


def plain_text(text):
    output = []
    headers = None
    for line in text.splitlines():
        if line.startswith('|'):
            cells = [c.strip() for c in line.strip('|').split('|')]
            if all(re.fullmatch(r':?-+:?', c) for c in cells):
                continue
            if headers is None:
                headers = cells
            else:
                output.append('；'.join(f'{h}：{v}' for h, v in zip(headers, cells)))
        else:
            headers = None
            output.append(re.sub(r'^#{1,3} ', '', line))
    return '\r\n'.join(output).replace('**', '').replace('`', '') + '\r\n'


def verify(path: Path):
    require(path.stat().st_size < 64*1024*1024, 'Unexpected OTA size.')
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        require(sum(i.file_size for i in archive.infolist()) < 64*1024*1024, 'Unexpected expanded OTA size.')
        require(len(names) == len({n.casefold() for n in names}) and archive.testzip() is None, 'Invalid ZIP inventory/CRC.')
        for name in names:
            parts = PurePosixPath(name)
            require(not parts.is_absolute() and '..' not in parts.parts and '\\' not in name and ':' not in name, 'Unsafe path.')
        require('build-info.json' not in names and 'SHA256.json' not in names, 'Ambiguous Manager metadata.')
        manifest = json.loads(archive.read('ota-manifest.json'))
        require(manifest['schema'] == 'nr-branch-ota-v1' and manifest['version'] == VERSION
                and manifest['sourceCommit'] == SOURCE and manifest['sourceDirty'] is False
                and manifest['api'] == 'D3D12-x64' and manifest['includesDx11'] is False,
                'Invalid OTA identity.')
        rows = manifest['files']
        require(len(rows) == len(names)-1 and {r['name'] for r in rows} == set(names)-{'ota-manifest.json'}, 'Invalid file manifest.')
        allowed = {CORE, 'nrchain_nvngx.dll', 'LICENSES.txt', 'Instructions.txt', '设置说明.txt', '更新说明.txt', 'ota-manifest.json'} | set(IMAGES.values())
        require(set(names) == allowed, 'Unexpected payload files.')
        for row in rows:
            data = archive.read(row['name'])
            require(len(data) == row['bytes'] and sha(data) == row['sha256'], f'Corrupt OTA entry: {row["name"]}')
        require(sha(archive.read(CORE)) == CORE_SHA and sha(archive.read('nrchain_nvngx.dll')) == CHAIN_SHA, 'Wrong runtime identity.')
        require(not any(n.lower().endswith(('.ini', '.onnx')) or n.lower().startswith(('nvngx', 'nr_face/')) for n in names), 'Configuration/vendor/independent-face payload refused.')
        require(manifest['face_assets_included'] is False, 'Independent face assets were deferred.')
    return {'file': path.name, 'bytes': path.stat().st_size, 'sha256': sha(path.read_bytes()), 'files': len(names), 'source_commit': SOURCE}


def package(source_package, ui_receipt, output):
    require(not output.exists(), 'Output exists; keep previous deliveries intact.')
    packaging_commit = clean_packaging_commit()
    data = load_payload(source_package)
    images = read_images(ui_receipt)
    documents = {key: (DOCS / filename).read_text(encoding='utf-8') for key, filename in
                 [('install', 'INSTALL.zh-CN.md'), ('settings', 'SETTINGS.zh-CN.md'), ('update', 'UPDATE.zh-CN.md')]}
    data.update(images)
    for key, name in [('install', 'Instructions.txt'), ('settings', '设置说明.txt'), ('update', '更新说明.txt')]:
        data[name] = b'\xef\xbb\xbf' + plain_text(documents[key]).encode('utf-8')
    manifest = {'schema': 'nr-branch-ota-v1', 'version': VERSION, 'display_version': '0.5 D13',
                'language': 'Chinese', 'api': 'D3D12-x64', 'sourceCommit': SOURCE,
                'sourceDirty': False, 'packaging_commit': packaging_commit, 'includesDx11': False,
                'channel': 'd13-test-upgrade', 'upgrade_from': '0.4.7 existing D3D12 installations',
                'core_pe_version': '0.5.1.13', 'validation_sha256': VALIDATION_SHA,
                'input_package_sha256': INPUT_SHA, 'local_checks_passed': 64,
                'maintainer_game_feedback': 'NTE: owner reports no obvious issues; unquantified smoke feedback only',
                'universal_game_compatibility_verified': False, 'whole_frame_performance_verified': False,
                'face_assets_included': False, 'face_experiment_default': False,
                'manager_deploys_face_assets': False,
                'manager_install_scope': 'Core and nrchain',
                'preserve': ['nr_before_sr.ini', 'ReShade.ini', 'ReShadePreset.ini', 'NVIDIA runtime DLLs', 'other game/MOD files'],
                'files': [{'name': name, 'bytes': len(blob), 'sha256': sha(blob)} for name, blob in sorted(data.items())]}
    data['ota-manifest.json'] = (json.dumps(manifest, ensure_ascii=False, indent=2)+'\n').encode('utf-8')
    output.mkdir(parents=True, exist_ok=False)
    target = output / 'DLSS5-0.5-D13-zh-CN-OTA.zip'
    with zipfile.ZipFile(target, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, blob in sorted(data.items()):
            info = zipfile.ZipInfo(name, date_time=(2026, 9, 11, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, blob, compresslevel=9)
    result = verify(target)
    for name in set(IMAGES.values()) | {'Instructions.txt', '设置说明.txt', '更新说明.txt'}:
        leaf = '安装说明.txt' if name == 'Instructions.txt' else name
        (output / leaf).write_bytes(data[name])
    (output / 'SHA256SUMS.txt').write_text(result['sha256']+'  '+target.name+'\n', encoding='utf-8')
    (output / 'package-receipt.json').write_text(json.dumps({**result, 'packaging_commit': packaging_commit}, indent=2), encoding='utf-8')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core-package', type=Path)
    parser.add_argument('--ui-receipt', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--verify', type=Path)
    args = parser.parse_args()
    if args.verify:
        result = verify(args.verify)
    else:
        require(all((args.core_package, args.ui_receipt, args.output)), 'Package, UI receipt and output are required.')
        result = package(args.core_package, args.ui_receipt, args.output)
    print(json.dumps(result, ensure_ascii=False, indent=2))
