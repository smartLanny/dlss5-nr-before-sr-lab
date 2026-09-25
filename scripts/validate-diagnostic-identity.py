"""Run extracted production diagnostic and success-gate code without a GPU."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def definition(source, marker, trailing_semicolon=False):
    start = source.index(marker)
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end + int(trailing_semicolon)]


parser = argparse.ArgumentParser()
parser.add_argument("--compiler", required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
source = (root / "src/nr_before_sr.cpp").read_text(encoding="utf-8-sig")


def completion_path(function_marker, completion_call):
    function = definition(source, function_marker)
    start = function.index("    if (evaluated != kNgxSuccess)")
    end = function.index(completion_call, start) + len(completion_call)
    return function[start:end]


binding = definition(source, "bool BindNrParameters(")
reset_start = binding.index("    const unsigned diagnostic_reset_reasons =")
reset_end = binding.index('    SetInt(params, "DLSSNR.Reset", reset_requested ? 1 : 0);', reset_start)
reset_end += len('    SetInt(params, "DLSSNR.Reset", reset_requested ? 1 : 0);')
harness = r'''
#include <atomic>
#include <cstdio>
#include "nr_guide_contract.h"
#include "nr_jitter_status.h"
enum class NrJitterMode : int { Legacy=0, GameInputPixel=1 };
TUNING
IDENTITY
struct Config {
    unsigned warmup_after_create=2;
    bool suppress_reset_frame=true;
} g_config;
std::atomic<unsigned> g_config_generation{7};
unsigned long long g_next_nr_frame_id=0;
NrDiagnosticIdentity g_applied_nr_identity{};
int g_applied_nr_mode=0;
struct NrContext {
    unsigned long long instance_id=10,feature_generation=8;
    bool jitter_binding_written=true,jitter_last_known=true;
    unsigned successful_evaluations_since_reset=0, evaluations=0, config_generation=7;
    bool diagnostic_reset_requested=false, reset_retry_pending=false;
    bool diagnostic_warmup_reset=false;
    unsigned diagnostic_reset_reasons=0;
    bool lifecycle_dependencies_quarantined=false;
    NrTuningSignature tuning_signature{};
    NrGuideHistory guide_history{};
    int mode=2;
};
struct NrSlot {
    bool delta_stats_capture_armed=true;
    bool suppress_transfer_this_frame=false;
    struct { bool reset=false; } capture_params;
    NrDiagnosticIdentity diagnostic_identity{};
    struct { bool armed=true; struct { NrDiagnosticIdentity identity{}; } info; } stage_capture;
};
unsigned long long GetTickCount64() { return 100; }
nr_jitter_status::Snapshot g_recorded_jitter{};
JITTER_RECORD
COMPLETE
constexpr unsigned kNgxSuccess=1;
enum class BypassReason { RuntimeRejected };
void Log(const char*,...) {}
void SetBypassReason(BypassReason) {}
const char* ResultName(unsigned) { return "stub"; }
struct NrGuideBinding { NrGuideSignature signature{}; };
struct PostSrSubstitution { NrGuideBinding guides{}; bool opaque_failure_is_fatal=false; unsigned fatal_recording=0; };
namespace nr_face_runtime {
void Commit(NrSlot&, const NrGuideBinding&) {}
}
unsigned preserve_calls=0;
void PreservePostSrOnFailure(void*,PostSrSubstitution&) { ++preserve_calls; }
std::atomic<bool> g_post_sr_runtime_rejected{false};
bool PreComplete(NrContext* context,NrSlot* slot,unsigned evaluated) {
    const bool substitution=false;
    NrGuideBinding guides{};
    struct { bool safe=true; } timing;
    (void)timing;
    PRE_PATH
    return true;
}
bool PostComplete(NrContext& context,NrSlot& slot,unsigned evaluated) {
    void* command_list=nullptr;
    PostSrSubstitution substitution{};
    struct { bool safe=true; } timing;
    (void)timing;
    POST_PATH
    return true;
}
struct NgxParams { int reset_value=-1; };
void SetInt(NgxParams* params,const char*,int value) { params->reset_value=value; }
int BindReset(NrContext& context,bool config_changed,bool color_contract_reset=false,
              bool guide_reset=false,bool* suppressed=nullptr,bool placeholder=false) {
    bool reset=false; int game_reset=0;
    NrGuideSignature s{};s.rr_depth_placeholder=placeholder;
    const bool history_changed=config_changed;
    NrSlot slot{};
    NgxParams block{}; NgxParams* params=&block;
    RESET_BINDING
    if (suppressed != nullptr)
        *suppressed=slot.suppress_transfer_this_frame;
    return block.reset_value;
}
int failures=0;
void check(bool ok,const char* label) { if(!ok) { ++failures; std::printf("FAIL: %s\n",label); } }
int main() {
    NrContext a{}; NrSlot slot{};
    a.tuning_signature.style=1; a.tuning_signature.intensity=1;
    bool suppressed=false;
    check(BindReset(a,true,false,false,&suppressed)==1,"history transition requests reset");
    check(suppressed,"reset frame suppresses visible V2 correction by default");
    g_config.suppress_reset_frame=false;
    suppressed=true;
    check(BindReset(a,true,false,false,&suppressed)==1 && !suppressed,
          "reset suppression can be explicitly disabled");
    g_config.suppress_reset_frame=true;
    check(!PreComplete(&a,&slot,0xBAD00001u),"pre-SR failure returns without success advancement");
    check(a.evaluations==0 && a.successful_evaluations_since_reset==0 && g_next_nr_frame_id==0,"failed pre-SR Evaluate creates no diagnostic frame");
    check(a.reset_retry_pending && BindReset(a,false)==1,"failed Evaluate retains consumed config Reset until retry");
    check(PreComplete(&a,&slot,kNgxSuccess),"successful pre-SR Evaluate passes success gate");
    check(!a.reset_retry_pending && a.successful_evaluations_since_reset==1,"successful Reset starts since-reset count at one and consumes retry");
    check(slot.diagnostic_identity.instance_id==10 && slot.diagnostic_identity.config_generation==7 && slot.diagnostic_identity.since_reset==1 && !slot.diagnostic_identity.warmed_up,"armed diagnostic snapshots effective instance/config/warmup");
    check(slot.stage_capture.info.identity.frame_id==slot.diagnostic_identity.frame_id,"stage capture and statistics use identical successful-frame identity");
    auto captured=slot.diagnostic_identity;
    g_config_generation.store(99); a.tuning_signature.style=2; a.tuning_signature.intensity=2;
    check(slot.diagnostic_identity.config_generation==7 && slot.diagnostic_identity.tuning.style==1 && slot.diagnostic_identity.tuning.intensity==1,"completed slot snapshot survives later requested config/tuning edits");
    slot.delta_stats_capture_armed=false; slot.stage_capture.armed=false;
    BindReset(a,false); PreComplete(&a,&slot,kNgxSuccess);
    check(slot.diagnostic_identity.frame_id==captured.frame_id && slot.stage_capture.info.identity.frame_id==captured.frame_id,"unarmed slot identity is never overwritten by a later evaluation");
    check(g_applied_nr_identity.config_generation==a.config_generation,"applied identity reports bound configuration rather than a later global request");
    BindReset(a,false); PreComplete(&a,&slot,kNgxSuccess);
    check(g_applied_nr_identity.since_reset==3 && g_applied_nr_identity.warmed_up,"warmup becomes true only after two successful evaluations");
    NrContext b{}; NrSlot second{}; b.instance_id=11; b.mode=1; b.config_generation=8;
    const auto previous_frame=g_next_nr_frame_id;
    check(BindReset(b,false,true)==1,"color contract transition requests Reset");
    check(!PostComplete(b,second,0xBAD00001u),"post-SR failure returns through preservation path");
    check(preserve_calls==1 && b.evaluations==0 && g_next_nr_frame_id==previous_frame && b.reset_retry_pending,"failed post-SR Evaluate preserves output and diagnostic/reset state");
    BindReset(b,false); PostComplete(b,second,kNgxSuccess);
    check(second.diagnostic_identity.frame_id==previous_frame+1 && second.diagnostic_identity.instance_id==11 && g_applied_nr_mode==1,"successful frame ids are monotonic across modes and instances");
    check(second.diagnostic_identity.since_reset==1 && !second.diagnostic_identity.warmed_up,"new context resets warmup independently");
    BindReset(a,false,false,true); PreComplete(&a,&slot,kNgxSuccess);
    check(g_applied_nr_identity.since_reset==1 && !g_applied_nr_identity.warmed_up,"successful guide Reset restarts warmup even on an old instance");
    const auto previous_jitter_frame = g_recorded_jitter.frame;
    a.jitter_binding_written = false;
    RecordSuccessfulJitterBinding(a, 9999);
    check(g_recorded_jitter.frame == previous_jitter_frame,
          "unwritten binding cannot publish a jitter sample");
    a.jitter_binding_written = true; a.jitter_last_known = false;
    RecordSuccessfulJitterBinding(a, 9998);
    check(g_recorded_jitter.frame == 9998 && !g_recorded_jitter.known &&
          g_recorded_jitter.instance == a.instance_id &&
          g_recorded_jitter.config == a.config_generation,
          "actual jitter publication freezes identity and missing state");
    std::printf("diagnostic identity regression: %s (%d failures)\n",failures?"FAIL":"PASS",failures);
    return failures ? 1:0;
}
'''
replacements = {
    "TUNING": definition(source, "struct NrTuningSignature", True),
    "IDENTITY": definition(source, "struct NrDiagnosticIdentity", True),
    "COMPLETE": definition(source, "void CompleteNrDiagnosticEvaluation("),
    "JITTER_RECORD": definition((root / "src/nr_jitter_status.inl").read_text(encoding="utf-8"), "void RecordSuccessfulJitterBinding("),
    "PRE_PATH": completion_path("Substitution PrepareBeforeSr(", "CompleteNrDiagnosticEvaluation(*context, *slot);"),
    "POST_PATH": completion_path("bool FinishAfterSr(", "CompleteNrDiagnosticEvaluation(context, slot);"),
    "RESET_BINDING": binding[reset_start:reset_end],
}
for key, value in replacements.items():
    harness = harness.replace(key, value)
with tempfile.TemporaryDirectory(prefix="dlss5-diagnostic-regression-") as tmp:
    cpp, exe = Path(tmp) / "test.cpp", Path(tmp) / "test.exe"
    cpp.write_text(harness, encoding="utf-8")
    subprocess.run([args.compiler, "-std=c++20", "-O0", "-static", "-I", str(root / "src"), str(cpp), "-o", str(exe)], check=True)
    raise SystemExit(subprocess.run([str(exe)]).returncode)
