#!/usr/bin/env python3
"""Extend the existing #133 transaction with SR/RR post-NR work scaling.

Run in a committed checkout containing #133. This stages and manifests
source (validation is a separate workflow), not a game installation; never edits a user's game or checks out/reset
another branch. The original generator remains usable for the dev1 baseline.
"""
from __future__ import annotations
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]
BASE='f367feac9da5ebf26b0f472f94c4440565664c8d'
VERSION='beta0.4.5-multiapi-dev2'

def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    if spec is None or spec.loader is None: raise ValueError(f'Cannot import {path}')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    return module

def configure(parent,core):
    if not parent.is_ancestor(core,BASE): raise ValueError('Missing actual #133 ancestry')
    parent.VERSION=VERSION
    old_configure=parent.configure
    post=load('post_compat_transform',core/'scripts/patch-post-compat.py')
    def merged_configure(root):
        m=old_configure(root)
        prior_core=m.legacy.patch_core
        prior_ui=m.legacy.patch_ui
        m.legacy.patch_core=lambda text:post.patch_core(prior_core(text))
        m.legacy.patch_ui=lambda text:post.patch_ui(prior_ui(text))
        return m
    parent.configure=merged_configure
    old_finalize=parent.finalize
    def finalize(root,stage,source):
        old_finalize(root,stage,source)
        descriptor_source=stage/'core/src/nr_codec_descriptors.inl'
        descriptor_text=post.patch_descriptors(descriptor_source.read_text(encoding='utf-8'))
        descriptor_source.write_text(descriptor_text,encoding='utf-8',newline='\n')
        names=stage/'core/src/interop/nr_deployment_names.h'
        text=names.read_text(encoding='utf-8')
        old='dlss5-native-carrier-multiapi-dev1.addon64'
        if text.count(old)!=1: raise ValueError('Unexpected companion name')
        names.write_text(text.replace(old,'dlss5-native-carrier-multiapi-dev2.addon64'),encoding='utf-8',newline='\n')
        # The numeric product version remains the 0.4.5 maintenance family,
        # but the descriptive PE identity must not still say DX11 exp3.
        rc=stage/'core/src/nr_before_sr.rc'
        resource_text=rc.read_text(encoding='utf-8')
        old_description='0.4.5 DX11 exp3 beta'
        if old_description not in resource_text:
            raise ValueError('Missing expected inherited PE description')
        rc.write_text(resource_text.replace(old_description,'0.4.5 multiapi dev2 beta'),
                      encoding='utf-8',newline='\n')
        ini=stage/'core/config/nr_before_sr.ini'
        with ini.open('a',encoding='utf-8',newline='\n') as out:
            out.write('\n; SR/RR post NR: output-relative N/O, not game RenderScale.\nRRPostEnabled=1\nPostWorkPercent=100\n')
        mf=stage/'source-manifest.json'
        manifest=json.loads(mf.read_text(encoding='utf-8'))
        manifest['integration'].update({
            'version':VERSION,
            'rr_post':'Feature13 observed HW-depth route; native evaluate once; original Mode retained; not game verified',
            'post_work':'50-100 percent of SR/RR output; exact 100 percent reference; original output snapshot',
            'jitter':'post colour zero jitter; native input jitter retained in capture; native depth phase needs runtime validation',
            'rr_linear_depth':'not converted: linear/unknown depth route keeps original RR output',
            'synthetic_legacy':'not integrated',
            'vulkan_native':'not enabled: no new submit-boundary transport',
            'fg_control':'not implemented',
            'source_parent':BASE,
        })
        manifest['files']=parent.file_hashes(stage)
        mf.write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        if json.loads(mf.read_text(encoding='utf-8'))['files']!=parent.file_hashes(stage):
            raise ValueError('Final dev2 manifest mismatch')
    parent.finalize=finalize

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--core',type=Path,default=ROOT)
    ap.add_argument('--upstream',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True)
    args=ap.parse_args();core=args.core.resolve()
    parent=load('dev1_transaction',core/'scripts/prepare-multi-api-beta.py')
    configure(parent,core)
    parent.prepare(core,args.upstream.resolve(),args.output.resolve())
    print(f'{VERSION}: generated source; no claim of game validation')

if __name__=='__main__':
    try: main()
    except (ValueError,OSError,subprocess.CalledProcessError) as exc:
        raise SystemExit(str(exc)) from exc
