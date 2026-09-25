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
    temporal_patch=load('temporal_patch',core/'scripts/patch-temporal-runtime.py')
    native_output_patch=load('native_output_patch',core/'scripts/patch-native-output-capture.py')
    acceptance_patch=load('acceptance_patch',core/'scripts/patch-acceptance.py')
    reconstruction_patch=load('reconstruction_patch',core/'scripts/patch-reconstruction.py')
    for commit in (previous.BASE,colour4.COLOUR3,colour4.SOURCE_GATES,BASE):
        if not base.is_ancestor(core,commit): raise ValueError('Missing reviewed ancestor: '+commit)
    inputs={}
    paths=previous.INPUTS+stage_patch.INPUTS+colour4.EXTRA_INPUTS+edge_patch.INPUTS+EXTRA+temporal_patch.INPUTS+native_output_patch.INPUTS+acceptance_patch.INPUTS+reconstruction_patch.INPUTS+('tests/test_reconstruction_runtime.py',)
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
        temporal_patch.apply(stage,inputs,m3)
        native_output_patch.apply(stage,inputs,m3)
        acceptance_patch.apply(stage,inputs,m3)
        check('test_colour_edge.py',stage)
        check('test_unified_ui.py',stage)
        check('test_user_controls.py',stage)
        reconstruction_patch.apply(stage,inputs,m3)
        check('test_reconstruction_runtime.py',stage)
        mf=stage/'source-manifest.json'
        manifest=json.loads(mf.read_text(encoding='utf-8'))
        manifest['colour_lab']={
            'version':edge_patch.VERSION,'display_version':'0.5.1-Beta-Reconstruction1-UI1','source_commit':source,'base_commit':BASE,
            'default_mode':1,'default_colour_strength':0.5,'default_colour_range':0.5,'priority_colour_strength':0.5,'conservative_colour_strength':1.0,'default_restore_mode':0,'default_input_curve':0,
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
        manifest['reconstruction']={
            'version':edge_patch.VERSION,'source_commit':source,
            'default_mode':0,'default_chroma_guard':False,
            'sampling':'paired decoded source-guided positive weights, modes 4/16; 1:1 retains original',
            'preserves_new_luminance':True,'extra_textures':0,'extra_passes':0,
            'extra_nr_calls':0,'history_changed':False,'whole_dark_edit_shrink_added':False,
            'capture':'same actual pre-generic P/N; original source/final stored; legacy detail plane unchanged',
            'unsupported':'unknown encoding/raw diagnostics/legacy edit field; controls do not imply adoption',
            'gpu_verified':False,'game_verified':False}
        manifest['files']=base.file_hashes(stage)
        manifest['temporal']={'default_enabled':False,'backends':['native-prepared-MV','AMD-FFX-1.1.4'],
            'scopes':['whole-chain','later-layers-composed-anchor'],
            'production_routes':{'DX12 Feature1 post-SR SDR':['native','AMD'],
                'DX12 Feature1 pre-SR explicit SDR whole-chain':['AMD'],
                'DX12 proven RR post SDR':['AMD'],
                'DX12 declared-linear-HDR pre-SR whole/post-SR/post-RR; same-frame PreExposure':['AMD']},
            'not_connected':['unknown-or-PQ-HDR','HDR-native-MV','pre-native-MV','pre-later-layers','NVOF'],
            'cpu_readback_wait':False,'history_half_life_ms':60,'reliability':0.65,'maximum_gap_ms':250,
            'requires_exact_owner_queue':True,'adoption':'actual Core Execute/Signal receipt','game_verified':False}
        manifest['acceptance']={'transient_only':True,'ini_writes':False,'model_switching':False,
            'captures_after_measurement':True,'manual_game_settings':['RR','HDR','FG'],
            'frame_timing':'ReShade callback intervals, not generated FPS','game_verified':False}
        manifest['native_output_capture']={'optional':True,'schema':'native-output-v1',
            'old_nrf_unchanged':True,'same_call_required':True,'known_state_required':True,
            'missing_is_explicit':True,'game_verified':False}
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
    print('Generated unified10 source only; Windows/GPU/game verification is separate.')
