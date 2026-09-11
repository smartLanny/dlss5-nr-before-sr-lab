"""CPU fault matrix for actual Create-boundary and pipeline observer functions.
COM/family adapters inject failures; real D3D12/MinHook coverage is a separate gate.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]


def definition(source, marker):
    start = source.index(marker)
    at = source.index('{', start)
    depth = 0
    while at < len(source):
        depth += (source[at] == '{') - (source[at] == '}')
        at += 1
        if depth == 0:
            return source[start:at]
    raise ValueError(marker)


PRE = r'''
#include <atomic>
#include <cstring>
#include <cstdio>
#include <new>
using UINT=unsigned; using UINT64=unsigned long long;
constexpr UINT D3D12_GRAPHICS_STATE_PIPELINE_STATE=1, kCommandStateCoveredDomains=7;
constexpr UINT64 kDynamicDepthBias=8192,kDynamicStripCut=16384;
struct ID3D12PipelineState{};struct ID3D12StateObject{};
struct ID3D12GraphicsCommandList{const int* lpVtbl=nullptr;};
enum class CommandRootKind{Empty,Table,Constants,Cbv};
struct Arg{CommandRootKind kind=CommandRootKind::Empty;UINT64 value=0,constant_mask=0;UINT constants[64]{};};
struct NativeCommandState {
 bool known=false,first_recording_observed=false,null_pipeline_baseline=false;
 bool null_guard_covered=false,null_guard_reset_observed=false,null_guard_tainted=false;
 const char* unknown_reason="awaiting-native-reset";const char* baseline_origin="unobserved";
 UINT unknown_domains=0,meta_initialization_dirty=0,meta_execution_dirty=0;
 UINT64 reset_serial=0,pipeline_writes=0;
 ID3D12PipelineState* pipeline=nullptr;ID3D12StateObject* raytracing_pipeline=nullptr;
 struct{UINT64 recorded=0;} dynamic;
 Arg arguments[2][64]{};
};
struct CommandCreationInfo{UINT64 serial=1,device_identity=1;unsigned method=1,flags=0;bool initial_pso=false;};
struct CommandStateMetaContract{bool known=true;UINT initialization_dirty=0,execution_dirty=0;UINT64 device_identity=1;};
struct CommandStateRecord {
 UINT64 token=1,prime_serial=0,captures=0,native_resets=0,reset_notifications=0,next_hook_attempt=0;
 ID3D12GraphicsCommandList* native=nullptr;NativeCommandState*state=nullptr;
 bool native_created=true,lifetime_ready=true,hooks_ready=false,null_guard_requested=false;
 CommandStateMetaContract meta_contract;CommandCreationInfo creation;
};
struct ObservedCommandFamily{UINT64 device_identity=1;const int* vtable=nullptr;bool null_guard=false;unsigned interface_mask=3;};
ObservedCommandFamily g_observed_command_families[16];
CommandStateRecord g_command_states[2];
constexpr unsigned kMaxPrimedCommandStates=128;
unsigned g_primed_command_state_count=0;UINT64 g_command_prime_serial=0;
std::atomic<UINT64> g_command_state_epoch{1};std::atomic<bool>g_enabled_fast{true};
int g_command_state_lock=0,depth=0;bool qi_ok=true;unsigned qi_mask=3;
void AcquireSRWLockExclusive(int*){++depth;}void ReleaseSRWLockExclusive(int*){--depth;}
UINT64 ReadCommandStateToken(ID3D12GraphicsCommandList*p){return p==g_command_states[0].native?1:0;}
CommandStateRecord*FindCommandStateTokenLocked(UINT64 t){return t==1?&g_command_states[0]:nullptr;}
bool CommandFamilyInterfaces(ID3D12GraphicsCommandList*,unsigned&mask){mask=qi_mask;return qi_ok;}
void ReleaseNativeCommandState(NativeCommandState&s){s=NativeCommandState{};}
template<class T>void CommandStateAssign(T*&a,T*b){a=b;}
int checks=0,failures=0;void check(bool b,const char*m){++checks;if(!b){++failures;std::printf("FAIL %s\n",m);}}
void clean(){for(auto&r:g_command_states){delete r.state;r=CommandStateRecord{};}g_primed_command_state_count=0;g_command_prime_serial=0;}
'''
MAIN = r'''
int main(){
 int table=0;ID3D12GraphicsCommandList list{&table},other{&table};ID3D12PipelineState pso;
 for(int fault=0;fault<20;++fault){
  clean();auto&r=g_command_states[0];r.native=&list;CommandCreationInfo receipt;
  g_observed_command_families[0]={1,&table,true,3};g_enabled_fast=true;qi_ok=true;qi_mask=3;
  switch(fault){
   case 1:g_enabled_fast=false;break;case 2:receipt.method=2;break;case 3:receipt.initial_pso=true;break;
   case 4:receipt.serial=0;break;case 5:receipt.flags=1;break;case 6:r.native_created=false;break;
   case 7:r.lifetime_ready=false;break;case 8:receipt.device_identity=2;break;case 9:r.creation.serial=9;break;
   case 10:r.captures=1;break;case 11:r.native_resets=1;break;case 12:r.reset_notifications=1;break;
   case 13:r.meta_contract.known=false;break;case 14:r.meta_contract.initialization_dirty=8;break;
   case 15:g_observed_command_families[0].vtable=nullptr;break;case 16:qi_ok=false;break;case 17:qi_mask=1;break;case 18:g_primed_command_state_count=kMaxPrimedCommandStates;break;case 19:g_observed_command_families[0].null_guard=false;break;
  }
  ObserveCommandFirstRecording(&list,receipt);
  check(bool(r.state&&r.state->first_recording_observed)==(fault==0),"EARLY_OBSERVATION_MATRIX");
  if(r.state){
   check(!r.state->known&&!CanRestoreNativeCommandState(*r.state),"receipt plus coverage alone is not admitted");
   CommandStateSetPipeline(*r.state,&pso);
   check(CanRestoreNativeCommandState(*r.state),"FIRST_RECORDING_CARRIER_ORACLE");
   check(!r.state->reset_serial&&!r.native_resets&&!r.state->null_guard_reset_observed,"no invented Reset evidence");
   check(!std::strcmp(r.state->baseline_origin,"Create-first-recording"),"distinct baseline provenance");
  }
  check(depth==0,"lock released");
 }
 clean();auto&r=g_command_states[0];r.native=&list;r.state=new NativeCommandState;
 r.state->pipeline_writes=42;g_enabled_fast=true;g_observed_command_families[0]={1,&table,true,3};qi_ok=true;qi_mask=3;
 ObserveCommandFirstRecording(&list,CommandCreationInfo{});CommandStateSetPipeline(*r.state,&pso);
 check(!r.state->first_recording_observed&&!r.state->known,"late recording cannot be backfilled");
 ObserveCommandFirstRecording(&other,CommandCreationInfo{});check(!r.state->known,"identity mismatch cannot upgrade");
 for(int fault=0;fault<5;++fault){
  clean();auto&v=g_command_states[0];v.native=&list;ObserveCommandFirstRecording(&list,CommandCreationInfo{});
  check(v.state!=nullptr,"early state allocated");
  if(!v.state)continue;
  if(fault==0)CommandStateSetValue(*v.state,0,64,CommandRootKind::Cbv,17);
  if(fault==1)CommandStateSetConstants(*v.state,0,0,1,nullptr,0);
  if(fault==2)CommandStateInvalidDynamic(*v.state);
  if(fault==3)CommandStateSetPipeline(*v.state,nullptr);
  if(fault==4){v.state->known=false;v.state->unknown_reason="execute-bundle";}
  CommandStateSetPipeline(*v.state,&pso);
  check(!v.state->known&&!CanRestoreNativeCommandState(*v.state),"unknown state stays revoked after later PSO");
 }
 clean();auto&v=g_command_states[0];v.native=&list;ObserveCommandFirstRecording(&list,CommandCreationInfo{});
 if(v.state){CommandStateSetPipeline(*v.state,&pso);v.state->unknown_domains=4;check(!CanRestoreNativeCommandState(*v.state),"meta dirty still blocks");}
 clean();std::printf("first recording CPU: %d checks / %d failures\n",checks,failures);return failures?1:0;
}
'''


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--compiler',default='c++')
    ap.add_argument('--negative-control',action='store_true')
    args=ap.parse_args()
    state=(ROOT/'src/nr_command_state.inl').read_text(encoding='utf-8-sig')
    pool=(ROOT/'src/nr_command_pool_observation.inl').read_text(encoding='utf-8-sig')
    record=(ROOT/'src/nr_native_command_record.inl').read_text(encoding='utf-8-sig')
    observer=definition(record,'void ObserveCommandFirstRecording(')
    if args.negative_control:
        observer=observer.replace('PrimeCommandListBeforeResetLocked(*record, native);','(void)0;')
    code=PRE+'\n'+ '\n'.join(definition(state,x) for x in [
        'void ObserveFirstRecordingPipeline(', 'void CommandStateSetPipeline(',
        'void CommandStateSetValue(', 'void CommandStateSetConstants(',
        'void CommandStateInvalidDynamic(', 'bool CanRestoreNativeCommandState('])
    code+='\n'+definition(pool,'const ObservedCommandFamily *FindObservedCommandFamilyLocked(')
    code+='\n'+definition(pool,'void PrimeCommandListBeforeResetLocked(')+'\n'+observer+'\n'+MAIN
    with tempfile.TemporaryDirectory(prefix='nr-first-recording-') as tmp:
        cpp=Path(tmp)/'test.cpp';exe=Path(tmp)/('test.exe' if __import__('os').name=='nt' else 'test')
        cpp.write_text(code,encoding='utf8')
        subprocess.run([args.compiler,'-std=c++20','-O1','-static','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)],check=True)
        result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
        print(result.stdout,end='')
        if args.negative_control:
            assert result.returncode==1 and 'EARLY_OBSERVATION_MATRIX' in result.stdout,result
        else:
            assert result.returncode==0,result

if __name__=='__main__':main()
