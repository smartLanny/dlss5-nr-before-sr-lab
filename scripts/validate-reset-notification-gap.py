"""Compile the actual native Reset result observer with boundary fault fixtures.
The fixture is not a D3D12 implementation; the real native harness separately
covers installed hooks, object identities, state replay and WARP.
"""
from pathlib import Path
import argparse
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
def definition(text, marker):
    start=text.index(marker); p=text.index('{',start); depth=0
    while p<len(text):
        depth+=(text[p]=='{')-(text[p]=='}'); p+=1
        if depth==0: return text[start:p]
    raise ValueError(marker)
PRE=r'''
#include <atomic>
#include <cstdio>
#include <cstring>
using UINT64=unsigned long long;using HRESULT=int;
#define SUCCEEDED(x) ((x)>=0)
#define FAILED(x) ((x)<0)
struct ID3D12PipelineState{};struct ID3D12GraphicsCommandList{};
struct NativeCommandState{bool known=false;const char*unknown_reason="awaiting-native-reset";ID3D12PipelineState*actual=nullptr;};
struct Record{ID3D12GraphicsCommandList*native=nullptr;NativeCommandState*state=nullptr;unsigned native_resets=0,reset_successes=0;bool family=false;};
std::atomic<UINT64> g_command_native_reset_events{0},g_command_native_reset_failures{0},g_command_reset_without_record{0},g_command_reset_identity_mismatches{0};
std::atomic<bool> g_enabled_fast{true};int g_command_state_lock=0,depth=0;Record record;NativeCommandState state;
void AcquireSRWLockExclusive(int*){++depth;}void ReleaseSRWLockExclusive(int*){--depth;}
UINT64 ReadCommandStateToken(ID3D12GraphicsCommandList*p){return p?1:0;}
Record* FindCommandStateTokenLocked(UINT64 token){return token?&record:nullptr;}
void PrimeCommandListBeforeResetLocked(Record&r,ID3D12GraphicsCommandList*p){if(r.family&&r.native==p&&!r.state)r.state=&state;}
template<class F>void MutateNativeCommandState(ID3D12GraphicsCommandList*p,F f){if(p&&p==record.native&&record.state)f(*record.state);}
void ResetNativeCommandState(NativeCommandState&s,ID3D12PipelineState*p){s.known=true;s.actual=p;s.unknown_reason="none";}
int failures=0,checks=0;void check(bool ok,const char*msg){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",msg);}}
'''
MAIN=r'''
int main(){
 ID3D12GraphicsCommandList list,other;ID3D12PipelineState pso;
 for(int enabled=0;enabled<2;++enabled)for(int family=0;family<2;++family)for(int okay=0;okay<2;++okay){
  record={};state={};record.native=&list;record.family=family;g_enabled_fast=enabled;
  OnCommandStateResetResult(&list,okay?0:-1,&pso);
  const bool expected=enabled&&family&&okay;
  check((record.state&&record.state->known)==expected,"RESET_NOTIFICATION_GAP_ORACLE");
  check(!expected||state.actual==&pso,"real initial pipeline preserved");
  check(depth==0,"publication lock released");
 }
 record={};state={};record.native=&list;record.family=true;record.state=&state;g_enabled_fast=false;
 OnCommandStateResetResult(&list,0,&pso);check(state.known&&state.actual==&pso,"already tracked reset observed with NR off");
 OnCommandStateResetResult(&list,-1,&pso);check(!state.known&&!std::strcmp(state.unknown_reason,"failed-native-reset"),"failed reset invalidates existing state");
 record={};record.native=&other;record.family=true;g_enabled_fast=true;state={};
 OnCommandStateResetResult(&list,0,&pso);check(!record.state&&!state.known,"mismatched native identity never primed");
 OnCommandStateResetResult(nullptr,0,&pso);check(!record.state,"unknown token never primed");
 std::printf("native Reset boundary: %d checks / %d failures\n",checks,failures);return failures?1:0;
}
'''
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler',default='c++');p.add_argument('--old-source',type=Path)
    args=p.parse_args()
    current=(ROOT/'src/nr_command_state.inl').read_text(encoding='utf-8-sig')
    source=args.old_source.read_text(encoding='utf-8-sig') if args.old_source else current
    code=PRE+definition(source,'void OnCommandStateResetResult(')+MAIN
    with tempfile.TemporaryDirectory(prefix='nr-reset-gap-') as tmp:
        cpp=Path(tmp)/'test.cpp';exe=Path(tmp)/'test';cpp.write_text(code)
        subprocess.run([args.compiler,'-std=c++20','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)],check=True)
        run=subprocess.run([str(exe)],capture_output=True,text=True);print(run.stdout,end='')
        if args.old_source:
            assert run.returncode==1 and '2 failures' in run.stdout and 'RESET_NOTIFICATION_GAP_ORACLE' in run.stdout,run
        else: assert run.returncode==0,run
if __name__=='__main__':main()
