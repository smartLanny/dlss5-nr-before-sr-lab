"""Validate the optional same-call O sidecar; legacy stage/NRF files are unchanged."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

HEADER=struct.Struct('<8s4I11Q16I2fQ48s48s8I')
BPP={2:16,10:8,24:4,26:4,28:4,29:4,87:4,91:4}

def read_capture(directory):
    directory=Path(directory)
    sidecar=directory/'native-output.json'
    if not sidecar.exists():
        return {'present':False,'reason':'legacy-capture-without-O','same_frame_verified':False}
    meta=json.loads(sidecar.read_text(encoding='utf-8'))
    if meta.get('schema')!='nr-native-output-sidecar-v1':raise ValueError('unknown O schema')
    if type(meta.get('present')) is not bool:raise ValueError('missing explicit O presence')
    if not meta['present']:
        if not meta.get('reason') or meta.get('reason')=='none' or meta.get('file'):
            raise ValueError('invalid missing O declaration')
        return {**meta,'same_frame_verified':False}
    if meta.get('file')!='native-output.nro' or meta.get('reason')!='none':raise ValueError('invalid O file declaration')
    path=directory/'native-output.nro'
    size=path.stat().st_size
    if not HEADER.size<=size<=HEADER.size+(128<<20):raise ValueError('O file exceeds bounded payload')
    raw=path.read_bytes();v=HEADER.unpack_from(raw)
    if v[:5]!=(b'NROCAP1\0',1,HEADER.size,3,1):raise ValueError('incomplete O binary')
    ids=dict(zip(('request','instance','frame','feature_generation','recording','reset','queue','fence',
                  'source_stream','source_epoch','source_sequence'),v[5:16]))
    config,evaluation,w,h,x,y,rw,rh,storage,view,bpp,row,state,flags,result,domain=v[16:32]
    payload=v[34]
    if any(not value for key,value in ids.items() if key!='reset') or result!=1 or domain!=0:
        raise ValueError('missing source, successful call or final completion identity')
    if not (0<w<=rw<=16384 and 0<h<=rh<=16384 and x<=rw-w and y<=rh-h and
            bpp==BPP.get(storage) and view==storage and row==w*bpp and payload==row*h and size==HEADER.size+payload):
        raise ValueError('invalid native storage/extent/payload')
    expected={**{k:ids[k] for k in ('request','instance','frame','feature_generation','recording','reset','queue','fence')},
              'source':[ids['source_stream'],ids['source_epoch'],ids['source_sequence']],
              'config':config,'evaluation':evaluation,'extent':[w,h],'origin':[x,y],
              'storage_format':storage,'view_format':view,'state_before_copy':state,
              'native_create_flags':flags,'native_result':result,'bytes':payload}
    if any(meta.get(k)!=value for k,value in expected.items()):raise ValueError('binary/sidecar identity mismatch')
    parent={}
    for line in (directory/'metadata.txt').read_text(encoding='utf-8').splitlines():
        key,separator,value=line.partition('=')
        if separator:parent[key]=value
    if parent.get('format')!='NRStageCapture-v2' or parent.get('complete')!='1' or parent.get('mode')!='2':
        raise ValueError('parent pre-SR stages incomplete')
    for key,other in (('request','request'),('instance','instance'),('frame','frame'),
                       ('featureGeneration','feature_generation'),('configGeneration','config'),('evaluation','evaluation')):
        if int(parent.get(key,'-1'))!=expected[other]:raise ValueError('O belongs to another parent capture: '+key)
    sampled=parent['sampledExtent'].split('x')
    if len(sampled)!=2 or any(not (0<int(n)<=1024) for n in sampled):raise ValueError('invalid legacy sampling grid')
    if (directory/'stages.rgba32f').stat().st_size!=int(sampled[0])*int(sampled[1])*64:
        raise ValueError('parent raw stage payload incomplete')
    return {**meta,'same_frame_verified':True,'sha256':hashlib.sha256(raw).hexdigest(),
            'photometric_domain_verified':False}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path);a=p.parse_args()
    print(json.dumps(read_capture(a.directory),ensure_ascii=False,indent=2))
if __name__=='__main__':main()
