"""Link the exact compiled optional temporal module into whole-Core test hosts."""
import hashlib
import json
from pathlib import Path

def objects_for(source):
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
