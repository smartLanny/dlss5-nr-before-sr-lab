#!/usr/bin/env python3
"""CPU checks for the actual bounded work receipt and production publishers."""
from pathlib import Path
import argparse
import importlib.util
import os
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('extract',ROOT/'scripts/extract-post-gpu.py')
extract=importlib.util.module_from_spec(spec);spec.loader.exec_module(extract)

HARNESS=r'''
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <map>
#include <string>
#include "nr_work_receipt.h"
struct NrContext {
    int mode=1;
    unsigned width=320,height=184,config_generation=7,history_generation=9;
    unsigned long long feature_generation=11,instance_id=13,pending_recording_token=17;
};
struct NrSlot { unsigned long long last_used_serial=19; };
struct Identity { unsigned long long instance_id=13,frame_id=23; unsigned config_generation=7; };
Identity g_applied_nr_identity;
nr_work_receipt::Receipt g_recorded_work_receipt{};
std::atomic<bool> g_lifecycle_tracking_uncertain{false};
struct Fence { unsigned long long completed=0; };
struct Queue { unsigned long long identity=0; Fence* fence=nullptr; };
Queue g_lifecycle_queues[2]{};
unsigned long long now=100;
unsigned fence_reads=0;
unsigned long long GetTickCount64() { return now; }
unsigned long long ID3D12Fence_GetCompletedValue(Fence* f) { ++fence_reads;return f->completed; }
constexpr unsigned D3D12_RESOURCE_DIMENSION_TEXTURE2D=3;
struct D3D12_RESOURCE_DESC {
    unsigned long long Width=0; unsigned Height=0,Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    struct { unsigned Count=1; } SampleDesc;
};
struct ID3D12Resource { D3D12_RESOURCE_DESC desc; };
D3D12_RESOURCE_DESC ID3D12Resource_GetDesc(ID3D12Resource* r) { return r->desc; }
struct FeatureRecord {
    int feature_id=1; unsigned width=640,height=360,out_width=1280,out_height=720;
    unsigned long long generation=11;
};
struct NgxParams { std::map<std::string,unsigned> values; ID3D12Resource* output=nullptr; };
bool GetUInt(const NgxParams* params,const char* key,unsigned* out) {
    const auto it=params->values.find(key);if(it==params->values.end())return false;
    *out=it->second;return true;
}
bool GetResource(const NgxParams* params,const char* key,ID3D12Resource** out) {
    if(std::string(key)!="Output" || !params->output)return false;
    *out=params->output;return true;
}
void Log(const char*,...) {}
HELPERS
unsigned checks=0;
void Check(bool value,const char* label) {
    ++checks;if(!value){std::printf("FAIL %s\n",label);std::exit(1);}
}
int main() {
    using namespace nr_work_receipt;
    NrContext context;NrSlot slot;
    Check(!SnapshotWorkReceiptLocked().valid,"no recording is unknown");
    RecordPostWorkReceiptLocked(0,context,slot,640,360,1280,720);
    Check(!g_recorded_work_receipt.valid,"external does not invent downstream O");
    RecordPostWorkReceiptLocked(1,context,slot,640,360,1280,720);
    auto a=SnapshotWorkReceiptLocked();
    Check(a.valid && a.route==Route::NativePostSr,"native post receipt recorded");
    Check(a.render.width==640 && a.render.height==360 && a.work.width==320 &&
          a.work.height==184 && a.output.width==1280 && a.output.height==720,"same-call R N O");
    Check(a.nr_evaluation==23 && a.command_token==17 && a.nr_instance==13 &&
          a.slot_use_serial==19 && a.feature_generation==11 && a.config_generation==7 &&
          a.history_generation==9,"real evaluation and lifetime identity copied together");
    Check(!a.submission_observed && !a.completion_observed,"recording alone proves no completion");
    Fence old{999};g_lifecycle_queues[0]={29,&old};
    a=SnapshotWorkReceiptLocked();
    Check(!a.completion_observed && fence_reads==0,"old slot/queue fence cannot prove new recording");
    Submitted(g_recorded_work_receipt,999,29,5,101);
    Check(!g_recorded_work_receipt.submission_observed,"unrelated command token rejected");
    for(auto fence:{0ull,~0ull}) Submitted(g_recorded_work_receipt,17,29,fence,101);
    Submitted(g_recorded_work_receipt,17,0,5,101);
    Submitted(g_recorded_work_receipt,17,29,5,99);
    Check(!g_recorded_work_receipt.submission_observed,"invalid fence queue or time not published");
    Submitted(g_recorded_work_receipt,17,29,5,101);
    Check(g_recorded_work_receipt.submission_observed && !g_recorded_work_receipt.completion_observed,
          "successful submission distinct from completed fence");
    old.completed=4;now=102;
    Check(!SnapshotWorkReceiptLocked().completion_observed,"pending exact fence not complete");
    old.completed=~0ull;
    Check(!SnapshotWorkReceiptLocked().completion_observed,"device removal is not completion");
    old.completed=5;g_lifecycle_tracking_uncertain=true;
    Check(!SnapshotWorkReceiptLocked().completion_observed,"uncertain lifetime blocks new completion evidence");
    g_lifecycle_tracking_uncertain=false;
    a=SnapshotWorkReceiptLocked();
    Check(a.completion_observed && a.completed_observed_ms==102,"exact matching fence observed without wait");
    old.completed=100;now=110;
    Check(SnapshotWorkReceiptLocked().completed_observed_ms==102,"first observation time preserved");
    // Frozen feedback copies remain unchanged while a later receipt is recorded.
    g_applied_nr_identity.frame_id=24;context.pending_recording_token=18;context.width=480;
    slot.last_used_serial=20;now=120;
    RecordPostWorkReceiptLocked(13,context,slot,640,360,1280,720);
    auto b=SnapshotWorkReceiptLocked();
    Check(b.route==Route::NativePostRr && b.nr_evaluation==24 && b.work.width==480,
          "next route and new dimensions form one new tuple");
    Check(!b.submission_observed && !b.completion_observed && b.fence_value==0,
          "old completion cannot carry into next receipt");
    Check(a.route==Route::NativePostSr && a.work.width==320 && a.nr_evaluation==23,
          "frozen capture copy cannot be changed by later recording");
    Submitted(g_recorded_work_receipt,17,29,100,121);
    Check(!g_recorded_work_receipt.submission_observed,"late prior submission cannot attach to new receipt");
    Submitted(g_recorded_work_receipt,18,30,8,121);
    now=122;
    Check(!SnapshotWorkReceiptLocked().completion_observed,"queue identity mismatch cannot prove completion");
    Fence current{8};g_lifecycle_queues[1]={30,&current};
    Check(SnapshotWorkReceiptLocked().completion_observed,"matched current queue completes new receipt");
    const auto before=g_recorded_work_receipt;
    g_applied_nr_identity.instance_id=99;
    RecordPostWorkReceiptLocked(1,context,slot,640,360,1280,720);
    Check(g_recorded_work_receipt.nr_evaluation==before.nr_evaluation,"wrong applied instance leaves last receipt");
    g_applied_nr_identity.instance_id=13;g_applied_nr_identity.config_generation=8;
    RecordPostWorkReceiptLocked(1,context,slot,640,360,1280,720);
    Check(g_recorded_work_receipt.nr_evaluation==before.nr_evaluation,"wrong applied config leaves last receipt");
    Receipt invalid=before;invalid.work.width=0;
    Check(!Record(g_recorded_work_receipt,invalid),"unknown extent never printed as valid zero");
    invalid=before;invalid.command_token=0;
    Check(!Record(g_recorded_work_receipt,invalid),"missing actual token is unsupported");
    invalid=before;invalid.work.width=1281;
    Check(!Record(g_recorded_work_receipt,invalid),"impossible post N rejected");
    // Native pre observation uses actual same-call Output contract, independent
    // of the work resolver's Reference early return. No shader/model is run.
    FeatureRecord feature;
    ID3D12Resource color{},output{};
    color.desc.Width=500;color.desc.Height=300;
    output.desc.Width=900;output.desc.Height=500;
    NgxParams params{{{"DLSS.Render.Subrect.Dimensions.Width",427},
                      {"DLSS.Render.Subrect.Dimensions.Height",240},
                      {"DLSS.Output.Subrect.Base.X",11},
                      {"DLSS.Output.Subrect.Base.Y",13},
                      {"DLSS.Output.Subrect.Dimensions.Width",854},
                      {"DLSS.Output.Subrect.Dimensions.Height",480}},&output};
    unsigned rw=0,rh=0;
    Check(ResolveRenderExtentFromEvaluate(&params,feature,&color,5,7,rw,rh) && rw==427 && rh==240,
          "actual Color active rect outranks stale Create dimensions");
    context.mode=2;context.width=427;context.height=240;context.pending_recording_token=31;
    g_applied_nr_identity={13,29,7};slot.last_used_serial=30;now=200;
    const auto original_params=params.values;
    RecordPreWorkReceiptLocked(&params,feature,context,slot,rw,rh);
    auto pre=g_recorded_work_receipt;
    Check(pre.valid && pre.route==Route::NativePreNr && pre.render.width==427 && pre.work.width==427 &&
          pre.render.height==240 && pre.work.height==240 && pre.output.width==854 && pre.output.height==480,
          "odd 100-percent pre N equals real R with independently observed O");
    Check(pre.nr_evaluation==29 && pre.command_token==31 && !pre.completion_observed &&
          params.values==original_params,"observation is read-only and not future SR completion");
    Submitted(g_recorded_work_receipt,31,30,12,201);current.completed=12;now=202;
    Check(SnapshotWorkReceiptLocked().completion_observed,"pre uses same exact signal/fence observation gate");
    params.values["DLSS.Render.Subrect.Dimensions.Width"]=320;
    params.values["DLSS.Render.Subrect.Dimensions.Height"]=180;
    params.values["DLSS.Output.Subrect.Dimensions.Width"]=640;
    params.values["DLSS.Output.Subrect.Dimensions.Height"]=360;
    Check(ResolveRenderExtentFromEvaluate(&params,feature,&color,5,7,rw,rh) && rw==320 && rh==180,
          "next DRS tuple resolves current render rect");
    context.width=160;context.height=88;context.pending_recording_token=33;
    g_applied_nr_identity.frame_id=32;slot.last_used_serial=31;now=210;
    RecordPreWorkReceiptLocked(&params,feature,context,slot,rw,rh);
    Check(g_recorded_work_receipt.render.width==320 && g_recorded_work_receipt.work.width==160 &&
          g_recorded_work_receipt.work.height==88 && g_recorded_work_receipt.output.width==640 &&
          !g_recorded_work_receipt.completion_observed,"next scaled pre tuple does not inherit old N/completion");
    Check(pre.work.width==427 && pre.nr_evaluation==29,"frozen prior 100-percent tuple retained by value");
    params.values.erase("DLSS.Output.Subrect.Dimensions.Width");
    params.values.erase("DLSS.Output.Subrect.Dimensions.Height");
    g_recorded_work_receipt={};
    RecordPreWorkReceiptLocked(&params,feature,context,slot,rw,rh);
    Check(!g_recorded_work_receipt.valid && context.width==160 && params.output==&output,
          "ambiguous nonzero atlas O suppresses receipt only");
    params.values["DLSS.Output.Subrect.Dimensions.Width"]=640;
    RecordPreWorkReceiptLocked(&params,feature,context,slot,rw,rh);
    Check(!g_recorded_work_receipt.valid,"partial O tuple is not invented");
    params.values["DLSS.Output.Subrect.Dimensions.Height"]=600;
    RecordPreWorkReceiptLocked(&params,feature,context,slot,rw,rh);
    Check(!g_recorded_work_receipt.valid,"O outside actual allocation rejected only for receipt");
    params.values["DLSS.Output.Subrect.Dimensions.Height"]=360;
    params.output=nullptr;
    RecordPreWorkReceiptLocked(&params,feature,context,slot,rw,rh);
    Check(!g_recorded_work_receipt.valid,"missing Output means unknown receipt");
    params.output=&output;output.desc.SampleDesc.Count=2;
    RecordPreWorkReceiptLocked(&params,feature,context,slot,rw,rh);
    Check(!g_recorded_work_receipt.valid,"unsupported Output sample contract is not a valid receipt");
    output.desc.SampleDesc.Count=1;feature.feature_id=0;
    RecordPreWorkReceiptLocked(&params,feature,context,slot,rw,rh);
    Check(!g_recorded_work_receipt.valid,"external still cannot supply downstream O");
    std::printf("work receipt: %u CPU cases passed; no GPU or waits\n",checks);
}
'''

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--compiler',required=True)
    ap.add_argument('--source-root',type=Path,default=ROOT)
    args=ap.parse_args();compiler=shutil.which(args.compiler)
    if not compiler: ap.error('compiler not found')
    os.environ['PATH']=str(Path(compiler).resolve().parent)+os.pathsep+os.environ.get('PATH','')
    source=(args.source_root/'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
    helpers='\n\n'.join(extract.function(source,s) for s in (
        'void RecordPostWorkReceiptLocked(', 'nr_work_receipt::Receipt SnapshotWorkReceiptLocked(',
        'bool TextureContainsRegion(', 'bool ResolveRenderExtentFromEvaluate(',
        'bool ResolveSrOutputExtent(', 'bool ResolveOutputExtentFromEvaluate(',
        'void RecordPreWorkReceiptLocked('))
    with tempfile.TemporaryDirectory(prefix='nr-work-receipt-') as directory:
        root=Path(directory);cpp=root/'test.cpp';exe=root/'test.exe'
        cpp.write_text(HARNESS.replace('HELPERS',helpers),encoding='utf-8')
        subprocess.run([compiler,'-std=c++20','-Wall','-Wextra','-Werror',
                        '-I'+str(args.source_root/'src'),str(cpp),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
    finish=extract.function(source,'bool FinishAfterSr(')
    assert finish.index('CompleteNrDiagnosticEvaluation(')<finish.index('KeepNativePostSrImage(')<finish.index('RecordVisibleTransferLocked(')<finish.index('RecordPostWorkReceiptLocked(')
    pre=extract.function(source,'Substitution PrepareBeforeSr(')
    assert pre.index('ResolveRenderExtentFromEvaluate(')<pre.index('ResolveNrWorkExtent(')<pre.index('CompleteNrDiagnosticEvaluation(')<pre.index('g_codec.combine_v2')<pre.index('RecordPreWorkReceiptLocked(')<pre.index('SetResource(substitution.params, "Color"')
    observer=extract.function(source,'void RecordPreWorkReceiptLocked(')
    assert 'SetBypassReason' not in observer and 'SetResource' not in observer and 'SetUInt' not in observer
    assert 'void RecordPreWorkReceiptLocked(' in observer and 'return substitution' not in observer
    batch=extract.function(source,'void CompleteSubmissionBatchLocked(')
    assert batch.index('nr_work_receipt::Submitted(')<batch.index('*record = {}')
    execute=extract.function(source,'void ProcessSubmittedCommandLists(')
    assert execute.index('ID3D12CommandQueue_Signal(')<execute.index('captured && signaled && batch.active && !batch.quarantine')<execute.index('CompleteSubmissionBatchLocked(')
    feedback=(args.source_root/'src/nr_feedback_core.inl').read_text(encoding='utf-8-sig')
    start=feedback.index('nr_work_receipt::Receipt work_receipt{};')
    tail=feedback[start:]
    assert tail.index('TryEnterCriticalSection')<tail.index('work_receipt=SnapshotWorkReceiptLocked();')<tail.index('LeaveCriticalSection')
    assert 'WaitFor' not in helpers and 'SetEventOnCompletion' not in helpers
    print('Production recording/signal/frozen-feedback ordering verified.')

if __name__=='__main__': main()
