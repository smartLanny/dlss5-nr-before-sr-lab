"""Compile extracted production forwarding and narrow null-raster policies.
No game, NVIDIA model or Windows driver behavior is simulated as runtime proof.
"""
from __future__ import annotations
import argparse
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def definition(text: str, marker: str) -> str:
    start = text.index(marker)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def run(compiler: str, name: str, code: str) -> None:
    with tempfile.TemporaryDirectory(prefix='nr044-') as td:
        src = Path(td) / (name + '.cpp')
        exe = Path(td) / (name + ('.exe' if os.name == 'nt' else ''))
        src.write_text(code, encoding='utf-8')
        flags = ['-std=c++20', '-O0', '-Wall', '-Wextra', '-Werror']
        if os.name == 'nt': flags.append('-static')
        subprocess.run([compiler, *flags, str(src), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)

COMMON = r'''
#include <atomic>
#include <cstdio>
#include <cstring>
#include <limits>
#include <initializer_list>
int checks=0, failures=0;
void check(bool ok,const char* label) { ++checks; if(!ok) {++failures; std::printf("FAIL %s\n",label);} }
int finish(const char* name) { std::printf("%s: %d checks, %d failures\n",name,checks,failures); return failures?1:0; }
'''

def forwarding(source: str) -> str:
    names = ['CallNativeEvaluatePassThrough', 'HookEvaluate', 'HookEvaluateC', 'HookCoreEvaluate', 'HookDlssdEvaluate']
    actual = '\n'.join(definition(source, 'NgxResult ' + n + '(') for n in names)
    return COMMON + r'''
using NgxResult = unsigned;
struct ID3D12GraphicsCommandList {}; struct NgxParams {}; struct NgxHandle { unsigned id; };
using EvaluateFeatureFn=NgxResult(*)(ID3D12GraphicsCommandList*,const NgxHandle*,const NgxParams*,void*);
NgxHandle public_handle{1}, internal_handle{2};
std::atomic<bool> g_enabled_fast{false},g_generic_renodx_conflict{false},g_shortfuse_nr_enabled{false},g_evaluate_target_overflow{false},g_core_unknown_handle_guard{true};
std::atomic<unsigned> g_wrapper_calls{0},g_core_calls{0},g_dlssd_evaluate_calls{0};
bool g_inside_ngx_evaluate=false,other_owner=false;
int unknowns=0,native_calls=0;
enum class BypassReason {ShortFuseOwnsNr}; enum class ModuleObserverFault {UnknownEvaluate};
void SetBypassReason(BypassReason) {}
bool LatchGenericRenoDxConflictIfLoaded() {return false;}
bool OtherNrOwnerActive() {return other_owner;}
void ObserveNullEvaluateHandle(const char*) {}
void LatchUnknownFeatureHandle(const char*,const char*,ModuleObserverFault) {++unknowns;}
struct Feature { const NgxHandle* handle; };
Feature FindFeature(const NgxHandle* h) {return {h==&public_handle?h:nullptr};}
bool IsUnknownOutermostEvaluateHandle(const NgxHandle* h) {return h&&!g_inside_ngx_evaluate&&!FindFeature(h).handle;}
template<class... T> void Log(const char*,T...) {}
NgxResult DispatchEvaluate(EvaluateFeatureFn f, ID3D12GraphicsCommandList* l,const NgxHandle* h,const NgxParams* p,void* c) {
    const bool was=g_inside_ngx_evaluate;g_inside_ngx_evaluate=true;
    auto r=f(l,h,p,c);g_inside_ngx_evaluate=was;return r;
}
NgxResult Native(ID3D12GraphicsCommandList*,const NgxHandle*,const NgxParams*,void*) {++native_calls;return 0x1234;}
EvaluateFeatureFn g_original_evaluate=Native,g_original_evaluate_c=Native,g_original_core_evaluate=Native,g_original_dlssd_evaluate=Native;
ACTUAL
NgxResult NestedCore(ID3D12GraphicsCommandList* l,const NgxHandle*,const NgxParams* p,void* c) {
    return HookCoreEvaluate(l,&internal_handle,p,c);
}
NgxResult NestedDlssd(ID3D12GraphicsCommandList* l,const NgxHandle*,const NgxParams* p,void* c) {
    return HookDlssdEvaluate(l,&internal_handle,p,c);
}
int main() {
    ID3D12GraphicsCommandList l;NgxParams p;
    EvaluateFeatureFn wrappers[]={HookEvaluate,HookEvaluateC,HookCoreEvaluate,HookDlssdEvaluate};
    for(unsigned i=0;i<4;++i) for(int reason=0;reason<3;++reason) {
        g_original_evaluate=g_original_evaluate_c=g_original_core_evaluate=g_original_dlssd_evaluate=Native;
        EvaluateFeatureFn *originals[]={&g_original_evaluate,&g_original_evaluate_c,&g_original_core_evaluate,&g_original_dlssd_evaluate};
        *originals[i]=i==2?NestedDlssd:NestedCore;
        g_enabled_fast=reason!=0;other_owner=reason==1;g_evaluate_target_overflow=reason==2;
        unknowns=native_calls=0;g_inside_ngx_evaluate=false;
        const auto result=wrappers[i](&l,&public_handle,&p,nullptr);
        check(result==0x1234&&native_calls==1,"fast path forwards once and preserves result");
        check(unknowns==0,"nested internal handle must not permanently fault observer while disabled");
        check(!g_inside_ngx_evaluate,"outer nesting flag restored");
    }
    g_original_core_evaluate=Native;other_owner=false;g_evaluate_target_overflow=false;
    unknowns=0;g_inside_ngx_evaluate=false;g_enabled_fast=false;
    HookCoreEvaluate(&l,&internal_handle,&p,nullptr);
    check(unknowns==1,"truly unknown OUTERMOST handle still faults even while disabled");
    unknowns=0;g_inside_ngx_evaluate=true;
    CallNativeEvaluatePassThrough(Native,&l,&public_handle,&p,nullptr);
    check(g_inside_ngx_evaluate,"pre-existing nested caller stays nested");
    return finish("actual NGX disabled/owner/overflow forwarding");
}
'''.replace('ACTUAL', actual)


def raster(source: str) -> str:
    helper = definition(source, 'bool CommandStateBlendFactorAdmissible(')
    actual = definition(source, 'bool CanAutoRestoreRasterNull(')
    return COMMON + r'''
using UINT=unsigned;using UINT64=unsigned long long;
constexpr float D3D12_VIEWPORT_BOUNDS_MIN=-32768.f,D3D12_VIEWPORT_BOUNDS_MAX=32767.f;
constexpr UINT64 kDynamicViewport=2,kDynamicScissor=4,kDynamicBlend=8,kDynamicTargets=64;
struct Viewport {float TopLeftX=0,TopLeftY=0,Width=0,Height=0,MinDepth=0,MaxDepth=1;};
struct Rect {long left=0,top=0,right=0,bottom=0;};
struct NativeCommandState {
 bool known=true,null_pipeline_baseline=true,null_guard_covered=true,null_guard_reset_observed=true,null_guard_tainted=false;
 void *pipeline=nullptr,*raytracing_pipeline=nullptr,*rtv_cache=nullptr,*dsv_cache=nullptr;UINT unknown_domains=0;
 struct {UINT target_count=0;bool contiguous_targets=false,has_depth_target=false;struct Handle{unsigned long long ptr=0;}targets[8],depth_target;UINT64 recorded=kDynamicViewport|kDynamicScissor;UINT vertex_mask=0,stream_mask=0,viewport_count=1,scissor_count=1;Viewport viewports[16];Rect scissors[16];float blend[4]={1,1,1,1};} dynamic;
};
HELPER
ACTUAL
int main() {
 NativeCommandState s;s.dynamic.viewports[0]={0,0,1920,1080,0,1};s.dynamic.scissors[0]={1,2,1919,1079};
 check(CanAutoRestoreRasterNull(s),"observed viewport/scissor subset admitted");
 for(unsigned bit=0;bit<20;++bit) {auto t=s;t.dynamic.recorded|=UINT64{1}<<bit;
   check(CanAutoRestoreRasterNull(t)==(bit==1||bit==2||bit==3||bit==6),"all other dynamic domains rejected");}
 for(auto member:{&NativeCommandState::known,&NativeCommandState::null_pipeline_baseline,&NativeCommandState::null_guard_covered,&NativeCommandState::null_guard_reset_observed}) {
   auto t=s;t.*member=false;check(!CanAutoRestoreRasterNull(t),"no missing coverage or baseline admitted");}
 {auto t=s;t.null_guard_tainted=true;check(!CanAutoRestoreRasterNull(t),"taint remains sticky");}
 {auto t=s;t.pipeline=&s;check(!CanAutoRestoreRasterNull(t),"not a replacement typed-PSO path");}
 {auto t=s;t.raytracing_pipeline=&s;check(!CanAutoRestoreRasterNull(t),"DXR not changed");}
 for(unsigned bit=0;bit<32;++bit) {auto t=s;t.unknown_domains=1u<<bit;check(!CanAutoRestoreRasterNull(t),"unknown dirty domain rejected");}
 {auto t=s;t.dynamic.vertex_mask=1;check(!CanAutoRestoreRasterNull(t),"VB resource state rejected");}
 {auto t=s;t.dynamic.stream_mask=1;check(!CanAutoRestoreRasterNull(t),"SO resource state rejected");}
 {auto t=s;t.dynamic.viewport_count=17;check(!CanAutoRestoreRasterNull(t),"viewport array bound enforced");}
 {auto t=s;t.dynamic.scissor_count=17;check(!CanAutoRestoreRasterNull(t),"scissor array bound enforced");}
 for(float bad:{-1.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
   auto t=s;t.dynamic.viewports[0].Width=bad;check(!CanAutoRestoreRasterNull(t),"invalid viewport width rejected");
 }
 {auto t=s;t.dynamic.viewports[0].MinDepth=2;check(!CanAutoRestoreRasterNull(t),"invalid depth range rejected");}
 {auto t=s;t.dynamic.scissors[0].right=-1;check(!CanAutoRestoreRasterNull(t),"inverted scissor rejected");}
 {auto t=s;t.dynamic.recorded=0;check(!CanAutoRestoreRasterNull(t),"pristine route remains distinct");}
 {auto t=s;t.dynamic.recorded=kDynamicBlend;check(CanAutoRestoreRasterNull(t),"blend-only reset subset admitted");}
 for(unsigned lane=0;lane<4;++lane) {
   for(float ok:{-0.f,-1.f,-2.f,-3.f,1.01f,2.f,std::numeric_limits<float>::max(),
                 std::numeric_limits<float>::lowest(),std::numeric_limits<float>::denorm_min(),
                 -std::numeric_limits<float>::denorm_min()}) {
     auto t=s;t.dynamic.recorded=kDynamicBlend;t.dynamic.blend[lane]=ok;
     float before[4];std::memcpy(before,t.dynamic.blend,sizeof(before));
     check(CanAutoRestoreRasterNull(t),"finite out-of-unit blend admitted");
     check(std::memcmp(before,t.dynamic.blend,sizeof(before))==0,"admission preserves observed blend bits");
   }
   for(float bad:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),
                  -std::numeric_limits<float>::infinity()}) {
     auto t=s;t.dynamic.recorded=kDynamicBlend;t.dynamic.blend[lane]=bad;
     check(!CanAutoRestoreRasterNull(t),"nonfinite blend rejected in every lane");
   }
 }
 {auto t=s;t.dynamic.recorded=kDynamicBlend;
  const float observed[4]={-0.f,-1.f,-2.f,-3.f};
  std::memcpy(t.dynamic.blend,observed,sizeof(observed));
  check(CanAutoRestoreRasterNull(t),"issue99 dev3 simultaneous four-factor state admitted");
  check(std::memcmp(observed,t.dynamic.blend,sizeof(observed))==0,"issue99 sample not clamped");}
 {auto t=s;t.dynamic.recorded=0x46;check(CanAutoRestoreRasterNull(t),"empty target set restores");
  t.dynamic.target_count=1;check(!CanAutoRestoreRasterNull(t),"raw RTV rejected");
  t.rtv_cache=&s;t.dynamic.contiguous_targets=true;t.dynamic.targets[0].ptr=1;
  check(CanAutoRestoreRasterNull(t),"owned RTV accepted");
  t.dynamic.has_depth_target=true;check(!CanAutoRestoreRasterNull(t),"missing DSV cache rejected");
  t.dsv_cache=&s;t.dynamic.depth_target.ptr=2;check(CanAutoRestoreRasterNull(t),"owned DSV accepted");
  t.dynamic.target_count=9;check(!CanAutoRestoreRasterNull(t),"RTV count bounded");
  t.dynamic.target_count=8;t.dynamic.contiguous_targets=false;check(!CanAutoRestoreRasterNull(t),"cache contiguous"); }
 return finish("actual bounded null-raster policy");
}
'''.replace('HELPER', helper).replace('ACTUAL', actual)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler',default=os.environ.get('CXX','c++'))
    p.add_argument('--forwarding-only',action='store_true')
    a=p.parse_args();compiler=shutil.which(a.compiler)
    if not compiler: raise SystemExit('C++ compiler not found: '+a.compiler)
    cpp=(ROOT/'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
    run(compiler,'ngx-forwarding',forwarding(cpp))
    if not a.forwarding_only:
        cs=(ROOT/'src/nr_command_state.inl').read_text(encoding='utf-8-sig')
        run(compiler,'null-raster',raster(cs))
        assert 'if (record == nullptr)\n        g_command_state_registration_repair_needed.store(true' in cs
        assert 'g_command_state_registration_repair_needed.store(record == nullptr' not in cs
        ui=(ROOT/'src/nr_settings_ui.inl').read_text(encoding='utf-8-sig')
        assert 'next.mode = 1; next.enabled = true; next.identity_use_original = false;' in ui
        print('PASS sticky registration / visible comparison integration')
