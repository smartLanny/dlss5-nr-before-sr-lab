#!/usr/bin/env python3
"""Compile eight real SM5 entries; legacy and post transport stay separate."""
from __future__ import annotations
import argparse, hashlib, json, subprocess
from pathlib import Path
ENTRIES=[('rr_guides','nr_rr_edit.hlsl','PrepareGuidesCS'),('rr_accumulate','nr_rr_edit.hlsl','AccumulateCS'),
 ('rr_compose','nr_rr_edit.hlsl','ComposeCS'),('deferred_encode','nr_deferred_edit.hlsl','EncodeCS'),
 ('deferred_compose','nr_deferred_edit.hlsl','ComposeCS'),('normalize','nr_normalize_guides.hlsl','NormalizeGuidesCS'),
 ('post_extract','nr_post_residual.hlsl','ExtractCS'),('post_compose','nr_post_residual.hlsl','ComposeCS')]
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--compiler',type=Path,required=True);p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
 a=p.parse_args();compiler=a.compiler.resolve(strict=True);root=a.source.resolve(strict=True);out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
 report={'schema':'nr-edit-sm5-v5','compiler_sha256':hashlib.sha256(compiler.read_bytes()).hexdigest(),'entries':{},'gpu_execution':'not-run'}
 (out/'nr_edit_binaries.hpp').unlink(missing_ok=True);(out/'edit-shader-manifest.json').unlink(missing_ok=True)
 lines=['#pragma once','#include "../src/nr_edit_dx12.hpp"','namespace nr_edit_binaries {']
 for name,file,entry in ENTRIES:
  text=(root/file).read_text(encoding='utf-8')
  for include in ['nr_edit_scalar.hlsli','nr_edit_carrier.hlsli']:
   text=text.replace('#include "'+include+'"',(root/include).read_text(encoding='utf-8'))
  if '#include' in text:raise RuntimeError('Unreviewed shader include')
  source=out/(name+'.hlsl');source.write_text(text,encoding='utf-8');binary=out/(name+'.cso');binary.unlink(missing_ok=True)
  subprocess.run([str(compiler),str(source),entry,'0','-1',str(binary)],check=True)
  b=binary.read_bytes()
  if len(b)<32 or b[:4]!=b'DXBC':raise RuntimeError('Not a real SM5 DXBC container: '+name)
  lines.append('inline constexpr unsigned char '+name+'[]={'+','.join(str(x) for x in b)+'};')
  report['entries'][name]={'entry':entry,'source_sha256':hashlib.sha256(text.encode()).hexdigest(),'dxbc_sha256':hashlib.sha256(b).hexdigest(),'bytes':len(b)}
 lines+=['inline std::array<nr_edit_dx12::Bytecode,5> ModelKernels(){return {{{rr_guides,sizeof(rr_guides)},{rr_accumulate,sizeof(rr_accumulate)},{rr_compose,sizeof(rr_compose)},{deferred_encode,sizeof(deferred_encode)},{deferred_compose,sizeof(deferred_compose)}}};}',
  'inline nr_edit_dx12::Bytecode Normalize(){return {normalize,sizeof(normalize)};}',
  'inline std::array<nr_edit_dx12::Bytecode,static_cast<unsigned>(nr_edit_dx12::Kernel::Count)> PostKernels(){std::array<nr_edit_dx12::Bytecode,static_cast<unsigned>(nr_edit_dx12::Kernel::Count)> out{};out[static_cast<unsigned>(nr_edit_dx12::Kernel::PostExtract)]={post_extract,sizeof(post_extract)};out[static_cast<unsigned>(nr_edit_dx12::Kernel::PostCompose)]={post_compose,sizeof(post_compose)};return out;}','}']
 (out/'nr_edit_binaries.hpp').write_text('\n'.join(lines)+'\n',encoding='utf-8')
 (out/'edit-shader-manifest.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
 print('Eight SM5 artifacts built; GPU execution is a separate test.')
if __name__=='__main__':main()
