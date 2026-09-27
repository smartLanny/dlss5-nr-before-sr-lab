"""Link verified optional temporal/private-SR objects into whole-Core test hosts."""
import hashlib
import json
from pathlib import Path
import re

def _temporal_objects(source):
    source=Path(source).resolve()
    folder=source.parent/'temporal'
    if not folder.is_dir():return []
    def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
    receipt=source.parent.parent/'build/obj/temporal/module.json'
    data=json.loads(receipt.read_text(encoding='utf-8'))
    if not data['passed']:raise ValueError('Temporal compilation was not successful')
    # Historical dynamic-compiler receipts have neither field. New baked builds
    # must retain both exact artifact receipts; no mixed/header-only acceptance.
    if 'temporal_artifacts_sha256' in data or 'temporal_header_sha256' in data:
        artifacts=receipt.parent/'bytecode'
        if sha(artifacts/'temporal-artifacts.json')!=data.get('temporal_artifacts_sha256') or \
           sha(artifacts/'nr_temporal_precompiled.hpp')!=data.get('temporal_header_sha256'):
            raise ValueError('Temporal precompiled artifact provenance mismatch')
    for name,digest in data['source_sha256'].items():
        if sha(folder/name)!=digest:raise ValueError('Temporal source mismatch: '+name)
    result=[]
    for name,digest in data['object_sha256'].items():
        p=Path(name)
        if sha(p)!=digest:raise ValueError('Temporal object mismatch: '+name)
        result.append(p)
    return result


def _private_objects(source):
    # Historical temporal provenance-only fixtures have no main TU.
    if not source.is_file():return []
    if not re.search(r'^\s*#\s*define\s+NR_HAS_PRIVATE_RESIDUAL_SR\s+1\b',
                     source.read_text(encoding='utf-8-sig'), re.MULTILINE):
        return []
    root=source.parent.parent
    receipt=root/'build/obj/private-sr/module.json'
    data=json.loads(receipt.read_text(encoding='utf-8'))
    def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
    def checked(path,digest,parent=None):
        p=Path(path).resolve(strict=True)
        if parent is not None and not p.is_relative_to(parent.resolve()):
            raise ValueError('Private SR artifact outside expected directory: '+str(p))
        if sha(p)!=digest:raise ValueError('Private SR artifact mismatch: '+str(p))
        return p
    if data.get('schema')!='private-sr-module-v2' or data.get('passed') is not True:
        raise ValueError('Private SR compilation has no successful v2 receipt')
    if Path(data['root']).resolve()!=root.resolve():
        raise ValueError('Private SR receipt belongs to a different generated Core')
    if not data.get('source_sha256'):
        raise ValueError('Private SR source provenance is empty')
    for name,digest in data['source_sha256'].items():
        checked(root/name,digest,root)
    generated=data.get('generated_source_sha256',{})
    if len(generated)!=1 or {Path(p).name for p in generated}!={'artifacts.cpp'}:
        raise ValueError('Private SR bytecode object source receipt is missing')
    for name,digest in generated.items():checked(name,digest,receipt.parent)
    checked(data['header'],data['header_sha256'],receipt.parent/'bytecode')
    checked(data['shader_manifest'],data['shader_manifest_sha256'],receipt.parent/'bytecode')
    expected_shaders={'rr_guides','rr_accumulate','rr_compose','deferred_encode',
                      'deferred_compose','normalize','post_extract','post_compose'}
    if set(data['shaders'])!=expected_shaders:
        raise ValueError('Private SR shader closure is incomplete')
    for shader in data['shaders'].values():
        checked(shader['path'],shader['sha256'],receipt.parent/'bytecode')
    names=data.get('objects',[])
    expected={'nr_edit_dx12.o','nr_post_residual_dx12.o','nr_post_private_sr_dx12.o',
              'nr_private_sr_guides.o','artifacts.o'}
    if len(names)!=5 or {Path(p).name for p in names}!=expected or set(names)!=set(data['object_sha256']):
        raise ValueError('Private SR object closure is incomplete or duplicated')
    return [checked(name,data['object_sha256'][name],receipt.parent) for name in names]


def objects_for(source):
    source=Path(source).resolve()
    # The absence of temporal must not hide a requested private SR consumer.
    return _temporal_objects(source)+_private_objects(source)
