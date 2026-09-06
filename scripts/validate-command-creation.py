"""Exercise actual optional Create observers and bounded one-shot publication.
Host COM/Win32 adapters are fault injection, not native D3D12/game verification.
"""
from pathlib import Path
import argparse
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
PRE=r'''
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdio>
using UINT=unsigned;using UINT64=unsigned long long;using HRESULT=int;using REFIID=int;
#define STDMETHODCALLTYPE
#define FAILED(x) ((x)<0)
#define SUCCEEDED(x) ((x)>=0)
#define SRWLOCK_INIT 0
using SRWLOCK=int;
struct GUID{unsigned a;unsigned short b,c;unsigned char d[8];};
constexpr int IID_ID3D12GraphicsCommandList=1,IID_ID3D12Device4=4,kCommandStateMetaContract=2;
using D3D12_COMMAND_LIST_TYPE=int;constexpr int D3D12_COMMAND_LIST_TYPE_DIRECT=0;
using D3D12_COMMAND_LIST_FLAGS=unsigned;
struct ID3D12CommandAllocator{};struct ID3D12PipelineState{};
struct CommandCreationInfo{UINT64 serial=0,tick=0,device_identity=0;unsigned method=0;UINT node_mask=0,flags=0;bool initial_pso=false;};
struct CommandStateMetaContract{bool known=false;UINT initialization_dirty=0,execution_dirty=0;UINT64 device_identity=0;};
struct ID3D12Device;using ID3D12Device4=ID3D12Device;
struct ID3D12GraphicsCommandList {CommandCreationInfo receipt;unsigned adds=0,releases=0;bool qi_fail=false,set_fail=false;};
using IUnknown=ID3D12GraphicsCommandList;
struct ID3D12DeviceVtbl{HRESULT(*CreateCommandList)(ID3D12Device*,UINT,D3D12_COMMAND_LIST_TYPE,ID3D12CommandAllocator*,ID3D12PipelineState*,REFIID,void**);HRESULT(*CreateCommandList1)(ID3D12Device4*,UINT,D3D12_COMMAND_LIST_TYPE,D3D12_COMMAND_LIST_FLAGS,REFIID,void**);};
using ID3D12Device4Vtbl=ID3D12DeviceVtbl;
struct ID3D12Device {ID3D12Device4Vtbl*lpVtbl;};
int lock_depth=0,publications=0,original_calls=0;bool publish_ok=true,meta_ok=true;HRESULT original_result=0;ID3D12GraphicsCommandList list;
void AcquireSRWLockExclusive(SRWLOCK*){++lock_depth;}void ReleaseSRWLockExclusive(SRWLOCK*){--lock_depth;}
UINT64 GetTickCount64(){return 600;}
HRESULT IUnknown_QueryInterface(IUnknown*p,REFIID,void**out){if(p->qi_fail)return -1;++p->adds;*out=p;return 0;}
HRESULT ID3D12Device_GetPrivateData(ID3D12Device*,int,UINT*,CommandStateMetaContract*m){if(!meta_ok)return -1;m->device_identity=731;return 0;}
HRESULT ID3D12GraphicsCommandList_SetPrivateData(ID3D12GraphicsCommandList*p,const GUID&,UINT,const CommandCreationInfo*m){if(p->set_fail)return -1;p->receipt=*m;return 0;}
void ID3D12GraphicsCommandList_Release(ID3D12GraphicsCommandList*p){++p->releases;}
HRESULT ID3D12Device_QueryInterface(ID3D12Device*p,REFIID,void**out){*out=p;return 0;}
void ID3D12Device4_Release(ID3D12Device4*){}
struct CommandStateHookFamily {void*originals[4]{};};
struct CommandStateHookRequest {CommandStateHookFamily*family;void*target;void*const*detours;};
bool PublishCommandStateHooks(CommandStateHookRequest*,unsigned){++publications;return publish_ok;}
HRESULT Original(ID3D12Device*,UINT,D3D12_COMMAND_LIST_TYPE,ID3D12CommandAllocator*,ID3D12PipelineState*,REFIID,void**out){++original_calls;if(SUCCEEDED(original_result)&&out)*out=&list;return original_result;}
HRESULT Original1(ID3D12Device4*,UINT,D3D12_COMMAND_LIST_TYPE,D3D12_COMMAND_LIST_FLAGS,REFIID,void**out){++original_calls;if(SUCCEEDED(original_result)&&out)*out=&list;return original_result;}
int checks=0,failures=0;void check(bool ok,const char*m){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",m);}}
'''
MAIN=r'''
int main(){
 g_state_CreateCommandList.originals[0]=reinterpret_cast<void*>(&Original);
 g_state_CreateCommandList1.originals[0]=reinterpret_cast<void*>(&Original1);
 ID3D12DeviceVtbl table{&Original,&Original1};ID3D12Device device{&table};ID3D12CommandAllocator allocator;ID3D12PipelineState pso;
 for(int method=1;method<=2;++method)for(int pass=0;pass<2;++pass)for(int direct=0;direct<2;++direct){
  list={};original_calls=0;original_result=pass?1:-7;void*out=nullptr;
  const HRESULT hr=method==1?StateHookCreateCommandList<0>(&device,3,direct?0:2,&allocator,&pso,1,&out):StateHookCreateCommandList1<0>(&device,3,direct?0:2,5,1,&out);
  check(hr==original_result&&original_calls==1,"original called exactly once and result unchanged");
  check(out==(pass?&list:nullptr),"output identity unchanged");
  const bool observed=pass&&direct;
  check((list.receipt.serial!=0)==observed,"only successful direct creation has receipt");
  check(!observed||(list.receipt.method==static_cast<unsigned>(method)&&list.receipt.device_identity==731&&list.receipt.tick==600),"receipt source method/time/device exact");
  check(!observed||(list.receipt.node_mask==3&&list.receipt.initial_pso==(method==1)&&list.receipt.flags==(method==2?5u:0u)),"initial state and creation flags only evidence");
  check(list.adds==list.releases,"temporary QI reference balanced");
 }
 for(int fail=0;fail<3;++fail){list={};list.qi_fail=fail==0;list.set_fail=fail==1;meta_ok=fail!=2;original_result=1;void*out=nullptr;
  check(StateHookCreateCommandList<0>(&device,0,0,&allocator,nullptr,1,&out)==1,"optional receipt error does not replace original success");
  check(list.adds==list.releases,"receipt failure balances reference");
  check(fail!=2||list.receipt.device_identity==0,"unknown device proof stays unknown");
 }
 CommandStateHookFamily family;void*detours[4]{};CommandCreationAttempts attempts;int targets[6]{};publish_ok=false;publications=0;
 for(int i=0;i<6;++i){CommandStateHookRequest req{&family,&targets[i],detours};PublishCommandCreationOnce(req,attempts);PublishCommandCreationOnce(req,attempts);}
 check(publications==4&&attempts.count==4,"bounded implementation attempts; failed target never retries per list");
 check(lock_depth==0,"attempt lock always released");
 publications=0;EnsureCommandCreationObserver(&device);EnsureCommandCreationObserver(&device);
 check(publications==2,"one attempt per actual Create and Create1 target");
 check(lock_depth==0,"device observer does not retain publication lock");
 std::printf("optional creation evidence: %d checks / %d failures\n",checks,failures);return failures?1:0;
}
'''
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--compiler',default='c++');a=p.parse_args()
 s=(ROOT/'src/nr_command_creation_observer.inl').read_text()
 with tempfile.TemporaryDirectory(prefix='nr-creation-evidence-') as d:
  cpp=Path(d)/'test.cpp';exe=Path(d)/'test';cpp.write_text(PRE+s+MAIN)
  subprocess.run([a.compiler,'-std=c++20','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
if __name__=='__main__':main()
