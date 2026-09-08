"""Compile production core collector with OS scheduling/runtime seams, real file IO.

The core collector, log streaming, privacy policy and ZIP writer are included
verbatim. A synthetic historical tuning identity tests serialization only;
GPU/NGX is not simulated as success: its counters stay zero. The test
must complete without a further Present and without any stage/image request.
"""
from pathlib import Path
import argparse
import importlib.util
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def definition(source, marker, semicolon=False):
    start = source.index(marker)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end + int(semicolon)]

HARNESS = r'''
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <cstdarg>
#include <cstring>
#include <cwchar>
#include <new>
#include <vector>
#include <string>
#include <filesystem>
#include <fstream>
#include <memory>
#include "nr_diagnostic_bundle.inl"
namespace reshade { namespace api {
enum class device_properties { description,driver_version,vendor_id };
enum class device_api { d3d12=49152 };
struct device {
    bool get_property(device_properties p,void *out) const {
        if(p==device_properties::description)std::strcpy(static_cast<char *>(out),"TEST GPU (no NGX)");
        else *static_cast<std::uint32_t *>(out)=p==device_properties::driver_version?12345:4318;
        return true;
    }
    device_api get_api() const{return device_api::d3d12;}
};
struct effect_runtime {
    api::device d;
    api::device *get_device(){return &d;}
    void get_screenshot_width_and_height(std::uint32_t *w,std::uint32_t *h){*w=3840;*h=2160;}
};
} namespace internal { HMODULE get_reshade_module_handle(){return GetModuleHandleW(nullptr);} } }
enum class NrJitterMode : int {Legacy=0,GameInputPixel=1};
CHAIN
TUNING
IDENTITY
SESSION
ChainConfig g_config{};
NrDiagnosticIdentity g_applied_nr_identity{};
int g_applied_nr_mode=0;
CRITICAL_SECTION g_chain_cs;
SRWLOCK g_feedback_lock=SRWLOCK_INIT;
std::atomic<bool> g_feedback_requested{false};
std::atomic<unsigned> g_feedback_status{0},g_config_generation{7},g_feedback_swap_format{10},g_feedback_swap_color_space{1};
FeedbackSession *g_feedback_session=nullptr;
wchar_t g_feedback_last_zip[1024]={},g_log_path[1024]={};
const char *kSourceCommit="0123456789abcdef0123456789abcdef01234567", *kBuildIdentity="core-test-only";
const bool kSourceDirty=false,kChineseUi=false;
struct IdentityResult {char sha256[65]="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";const char *status="synthetic-file-hash";};
const IdentityResult &EnsureRuntimeIdentityOnWorker(){static IdentityResult identity;return identity;}
struct ColorPolicy {float requested_color_strength=0,effective_color_strength=0;int color_reason=0;};
struct ColorSnapshot {int status=0;ColorPolicy policy{};};
ColorSnapshot SnapshotColorPolicy(){return {};}
const char *NrColorPolicyStatusName(int){return "unavailable";}
const char *NrColorReasonName(int){return "unknown";}
APPEND
#define NR_HAS_COMMAND_STATE_EVIDENCE 1
#define NR_HAS_NGX_HOOK_EVIDENCE 1
COMMAND_EVIDENCE
SRWLOCK g_command_state_lock=SRWLOCK_INIT;
CommandStateEvidence g_command_state_evidence;
COMMAND_SNAPSHOT
HOOK_EVIDENCE
SRWLOCK g_ngx_hook_failure_lock=SRWLOCK_INIT;
NgxHookFailureEvidence g_ngx_hook_failure;
HOOK_SNAPSHOT
std::atomic<unsigned long long> g_nr_successful_evaluations{0},g_nr_transfer_submitted_frames{0},g_carrier_evaluations{0},g_rr_passthrough_evaluations{0},g_feature1_preflight_covered{0},g_feature1_preflight_attempts{0},g_command_signature_create_calls{0},g_command_signature_unknown_uses{0};
std::atomic<unsigned> g_live_rr_handles{0},g_rr_create_in_progress{0};
std::atomic<int> g_last_bypass_reason{0},g_last_carrier_feature{1},g_module_observer_fault{0};
std::atomic<bool> g_generic_renodx_conflict{false},g_preflight_core_loaded{true},g_preflight_dlssd_loaded{false},g_command_signature_hook_published{true},g_command_signature_start_deferred{false};
enum class BypassReason{Unknown};enum class ModuleObserverFault{None};
const char *BypassReasonName(BypassReason){return "command-state-unverified";}
const char *ModuleObserverFaultName(ModuleObserverFault){return "none";}
bool OtherNrOwnerActive(){return false;}
ACTIVITY
RELEVANT
STRENGTH
void WideToUtf8(const wchar_t *w,char *p,std::size_t n){WideCharToMultiByte(CP_UTF8,0,w,-1,p,static_cast<int>(n),nullptr,nullptr);}
std::wstring output_root;unsigned directory_id=0;
bool directory_fail=false,queue_fail=false,pin_fail=false,snapshot_busy=false;
ULONGLONG test_clock=100000;
bool FeedbackCreateDirectory(wchar_t (&path)[1024]) {
    if(directory_fail)return false;
    swprintf_s(path,L"%ls\\core-%u",output_root.c_str(),++directory_id);
    return CreateDirectoryW(path,nullptr)!=FALSE;
}
struct Work {DWORD(WINAPI *fn)(void *);void *arg;};std::vector<Work> work;
BOOL TestQueue(DWORD(WINAPI *fn)(void *),void *arg,ULONG){if(queue_fail)return FALSE;work.push_back({fn,arg});return TRUE;}
BOOL TestPin(DWORD,LPCWSTR,HMODULE *p){*p=GetModuleHandleW(nullptr);return !pin_fail;}
BOOL TestTryEnter(CRITICAL_SECTION *lock){return snapshot_busy?FALSE:TryEnterCriticalSection(lock);}
void TestSleep(DWORD ms){test_clock+=ms;}
#define QueueUserWorkItem TestQueue
#define GetModuleHandleExW TestPin
#define GetTickCount64() test_clock
#define TryEnterCriticalSection TestTryEnter
#define Sleep TestSleep
#include "nr_feedback_core.inl"
#undef QueueUserWorkItem
#undef GetModuleHandleExW
#undef GetTickCount64
#undef TryEnterCriticalSection
#undef Sleep
namespace nr_f8 {
enum class Status {Idle,Requested,Running,Packaging,Done};
std::atomic<Status> g_status{Status::Done};
SRWLOCK g_control=SRWLOCK_INIT,g_lock=SRWLOCK_INIT;
wchar_t g_last_zip[1024]={};
bool IsActive(){return g_status==Status::Requested || g_status==Status::Running;}
unsigned StageCaptureStatus(){return 0;}
REQUEST_CORE
}
LEGACY_REQUEST
unsigned checks=0;
void check(bool ok,const char *message){++checks;if(!ok){std::fprintf(stderr,"FAIL: %s Win32=%lu\n",message,GetLastError());std::exit(1);}}
void drain(){
    unsigned char before[sizeof(g_config)];std::memcpy(before,&g_config,sizeof(g_config));
    const auto generation=g_config_generation.load();
    while(!work.empty()){const auto w=work.front();work.erase(work.begin());w.fn(w.arg);}
    check(std::memcmp(before,&g_config,sizeof(g_config))==0 && g_config_generation==generation,
        "worker collection preserves all requested configuration bytes and generation");
}
void put(const std::filesystem::path &path,const std::string &value){std::ofstream out(path,std::ios::binary);out<<value;check(out.good(),"write fixture");}
void save(const char *name){check(g_feedback_last_zip[0]!=0,"current ZIP exists");std::filesystem::copy_file(g_feedback_last_zip,std::filesystem::path(output_root)/name);}
void begin(reshade::api::effect_runtime &runtime, bool archive=true){
    unsigned char before[sizeof(g_config)];std::memcpy(before,&g_config,sizeof(g_config));
    const auto generation=g_config_generation.load();
    check(archive ? nr_core_feedback::Request(true) : RequestFeedbackBundle(),"explicit F7 archive or legacy F8 TXT request");check(!g_feedback_last_zip[0],"new request clears old path");check(!nr_core_feedback::g_submission[0],"new request clears old ticket");check(!nr_core_feedback::g_full_report && !nr_core_feedback::g_report_path[0],"new request clears old TXT");check(nr_core_feedback::SnapshotPair().session[0] && g_feedback_status==1,"legacy request initializes session and parameter observer");nr_core_feedback::Present(&runtime);
    check(std::memcmp(before,&g_config,sizeof(g_config))==0 && g_config_generation==generation,
        "request and Present preserve all requested configuration bytes and generation");
}
void check_light_request(const std::string &report){
    for(const char *line:{"requested.lightStyleVersion=1\n","requested.lightPreset=2\n","requested.lightPreview=1\n",
        "requested.lightBroad=0.5\n","requested.lightDark=0.75\n","requested.lightReflection=1.25\n",
        "requested.lightStructure=1.5\n","requested.lightGlow=0.25\n","requested.lightPresetColor=0.625\n"})
        check(report.find(line)!=std::string::npos,"all D requested values survive actual TXT publication");
}
void check_applied_unavailable(const std::string &report,const char *status){
    check(report.find("applied.valid=0\n")!=std::string::npos &&
        report.find(std::string("applied.status=")+status+"\n")!=std::string::npos,
        "missing applied identity has an explicit validity and reason");
    for(const char *key:{"instance","frame","config","mode","evaluation","sinceReset","warmedUp","intensity",
        "localTone","localStructure","skin","autoMask","style","uiCorrection","jitterMode","inputFilter"})
        check(report.find(std::string("applied.")+key+"=")==std::string::npos,
            "unavailable historical identity never emits constructor or stale applied values");
}
int main(int argc,char **argv) {
    check(argc==3,"output directory and matched companion fixture supplied");output_root=std::filesystem::absolute(argv[1]).wstring();
    std::filesystem::create_directories(output_root);InitializeCriticalSection(&g_chain_cs);
    const auto log=std::filesystem::path(output_root)/"nr-before-sr.log";
    std::wstring logwide=log.wstring();wcscpy_s(g_log_path,logwide.c_str());
    std::string content="build=fixture\naddonPath=C:\\Users\\Private Tester\\Game\\mod.addon64 | valid=1\n";
    content+=std::string(90000,'a')+"\n";
    content+="NR bypass: reason=null-pso-policy-disabled token=00001234ABCDEF00\n";
    for(unsigned i=0;i<20000;++i)content+="Feature 1 native frame; NR success=0\n";
    put(log,content);
    const auto carrier_path=std::filesystem::absolute(argv[2]);
    HMODULE carrier=LoadLibraryW(carrier_path.c_str());
    check(carrier!=nullptr,"actual matching companion fixture loaded from separate directory");
    const auto carrier_log=carrier_path.parent_path()/"dlss5-bridge.log";
    std::string bridge_content;
    for(unsigned i=0;i<3000;++i)bridge_content+="ordinary carrier progress before diagnostic\n";
    bridge_content+="[native-carrier] private baseline unavailable; native SR retained.\n"
        "[native-carrier] real depth transport failed; native SR retained.\n"
        "addonPath=C:\\Users\\Private Tester\\carrier.addon64 | owner=fixture\n"
        "[native-contract] originalCreate=1920x1080 Evaluate=1280x720\n";
    for(unsigned i=0;i<3000;++i)bridge_content+="ordinary carrier progress after diagnostic\n";
    put(carrier_log,bridge_content);
    reshade::api::effect_runtime runtime;
    g_command_state_evidence.available=true;g_command_state_evidence.sampled_at=GetTickCount64();
    g_command_state_evidence.reason="null-guard-awaiting-reset";g_command_state_evidence.token=42;
    g_command_state_evidence.sequence=3;g_command_state_evidence.guard_requested=true;
    g_command_state_evidence.guard_covered=true;g_command_state_evidence.registered=true;
    g_command_state_evidence.baseline="Reset";
    g_ngx_hook_failure.available=true;g_ngx_hook_failure.sampled_at=GetTickCount64();
    std::strcpy(g_ngx_hook_failure.operation,"core EvaluateFeature");
    g_ngx_hook_failure.status=8;g_ngx_hook_failure.entry="existing-short-hotpatch";
    AcquireSRWLockExclusive(&g_command_state_lock);
    check(!SnapshotCommandStateEvidence().available,"command evidence contention is nonblocking");
    ReleaseSRWLockExclusive(&g_command_state_lock);
    AcquireSRWLockExclusive(&g_ngx_hook_failure_lock);
    check(!SnapshotNgxHookFailure().available,"hook evidence contention is nonblocking");
    ReleaseSRWLockExclusive(&g_ngx_hook_failure_lock);
    check(nr_core_feedback::Request(),"pre-Present request accepted");
    const auto cancelled_before_present=nr_core_feedback::SnapshotPair();
    nr_core_feedback::Cancel();
    check(!g_feedback_requested.load() && !nr_core_feedback::Busy() && g_feedback_status==5,
        "pre-Present cancellation retires pending request without another render callback");
    check(nr_core_feedback::SnapshotPair().ready && !nr_core_feedback::SnapshotPair().complete &&
        std::strstr(nr_core_feedback::g_reason,"before a render callback"),
        "pre-Present cancellation publishes terminal session-bound state");
    nr_core_feedback::Cancel();
    check(g_feedback_status==5 && std::strcmp(cancelled_before_present.session,
        nr_core_feedback::SnapshotPair().session)==0,
        "repeated cancellation cannot rewrite or revive the retired request");
    check(nr_core_feedback::Request(),"fresh request accepted after pre-Present cancellation");
    check(std::strcmp(cancelled_before_present.session,nr_core_feedback::SnapshotPair().session)!=0,
        "retry after pre-Present cancellation receives a new session identity");
    nr_core_feedback::Cancel();
    {
        g_command_state_evidence.first_recording_observed=true;
        g_command_state_evidence.baseline="Create-first-recording";
        g_command_state_evidence.reset_serial=0;g_command_state_evidence.fresh_reset=false;
        FeedbackSession first_recording;
        FeedbackAppendActivity(first_recording,"sample");
        check(std::strstr(first_recording.report,"sample.commandState.firstRecordingObserved=1\n"),"first recording provenance reaches F8");
        check(std::strstr(first_recording.report,"sample.commandState.baseline=Create-first-recording\n"),"first recording baseline is not relabeled Reset");
        check(std::strstr(first_recording.report,"sample.commandState.resetSerial=0\n") &&
            std::strstr(first_recording.report,"sample.commandState.freshReset=0\n"),"first recording F8 does not fabricate Reset proof");
        g_command_state_evidence.first_recording_observed=false;g_command_state_evidence.baseline="Reset";
        FeedbackSession unobserved;
        FeedbackAppendActivity(unobserved,"sample");
        check(std::strstr(unobserved.report,"sample.commandState.blend=unobserved\n"),"unobserved blend never masquerades as four zeroes");
        g_command_state_evidence.blend_observed=true;
        const float blend[4]={0.25f,0.5f,0.75f,1.0f};
        std::memcpy(g_command_state_evidence.blend,blend,sizeof(blend));
        FeedbackSession measured;
        FeedbackAppendActivity(measured,"sample");
        check(std::strstr(measured.report,"sample.commandState.blend=0.25,0.5,0.75,1\n"),"all four sampled blend values reach TXT formatting");
        AcquireSRWLockExclusive(&g_command_state_lock);
        FeedbackSession unavailable;
        FeedbackAppendActivity(unavailable,"sample");
        ReleaseSRWLockExclusive(&g_command_state_lock);
        check(std::strstr(unavailable.report,"sample.commandState.blend=unavailable\n"),"busy snapshot does not expose historical blend as current");
    }
    g_config.high_strength_protection=false;g_config.layered_enhancement=true;
    g_config.layer_tone=.5f;g_config.layer_structure=1.5f;g_config.layer_bright=.75f;g_config.layer_shadow=1.25f;
    g_config.light_preset=2;g_config.light_preview=1;g_config.light_broad=.5f;g_config.light_dark=.75f;
    g_config.light_reflection=1.25f;g_config.light_structure=1.5f;g_config.light_glow=.25f;g_config.light_preset_color=.625f;
    begin(runtime);check(nr_core_feedback::Busy(),"worker owned by current session");
    g_config.layer_bright=2.f; // Worker must serialize the earlier coherent snapshot.
    g_config.light_broad=1.75f;
    const auto first=nr_core_feedback::SnapshotPair();check(!nr_core_feedback::Request(),"no overlapping core jobs");
    drain(); // no second Present, no NR frame, no image or GPU stage functions even exist in this harness
    check(g_feedback_status==3 && !nr_core_feedback::Busy(),"zero-NR core completes independently");
    check(nr_core_feedback::SnapshotPair().complete,"core completion recorded");
    check(g_feedback_session==nullptr,"observer pointer retired before worker deletion");
    check(std::strstr(nr_core_feedback::g_submission,first.session)!=nullptr,"ticket bound to completed core session");
    check(std::strstr(nr_core_feedback::g_submission,"Private Tester")==nullptr,"ticket excludes local path");
    check(std::strstr(nr_core_feedback::g_submission,"end.commandState.reason=null-guard-awaiting-reset"),"actual ticket includes command-state reason");
    check(std::strstr(nr_core_feedback::g_submission,"end.ngxHookFailure.entry=existing-short-hotpatch"),"actual ticket includes historical hook shape");
    save("valid-core.zip");
    check(nr_core_feedback::g_full_report && nr_core_feedback::g_report_path[0],"standalone TXT and cached content published");
    wchar_t copied_path[1024]{};
    check(nr_core_feedback::TakeReportPathForClipboard(copied_path) &&
          std::filesystem::path(copied_path)==std::filesystem::path(nr_core_feedback::g_report_path),
          "successful atomic TXT publication exposes exactly its path for automatic clipboard copy");
    wchar_t claimed_path[1024]{};wcscpy_s(claimed_path,copied_path);
    check(!nr_core_feedback::TakeReportPathForClipboard(copied_path) &&
          nr_core_feedback::GetReportClipboardStatus()==nr_core_feedback::ReportClipboardStatus::Pending,
          "automatic TXT path is claimed once while copier result remains pending");
    nr_core_feedback::SetReportClipboardResult(L"C:\\stale-report.txt",true);
    check(nr_core_feedback::GetReportClipboardStatus()==nr_core_feedback::ReportClipboardStatus::Pending,
          "stale clipboard completion cannot mark a newer TXT copied");
    nr_core_feedback::SetReportClipboardResult(claimed_path,true);
    check(nr_core_feedback::GetReportClipboardStatus()==nr_core_feedback::ReportClipboardStatus::Copied,
          "UI may claim copied only after the platform copier reports success");
    const auto &report=*nr_core_feedback::g_full_report;
    check(report.find(first.session)!=std::string::npos && report.find("null-pso-policy-disabled")!=std::string::npos,"TXT carries this session and first-error log");
    check(report.find("Private Tester")==std::string::npos && report.find("ABCDEF00")==std::string::npos,"TXT privacy matches sanitized ZIP");
    check(report.find("text.complete=1")!=std::string::npos,"all included core summaries present in TXT");
    check(report.size()<64*1024 && report.size()<=nr_feedback_single::kTextLimit,"representative F8 TXT stays in tens of KiB and below hard limit");
    check(report.find("requested.highStrengthProtection=0\n")!=std::string::npos &&
          report.find("requested.layeredEnhancement=1\n")!=std::string::npos &&
          report.find("requested.layerBright=0.75\n")!=std::string::npos &&
          report.find("requested.layerShadow=1.25\n")!=std::string::npos,
          "TXT carries frozen 0.5 picture settings, not current globals");
    check_light_request(report);
    check_applied_unavailable(report,"no-successful-NR");
    std::ifstream txt(std::filesystem::path(nr_core_feedback::g_report_path),std::ios::binary);
    check(std::string((std::istreambuf_iterator<char>(txt)),{})==report,"disk TXT equals clipboard cache");
    txt.close();
    std::filesystem::copy_file(nr_core_feedback::g_report_path,std::filesystem::path(output_root)/"valid-core.txt");

    {
        nr_core_feedback::nr_feedback_report::Output blocked;
        const auto dir=std::filesystem::path(output_root)/"core-1";
        check(!nr_core_feedback::nr_feedback_report::WriteAtomic(dir.c_str(),"replacement",blocked),"TXT cannot overwrite this session's published report");
        check(blocked.error!=0 && blocked.path.empty(),"TXT error is explicit and has no false published path");
        check(g_feedback_last_zip[0] && g_feedback_status==3,"independent TXT failure preserves valid ZIP");
        check(!std::filesystem::exists(dir/"feedback-report.txt.tmp"),"failed TXT publication removes only owned temporary file");
    }
    std::ifstream summary(std::filesystem::path(output_root)/"core-1/summary-0.txt",std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(summary)),{});
    check(text.find("null-pso-policy-disabled")!=std::string::npos,"first bypass survives long later success tail");
    check(text.find("Private Tester")==std::string::npos && text.find("ABCDEF00")==std::string::npos,"shared excerpts redact paths and pointers");
    check(text.size()<=nr_feedback_text::kSummaryLimit && text.find("summary.limitBytes=24576")!=std::string::npos,
        "per-source summary advertises and obeys 24 KiB bound");
    check(text.find("latest.repetitionsSuppressed=19999")!=std::string::npos && text.find("count=19999")==std::string::npos,
        "repetitive feature lines are counted without producing unbounded output");
    {
        const auto capped=std::filesystem::path(output_root)/"capped-source.log";
        std::ofstream out(capped,std::ios::binary);
        const std::string block(65536,'p');
        for(unsigned i=0;i<513;++i){out<<"frame="<<i<<" progress count="<<i<<"\n";out.write(block.data(),block.size());out<<"\n";}
        out<<"NR bypass transition reason=tail-only-critical HRESULT=0x887A0006\n";out.close();
        std::string bounded;bool full=true;
        check(nr_core_feedback::ReadSummary(capped.c_str(),bounded,full,GetTickCount64()+3000),"Windows reader summarizes capped source");
        check(!full && bounded.find("scan.status=byte-limit")!=std::string::npos,"byte-limited source never claims complete scan");
        check(bounded.find("scan.prefixBytes=33554432")!=std::string::npos && bounded.find("scan.tailBytes=")!=std::string::npos,
            "Windows reader reports separate prefix and newest-tail coverage");
        check(bounded.find("reason=tail-only-critical")!=std::string::npos && bounded.find("0x887A0006")!=std::string::npos,
            "newest tail diagnostic and HRESULT survive capped prefix");
        check(bounded.size()<=nr_feedback_text::kSummaryLimit,"capped Windows summary obeys 24 KiB bound");
        std::string timed;bool timed_full=true;
        check(nr_core_feedback::ReadSummary(capped.c_str(),timed,timed_full,test_clock),"deadline-limited source still emits bounded metadata");
        check(!timed_full && timed.find("scan.status=time-limit")!=std::string::npos && timed.find("scan.status=read-error")==std::string::npos,
            "deadline exhaustion is distinct from a Windows read failure");
        const auto boundary=std::filesystem::path(output_root)/"boundary-source.log";
        out.open(boundary,std::ios::binary|std::ios::trunc);
        out.seekp(static_cast<std::streamoff>(nr_feedback_text::kScanLimit-1));out<<"\nNR bypass reason=first-contiguous-tail HRESULT=0x887A0005\n";out.close();
        bounded.clear();full=true;
        check(nr_core_feedback::ReadSummary(boundary.c_str(),bounded,full,test_clock+3000),"newline-boundary source summarized");
        check(!full && bounded.find("reason=first-contiguous-tail")!=std::string::npos,
            "first complete tail line is retained when prefix ends exactly at newline");
        const auto partial=std::filesystem::path(output_root)/"partial-boundary-source.log";
        const std::string partial_reason="NR bypass reason=partial-cut";
        out.open(partial,std::ios::binary|std::ios::trunc);
        out.seekp(static_cast<std::streamoff>(nr_feedback_text::kScanLimit-partial_reason.size()-1));
        out<<"\n"<<partial_reason<<"-continuation\nNR bypass reason=tail-after-partial\n";out.close();
        bounded.clear();full=true;
        check(nr_core_feedback::ReadSummary(partial.c_str(),bounded,full,test_clock+3000),"partial-boundary source summarized");
        check(!full && bounded.find("reason=partial-cut [line truncated]")!=std::string::npos &&
              bounded.find("partial-cut-continuation")==std::string::npos,
            "prefix fragment is marked truncated and never joined to skipped continuation");
        check(bounded.find("reason=tail-after-partial")!=std::string::npos,"complete diagnostic after skipped continuation retained");
    }
    begin(runtime);check(std::strcmp(first.session,nr_core_feedback::SnapshotPair().session)!=0,"new session ID differs");nr_core_feedback::Cancel();drain();
    check(g_feedback_status==4 && !nr_core_feedback::SnapshotPair().complete,"cancelled core is explicitly partial");save("cancelled-core.zip");
    snapshot_busy=true;begin(runtime);drain();snapshot_busy=false;
    check(g_feedback_status==4,"unavailable config lock yields partial, not an invented snapshot");save("busy-core.zip");
    queue_fail=true;begin(runtime);queue_fail=false;
    check(g_feedback_status==5 && !g_feedback_last_zip[0] && !g_feedback_session,"queue failure releases publication with no stale ZIP");
    check(!nr_core_feedback::g_submission[0],"failed queue exposes no previous ticket");
    pin_fail=true;begin(runtime);pin_fail=false;
    check(g_feedback_status==5 && !g_feedback_last_zip[0],"pin failure does not expose previous path");
    directory_fail=true;begin(runtime);drain();directory_fail=false;
    check(g_feedback_status==5 && !g_feedback_last_zip[0],"directory failure has no previous artifact");
    nr_f8::g_status=nr_f8::Status::Running;
    wcscpy_s(nr_f8::g_last_zip,L"previous-image.zip");
    check(!nr_f8::RequestCore() && nr_f8::g_last_zip[0],"active image request is not cleared by rejected core request");
    nr_f8::g_status=nr_f8::Status::Done;
    // Deliberately differ from the requested configuration; this is fixture data,
    // not an NGX success or evidence that the D composition reached a GPU.
    g_config.light_broad=.5f;g_config.style=1;g_config.intensity=.75f;g_config.mode=2;
    g_config.color_strength=.875f;
    g_applied_nr_identity.instance_id=73;g_applied_nr_identity.frame_id=84;g_applied_nr_identity.config_generation=6;
    g_applied_nr_identity.evaluation=19;g_applied_nr_identity.since_reset=3;g_applied_nr_identity.warmed_up=true;
    g_applied_nr_identity.tuning.intensity=1.75f;g_applied_nr_identity.tuning.local_tone=.5f;
    g_applied_nr_identity.tuning.local_structure=1.5f;g_applied_nr_identity.tuning.skin_structure=.25f;
    g_applied_nr_identity.tuning.auto_mask=true;g_applied_nr_identity.tuning.style=2;
    g_applied_nr_identity.tuning.ui_correction=true;g_applied_nr_identity.tuning.jitter_mode=NrJitterMode::Legacy;
    g_applied_nr_identity.tuning.input_filter=1;g_applied_nr_mode=1;
    check(nr_f8::RequestCore(),"core request accepted after completed images");
    check(!nr_f8::g_last_zip[0] && nr_f8::g_status==nr_f8::Status::Idle,"new core clears old image path and completion status");
    unsigned char requested_before[sizeof(g_config)];std::memcpy(requested_before,&g_config,sizeof(g_config));
    nr_core_feedback::Present(&runtime);
    check(std::memcmp(requested_before,&g_config,sizeof(g_config))==0 && g_config_generation==7,
        "F8 Present preserves requested values while reading historical tuning");
    g_applied_nr_identity={};g_applied_nr_mode=2;g_config.light_broad=1.75f;
    drain();
    check(g_feedback_status==3,"fresh core completes after image-result reset");
    check(!g_feedback_last_zip[0],"F8 never publishes a ZIP path");
    check(!nr_core_feedback::SnapshotPair().complete,"text-only capture cannot masquerade as a paired image core");
    check(nr_core_feedback::g_full_report && nr_core_feedback::g_report_path[0],"F8 standalone text saved");
    const auto named = std::filesystem::path(nr_core_feedback::g_report_path);
    unsigned siblings=0;
    for (const auto &entry : std::filesystem::directory_iterator(named.parent_path())) {
        ++siblings; check(entry.path()==named,"F8 has no ZIP, summaries or temporary siblings");
    }
    check(siblings==1,"successful F8 physically leaves exactly one TXT");
    check(named.filename().string().find("-core-test-only-")!=std::string::npos,"filename carries actual build");
    const auto fresh_report=*nr_core_feedback::g_full_report;
    check_light_request(fresh_report);
    for(const char *line:{"applied.valid=1\n","applied.status=historical-success\n",
        "applied.instance=73\n","applied.frame=84\n","applied.config=6\n","applied.mode=1\n",
        "applied.evaluation=19\n","applied.sinceReset=3\n","applied.warmedUp=1\n",
        "applied.intensity=1.75\n","applied.localTone=0.5\n","applied.localStructure=1.5\n","applied.skin=0.25\n",
        "applied.autoMask=1\n","applied.style=2\n","applied.uiCorrection=1\n","applied.jitterMode=0\n","applied.inputFilter=1\n",
        "requested.config=7\n","requested.mode=2\n","requested.style=1\n","requested.intensity=0.75\n","requested.color=0.875\n"})
        check(fresh_report.find(line)!=std::string::npos,"F8 exports frozen requested and complete historical tuning as distinct records");
    check(fresh_report.find("model tuning only, not D light composition or measured output")!=std::string::npos,
        "applied contract cannot claim actual D composition or visual quality");
    std::filesystem::copy_file(named,std::filesystem::path(output_root)/"applied-core.txt");
    check(fresh_report.find("format=DLSS5-CoreText-v2")!=std::string::npos,"single text schema");
    check(fresh_report.find("archive.requested=0")!=std::string::npos,"no false archive status");
    check(fresh_report.find("capture.localTime=")!=std::string::npos && fresh_report.find("capture.utcTime=")!=std::string::npos,"local and UTC timestamps recorded");
    check(fresh_report.find("null-pso-policy-disabled")!=std::string::npos,"standalone text retains first actual bypass");
    check(fresh_report.find("Private Tester")==std::string::npos,"standalone text retains privacy rules");
    check(fresh_report.find("carrierLog.status=matched-loaded-path")!=std::string::npos,"matched loaded companion path discovered by actual module enumeration");
    check(fresh_report.find("[native-carrier] real depth transport failed")!=std::string::npos &&
          fresh_report.find("[native-carrier] private baseline unavailable")!=std::string::npos &&
          fresh_report.find("[native-contract] originalCreate=1920x1080")!=std::string::npos,"F8 single TXT retains carrier failure and native contract from separate module directory");
    std::filesystem::remove(carrier_log);
    begin(runtime,false);drain();
    check(g_feedback_status==3 && nr_core_feedback::g_full_report && !g_feedback_last_zip[0],"missing optional bridge log leaves complete single TXT");
    check(nr_core_feedback::g_full_report->find("real depth transport failed")==std::string::npos,"missing log never reuses previous carrier contents");
    FreeLibrary(carrier);
    begin(runtime,false);drain();
    check(g_feedback_status==3 && nr_core_feedback::g_full_report->find("carrierLog.status=matching-companion-not-loaded")!=std::string::npos,"unloaded companion is explicit and not required for D3D12 feedback");
    begin(runtime,false);nr_core_feedback::Cancel();drain();
    check(g_feedback_status==4 && nr_core_feedback::g_full_report && !g_feedback_last_zip[0],"cancelled F8 keeps partial TXT, not ZIP or old results");
    g_applied_nr_identity.instance_id=99;g_applied_nr_identity.frame_id=101;g_applied_nr_mode=1;
    snapshot_busy=true;begin(runtime,false);drain();snapshot_busy=false;
    check(g_feedback_status==4 && nr_core_feedback::g_full_report->find("snapshot.valid=0")!=std::string::npos,"busy F8 snapshot explicit");
    check_applied_unavailable(*nr_core_feedback::g_full_report,"snapshot-unavailable");
    for(const char *key:{"config","enabled","mode","blend","color","workMode","workScale","lightingLock",
        "detailStability","edgeGuard","intensity","localTone","localStructure","skin","faceMask","uiCorrection","jitterMode","style",
        "lightStyleVersion","lightPreset","lightPreview","lightBroad","lightDark","lightReflection","lightStructure","lightGlow","lightPresetColor"})
        check(nr_core_feedback::g_full_report->find(std::string("requested.")+key+"=")==std::string::npos,
            "busy snapshot never exports default or later global requested values");
    check(nr_core_feedback::g_full_report->find("requested.pictureSnapshotValid=0\n")!=std::string::npos,
        "D requested snapshot explicitly unavailable");
    std::filesystem::copy_file(nr_core_feedback::g_report_path,std::filesystem::path(output_root)/"busy-core.txt");
    g_applied_nr_identity={};g_applied_nr_identity.instance_id=99; // Create without Evaluate is not success.
    directory_fail=true;begin(runtime,false);drain();directory_fail=false;
    check(g_feedback_status==5 && !nr_core_feedback::g_full_report && !nr_core_feedback::g_report_path[0],"failed F8 has no stale text");
    begin(runtime,false);drain();
    check(g_feedback_status==3 && std::filesystem::path(nr_core_feedback::g_report_path)!=named,"repeat F8 has a new filename");
    check_applied_unavailable(*nr_core_feedback::g_full_report,"no-successful-NR");
    check(std::filesystem::exists(named),"later F8 never overwrites previous evidence");
    // The generalized archive cap cannot be raised and handles underflow safely.
    auto *job=new nr_bundle::Job{};job->archive_limit=UINT64_MAX;
    check(nr_bundle::ArchiveLimit(*job)==nr_bundle::kArchiveLimit,"caller cannot raise global ZIP limit");
    job->archive_limit=8;job->archive_bytes=9;
    check(!nr_bundle::Write(*job,"x",1),"over-budget count rejected before subtraction underflow");delete job;
    const auto large=std::filesystem::path(output_root)/"large.txt";put(large,std::string(9*1024*1024,'x'));
    const auto budgetdir=std::filesystem::path(output_root)/"budget";std::filesystem::create_directory(budgetdir);
    job=nr_bundle::CreateJob(budgetdir.c_str(),"fixture=budget\n");check(job!=nullptr,"budget job created");
    job->archive_limit=nr_feedback_text::kCoreZipLimit;
    nr_bundle::AddFile(*job,large.c_str(),"too-large.txt",nr_bundle::FileKind::Copy,true);
    const auto result=nr_bundle::Run(*job);nr_bundle::DestroyJob(job);
    check(result.status==nr_bundle::Status::Partial && !result.required_complete,"oversized payload excluded with explicit missing record");
    check(std::filesystem::file_size(result.zip_path)<=nr_feedback_text::kCoreZipLimit,"physical ZIP never exceeds core budget");
    DeleteCriticalSection(&g_chain_cs);
    std::printf("production core coordinator and ZIP: %u checks passed; no NR/images/GPU seam was called\n",checks);
}
'''

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler', required=True)
    p.add_argument('--compile-only', action='store_true')
    args = p.parse_args()
    source = (ROOT/'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
    legacy = (ROOT/'src/nr_feedback_capture.inl').read_text(encoding='utf-8-sig')
    command = (ROOT/'src/nr_command_state.inl').read_text(encoding='utf-8-sig')
    values = {
        'LEGACY_REQUEST': definition(legacy, 'bool RequestFeedbackBundle('),
        'COMMAND_EVIDENCE': definition(command, 'struct CommandStateEvidence', True),
        'COMMAND_SNAPSHOT': definition(command, 'CommandStateEvidence SnapshotCommandStateEvidence('),
        'HOOK_EVIDENCE': definition(source, 'struct NgxHookFailureEvidence', True),
        'HOOK_SNAPSHOT': definition(source, 'NgxHookFailureEvidence SnapshotNgxHookFailure('),
        'ACTIVITY': definition(legacy, 'void FeedbackAppendActivity('),
        'CHAIN': definition(source, 'struct ChainConfig', True),
        'TUNING': definition(source, 'struct NrTuningSignature', True),
        'IDENTITY': definition(source, 'struct NrDiagnosticIdentity', True),
        'SESSION': definition(legacy, 'struct FeedbackSession', True),
        'APPEND': definition(legacy, 'void FeedbackAppend('),
        'RELEVANT': definition(legacy, 'bool FeedbackModuleRelevant('),
        'STRENGTH': definition(source, 'float ActiveTransferStrength('),
        'REQUEST_CORE': definition((ROOT/'src/nr_f8_sequence.inl').read_text(encoding='utf-8-sig'), 'bool RequestCore('),
    }
    text = HARNESS
    for key, value in values.items():
        text = text.replace('\n'+key+'\n', '\n'+value+'\n')
    # Production writes use CREATE_NEW. Preserve prior fixture evidence and
    # isolate each invocation so repeat validation never reuses old reports.
    import tempfile
    base = ROOT/'test-output/core-feedback'
    base.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='run-', dir=base))
    print(f'Core feedback validation output: {out}', flush=True)
    cpp, exe = out/'core.cpp', out/'core.exe'
    cpp.write_text(text, encoding='utf-8')
    subprocess.run([args.compiler, '-std=c++20', '-O0', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    '-static', '-I', str(ROOT/'src'), str(cpp), '-o', str(exe)], check=True)
    if args.compile_only:
        print('Windows harness cross-compiled; not executed.')
        return
    matching = definition((ROOT/'src/interop/nr_deployment_names.h').read_text(encoding='utf-8'), 'inline bool MatchingCarrier(')
    leaf = re.search(r'Equal\(name, L"([^"]+)"\)', matching).group(1)
    carrier_dir = out/'matched-companion'
    carrier_dir.mkdir(exist_ok=True)
    carrier_cpp, carrier_dll = carrier_dir/'fixture.cpp', carrier_dir/leaf
    carrier_cpp.write_text('extern "C" __declspec(dllexport) void FeedbackFixture() {}\n')
    subprocess.run([args.compiler, '-shared', '-static', str(carrier_cpp), '-o', str(carrier_dll)], check=True)
    subprocess.run([str(exe), str(out/'fixtures'), str(carrier_dll)], check=True)
    spec = importlib.util.spec_from_file_location('core_parser', ROOT/'scripts/parse-core-feedback.py')
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    for name in ('valid-core.zip','cancelled-core.zip','busy-core.zip'):
        result = module.inspect_core(out/'fixtures'/name)
        assert result['required_complete'] == (name == 'valid-core.zip'), result
    import zipfile
    with zipfile.ZipFile(out/'fixtures/valid-core.zip') as archive:
        bridge = archive.read('logs/carrier-summary.txt').decode('utf-8-sig')
        assert 'real depth transport failed' in bridge and 'Private Tester' not in bridge
        metadata = archive.read('capture-info.txt').decode('utf-8-sig')
        for key in ('start.commandState.schema=1', 'end.commandState.token=42',
                    'end.commandState.reason=null-guard-awaiting-reset',
                    'end.commandState.guardCovered=1', 'end.commandState.freshReset=0',
                    'start.commandState.blend=0.25,0.5,0.75,1',
                    'end.commandState.blend=0.25,0.5,0.75,1',
                    'end.ngxHookFailure.entry=existing-short-hotpatch',
                    'requested.highStrengthProtection=0', 'requested.layeredEnhancement=1',
                    'requested.layerBright=0.75', 'requested.layerShadow=1.25'):
            assert key in metadata, key
    print('Actual Windows core ZIPs independently verified (complete / cancelled / snapshot busy).')

if __name__ == '__main__': main()
