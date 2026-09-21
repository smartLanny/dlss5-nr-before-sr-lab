#!/usr/bin/env python3
"""Colour4 edge1 source exporter; preserves colour3 and colour4 validation gates."""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
BASE='843d1e1c7f61e563b8913242b8756542af169a80'
EXTRA=('scripts/patch-user-controls.py','tests/test_user_controls.py','tests/test_unified_ui.py','scripts/prepare-colour4-edge.py','scripts/build-colour4-edge.ps1',
       'tests/test_colour_edge.py','tests/run_colour_edge_math.py','tests/colour_edge_math.cpp', 'tests/test_quality_panel.py', 'tests/quality_recipe_test.cpp', 'tests/run_quality_recipe.py')

def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    if spec is None or spec.loader is None: raise ValueError('Cannot load '+str(path))
    m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m

def prepare(core: Path,upstream: Path,output: Path) -> None:
    base=load('edge_uniform',core/'scripts/prepare-multi-api-beta.py')
    previous=load('edge_colour3',core/'scripts/prepare-colour-lab.py')
    colour4=load('edge_colour4',core/'scripts/prepare-colour4.py')
    stage_patch=load('edge_stage',core/'scripts/patch-colour-stage.py')
    edge_patch=load('edge_patch',core/'scripts/patch-colour-edge.py')
    user_patch=load('user_patch',core/'scripts/patch-user-controls.py')
    for commit in (previous.BASE,colour4.COLOUR3,colour4.SOURCE_GATES,BASE):
        if not base.is_ancestor(core,commit): raise ValueError('Missing reviewed ancestor: '+commit)
    inputs={}
    paths=previous.INPUTS+stage_patch.INPUTS+colour4.EXTRA_INPUTS+edge_patch.INPUTS+EXTRA
    for path in paths:
        data=base.git(core,'show','HEAD:'+path)
        if (core/path).is_symlink() or base.git(core,'hash-object',path).strip()!=base.git(core,'rev-parse','HEAD:'+path).strip():
            raise ValueError('Dirty/untracked edge input: '+path)
        inputs[path]=data
    m1=load('edge_m1',core/'scripts/patch-colour-lab.py')
    m3=load('edge_m3',core/'scripts/patch-colour-restore.py')
    prior=base.finalize
    def check(script, stage):
        # test_colour_stage.py intentionally accepts only one actual source.
        # Calling it with BOTH flags aborts before any generated-source test.
        # Run raw-source gates independently in the builder, then inspect this
        # complete intermediate stage. Other inherited tests also inspect raw.
        args = ['--generated', str(stage/'core')]
        if script != 'test_colour_stage.py':
            args = ['--repo', str(core)] + args
        subprocess.run([sys.executable, str(core/'tests'/script), *args, '-v'], check=True)
    def finalize(core_root,stage,source):
        prior(core_root,stage,source)
        m1.apply(stage,inputs);m3.apply(stage,inputs)
        check('test_colour_restore.py',stage)
        stage_patch.apply(stage,inputs,m3)
        check('test_colour_stage.py',stage)
        edge_patch.apply(stage,inputs,m3)
        user_patch.apply(stage,inputs,m3)
        check('test_colour_edge.py',stage)
        check('test_unified_ui.py',stage)
        check('test_user_controls.py',stage)
        mf=stage/'source-manifest.json'
        manifest=json.loads(mf.read_text(encoding='utf-8'))
        manifest['colour_lab']={
            'version':edge_patch.VERSION,'source_commit':source,'base_commit':BASE,
            'default_mode':2,'default_colour_strength':1.0,'priority_colour_strength':0.7,'conservative_colour_strength':1.0,'default_restore_mode':0,'default_input_curve':0,
            'raw_reference_before_aesthetics':True,'neutral_probe_persisted':False,
            'default_edge_recovery':0,'edge_flag':16777216,
            'edge_routes':['scaled-v2-rgb'],'edge_selection_resets_history':False,
            'edge_sampling_same_for_proxy_neural':True,'edge_weights_depend_on_neural':False,
            'captures_keep_actual_neural_output':True,
            'v2_capture_proxy_neural_use_effective_edge_weights':True,
            'capture_detail_decision_plane':'legacy filter, not edge or early raw consumer',
            'extra_nr_invocations':0,'extra_gpu_passes':0,'default_model_input_changed':False,
            'ui_pages':['picture','lighting','experiments','developer','feedback'],
            'model_default_strength':1.5, 'skin_default':0.4, 'light_edge_protection_defaults':[0,0],
            'saved_custom_settings_preserved':True, 'diagnostics_collapsed':False,
            'n2_legacy_combination_game_verified':False,
            'runtime_verified':False,
            'overlay_sha256':{p:hashlib.sha256(data).hexdigest() for p,data in inputs.items()}}
        manifest['files']=base.file_hashes(stage)
        mf.write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        if json.loads(mf.read_text(encoding='utf-8'))['files']!=base.file_hashes(stage):
            raise ValueError('Edge stage hash mismatch')
    base.finalize=finalize
    base.prepare(core,upstream,output)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--core',type=Path,default=ROOT)
    p.add_argument('--upstream',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    try: prepare(a.core.resolve(),a.upstream.resolve(),a.output.resolve())
    except (ValueError,OSError,subprocess.SubprocessError) as error:
        raise SystemExit(str(error)) from error
    print('Generated unified5 source only; Windows/GPU/game verification is separate.')
