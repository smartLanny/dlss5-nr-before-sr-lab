#!/usr/bin/env python3
"""Relocate the verified eight-object FFX cache; never claim to rebuild it.

Input is an owner-reviewed private attachment containing the unchanged original
build.json and its eight named .o files. The output is a new directory.
"""
import argparse
import copy
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

SDK_COMMIT = 'c6efa6bf7f2027b3ec94f28578bb5965eabb9e55'
EXPECTED = {
    'ffx_opticalflow.o':'b2b08290f86875f77cced719fab6be39d0cff7f57d241956ae7677fe5818665a',
    'ffx_dx12_no_pix.o':'6e44778a345999c2bbed5810e95edc619f1825195a3b658dc45cb82a488755fb',
    'ffx_shader_blobs.o':'26c5617efb161c0b16ac4cebac534b2c99737ce99ae3e758953218cb7b1940bc',
    'ffx_opticalflow_shaderblobs.o':'6783084baa80fe173cdbbf82c6ed61cdc7e7d2b7917036da4419261186c5421e',
    'ffx_assert.o':'f0e020c226839ae704540ac8ec551648aced652b8ccc05f4a9e6af8d224e4f0a',
    'ffx_message.o':'733fc663f11bd21e9030e7485129f6c3fe3f634874892a22757d19a126d364d5',
    'ffx_object_management.o':'d2f167e4afddae98b5f9a05467a1d5aa86a1798d0446eea2e6876978e57be876',
    'ffx_breadcrumbs_list.o':'e4f880b8d1d0531af7336684413da7de508793693a62c015c8de93256720e3f1',
}
ORIGINAL_RECEIPT_SHA = '94d6b882cd1bc4507c2d89c51825c3bc954d8877373f615b2e4aa54500c8b29c'

def sha(path):
    digest=hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024*1024),b''):
            digest.update(block)
    return digest.hexdigest()

def require(ok,message):
    if not ok: raise ValueError(message)

def consumer_preflight(manifest, cache, sdk, compiler):
    """Replicate the exact build-temporal-module.py input checks before compilation."""
    require(subprocess.check_output(['git','-C',str(sdk),'rev-parse','HEAD'],text=True).strip()==SDK_COMMIT,
            'wrong SDK commit')
    require(not subprocess.check_output(['git','-C',str(sdk),'status','--porcelain'],text=True).strip(),
            'SDK checkout is dirty')
    require(manifest['sdk_commit']==SDK_COMMIT and manifest['effect_context_alignment_fixed'] is True,
            'SDK/alignment receipt mismatch')
    require(manifest['compiler_sha256']==sha(compiler) and manifest['license_sha256']==sha(sdk/'LICENSE.txt'),
            'compiler or SDK license changed')
    objects=manifest['sdk_objects']
    require(len(objects)==8 and {Path(x['path']).name for x in objects}==set(EXPECTED),
            'object names/count differ from the frozen eight')
    for item in objects:
        path=Path(item['path'])
        require(path.parent.resolve()==cache.resolve(),'object path escapes relocated cache')
        require(item['sha256']==EXPECTED[path.name] and sha(path)==item['sha256'],
                'object receipt or bytes changed: '+path.name)

def restore(source, output, sdk, compiler):
    source=source.resolve();output=output.absolute();sdk=sdk.resolve();compiler=compiler.resolve()
    require(source.is_dir() and (source/'build.json').is_file(),'source cache missing')
    require(not output.exists(),'refuse to overwrite cache: '+str(output))
    require(output.parent.is_dir(),'output parent missing')
    original=(source/'build.json').read_bytes()
    require(hashlib.sha256(original).hexdigest()==ORIGINAL_RECEIPT_SHA,'original build.json hash mismatch')
    manifest=json.loads(original)
    require(len(manifest['sdk_objects'])==8,'not eight objects')
    names=[Path(item['path']).name for item in manifest['sdk_objects']]
    require(len(set(names))==8 and set(names)==set(EXPECTED),'unexpected object list')
    require(manifest['sdk_commit']==SDK_COMMIT and manifest['effect_context_alignment_fixed'] is True,
            'wrong SDK or unfixed alignment')
    require(subprocess.check_output(['git','-C',str(sdk),'rev-parse','HEAD'],text=True).strip()==SDK_COMMIT,
            'wrong SDK checkout')
    require(not subprocess.check_output(['git','-C',str(sdk),'status','--porcelain'],text=True).strip(),
            'SDK checkout is dirty')
    require(manifest['compiler_sha256']==sha(compiler) and manifest['license_sha256']==sha(sdk/'LICENSE.txt'),
            'new compiler/SDK license differs from original receipt')
    for item in manifest['sdk_objects']:
        name=Path(item['path']).name
        require(item['sha256']==EXPECTED[name] and sha(source/name)==item['sha256'],
                'source object hash mismatch: '+name)
    temp=output.with_name(output.name+'.staging')
    require(not temp.exists(),'refuse to overwrite staging cache: '+str(temp))
    temp.mkdir()
    try:
        for name in EXPECTED:
            shutil.copy2(source/name,temp/name)
        (temp/'build.original.json').write_bytes(original)
        derived=copy.deepcopy(manifest)
        for item in derived['sdk_objects']:
            item['path']=str(output/Path(item['path']).name)
        # The original commands remain as historical provenance, not commands
        # to execute on this machine. No other receipt fields are edited.
        (temp/'build.json').write_text(json.dumps(derived,indent=2)+'\n',encoding='utf-8')
        expected=copy.deepcopy(derived)
        for old,new in zip(manifest['sdk_objects'],expected['sdk_objects']):
            new['path']=old['path']
        require(expected==manifest,'non-path receipt field changed')
        (temp/'relocation.json').write_text(json.dumps({
            'schema':'ffx-cache-relocation-v1','kind':'precompiled-objects-not-rebuilt',
            'original_receipt_sha256':ORIGINAL_RECEIPT_SHA,
            'derived_receipt_sha256':sha(temp/'build.json'),
            'original_commands_preserved':derived['commands']==manifest['commands'],
            'object_sha256':EXPECTED,
            'note':'build.original.json is unchanged; only sdk_objects.path relocated in build.json'
        },indent=2)+'\n',encoding='utf-8')
        # Preflight uses final paths, so verify the temporary copies byte-wise
        # before moving, and then use the actual consumer path after rename.
        require(all(sha(temp/name)==digest for name,digest in EXPECTED.items()),'copy changed object')
        temp.rename(output)
        consumer_preflight(derived,output,sdk,compiler)
        require((output/'build.original.json').read_bytes()==original,'original receipt changed')
        return output
    finally:
        # A failed relocation leaves staging bytes for inspection. Never run
        # recursive cleanup against an unverified/reparsed Windows path.
        pass

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('source-cache','output','sdk','compiler'):
        ap.add_argument('--'+key,required=True,type=Path)
    args=ap.parse_args()
    result=restore(args.source_cache,args.output,args.sdk,args.compiler)
    print(json.dumps({'status':'verified-relocation','cache':str(result),
                      'receipt_sha256':sha(result/'build.json')},ensure_ascii=False))
