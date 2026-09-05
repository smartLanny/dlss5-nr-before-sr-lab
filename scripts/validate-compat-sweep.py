#!/usr/bin/env python3
"""Extracted production policies; real native hooks and GPU output are tested separately."""
from pathlib import Path
import argparse
import os
import re
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
def definition(text, marker, semicolon=False):
    start=text.index(marker);end=text.index('{',start)+1;depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end+int(semicolon)]
HARNESS=r'''
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "nr_hook_entry.h"
#include "nr_feedback_ticket.h"
using UINT=unsigned; using UINT64=std::uint64_t;
constexpr UINT64 kDynamicDepthBias=8192, kDynamicStripCut=16384;
struct NativeCommandState {
 bool known=false, null_pipeline_baseline=false, null_guard_covered=false;
 bool null_guard_reset_observed=false, null_guard_tainted=false;
 void *pipeline=nullptr,*raytracing_pipeline=nullptr;
 UINT unknown_domains=0;
 struct {UINT64 recorded=0; UINT vertex_mask=0,stream_mask=0;} dynamic;
};
AUTO
RESTORE
struct ChainConfig {bool enabled=true;float strength=1.2f;};
ChainConfig g_config;std::atomic<unsigned> g_config_generation{7};
int g_chain_cs=0,locks=0,applied=0,reloads=0;
namespace nr_f8 {std::atomic<bool> g_config_hold{false};}
unsigned checks=0;
void check(bool ok,const char *label) {++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",label);std::exit(1);}}
void EnterCriticalSection(int *){++locks;}
void LeaveCriticalSection(int *){--locks;}
void ReloadConfig(){check(locks==1,"reload is under snapshot lock");++reloads;}
bool late_capture=false,noop_apply=false;
bool ApplyConfig(ChainConfig next,const char *){check(locks==1,"generation test and commit indivisible");if(late_capture){late_capture=false;nr_f8::g_config_hold=true;return false;}if(noop_apply)return false;g_config=next;++g_config_generation;++applied;return true;}
SNAP_STRUCT
SNAPSHOT
RESULT
APPLY
int main() {
 NativeCommandState s;
 check(!CanAutoRestorePristineNull(s),"empty record not a baseline");
 s.known=true;s.null_pipeline_baseline=true;
 check(!CanAutoRestorePristineNull(s),"observed Reset without guards still rejected");
 s.null_guard_covered=true;
 check(!CanAutoRestorePristineNull(s),"guard upgrade not retroactive");
 s.null_guard_reset_observed=true;
 check(CanAutoRestorePristineNull(s),"fresh guarded pristine baseline");
 check(!CanRestoreNativeCommandState(s),"raw default null predicate remains closed");
 check(CanRestoreNativeCommandState(s,CanAutoRestorePristineNull(s)),"automatic proof feeds actual restore admission");
 const auto valid=s;
 for(UINT64 bit=1;bit!=0;bit<<=1) {
     s=valid;s.dynamic.recorded=bit;check(!CanAutoRestorePristineNull(s),"every recorded graphics domain rejects auto");
 }
 for(unsigned bit=0;bit<32;++bit) {
     s=valid;s.dynamic.vertex_mask=1u<<bit;check(!CanAutoRestorePristineNull(s),"any vertex slot rejects auto");
     s=valid;s.dynamic.stream_mask=1u<<bit;check(!CanAutoRestorePristineNull(s),"any stream-output slot rejects auto");
     s=valid;s.unknown_domains=1u<<bit;check(!CanAutoRestorePristineNull(s),"unknown domain rejects auto");
 }
 s=valid;s.null_guard_tainted=true;check(!CanAutoRestorePristineNull(s),"opaque command taint rejects");
 s=valid;s.pipeline=&s;check(!CanAutoRestorePristineNull(s) && CanRestoreNativeCommandState(s),"typed PSO still uses existing path");
 s=valid;s.raytracing_pipeline=&s;check(!CanAutoRestorePristineNull(s) && CanRestoreNativeCommandState(s),"DXR state does not become dummy PSO");
 s=valid;s.known=false;check(!CanAutoRestorePristineNull(s),"failed reset remains unknown");
 const auto snapshot=SnapshotOverlayConfig();
 check(snapshot.generation==7 && snapshot.config.enabled && locks==0 && reloads==1,"atomic UI snapshot");
 ChainConfig pending=snapshot.config;pending.strength=2.f;
 g_config.enabled=false;++g_config_generation;
 check(ApplyOverlayConfig(pending,snapshot.generation)==OverlayApplyResult::Conflict,"stale UI cannot undo F6 or a newer config");
 check(!g_config.enabled && applied==0 && locks==0,"conflict preserves live state and releases lock");
 const auto current=SnapshotOverlayConfig();pending=current.config;pending.strength=3.f;
 nr_f8::g_config_hold=true;
 check(ApplyOverlayConfig(pending,current.generation)==OverlayApplyResult::Busy,"capture lock defers pending edit");
 check(applied==0 && locks==0,"busy does not persist or leak lock");
 nr_f8::g_config_hold=false;
 late_capture=true;
 check(ApplyOverlayConfig(pending,current.generation)==OverlayApplyResult::Busy,"late capture start cannot falsely complete a refused edit");
 check(applied==0 && locks==0,"late refusal preserves live values");
 nr_f8::g_config_hold=false;
 check(ApplyOverlayConfig(pending,current.generation)==OverlayApplyResult::Applied,"pending edit can be retried after capture");
 check(applied==1 && !g_config.enabled && g_config.strength==3.f && locks==0,"retry preserves unrelated user change");
 check(ApplyOverlayConfig(pending,current.generation)==OverlayApplyResult::Conflict,"same stale generation cannot be applied twice");
 noop_apply=true;
 const auto before_noop=g_config_generation.load();
 check(ApplyOverlayConfig(g_config,before_noop)==OverlayApplyResult::Applied,"no-op does not falsely remain busy");
 check(g_config_generation==before_noop && applied==1 && locks==0,"no-op preserves state");
 using namespace nr_hook_entry;
 unsigned char entry[16]={0xEB,0xF9},prefix[5]={0xE9};
 const std::string before(reinterpret_cast<char *>(entry),sizeof(entry));
 check(Classify(nullptr,0,nullptr,0)==Kind::Unreadable,"null byte buffer is not dereferenced");
 check(Classify(entry,1,prefix,5)==Kind::Unreadable,"short read has no pattern claim");
 check(Classify(entry,2,prefix,4)==Kind::Ordinary,"hotpatch requires complete five-byte prefix");
 check(Classify(entry,2,prefix,5)==Kind::ShortHotpatch,"EB F9 and E9 evidence");
 check(std::strcmp(Name(Kind::ShortHotpatch),"existing-short-hotpatch")==0,"stable diagnostic vocabulary");
 check(before==std::string(reinterpret_cast<char *>(entry),sizeof(entry)),"classification never patches source");
 entry[0]=0xE9;check(Classify(entry,4,nullptr,0)==Kind::Ordinary,"truncated relative jump not classified");
 check(Classify(entry,5,nullptr,0)==Kind::RelativeJump,"relative jump metadata only");
 entry[0]=0xFF;entry[1]=0x25;check(Classify(entry,6,nullptr,0)==Kind::IndirectJump,"indirect pointer never followed");
 entry[0]=0x49;entry[1]=0xBA;check(Classify(entry,10,nullptr,0)==Kind::ContextRelay,"register-context prefix distinguished");
 for(unsigned n=0;n<16;++n) {
     unsigned char local[16]{};local[0]=0xEB;local[1]=0xF9;
     const auto k=Classify(local,n,prefix,n<5?n:5);
     check(n<2?k==Kind::Unreadable:n<5?k==Kind::Ordinary:k==Kind::ShortHotpatch,"all truncated-prefix boundaries");
 }
 char ticket[nr_feedback_ticket::kCapacity];
 const char *meta="session=DLSS5_CORE_9_9_9\nend.commandState.available=1\nend.commandState.ageMs=10\nend.commandState.reason=null-guard-awaiting-reset\nend.commandState.route=native-bypass\nend.ngxHookFailure.available=1\nend.ngxHookFailure.operation=core EvaluateFeature\nend.ngxHookFailure.status=8\nend.ngxHookFailure.entry=existing-short-hotpatch\nprivate.raw=SECRET\n";
 check(nr_feedback_ticket::Build(ticket,sizeof(ticket),meta,true),"expanded diagnostic ticket fits real fixed capacity");
 check(std::strstr(ticket,"existing-short-hotpatch") && std::strstr(ticket,"null-guard-awaiting-reset"),"ticket carries useful paired reasons");
 check(!std::strstr(ticket,"SECRET"),"new whitelist does not leak arbitrary metadata");
 check(nr_feedback_ticket::Build(ticket,sizeof(ticket),"session=new\n",false) && !std::strstr(ticket,"existing-short-hotpatch"),"absent new evidence never reuses old ticket");
 std::printf("PASS compatibility policies / UI transaction / hook entry / ticket: %u checks\n",checks);
}
'''
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--compiler',default=os.environ.get('CXX','c++'));a=p.parse_args()
    source=(ROOT/'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
    state=(ROOT/'src/nr_command_state.inl').read_text(encoding='utf-8-sig')
    ui=(ROOT/'src/nr_settings_ui.inl').read_text(encoding='utf-8-sig')
    fields={'AUTO':definition(state,'bool CanAutoRestorePristineNull('),'RESTORE':definition(state,'bool CanRestoreNativeCommandState('),
        'SNAP_STRUCT':definition(source,'struct OverlayConfigSnapshot',True),'SNAPSHOT':definition(source,'OverlayConfigSnapshot SnapshotOverlayConfig('),
        'RESULT':definition(source,'enum class OverlayApplyResult',True),'APPLY':definition(source,'OverlayApplyResult ApplyOverlayConfig(')}
    harness=HARNESS
    for name,value in fields.items():harness=harness.replace('\n'+name+'\n','\n'+value+'\n')
    with tempfile.TemporaryDirectory(prefix='nr-compat-') as td:
        cpp=Path(td)/'compat.cpp';exe=Path(td)/('compat.exe' if os.name=='nt' else 'compat');cpp.write_text(harness,encoding='utf-8')
        subprocess.run([a.compiler,'-std=c++20','-O0','-Wall','-Wextra','-Werror',*(['-static'] if os.name=='nt' else []),'-I',str(ROOT/'src'),str(cpp),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
    version=re.search(r'kAddonVersion = "([^"]+)"',source).group(1)
    names=re.findall(r'const char \*NAME =\s*"([^"]+)"',source)
    assert len(names)==2 and all(version in n for n in names)
    assert 'if (result != OverlayApplyResult::Busy) has_pending = false;' in ui
    assert 'pending_generation != current.generation' in ui
    assert source.count('OnCommandStateObserveSubmission(command_list);')==1
    assert 'g_command_state_registration_repair_needed.load' in state
    assert 'ExperimentalNullPsoCarrier=0' in (ROOT/'config/nr_before_sr.ini').read_text()
    assert 'TryAcquireSRWLockShared(&g_command_state_lock)' in state
    print('PASS integration: version / pending edits / real submission / frozen full replay / nonblocking snapshot')
if __name__=='__main__':main()
