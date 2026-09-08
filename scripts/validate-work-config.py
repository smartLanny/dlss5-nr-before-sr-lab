#!/usr/bin/env python3
"""Focused Win32 INI migration/rejection checks; no GPU or NR execution."""
from pathlib import Path
import argparse
import importlib.util
import os
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('extract',ROOT/'scripts/extract-post-gpu.py')
extract=importlib.util.module_from_spec(spec)
spec.loader.exec_module(extract)

HARNESS=r'''
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <iterator>
#include <limits>
#include "nr_work_config.h"
wchar_t g_ini_path[MAX_PATH]={};
struct Config { int work_mode=0; float custom_work_scale=1; } g_config;
bool g_work_config_loaded=false;
std::atomic<bool> g_work_request_rejected{false},g_config_persistence_failed{false};
std::atomic<unsigned long> g_config_persistence_error{0};
void Log(const char*,...) {}
HELPERS
unsigned checks=0;
void Check(bool value,const char* message) {
    ++checks;
    if (!value) { std::printf("FAIL %s\n",message); std::exit(1); }
}
void ResetFile(const wchar_t* name) {
    wcscpy_s(g_ini_path,name);
    // The caller owns this newly created private test directory only.
    DeleteFileW(g_ini_path);
    g_work_config_loaded=false;
    g_config={};
    g_config_persistence_failed=false;
    g_config_persistence_error=0;
}
void Previous(int mode,float scale) {
    g_config={mode,scale};g_work_config_loaded=true;
}
int wmain(int argc,wchar_t** argv) {
    if (argc!=3) return 2;
    using namespace nr_work_config;
    ResetFile(argv[1]);
    Check(IsNewWorkConfigFile(),"absent file proven before migration writes");
    auto request=ReadWorkRequest(IsNewWorkConfigFile());
    Check(request.work.mode==0 && request.work.scale==1,"new file exact Reference");
    WriteIniInt(L"ConfigVersion",4); // Other migration may create file later.
    PersistWorkControl(request.work,false);
    Previous(request.work.mode,request.work.scale);
    request=ReadWorkRequest(false);
    Check(request.work.mode==0 && request.work.scale==1,"new defaults survive reload");
    ResetFile(argv[1]);
    WriteIniInt(L"Enabled",1); // Old file with both work keys absent.
    Check(!IsNewWorkConfigFile(),"missing key does not mean new install");
    request=ReadWorkRequest(false);
    Check(request.work.mode==1 && request.work.scale==.75f,"old missing keys retain legacy Quality");
    for (int mode=0;mode<=5;++mode) for (float scale:{.5f,.67f,.75f,1.f}) {
        WriteIniInt(L"WorkMode",mode);WriteIniFloat(L"CustomWorkScale",scale);
        request=ReadWorkRequest(false);
        Check(request.status==Status::Accepted && request.work.mode==mode && request.work.scale==scale,
              "explicit old preset/custom exact float retained");
    }
    // Initial load, not just reload: invalid explicit custom is rejected and
    // uses the labelled legacy default, never a fabricated 50% request.
    WriteIniInt(L"WorkMode",5);WriteIniFloat(L"CustomWorkScale",.25f);
    g_work_config_loaded=false;
    request=ReadWorkRequest(false);
    Check(request.status==Status::Rejected && request.work.mode==1 && request.work.scale==.75f,
          "initial invalid custom uses declared legacy fallback");
    Previous(5,.67f);
    for (const wchar_t* invalid:{L"0.49",L"0.499999",L"-1",L"1.01",L"nan",L"inf",
                               L"",L"0.75junk",L"0.75%",L"garbage",L"1e-9999",L"1e9999",
                               L"0.7500000000000000000000000000000000000000000000000000000000000000000000001"}) {
        WritePrivateProfileStringW(L"NRBeforeSR",L"CustomWorkScale",invalid,g_ini_path);
        request=ReadWorkRequest(false);
        Check(request.status==Status::Rejected && request.work.mode==5 && request.work.scale==.67f,
              "invalid reload retains previous work pair");
        wchar_t retained[160]={};
        GetPrivateProfileStringW(L"NRBeforeSR",L"CustomWorkScale",L"missing",retained,160,g_ini_path);
        Check(std::wcscmp(retained,invalid)==0,"rejection never rewrites invalid INI evidence");
    }
    for (const wchar_t* mode:{L"-1",L"6",L"1junk",L"",L"999999999999999999999999"}) {
        WritePrivateProfileStringW(L"NRBeforeSR",L"WorkMode",mode,g_ini_path);
        WriteIniFloat(L"CustomWorkScale",.75f);
        request=ReadWorkRequest(false);
        Check(request.status==Status::Rejected && request.work.mode==5 && request.work.scale==.67f,
              "invalid mode cannot be clamped to another preset");
    }
    WriteIniInt(L"WorkMode",5);WriteIniFloat(L"CustomWorkScale",0);
    request=ReadWorkRequest(false);
    Check(request.status==Status::Disable && request.work.scale==.67f,"zero keeps prior precision");
    PersistWorkControl(request.work,true);
    Check(GetPrivateProfileIntW(L"NRBeforeSR",L"Enabled",1,g_ini_path)==0,"zero disables master");
    WriteIniInt(L"Enabled",1); // Explicit later F6 re-enable.
    request=ReadWorkRequest(false);
    Check(request.status==Status::Accepted && request.work.scale==.67f &&
          GetPrivateProfileIntW(L"NRBeforeSR",L"Enabled",0,g_ini_path)==1,
          "consumed zero cannot disable subsequent F6 re-enable");
    request=Parse(L"1",L"0",{1,.75f});
    Check(request.status==Status::Rejected,"inactive zero is not a surprise master-off action");
    for (float bad:{-.1f,.49f,1.1f,std::numeric_limits<float>::quiet_NaN(),
                   std::numeric_limits<float>::infinity()}) {
        request=Resolve({5,bad},{5,.75f});
        Check(request.status==Status::Rejected && request.work.scale==.75f,
              "ApplyConfig numeric path rejects without clamp");
    }
    // A nonexistent parent forces real Win32 writes to fail. The caller never
    // creates that parent, so no payload can be written outside the fixture.
    wcscpy_s(g_ini_path,argv[2]);g_work_config_loaded=false;
    Check(!IsNewWorkConfigFile(),"unknown parent/path is not proven-new file");
    PersistWorkControl({0,1},false);
    Check(g_config_persistence_failed && g_config_persistence_error!=0,
          "failed writes latch session-only state without claiming persistence");
    std::printf("work config: %u focused Win32/CPU cases passed; GPU/NR calls 0\n",checks);
}
'''

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--compiler',required=True)
    args=ap.parse_args()
    compiler=shutil.which(args.compiler)
    if not compiler: ap.error('compiler not found')
    os.environ['PATH']=str(Path(compiler).resolve().parent)+os.pathsep+os.environ.get('PATH','')
    source=(ROOT/'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
    signatures=('bool WriteIniInt(', 'bool WriteIniFloat(')
    # Existing declarations precede definitions. Resolve their last occurrence.
    helpers=[]
    for signature in signatures:
        helpers.append(extract.function(source[source.rindex(signature):],signature))
    for signature in ('bool IsNewWorkConfigFile(', 'void RecordWorkRequestStatus(',
                      'void PersistWorkControl(', 'nr_work_config::Request ReadWorkRequest('):
        helpers.append(extract.function(source,signature))
    with tempfile.TemporaryDirectory(prefix='nr-work-config-') as directory:
        root=Path(directory);cpp=root/'test.cpp';exe=root/'test.exe'
        cpp.write_text(HARNESS.replace('HELPERS','\n\n'.join(helpers)),encoding='utf-8')
        subprocess.run([compiler,'-std=c++20','-Wall','-Wextra','-Werror','-municode',
                        '-I'+str(ROOT/'src'),str(cpp),'-o',str(exe)],check=True)
        subprocess.run([str(exe),str(root/'fixture.ini'),str(root/'absent'/'fixture.ini')],check=True)
    reload=extract.function(source,'void ReloadConfig(')
    assert reload.index('IsNewWorkConfigFile()')<reload.index('ReadConfiguredTransferStrength(')
    assert reload.index('ReadWorkRequest(')<reload.index('ReadConfiguredTransferStrength(')
    apply=extract.function(source,'bool ApplyConfig(')
    assert apply.index('EnterCriticalSection')<apply.index('nr_work_config::Resolve(')<apply.index('SameConfig(')
    normalize=extract.function(source,'void NormalizeConfig(')
    assert 'config.work_mode =' not in normalize and 'config.custom_work_scale =' not in normalize
    print('Production load ordering and locked ApplyConfig entry verified.')

if __name__=='__main__': main()
