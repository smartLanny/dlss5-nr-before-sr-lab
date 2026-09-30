#!/usr/bin/env python3
"""Make a complete Core component from verified language packages, not a standalone runtime installer."""
from __future__ import annotations
import argparse
import os
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess

MANAGER = '211a85570bdf5ef610c4104d345d290459013d52'
FACE = ('LICENSE', 'onnxruntime_providers_shared.dll', 'onnxruntime.dll',
        'ThirdPartyNotices.txt', 'yunet-dynamic.json', 'yunet-dynamic.onnx', 'YUNET-LICENSE')
EXPORTS = {'NRExternalProvider_'+name+'V1' for name in
           ('Query', 'ClaimOwnership', 'ReleaseOwnership', 'SubmitFrame')}

def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def read(path):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result: raise ValueError('Duplicate JSON key')
            result[key] = value
        return result
    return json.loads(path.read_text(encoding='utf-8-sig'), object_pairs_hook=unique)


def file_hashes(rows):
    result = {}
    for row in rows:
        name = row['file']
        if name.casefold() in {key.casefold() for key in result}:
            raise ValueError('Duplicate file inventory entry')
        result[name] = row['sha256']
    return result

def is_reparse_point(path):
    # Path.is_junction() is Python 3.12+. On older interpreters check the
    # Windows reparse-point attribute directly so junctions are still refused.
    method = getattr(path, 'is_junction', None)
    if method is not None:
        return method()
    try:
        attributes = getattr(os.lstat(path), 'st_file_attributes', 0)
    except FileNotFoundError:
        return False
    return bool(attributes & 0x400)  # FILE_ATTRIBUTE_REPARSE_POINT

def child(root, name):
    relative = Path(name)
    if not name or relative.is_absolute() or '\\' in name or ':' in name or '..' in relative.parts:
        raise ValueError('Unsafe relative file name')
    file = root / relative
    for path in (file, *file.parents):
        if path == root.parent:
            break
        if path.is_symlink() or is_reparse_point(path):
            raise ValueError('Reparse point in component input')
    if not file.resolve().is_relative_to(root.resolve()):
        raise ValueError('Component input escapes root')
    return file

def checked(root, name, expected):
    file = child(root, name)
    if not file.is_file() or sha(file) != expected:
        raise ValueError('Identity mismatch: '+name)
    return file

def pe_exports(path):
    data = path.read_bytes()
    def u16(at): return struct.unpack_from('<H', data, at)[0]
    def u32(at): return struct.unpack_from('<I', data, at)[0]
    if data[:2] != b'MZ': raise ValueError('Core is not PE')
    nt = u32(60)
    if data[nt:nt+4] != b'PE\0\0' or u16(nt+4) != 0x8664 or u16(nt+24) != 0x20b:
        raise ValueError('Core must be x64 PE32+')
    sections = nt+24+u16(nt+20)
    def offset(rva):
        for index in range(u16(nt+6)):
            at = sections+40*index
            virtual, size, raw = u32(at+12), u32(at+16), u32(at+20)
            if virtual <= rva < virtual+size:
                result = raw+rva-virtual
                if result < len(data): return result
        raise ValueError('Invalid PE export RVA')
    directory = offset(u32(nt+24+112))
    count = u32(directory+24)
    if count > 4096: raise ValueError('Unbounded export table')
    names = offset(u32(directory+32))
    result = set()
    for index in range(count):
        start = offset(u32(names+4*index)); end = data.find(b'\0', start, start+256)
        if end < 0: raise ValueError('Invalid export name')
        result.add(data[start:end].decode('ascii'))
    return result

def template_values(file):
    # Only a pristine generated first-install template is parsed here. Existing
    # user INI must use Windows profile API semantics, never this text reader.
    values = {}; section = ''; sections = set()
    text = file.read_text(encoding='utf-8-sig')
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith((';', '#')): continue
        if line.startswith('[') and line.endswith(']'):
            section = line[1:-1].casefold()
            if section in sections: raise ValueError('Duplicate template section')
            sections.add(section); continue
        if section == 'nrbeforesr' and '=' in line:
            key, value = (part.strip() for part in line.split('=', 1))
            if key.casefold() in {x.casefold() for x in values}:
                raise ValueError('Duplicate template key')
            values[key] = value
    if not values: raise ValueError('Missing NRBeforeSR template section')
    return values

def manager_probe(root, core, version, commit, node):
    actual = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
    if actual != MANAGER: raise ValueError('Expected exact Beta4 Manager source')
    for name in ('nr-core-identity.js', 'nr-config-contract.js', 'nr-config.js'):
        relative = 'desktop/src/product/'+name
        expected = subprocess.check_output(['git','-C',str(root),'show','HEAD:'+relative])
        actual = (root/relative).read_bytes().replace(b'\r\n', b'\n')
        if actual != expected.replace(b'\r\n', b'\n'): raise ValueError('Dirty Manager contract input')
    script = r'''
const path=require('node:path'),crypto=require('node:crypto'),fs=require('node:fs');
const [root,core,version,sourceCommit]=process.argv.slice(1);
const contract=require(path.join(root,'desktop/src/product/nr-config-contract.js'));
const config=require(path.join(root,'desktop/src/product/nr-config.js'));
const identity=require(path.join(root,'desktop/src/product/nr-core-identity.js'));
(async()=>{const digest=crypto.createHash('sha256').update(fs.readFileSync(core)).digest('hex');
 const found=await identity.createNrCoreIdentity()({version,files:[{path:core,sha256:digest}],fresh:true});
 const descriptor={version,sourceCommit,configContract:'nr-uniform-v1'};
 console.log(JSON.stringify({identifiedContract:found.configContract,identityStatus:found.identityStatus,
   registeredUniformHashes:identity.UNIFORM_CORE_HASHES,fields:contract.FIELD_DEFINITIONS,
   resetPatch:config.defaultPatch(descriptor)}));})();
'''
    return json.loads(subprocess.check_output([node, '-e', script, str(root.resolve()),
                     str(core.resolve()), version, commit], text=True, encoding='utf-8'))

def stage(args):
    output = args.output.resolve()
    if output.exists(): raise ValueError('Output already exists')
    manifest_file = args.build_root/'source/source-manifest.json'
    manifest = read(manifest_file); validation_file = args.build_root/'validation.json'
    validation = read(validation_file); commit = manifest['source_commit']
    if not re.fullmatch('[0-9a-f]{40}', commit) or validation['source_commit'] != commit:
        raise ValueError('Source identity mismatch')
    for flag in ('source_verified','compile_verified','warp_verified','full_face_backend'):
        if validation.get(flag) is not True: raise ValueError('Missing build evidence: '+flag)
    for name, digest in manifest['files'].items(): checked(manifest_file.parent, name, digest)
    ini = checked(manifest_file.parent, 'core/config/nr_before_sr.ini', manifest['files']['core/config/nr_before_sr.ini'])
    values = template_values(ini)
    components = read(args.manager_components)
    if components['sourceCommit'] != MANAGER: raise ValueError('Wrong Manager component inventory')
    face_pins = components['coreCatalog']['versions']['0.5-dline21-unified3']['companions']
    face_file = args.build_root/'source/core/build/face-backend.json'
    face = read(face_file)
    if face.get('enabled') is not True or face.get('runtime_version') != '1.23.2':
        raise ValueError('Expected complete pinned face backend')
    if (face['runtime_sha256'] != face_pins['nr_face/onnxruntime.dll'] or
            face['model_sha256'] != face_pins['nr_face/yunet-dynamic.onnx']):
        raise ValueError('Face build receipt does not match Beta4 asset pins')
    packages = {}
    for directory in args.packages_root.iterdir():
        if not directory.is_dir() or not (directory/'build-info.json').is_file(): continue
        info = read(directory/'build-info.json'); language = info['language']
        if language not in ('zh-CN','en') or language in packages: raise ValueError('Ambiguous language package')
        if info['source_commit'] != commit or info['source_manifest_sha256'] != sha(manifest_file) or info['validation_sha256'] != sha(validation_file):
            raise ValueError('Package does not match the exact validated build')
        if info.get('full_face_backend') is not True: raise ValueError('Full face package required')
        if info.get('face_backend_receipt_sha256') != sha(face_file):
            raise ValueError('Face build receipt identity mismatch')
        hashes = file_hashes(read(directory/'SHA256.json'))
        for name, digest in hashes.items(): checked(directory,name,digest)
        addons = [name for name in hashes if name.endswith('.addon64') and not name.startswith('dlss5-native-carrier-')]
        if len(addons) != 1: raise ValueError('Exactly one language Core required')
        carrier = manifest['integration']['carrier_filename']
        required = [addons[0], 'nrchain_nvngx.dll', carrier] + ['nr_face/'+name for name in FACE]
        binaries = file_hashes(validation['binaries'])
        for name in required:
            if name not in hashes: raise ValueError('Missing complete component file: '+name)
            if not name.startswith('nr_face/') and binaries.get(name) != hashes[name]:
                raise ValueError('Binary absent from exact build receipt: '+name)
            if name.startswith('nr_face/') and face_pins.get(name) != hashes[name]:
                raise ValueError('Face companion differs from fixed Beta4 assets: '+name)
        exports = pe_exports(directory/addons[0])
        probe = manager_probe(args.manager_source, directory/addons[0], info['version'], commit, args.node)
        packages[language] = (directory, info, hashes, required, exports, probe)
    if set(packages) != {'en','zh-CN'}: raise ValueError('Both language components are required')
    output.mkdir(parents=True)
    result = {'schema':'dlss5-core-manager-handoff-v1','sourceCommit':commit,
        'sourceManifestSha256':sha(manifest_file),'buildValidationSha256':sha(validation_file),
        'managerSourceCommit':MANAGER,'componentType':'complete-core-with-manager-runtime-dependencies',
        'standaloneInstaller':False,'ota':False,'stableRelease':False,'languages':{},
        'dependencies':{family:{'files':{name:digest for name,digest in item['files'].items()
                            if name in ('ReShade64.dll','nvngx_dlssnr.dll')}}
                        for family,item in components['coreCatalog']['fixed'].items()},
        'routeStatus':{'native':'candidate; use attached build evidence',
                       'bridge-pre8':'pending-component-and-game-validation',
                       'feeder-dx12':'pending-component-and-game-validation',
                       'feeder-vulkan':'pending-component-and-game-validation',
                       'feeder-legacy-host':'pending-component-and-game-validation'}}
    for language, (directory, info, hashes, required, exports, probe) in packages.items():
        target = output/language; target.mkdir()
        core_name = 'nr-before-sr.'+language+'.addon64'
        for name in required + [name for name in hashes if '/' not in name and 'LICENSE' in name.upper()]:
            destination = target/(core_name if name == required[0] else name)
            destination.parent.mkdir(parents=True, exist_ok=True);shutil.copyfile(directory/name,destination)
            if sha(destination) != hashes[name]: raise ValueError('Copy verification failed')
        shutil.copyfile(ini,target/'nr_before_sr.ini')
        mismatches = {key:{'template':value,'managerReset':probe['resetPatch'][key]}
                      for key,value in values.items() if key in probe['resetPatch'] and str(probe['resetPatch'][key]) != value
                      and not (re.fullmatch(r'-?\d+(\.\d+)?', value) and float(value) == probe['resetPatch'][key])}
        contract = {'family':'nr-uniform-v1','managerEditingReady':False,
            'managerIdentity':{key:probe[key] for key in ('identifiedContract','identityStatus')},
            'templateValues':values,'managerResetDifferences':mismatches,
            'colourPolicyEditableByBeta4':'ColourLabMode' in probe['fields'],
            'parser':'Core Windows GetPrivateProfile* API; do not choose the first text section',
            'update':'preserve existing INI; template is first-install only',
            'rules':{
                'section':'NRBeforeSR',
                'layers':'primary uses unprefixed model keys; Layer2 through Layer5 use independent keys; active layers form a contiguous prefix',
                'modelRanges':{'Intensity':[0,2],'LocalToneStrength':[0,2],
                    'LocalStructureStrength':[0,2],'Style':[0,2],
                    'SkinStructureStrength':'0..2 with AutoMask; -1..2 without; model receives -1 when AutoMask is off'},
                'scale':'one WorkMode/CustomWorkScale for the whole chain; legacy auxiliary scales are archived by UniformChainVersion migration',
                'colour':'If AIColorRange is present it owns admission and amount (0..1, new default 0.5), using colour-priority admission. Otherwise preserve legacy ColourLabMode/ColorStrength behaviour until an explicit range edit. Manager must not add the new key during an unrelated edit.',
                'colourMemory':'Legacy policy strengths remain for rollback; do not silently map Conservative to zero (verified SDR could still adopt colour). Core range edits supersede old HDR permission.',
                'energy':'missing pause/guess keys are OFF; preserve explicit saved values',
                'provider':'external provider has separate configuration; no new Core INI keys in this adapter'},
            'authoritativeSourceFiles':{name:digest for name,digest in manifest['files'].items()
                if name in ('core/src/nr_before_sr.cpp','core/src/nr_layer_config.h',
                            'core/src/nr_colour_lab_policy.h','core/src/nr_user_controls.h',
                            'core/config/nr_before_sr.ini')},
            'authority':'exact generated Core source, not Manager historical defaults'}
        if 'temporal' in manifest:
            contract['rules']['residualStabilizer']={
                'ResidualStabilizer':'0 OFF (missing/default), 1 prepared native motion, 2 AMD OF; preserve saved choice',
                'ResidualStabilizerScope':'0 whole chain (missing/default), 1 later layers; one layer retains selection but is unavailable',
                'ResidualLegacyWeight':'0 time-weighted basic (missing/default), 1 fixed 0.5 developer comparison',
                'route':'native DX12 post-Feature1 SDR only; unavailable on external ProviderV1/Bridge/Feeder routes in this package',
                'providerABI':'ProviderV1 unchanged; no reserved fields consumed; never auto-enable from bridge recipe'}
        (target/'CONFIG-CONTRACT.json').write_text(json.dumps(contract,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        public_info = {key:info[key] for key in ('version','display_version','source_commit','packaging_commit',
            'language','full_face_backend','face_backend_receipt_sha256','core_pe_version','core_file_version',
            'carrier_upstream_file_version','bridge_upstream_commit','source_manifest_sha256',
            'validation_sha256','local_windows_verified','local_warp_verified','game_runtime_verified','stable_release') if key in info}
        (target/'build-info.json').write_text(json.dumps(public_info,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        (target/'READ-FIRST.txt').write_text('Complete Core component, not an OTA and not a standalone installer.\n'
            'Use exactly one language Core. The INI is a first-install template; preserve an existing INI.\n'
            'Keep all nr_face files beside Core. Manager supplies its pinned ReShade and RTX-family NVIDIA runtime.\n'
            'D3D12 native does not install the DX11 carrier. DX11 carrier/Bridge and Feeder routes require their own validation.\n'
            'Beta4 must import this Core identity and updated configuration contract before full settings editing.\n'
            'No game deployment or game validation is implied.\n',encoding='utf-8')
        files = [{'file':str(file.relative_to(target)).replace('\\','/'),'bytes':file.stat().st_size,'sha256':sha(file)}
                 for file in sorted(target.rglob('*')) if file.is_file()]
        implementation = ['NGX-D3D12-Feature1']
        if EXPORTS <= exports: implementation.append('NRExternalProviderV1')
        result['languages'][language] = {'directory':language,'files':files,
            'implementedInterfaces':implementation,'managerAutoEnableExternalRoutes':False,
            'configContract':contract,'catalogFragment':{'sourceCommit':commit,'version':info['version'],
                'configContract':'unknown','ota':False,'coreUpdateOnly':False,'validation':'candidate',
                'inputInterfaces':['NGX-D3D12-Feature1'],'capabilities':['same-frame-output'],
                'files':{row['file']:row['sha256'] for row in files if row['file'] in (core_name,'nrchain_nvngx.dll',required[2],'nr_before_sr.ini')},
                'companions':{row['file']:row['sha256'] for row in files if row['file'].startswith('nr_face/')}}}
    (output/'manager-core-handoff.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    # A fresh read verifies the handback inventory, including the complete INI.
    for package in read(output/'manager-core-handoff.json')['languages'].values():
        for row in package['files']:
            file = checked(output/package['directory'],row['file'],row['sha256'])
            if file.stat().st_size != row['bytes']: raise ValueError('Final file size mismatch')
    return result

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('build-root','packages-root','manager-components','manager-source','output'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--node',default='node')
    options = parser.parse_args()
    stage(options)
    print('Verified complete bilingual Core component; Manager runtime dependencies and pending route/config gates are explicit.')
