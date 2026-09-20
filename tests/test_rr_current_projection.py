"""Actual admission/reset blocks with Win32 pointer reads; no NGX/game claim."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_rr_depth_device_identity import compiler, definition

ROOT = Path(__file__).resolve().parents[1]

HARNESS = r'''
#include <windows.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>
#include "src/nr_rr_depth_projection.h"
#include "src/nr_rr_projection_source.h"
#include "src/nr_guide_contract.h"
constexpr int kNgxSuccess=1;
struct NgxParams {void* matrix=nullptr; bool present=true;};
struct FeatureRecord {int feature_id=13,rr_depth_type=0;unsigned long long generation=1;};
using GetVoidFn=int(*)(const NgxParams*,const char*,void**);
int Get(const NgxParams* p,const char* key,void** out){assert(!std::strcmp(key,"ViewToClipMatrix"));*out=p->matrix;return p->present?1:0;}
template<class T>T ParamSlot(const NgxParams*,unsigned slot){assert(slot==8);return Get;}
namespace nr_post_settings {std::atomic<bool> missing_projection{false};}
constexpr unsigned D3D12_RESOURCE_DIMENSION_TEXTURE2D=3,D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE=8;
constexpr unsigned DXGI_FORMAT_R32_FLOAT=41,DXGI_FORMAT_R16_FLOAT=54;
struct Desc {unsigned Dimension=3,DepthOrArraySize=1,MipLevels=1;struct{unsigned Count=1;}SampleDesc;unsigned Flags=0,Format=41;};
struct Resource{Desc desc{};};
Desc ID3D12Resource_GetDesc(Resource* r){return r->desc;}
struct NrGuideBinding {Resource* depth=nullptr;NrGuideSignature signature{};int game_reset=0;};
enum class RrDepthReject {None,Projection,Storage};
RrDepthReject rejected{};
bool RejectRrDepth(RrDepthReject r){rejected=r;return false;}
@ADMISSION@
struct Context {bool reset_retry_pending=false,diagnostic_reset_requested=false,diagnostic_warmup_reset=false;
 unsigned diagnostic_reset_reasons=0,successful_evaluations_since_reset=0;};
struct Slot {bool suppress_transfer_this_frame=false;struct{bool reset=false;}capture_params;};
struct{bool suppress_reset_frame=true;}g_config;
int bound_reset=-1;
void SetInt(NgxParams*,const char* key,int value){assert(!std::strcmp(key,"DLSSNR.Reset"));bound_reset=value;}
void BindReset(Context& context,Slot& slot,const NrGuideSignature& s,bool reset=false,int game_reset=0,
 bool history_changed=false,bool color_contract_reset=false,bool guide_reset=false){
 NgxParams* params=nullptr;
@RESET@
}
void Complete(Context& context){
@COMPLETE@
}
int main(){
 Resource depth;NgxParams params;FeatureRecord feature;NrGuideBinding guides;guides.depth=&depth;
 // Owner F8 state=1: absent matrix. OFF keeps the old exact refusal.
 assert(!ResolveRrLinearDepth(&params,feature,guides));assert(rejected==RrDepthReject::Projection);
 assert(nr_rr_depth::observations.input==nr_rr_depth::InputState::Absent);
 assert(!guides.signature.linear_depth);
 // Real admission consumes a same-evaluation SL camera only when native NGX
 // lacks it. No opt-in placeholder is required for this verified conversion.
 float camera[16]{1,0,0,0,0,1,0,0,0,0,0,1,0,0,.25f,0};
 nr_rr_depth::CurrentInput sl;
 std::memcpy(sl.matrix,camera,sizeof(camera));
 assert(nr_rr_depth::Resolve(sl.matrix,sl.projection));
 sl.state=nr_rr_depth::InputState::Supported;
 int commands=0,other_commands=0;
 {
   nr_rr_projection::Evaluation evaluation;evaluation.Set({1,1,8},sl,&commands);
   nr_rr_projection::NativeScope native(&commands,&params,feature.generation);
   assert(ResolveRrLinearDepth(&params,feature,guides));
   assert(guides.signature.linear_depth&&!guides.signature.rr_depth_placeholder);
   assert(guides.signature.depth_projection_b==.25f&&nr_rr_projection::last_origin==1);
   {nr_rr_projection::NativeScope wrong(&other_commands,&params,feature.generation);
    assert(!ResolveRrLinearDepth(&params,feature,guides));}
   params.matrix=reinterpret_cast<void*>(1);
   assert(!ResolveRrLinearDepth(&params,feature,guides));
   assert(nr_rr_depth::observations.input==nr_rr_depth::InputState::Unreadable);
   params.matrix=nullptr;
 }
 assert(!ResolveRrLinearDepth(&params,feature,guides)); // Lease ended, no stale camera.
 nr_post_settings::missing_projection=true;
 assert(ResolveRrLinearDepth(&params,feature,guides));assert(guides.signature.rr_depth_placeholder);
 assert(guides.signature.linear_depth&&guides.signature.depth_inverted==1);
 assert(guides.game_reset==0); // Native Reset is not fabricated.
 auto placeholder=guides.signature;
 float m[16]{1,0,0,0,0,1,0,0,0,0,0,1,0,0,10,0};params.matrix=m;
 auto original=m[14];guides.signature={};
 assert(ResolveRrLinearDepth(&params,feature,guides));assert(!guides.signature.rr_depth_placeholder);
 assert(guides.signature.depth_projection_b==10&&m[14]==original);
 assert(!SameNrGuideStorageContract(placeholder,guides.signature));
 // No stale matrix reuse after a successful current conversion.
 params.matrix=nullptr;nr_post_settings::missing_projection=false;
 assert(!ResolveRrLinearDepth(&params,feature,guides));
 nr_post_settings::missing_projection=true;params.matrix=reinterpret_cast<void*>(1);
 assert(!ResolveRrLinearDepth(&params,feature,guides));
 assert(nr_rr_depth::observations.input==nr_rr_depth::InputState::Unreadable);
 params.matrix=m;m[2]=.1f;
 assert(!ResolveRrLinearDepth(&params,feature,guides));
 assert(nr_rr_depth::observations.input==nr_rr_depth::InputState::Unsupported);
 m[2]=std::numeric_limits<float>::quiet_NaN();
 assert(!ResolveRrLinearDepth(&params,feature,guides));
 assert(nr_rr_depth::observations.input==nr_rr_depth::InputState::Nonfinite);
 params.matrix=nullptr;depth.desc.Format=28;
 assert(!ResolveRrLinearDepth(&params,feature,guides)&&rejected==RrDepthReject::Storage);
 depth.desc.Format=41;depth.desc.SampleDesc.Count=4;
 assert(!ResolveRrLinearDepth(&params,feature,guides));
 depth.desc.SampleDesc.Count=1;depth.desc.Flags=8;
 assert(!ResolveRrLinearDepth(&params,feature,guides));
 depth.desc.Flags=0;feature.rr_depth_type=9;
 assert(!ResolveRrLinearDepth(&params,feature,guides));
 feature.rr_depth_type=1;guides.signature={};
 assert(ResolveRrLinearDepth(&params,feature,guides)&&!guides.signature.linear_depth);
 // Every layer requests real Reset each frame, but intentional spatial-mode
 // resets do not permanently hide all output behind temporal warmup.
 for(unsigned layer=0;layer<5;++layer){
   Context context;Slot slot;
   for(unsigned frame=0;frame<5;++frame){
     BindReset(context,slot,placeholder,frame==0);
     assert(bound_reset==1&&slot.capture_params.reset&&context.diagnostic_reset_requested);
     assert(context.diagnostic_warmup_reset==(frame==0));
     assert(slot.suppress_transfer_this_frame==(frame==0));
     Complete(context);assert(context.successful_evaluations_since_reset==frame+1);
   }
   BindReset(context,slot,placeholder,false,1);Complete(context);
   assert(slot.suppress_transfer_this_frame&&context.successful_evaluations_since_reset==1);
   BindReset(context,slot,placeholder,false,0,true);Complete(context);
   assert(slot.suppress_transfer_this_frame&&context.successful_evaluations_since_reset==1);
 }
 Context normal;Slot slot;NrGuideSignature real{};
 BindReset(normal,slot,real);assert(bound_reset==0&&!slot.suppress_transfer_this_frame);
 BindReset(normal,slot,real,false,0,false,false,true);assert(bound_reset==1&&slot.suppress_transfer_this_frame);
 std::puts("PASS current projection admission, opt-in missing-only path, storage refusals, five-layer per-frame Reset and real transition warmup");
}
'''


class CurrentProjection(unittest.TestCase):
    def test_real_admission_and_reset(self):
        if os.name != 'nt':
            self.skipTest('Actual ReadProcessMemory boundary needs Windows')
        adapter = (ROOT/'src/nr_rr_depth_adapter.inl').read_text()
        core = (ROOT/'src/nr_before_sr.cpp').read_text(encoding='utf-8')
        binding = definition(core, 'bool BindNrParameters(')
        reset = binding[binding.index('    const unsigned diagnostic_reset_reasons'):binding.index('    const NrTuningSignature &tuning')]
        complete = definition(core, 'void CompleteNrDiagnosticEvaluation(')
        complete = complete[complete.index('{')+1:complete.index('    NrDiagnosticIdentity identity')]
        code = HARNESS.replace('@ADMISSION@', definition(adapter, 'bool ResolveRrLinearDepth(')).replace('@RESET@', reset).replace('@COMPLETE@', complete)
        cxx=compiler()
        with tempfile.TemporaryDirectory(prefix='rr-current-') as tmp:
            source=Path(tmp)/'test.cpp';source.write_text(code,encoding='utf-8')
            for opt in ('-O0','-O2'):
                exe=Path(tmp)/('test'+opt+'.exe')
                subprocess.run([str(cxx),'-std=c++17',opt,'-Wall','-Wextra','-Werror','-static',str(source),'-I'+str(ROOT),'-o',str(exe)],check=True)
                subprocess.run([str(exe)],check=True)


if __name__ == '__main__':
    unittest.main()
