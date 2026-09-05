"""Compile production core collector with OS scheduling/runtime seams, real file IO.

The core collector, log streaming, privacy policy and ZIP writer are included
verbatim. GPU/NGX is not simulated as success: its counters stay zero. The test
must complete without a further Present and without any stage/image request.
"""
from pathlib import Path
import argparse
import importlib.util
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
void FeedbackAppendActivity(FeedbackSession &s,const char *prefix){FeedbackAppend(s,"%s.nrSuccess=0\n%s.transferSubmitted=0\n%s.bypass=null-pso-policy-disabled\n",prefix,prefix,prefix);}
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
unsigned checks=0;
void check(bool ok,const char *message){++checks;if(!ok){std::fprintf(stderr,"FAIL: %s Win32=%lu\n",message,GetLastError());std::exit(1);}}
void drain(){while(!work.empty()){const auto w=work.front();work.erase(work.begin());w.fn(w.arg);}}
void put(const std::filesystem::path &path,const std::string &value){std::ofstream out(path,std::ios::binary);out<<value;check(out.good(),"write fixture");}
void save(const char *name){check(g_feedback_last_zip[0]!=0,"current ZIP exists");std::filesystem::copy_file(g_feedback_last_zip,std::filesystem::path(output_root)/name);}
void begin(reshade::api::effect_runtime &runtime){check(nr_core_feedback::Request(),"new core request");check(!g_feedback_last_zip[0],"new request clears old path");nr_core_feedback::Present(&runtime);}
int main(int argc,char **argv) {
    check(argc==2,"output directory supplied");output_root=std::filesystem::absolute(argv[1]).wstring();
    std::filesystem::create_directories(output_root);InitializeCriticalSection(&g_chain_cs);
    const auto log=std::filesystem::path(output_root)/"nr-before-sr.log";
    std::wstring logwide=log.wstring();wcscpy_s(g_log_path,logwide.c_str());
    std::string content="build=fixture\naddonPath=C:\\Users\\Private Tester\\Game\\mod.addon64 | valid=1\n";
    content+=std::string(90000,'a')+"\n";
    content+="NR bypass: reason=null-pso-policy-disabled token=00001234ABCDEF00\n";
    for(unsigned i=0;i<20000;++i)content+="Feature 1 native frame; NR success=0\n";
    put(log,content);
    reshade::api::effect_runtime runtime;
    begin(runtime);check(nr_core_feedback::Busy(),"worker owned by current session");
    const auto first=nr_core_feedback::SnapshotPair();check(!nr_core_feedback::Request(),"no overlapping core jobs");
    drain(); // no second Present, no NR frame, no image or GPU stage functions even exist in this harness
    check(g_feedback_status==3 && !nr_core_feedback::Busy(),"zero-NR core completes independently");
    check(nr_core_feedback::SnapshotPair().complete,"core completion recorded");
    check(g_feedback_session==nullptr,"observer pointer retired before worker deletion");
    save("valid-core.zip");
    std::ifstream summary(std::filesystem::path(output_root)/"core-1/summary-0.txt",std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(summary)),{});
    check(text.find("null-pso-policy-disabled")!=std::string::npos,"first bypass survives long later success tail");
    check(text.find("Private Tester")==std::string::npos && text.find("ABCDEF00")==std::string::npos,"shared excerpts redact paths and pointers");
    begin(runtime);check(std::strcmp(first.session,nr_core_feedback::SnapshotPair().session)!=0,"new session ID differs");nr_core_feedback::Cancel();drain();
    check(g_feedback_status==4 && !nr_core_feedback::SnapshotPair().complete,"cancelled core is explicitly partial");save("cancelled-core.zip");
    snapshot_busy=true;begin(runtime);drain();snapshot_busy=false;
    check(g_feedback_status==4,"unavailable config lock yields partial, not an invented snapshot");save("busy-core.zip");
    queue_fail=true;begin(runtime);queue_fail=false;
    check(g_feedback_status==5 && !g_feedback_last_zip[0] && !g_feedback_session,"queue failure releases publication with no stale ZIP");
    pin_fail=true;begin(runtime);pin_fail=false;
    check(g_feedback_status==5 && !g_feedback_last_zip[0],"pin failure does not expose previous path");
    directory_fail=true;begin(runtime);drain();directory_fail=false;
    check(g_feedback_status==5 && !g_feedback_last_zip[0],"directory failure has no previous artifact");
    nr_f8::g_status=nr_f8::Status::Running;
    wcscpy_s(nr_f8::g_last_zip,L"previous-image.zip");
    check(!nr_f8::RequestCore() && nr_f8::g_last_zip[0],"active image request is not cleared by rejected core request");
    nr_f8::g_status=nr_f8::Status::Done;
    check(nr_f8::RequestCore(),"core request accepted after completed images");
    check(!nr_f8::g_last_zip[0] && nr_f8::g_status==nr_f8::Status::Idle,"new core clears old image path and completion status");
    nr_core_feedback::Present(&runtime);drain();
    check(g_feedback_status==3,"fresh core completes after image-result reset");
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
    args = p.parse_args()
    source = (ROOT/'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
    legacy = (ROOT/'src/nr_feedback_capture.inl').read_text(encoding='utf-8-sig')
    values = {
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
    out = ROOT/'test-output/core-feedback'
    out.mkdir(parents=True, exist_ok=True)
    cpp, exe = out/'core.cpp', out/'core.exe'
    cpp.write_text(text, encoding='utf-8')
    subprocess.run([args.compiler, '-std=c++20', '-O0', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    '-static', '-I', str(ROOT/'src'), str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe), str(out/'fixtures')], check=True)
    spec = importlib.util.spec_from_file_location('core_parser', ROOT/'scripts/parse-core-feedback.py')
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    for name in ('valid-core.zip','cancelled-core.zip','busy-core.zip'):
        result = module.inspect_core(out/'fixtures'/name)
        assert result['required_complete'] == (name == 'valid-core.zip'), result
    print('Actual Windows core ZIPs independently verified (complete / cancelled / snapshot busy).')

if __name__ == '__main__': main()
