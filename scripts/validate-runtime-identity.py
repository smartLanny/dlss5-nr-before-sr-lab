"""Real worker, local JSON and self-file hash, including a 64 KiB worker stack."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

p=argparse.ArgumentParser();p.add_argument('--compiler',required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[1]
runtime=(root/'src/nr_runtime_status.inl').read_text().split('void QueueRuntimeStatus()')[0]
identity=(root/'src/nr_runtime_identity.inl').read_text()
h=r'''
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <new>
#include "nr_diagnostic_bundle.inl"
constexpr const char*kBuildIdentity="fixture";
constexpr const char*kSourceCommit="0123456789012345678901234567890123456789";
constexpr bool kSourceDirty=false;
enum class BypassReason{None};const char*BypassReasonName(BypassReason){return "none";}
template<class...A>void Log(const char*,A...){}
void WideToUtf8(const wchar_t*s,char*d,std::size_t n){WideCharToMultiByte(CP_UTF8,0,s,-1,d,static_cast<int>(n),nullptr,nullptr);}
IDENTITY
RUNTIME
int main(){
 auto *job=new RuntimeStatusJob{};job->sample={3,12,5,4,2,true,false,true};job->active=true;
 GetSystemTimeAsFileTime(&job->sampled_at);
 HANDLE t=CreateThread(nullptr,65536,RuntimeStatusWorker,job,STACK_SIZE_PARAM_IS_A_RESERVATION,nullptr);
 if(!t||WaitForSingleObject(t,30000)!=WAIT_OBJECT_0)return 10;CloseHandle(t);
 if(g_runtime_status_worker.load())return 11;
 const auto* first=&RuntimeIdentitySnapshot();
 if(first!=&EnsureRuntimeIdentityOnWorker()||std::strcmp(first->status,"verified")!=0)return 12;
 wchar_t *wide=new wchar_t[32768];std::fill_n(wide,32767,L'中');wide[32767]=0;
 char *encoded=new char[196608];
 if(!RuntimeStatusEscape(wide,encoded,196608)||std::strlen(encoded)!=32767*6)return 13;
 char small[6];if(RuntimeStatusEscape(L"中",small,sizeof(small)))return 14;
 delete[]wide;delete[]encoded;
 std::puts("real worker: 64KiB stack / immutable once identity / full UTF16 boundary PASS");return 0;
}
'''.replace('IDENTITY',identity).replace('RUNTIME',runtime)
with tempfile.TemporaryDirectory(prefix='nr-runtime-') as td:
    folder=Path(td)/'中文 空格';folder.mkdir()
    cpp=folder/'runtime.cpp';exe=folder/'实际插件.exe';cpp.write_text(h,encoding='utf-8')
    subprocess.run([a.compiler,'-std=c++20','-O2','-static','-fstack-usage','-I'+str(root/'src'),str(cpp),'-o',str(exe)],check=True)
    expected=hashlib.sha256(exe.read_bytes()).hexdigest()
    env=os.environ.copy();env['LOCALAPPDATA']=str(folder)
    subprocess.run([str(exe)],env=env,check=True)
    files=list((folder/'DLSS5InstallAssistant/runtime').glob('*.json'));assert len(files)==1
    result=json.loads(files[0].read_text())
    assert result['schema']=='dlss5-runtime-status/v2'
    assert Path(result['addonPath'])==exe and Path(result['executable'])==exe
    assert result['addonSha256']==expected and result['artifactId']=='sha256:'+expected
    assert result['addonIdentityStatus']=='verified' and result['sourceDirty'] is False
    assert result['sourceCommit']=='0123456789012345678901234567890123456789'
    assert result['active'] is True and result['nrEvaluations']==5 and result['transferSubmitted']==4
    print('real local status JSON: exact self-file SHA256 / loaded path / source identity / activity PASS')
