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
#include "nr_work_receipt.h"
struct NrContext {
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
        'void RecordPostWorkReceiptLocked(', 'nr_work_receipt::Receipt SnapshotWorkReceiptLocked('))
    with tempfile.TemporaryDirectory(prefix='nr-work-receipt-') as directory:
        root=Path(directory);cpp=root/'test.cpp';exe=root/'test.exe'
        cpp.write_text(HARNESS.replace('HELPERS',helpers),encoding='utf-8')
        subprocess.run([compiler,'-std=c++20','-Wall','-Wextra','-Werror',
                        '-I'+str(args.source_root/'src'),str(cpp),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
    finish=extract.function(source,'bool FinishAfterSr(')
    assert finish.index('CompleteNrDiagnosticEvaluation(')<finish.index('KeepNativePostSrImage(')<finish.index('RecordVisibleTransferLocked(')<finish.index('RecordPostWorkReceiptLocked(')
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
