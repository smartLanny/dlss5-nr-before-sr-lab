#!/usr/bin/env python3
"""Package a frozen canonical Core as optional Manager OTA; never deploy or publish."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import zipfile
from nr_ota_pe import companion_check


def digest(data):
    return hashlib.sha256(data).hexdigest()


def load(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def require(ok, message):
    if not ok:
        raise ValueError(message)


def leaf(name):
    require(isinstance(name, str) and name not in ('', '.', '..') and not any(c in name for c in '/\\:'), 'Unsafe payload name')
    return name


def pe_check(data):
    """Bounded x64 DLL/import inspection; no DLL execution."""
    def u(fmt, at):
        require(0 <= at <= len(data)-struct.calcsize(fmt), 'PE range out of bounds')
        return struct.unpack_from(fmt, data, at)[0]
    require(data[:2] == b'MZ', 'Missing PE header')
    pe = u('<I', 60)
    require(data[pe:pe+4] == b'PE\0\0' and u('<H', pe+4) == 0x8664 and u('<H', pe+22) & 0x2000, 'Expected x64 DLL')
    opt = pe+24
    require(u('<H', opt) == 0x20b and u('<H', pe+20) >= 128, 'Expected PE32+')
    count = u('<H', pe+6)
    require(1 <= count <= 96, 'Invalid section count')
    sections = []
    for i in range(count):
        p = opt+u('<H', pe+20)+40*i
        sections.append((u('<I', p+12), u('<I', p+16), u('<I', p+20)))
    def rva(v, size=1):
        for va, n, raw in sections:
            if va <= v and v-va+size <= n and raw+v-va+size <= len(data):
                return raw+v-va
        raise ValueError('Unbacked PE RVA')
    imports = []
    address, size = u('<I', opt+120), u('<I', opt+124)
    require(address and size >= 20 and size < 1024*1024, 'Missing import directory')
    for i in range(min(size//20, 4096)):
        p = rva(address+20*i, 20)
        if not any(data[p:p+20]):
            break
        name = u('<I', p+12)
        chars = []
        for j in range(256):
            v = data[rva(name+j)]
            if not v:
                break
            chars.append(v)
        else:
            raise ValueError('Unbounded DLL name')
        label = bytes(chars).decode('ascii').lower()
        require('/' not in label and '\\' not in label and ':' not in label, 'Unsafe imported DLL')
        require(not re.match(r'(libstdc\+\+|libgcc|libwinpthread|nvngx|nrchain)', label), 'Unexpected dynamic runtime import: '+label)
        imports.append(label)
    else:
        raise ValueError('Unterminated import directory')
    require(imports, 'No imports')
    return imports


def pe_version(path, version):
    env = dict(os.environ, CORE_OTA_PE=str(path.resolve()))
    code = "$v=[Diagnostics.FileVersionInfo]::GetVersionInfo($env:CORE_OTA_PE); @($v.FileMajorPart,$v.FileMinorPart,$v.FileBuildPart,$v.FilePrivatePart,$v.ProductMajorPart,$v.ProductMinorPart,$v.ProductBuildPart,$v.ProductPrivatePart) | ConvertTo-Json -Compress"
    values = json.loads(subprocess.check_output(['powershell.exe', '-NoProfile', '-Command', code], env=env, text=True))
    numbers = [int(v) for v in re.fullmatch(r'(\d+)\.(\d+)\.(\d+)-beta\.(\d+)', version).groups()]
    require(values == numbers+numbers, 'PE File/Product numeric version mismatch')
    return '.'.join(map(str, numbers))


def make_payload(core_name, core, bridge, version, source, language, changelog, licenses, numeric_version):
    leaf(core_name)
    require(core_name.endswith('.addon64'), 'Core suffix')
    instructions = '''Core-only optional OTA / 仅核心可选升级\n退出游戏，备份旧 Core 与 nrchain 到插件搜索目录外；仅保留一个 Core。\n保留所有 INI、快捷键、画质和实验设置；本包不写 INI，不自动启用新功能。\n本 OTA 不含 NVIDIA NR/SR/RR/FG 运行库或面部模型/资源；新安装需另行配置所需资源，已有安装保持不动。\n从 D21 升级：旧辅助比例迁移到主工作网格；保留层数意图，迁移后先核对负载。\nExit the game; back up Core and nrchain outside plugin search paths. Keep one Core.\nPreserve saved settings. No INI, runtime or face resources are installed by this OTA.\nNR/SR/RR/FG runtimes and face resources are not included. New installations require separately configured resources; existing resources are left unchanged.\nD21 auxiliary layers migrate to the primary work grid; verify workload after migration.\nManager import/selection is optional. No default pin or stable/latest publication is changed.\nSoftware validation does not prove game acceptance, denoising benefit or a whole-frame 3% cost.\n'''
    payload = {core_name: core, 'nrchain_nvngx.dll': bridge, 'Instructions.txt': instructions.encode('utf-8'), 'CHANGELOG.md': changelog, 'LICENSES.txt': licenses}
    require(all(len(v) <= 64*1024*1024 for v in payload.values()) and sum(map(len, payload.values())) < 128*1024*1024, 'Manager size limit')
    manifest = dict(schema='nr-branch-ota-v1', version=version, display_version=version.replace('-beta.', ' Beta '), language=language, api='D3D12-x64', sourceCommit=source, sourceDirty=False, source_checkout_clean=True, generated_source=True, sourceDirty_scope='frozen integration checkout', includesDx11=False, core_pe_version=numeric_version, channel='optional-core-candidate', manager_install_scope='Core and nrchain only', face_assets_included=False, nr_runtime_included=False, stable_release=False, game_runtime_verified=False, preserve=['nr_before_sr.ini','ReShade.ini','ReShadePreset.ini','nvngx_dlssnr.dll','nvngx_dlss.dll','nvngx_dlssd.dll','nvngx_dlssg.dll'], files=[dict(name=n, bytes=len(b), sha256=digest(b)) for n,b in sorted(payload.items())])
    payload['ota-manifest.json'] = (json.dumps(manifest, ensure_ascii=False, indent=2)+'\n').encode('utf-8')
    return payload


def write_zip(path, payload):
    require(len({n.lower() for n in payload}) == len(payload), 'Duplicate entry')
    require(sum(n.lower().endswith('.addon64') for n in payload) == 1, 'Exactly one Core required')
    for n in payload:
        leaf(n)
    require('nrchain_nvngx.dll' in payload, 'Missing bridge')
    with zipfile.ZipFile(path, 'x', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name, blob in sorted(payload.items()):
            z.writestr(name, blob)
    with zipfile.ZipFile(path) as z:
        require(z.testzip() is None and set(z.namelist()) == set(payload), 'ZIP verification failed')
        require(all(z.read(n) == b for n,b in payload.items()), 'ZIP content changed')


def postbuild_checks(base, source):
    generated_hash = digest((base/'source/core/src/nr_before_sr.cpp').read_bytes())
    receipts = {}
    for name in ('unified8-checks', 'reconstruction-checks'):
        path = base/name/'validation.json'
        gate = load(path)
        require(gate.get('passed') is True, 'Postbuild gate failed: '+name)
        require(gate.get('source_sha256') == generated_hash, 'Postbuild source hash mismatch: '+name)
        require('source_commit' not in gate or gate['source_commit'] == source, 'Postbuild source commit mismatch: '+name)
        receipts[name] = digest(path.read_bytes())
    return receipts


def package(args):
    receipt = load(args.candidate)
    version, source = receipt['candidate'], receipt['source_commit']
    require(re.fullmatch(r'\d+\.\d+\.\d+-beta\.\d+', version), 'Expected final Beta identity')
    require(re.fullmatch(r'[0-9a-f]{40}', source), 'Invalid source commit')
    require(subprocess.check_output(['git','-C',str(args.checkout),'rev-parse','HEAD'], text=True).strip() == source, 'Checkout/source mismatch')
    require(not subprocess.check_output(['git','-C',str(args.checkout),'status','--porcelain'], text=True).strip(), 'Dirty checkout')
    canonical_path = Path(receipt['canonical_validation'])
    canonical = load(canonical_path)
    require(canonical['source_commit'] == source and canonical['candidate'] == version, 'Canonical identity mismatch')
    for key in ('source_verified','compile_verified','warp_verified'):
        require(canonical.get(key) is True, 'Canonical gate missing: '+key)
    for key, value in receipt.items():
        if key.endswith('_gate'):
            require(load(value).get('status') == 'passed', 'Failed candidate gate: '+key)
    postbuild_receipts = postbuild_checks(args.candidate.parent, source)
    manifest_path = args.candidate.parent/'source/source-manifest.json'
    manifest = load(manifest_path)
    require(manifest['source_commit'] == source, 'Source manifest identity mismatch')
    final_identity = next((manifest[k]['version'] for k in ('private_residual_sr','colour_edge','colour_lab','integration') if isinstance(manifest.get(k),dict) and 'version' in manifest[k]), None)
    require(final_identity == version, 'Source overlay version mismatch')
    for name, expected in manifest['files'].items():
        path = (manifest_path.parent/name).resolve()
        require(path.is_relative_to(manifest_path.parent.resolve()) and digest(path.read_bytes()) == expected, 'Exported source changed: '+name)
    rows = [r for r in receipt['binaries'] if r['language'] == args.language]
    require(len(rows) == 1, 'Duplicate/missing language')
    row = rows[0]
    require(row['source_commit'] == source and leaf(row['file']) == args.core.name, 'Core filename/source mismatch')
    core, bridge = args.core.read_bytes(), args.bridge.read_bytes()
    require(digest(core) == row['sha256'], 'Core hash mismatch')
    require(sum(r['sha256'] == digest(core) and r['file'].endswith('.addon64') for r in canonical['binaries']) == 1, 'Core absent/duplicated in canonical')
    chains = [r for r in canonical['binaries'] if r['file'] == 'nrchain_nvngx.dll']
    require(args.bridge.name == 'nrchain_nvngx.dll' and len(chains) == 1 and chains[0]['sha256'] == digest(bridge), 'Bridge identity mismatch')
    require(version.encode() in core and source.encode() in core, 'Embedded Core identity mismatch')
    imports = pe_check(core)
    companion_check(bridge)
    numeric_version = pe_version(args.core, version)
    changelog = args.changelog.read_bytes()
    require(changelog.strip(), 'Empty changelog')
    license_paths = [manifest_path.parent/'core/third_party/minhook/LICENSE.txt', manifest_path.parent/'core/build/NVIDIA-NGX-LICENSE.txt']
    license_paths.extend(args.notice)
    licenses = b'\n\n'.join(p.read_bytes() for p in license_paths)
    payload = make_payload(row['file'], core, bridge, version, source, args.language, changelog, licenses, numeric_version)
    require(not args.output.exists(), 'Preserve previous output; choose a fresh directory')
    args.output.mkdir(parents=True)
    archive = args.output/f'DLSS5-{version}-{args.language}-Core-OTA.zip'
    write_zip(archive, payload)
    result = dict(schema='core-only-ota-receipt-v1', version=version, source_commit=source, language=args.language, file=str(archive.resolve()), sha256=digest(archive.read_bytes()), core_sha256=digest(core), nrchain_sha256=digest(bridge), candidate_sha256=digest(args.candidate.read_bytes()), canonical_sha256=digest(canonical_path.read_bytes()), postbuild_receipt_sha256=postbuild_receipts, source_manifest_sha256=digest(manifest_path.read_bytes()), changelog_sha256=digest(changelog), license_sha256=digest(licenses), core_imports=imports, core_pe_version=numeric_version, manager_ui_tested=False, deployed=False, published=False)
    (args.output/'package-receipt.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    (args.output/'SHA256SUMS.txt').write_text(result['sha256']+'  '+archive.name+'\n',encoding='utf-8')
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for option in ('candidate','checkout','core','bridge','changelog','output'):
        p.add_argument('--'+option, type=Path, required=True)
    p.add_argument('--notice', type=Path, action='append', default=[], help='Additional redistribution notices, appended verbatim (repeatable)')
    p.add_argument('--language', choices=('zh-CN','en'), required=True)
    print(json.dumps(package(p.parse_args()),ensure_ascii=False,indent=2))


if __name__ == '__main__':
    main()
