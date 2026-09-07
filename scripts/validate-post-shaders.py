#!/usr/bin/env python3
"""Compile every compute entry in the actual generated production codec."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

def extract(source):
    start='const char kCodecShader[] = R"HLSL('
    stop=')HLSL";'
    if source.count(start)!=1: raise ValueError('Expected exactly one actual codec shader')
    body=source.split(start,1)[1]
    if stop not in body: raise ValueError('Unterminated production shader')
    body=body.split(stop,1)[0]
    names=re.findall(r'\[numthreads\([^\)]*\)\]\s*void\s+(\w+)\s*\(',body)
    if len(set(names))!=len(names) or not {'EncodeCS','CombineCS','EncodeV2CS','CombineV2CS'}.issubset(names):
        raise ValueError('Missing or duplicated expected production entry points')
    if 'LoadPostArea' not in body or 'const bool scaled = ModelWidth != Width' not in body:
        raise ValueError('This is not the post-compat generated shader')
    return body,names

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--source',type=Path,required=True)
    ap.add_argument('--compiler',type=Path,required=True)
    a=ap.parse_args();body,names=extract(a.source.read_text(encoding='utf-8'))
    with tempfile.TemporaryDirectory(prefix='nr-post-shader-') as tmp:
        path=Path(tmp)/'actual-codec.hlsl';path.write_text(body,encoding='utf-8')
        for name in names:subprocess.run([str(a.compiler.resolve()),str(path),name],check=True)
    print(f'Compiled all {len(names)} production compute entries; not a GPU/runtime test')
if __name__=='__main__':main()
