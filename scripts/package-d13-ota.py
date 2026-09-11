"""Convert the exact validated D13 Chinese acceptance payload to standard DX12 OTA.

Keeps the binary identities frozen. Does not modify games, Manager, or old packages.
The existing Manager standard reader installs Core/chain only; face assets are an
explicit manual supplement, as explained in the player instructions and manifest.
"""
from __future__ import annotations
import argparse
import base64
import hashlib
import html
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
FACE_PINS = {
    'nr_face/onnxruntime.dll': 'dec964ab1ee36cc9b0ae247d13b376627992fc57dec0454354017ab8fd84f1ea',
    'nr_face/onnxruntime_providers_shared.dll': 'a2b3a50956aa75a9879c8472bc7df4f7a8072bcd2db19a1b7d988e7688f293ef',
    'nr_face/LICENSE': 'c250d6278f0b47a6439fb7592b08b58a55eb9f535aa49a1db63211c3f982b674',
    'nr_face/ThirdPartyNotices.txt': 'e00f828e0a33de591a355ae6606d2625f5758da7d2c844db7821c9dd3e3647b6',
    'nr_face/yunet-dynamic.onnx': 'b1b3061b1ae7a43bcc1497284ade754f701db774ef3a19481edcaf27fbc425b1',
    'nr_face/YUNET-LICENSE': 'c83b8120c50ccbd4c4f96edf53141bdd566ebb8f8e9227e415326aa1b1aba958',
}
FACE = set(FACE_PINS)
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
        selected = {CORE, 'nrchain_nvngx.dll', 'LICENSES.txt'} | FACE
        require(selected.issubset(names), 'Missing owned payload or face licenses.')
        data = {name: archive.read(name) for name in selected}
    require(sha(data[CORE]) == CORE_SHA and sha(data['nrchain_nvngx.dll']) == CHAIN_SHA,
            'Frozen Core/chain bytes changed.')
    for name, digest in FACE_PINS.items():
        require(sha(data[name]) == digest, f'Frozen face asset changed: {name}')
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


def inline(text):
    value = html.escape(text)
    value = re.sub(r'`([^`]+)`', r'<code>\1</code>', value)
    return re.sub(r'\*\*([^*]+)\*\*', r'<strong>\1</strong>', value)


def render_markdown(text):
    # Deliberately bounded to the headings, lists, tables, paragraphs in these docs.
    lines = text.splitlines()
    blocks = []
    index = 0
    while index < len(lines):
        line = lines[index].strip()
        if not line:
            index += 1
            continue
        if line.startswith('|'):
            table = []
            while index < len(lines) and lines[index].strip().startswith('|'):
                row = [v.strip() for v in lines[index].strip().strip('|').split('|')]
                if not all(re.fullmatch(r':?-+:?', v) for v in row):
                    table.append(row)
                index += 1
            blocks.append('<div class="table-wrap"><table><thead><tr>' + ''.join('<th>'+inline(v)+'</th>' for v in table[0]) +
                          '</tr></thead><tbody>' + ''.join('<tr>'+''.join('<td>'+inline(v)+'</td>' for v in row)+'</tr>' for row in table[1:]) + '</tbody></table></div>')
            continue
        heading = re.match(r'^(#{1,3}) (.+)$', line)
        if heading:
            level = min(3, len(heading[1]) + 1)
            blocks.append(f'<h{level}>{inline(heading[2])}</h{level}>')
            index += 1
            continue
        item = re.match(r'^(?:- |\d+\. )(.+)$', line)
        if item:
            ordered = not line.startswith('- ')
            tag = 'ol' if ordered else 'ul'
            values = []
            while index < len(lines):
                candidate = lines[index].strip()
                found = re.match(r'^\d+\. (.+)$' if ordered else r'^- (.+)$', candidate)
                if not found:
                    break
                values.append('<li>'+inline(found[1])+'</li>')
                index += 1
            blocks.append(f'<{tag}>'+''.join(values)+f'</{tag}>')
            continue
        paragraph = [line]
        index += 1
        while index < len(lines) and lines[index].strip():
            paragraph.append(lines[index].strip())
            index += 1
        blocks.append('<p>'+inline(' '.join(paragraph))+'</p>')
    return '\n'.join(blocks)


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


def guide(documents, images):
    figures = ''.join('<figure><img alt="'+html.escape(name)+'" src="data:image/png;base64,'+
                      base64.b64encode(data).decode()+'"><figcaption>'+html.escape(name[:-4])+
                      ' · 实际 D13 菜单展示，非游戏效果对比</figcaption></figure>' for name, data in images.items())
    body = ''.join(f'<section id="{key}">{render_markdown(documents[key])}</section>' for key in ['install', 'settings', 'update'])
    return ('<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">'
            '<title>DLSS5 0.5 D13 · 安装与设置</title><style>'
            '*{box-sizing:border-box}body{margin:0;background:#f3f5f8;color:#202b3b;font:16px/1.8 "Microsoft YaHei","Segoe UI",sans-serif}'
            'header{background:#172337;color:white;padding:40px max(24px,calc((100vw - 1240px)/2))}h1{font-size:34px;line-height:1.3;margin:8px 0}'
            'header p{color:#ced9ec}nav{display:flex;gap:22px;flex-wrap:wrap}nav a{color:#c2d9ff}a{color:#285aa5}'
            'main{max-width:1300px;margin:auto;padding:28px;display:grid;grid-template-columns:minmax(0,1fr) 370px;gap:28px;align-items:start}'
            'section{background:white;border:1px solid #e0e5ed;border-radius:14px;padding:26px 32px;margin-bottom:24px;scroll-margin-top:24px}'
            'h2{font-size:26px;line-height:1.45;margin:0 0 24px}h3{font-size:20px;margin:30px 0 12px;color:#22456d}p{margin:12px 0}'
            'li{margin:10px 0}ol,ul{padding-left:24px}code{font:0.91em Consolas,monospace;overflow-wrap:anywhere;background:#edf2f8;padding:2px 5px;border-radius:4px}'
            'table{border-collapse:collapse;width:100%;font-size:14px}th,td{text-align:left;vertical-align:top;border:1px solid #d9e0e9;padding:11px}th{background:#eaf0f8}'
            '.table-wrap{overflow-x:auto}figure{margin:0 0 24px;background:#172337;border-radius:12px;overflow:hidden}figure img{display:block;width:100%;height:auto}'
            'figcaption{padding:12px 16px;color:#d3ddee;font-size:13px}footer{max-width:1240px;padding:0 24px 32px;margin:auto;color:#5e6f86;font-size:14px}'
            '@media(max-width:960px){main{grid-template-columns:1fr;padding:16px}aside{max-width:600px;margin:auto}section{padding:22px}h1{font-size:28px}}'
            '@media print{body{background:white}header{background:white;color:black;padding:0}header p,nav{color:black}main{display:block;padding:0}section{border:0;padding:0;break-inside:auto}aside{display:none}}'
            '</style><header><p>装机宅 DLSS5 · 中文升级指南</p><h1>从 0.4.7 升级到 0.5 D13</h1>'
            '<p>先保留旧设置，再按需尝试二次 AI。面向已有插件环境的 64 位 DX12 游戏。</p>'
            '<nav><a href="#install">安装与回退</a><a href="#settings">常用设置</a><a href="#update">这版更新了什么</a></nav></header>'
            '<main><article>'+body+'</article><aside>'+figures+'</aside></main>'
            '<footer>游戏内版本：beta0.5-dline13 · 不同游戏的实际效果与开销请在同一场景比较。此文件可离线打开、转发或打印。</footer></html>').encode('utf-8')


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
        allowed = {CORE, 'nrchain_nvngx.dll', 'LICENSES.txt', 'Instructions.txt', '设置说明.txt', '更新说明.txt', '安装与设置.html', 'ota-manifest.json'} | FACE | set(IMAGES.values())
        require(set(names) == allowed, 'Unexpected payload files.')
        for row in rows:
            data = archive.read(row['name'])
            require(len(data) == row['bytes'] and sha(data) == row['sha256'], f'Corrupt OTA entry: {row["name"]}')
        require(sha(archive.read(CORE)) == CORE_SHA and sha(archive.read('nrchain_nvngx.dll')) == CHAIN_SHA, 'Wrong runtime identity.')
        for name, digest in FACE_PINS.items():
            require(sha(archive.read(name)) == digest, f'Wrong face asset identity: {name}')
        require(not any(n.lower().endswith('.ini') or n.lower().startswith('nvngx') for n in names), 'Configuration/vendor payload refused.')
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
    data['安装与设置.html'] = guide(documents, images)
    manifest = {'schema': 'nr-branch-ota-v1', 'version': VERSION, 'display_version': '0.5 D13',
                'language': 'Chinese', 'api': 'D3D12-x64', 'sourceCommit': SOURCE,
                'sourceDirty': False, 'packaging_commit': packaging_commit, 'includesDx11': False,
                'channel': 'd13-test-upgrade', 'upgrade_from': '0.4.7 existing D3D12 installations',
                'core_pe_version': '0.5.1.13', 'validation_sha256': VALIDATION_SHA,
                'input_package_sha256': INPUT_SHA, 'local_checks_passed': 64,
                'maintainer_game_feedback': 'NTE: owner reports no obvious issues; unquantified smoke feedback only',
                'universal_game_compatibility_verified': False, 'whole_frame_performance_verified': False,
                'face_assets_included': True, 'face_experiment_default': False,
                'manager_deploys_face_assets': False,
                'manager_install_scope': 'Core and nrchain; copy nr_face beside the active nr_before_sr.ini only for the independent face experiment',
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
    for name in set(IMAGES.values()) | {'Instructions.txt', '设置说明.txt', '更新说明.txt', '安装与设置.html'}:
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
