#!/usr/bin/env python3
"""Actual guide transport on D3D12 WARP; does not claim NGX/FG inference."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--compiler',required=True)
    parser.add_argument('--output',required=True)
    parser.add_argument('--default-view-negative',action='store_true',help='Require the old explicit-type-only view rule to fail')
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[1]
    output=Path(args.output).resolve()
    output.mkdir(parents=True,exist_ok=True)
    core=(root/'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
    start=core.index('struct NrJitterBinding {')
    end=core.index('// Jitter is expressed in source pixels',start)
    harness=(root/'tests/nr_source_transport_warp.cpp.in').read_text(encoding='utf-8')
    runtime=(root/'src/nr_source_frame_runtime.inl').read_text(encoding='utf-8-sig')
    guid_start=runtime.index('const GUID source_identity_guid=');guid_end=runtime.index('struct Lock',guid_start)
    cookie_start=runtime.index('Id ObjectId(');cookie_end=runtime.index('#include "nr_source_observer.inl"',cookie_start)
    harness=harness.replace('// @ACTUAL_OBJECT_COOKIES@',runtime[guid_start:guid_end]+runtime[cookie_start:cookie_end])
    state_start=runtime.index('struct ResourceState {');state_end=runtime.index('struct Producer {',state_start)
    harness=harness.replace('// @ACTUAL_SOURCE_STATES@',runtime[state_start:state_end])
    capture_start=runtime.index('void CaptureNgxGuides(');capture_end=runtime.index('// Callback executes',capture_start)
    harness=harness.replace('// @ACTUAL_CAPTURE_NGX_GUIDES@',runtime[capture_start:capture_end])
    views=(root/'src/nr_source_views.inl').read_text(encoding='utf-8-sig')
    if args.default_view_negative:
        old='desc.type==reshade::api::resource_view_type::unknown||'
        if views.count(old)!=1:raise RuntimeError('default-view negative mutation no longer matches production rule')
        views=views.replace(old,'')
    harness=harness.replace('#include "nr_source_views.inl"',views)
    generated=output/'source_transport_warp.cpp'
    generated.write_text(harness.replace('// @ACTUAL_GUIDE_BINDING@',core[start:end]),encoding='utf-8')
    exe=output/'source_transport_warp.exe'
    command=[args.compiler,'-std=c++20','-O2','-static','-Wall','-Wextra','-Werror',
             '-I'+str(root/'src'),str(generated),'-o',str(exe),'-ld3d12','-ldxgi','-ldxguid','-ld3dcompiler']
    build=subprocess.run(command,capture_output=True,text=True)
    (output/'compile.log').write_text(build.stdout+build.stderr,encoding='utf-8')
    if build.returncode:raise RuntimeError(build.stderr)
    run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=60)
    (output/'run.log').write_text(run.stdout+run.stderr,encoding='utf-8')
    negative_caught=args.default_view_negative and run.returncode!=0 and 'default RTV' in run.stderr
    passed=negative_caught if args.default_view_negative else run.returncode==0
    receipt={'success':passed,'scope':'Actual native-success receipt gating, padded depth-stencil/full-subresource guide copies, native-seam HLSL snapshots, blocked cross-queue GPU wait/readback, and resource/fence reuse on D3D12 WARP',
              'warp_verified':run.returncode==0,'ngx_verified':False,'fg_verified':False,'game_verified':False,'stdout':run.stdout,
              'source_sha256':hashlib.sha256((root/'src/nr_source_transport.inl').read_bytes()).hexdigest(),
              'native_guide_snapshot_sha256':hashlib.sha256((root/'src/nr_native_guide_snapshot.inl').read_bytes()).hexdigest(),
              'runtime_sha256':hashlib.sha256((root/'src/nr_source_frame_runtime.inl').read_bytes()).hexdigest(),
              'receipt_policy_sha256':hashlib.sha256((root/'src/nr_late_present_receipt.hpp').read_bytes()).hexdigest(),
              'views_sha256':hashlib.sha256((root/'src/nr_source_views.inl').read_bytes()).hexdigest(),
             'default_view_negative':args.default_view_negative,'negative_caught':negative_caught,'stderr':run.stderr,
              'cross_queue_scope':'producer queue is deliberately GPU-blocked; consumer queue Waits on the exact receipt fence before readback; no CPU producer wait or flush',
              'native_guide_warp_scope':{
                  'padded_depth':{'format':'R32G8X24_TYPELESS','physical':[2228,1256],'active':[2228,1253],
                                  'operation':'full depth-stencil subresource copy with active/padded signature retained'},
                  'shader_snapshot':{'real_hlsl_compile_and_dispatch':run.returncode==0,'depth_format':'R32_FLOAT from R32G8X24_TYPELESS SRV',
                                     'motion_format':'R16G16_FLOAT','nonzero_origins':True,'signed_half_motion':True,
                                     'inputs_actual_state':'NON_PIXEL_SHADER_RESOURCE','observer_state':'unknown'},
                  'predicate_input':'nonzero EQUAL_ZERO predicate is active before capture; helper must clear it for snapshot dispatch',
                  'source_contents_unchanged':run.returncode==0,'exact_producer_and_consumer_fences':run.returncode==0},
              'command_state_envelope_scope':'fixture supplies admission only; full Core NativeCommandStateScope restoration is not verified by this WARP gate',
              'enhanced_barrier_scope':'production callback invalidation tested; actual enhanced GPU barriers not executed'}
    (output/'validation.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
    print(run.stdout,end='')
    if negative_caught:print('default-view negative control: old explicit-type-only rule rejected as expected')
    if not passed:raise RuntimeError(run.stderr)
if __name__=='__main__':main()
