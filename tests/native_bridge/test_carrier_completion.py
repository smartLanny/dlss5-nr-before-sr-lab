"""Actual staged carrier transformations and production completion/error seams.

Set CARRIER_COMPLETION_SOURCE to the canonical stage and CXX to a host compiler.
This executes extracted production functions; fake queues are not a GPU claim.
"""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('completion',ROOT/'scripts/patch-carrier-completion.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

def extract(source,signature):
    a,b=m.base.block(source,signature);return source[a:b]

@unittest.skipUnless(os.environ.get('CARRIER_COMPLETION_SOURCE'),'requires actual staged carrier')
class CarrierCompletion(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        stage=Path(os.environ['CARRIER_COMPLETION_SOURCE'])/'carrier/src'
        cls.original=(stage/'bridge.inc').read_text(encoding='utf-8')
        patched=os.environ.get('CARRIER_COMPLETION_ALREADY_PATCHED')=='1'
        if patched and cls.original.count('static bool g_nr_carrier_quarantined = false;')!=1:
            raise ValueError('Expected completed carrier transform in final stage')
        cls.bridge=cls.original if patched else m.patch_bridge(cls.original)
        host=(stage/'dlss5-bridge.cpp').read_text(encoding='utf-8')
        cls.host=host if patched else m.patch_host(host)

    def test_real_transform_preserves_ordinary_failure_and_no_fake_completion(self):
        for signature in ('static void BridgeDisable(', 'static void BridgeEnterStall(',
                          'static void BridgeRescueStalledQueue('):
            body=extract(self.bridge,signature)
            self.assertNotIn('->Signal(',body)
        self.assertNotIn('g_bridge.ctx4->Wait(g_bridge.fence11, v_out)',self.bridge)
        wait=self.bridge.index('NrWaitForGpu(g_bridge.fence_progress, v_out, 500)')
        copy=self.bridge.index('nr_mirror_rect::CopyOutput<')
        self.assertLess(wait,copy)
        self.assertNotIn('__except',extract(self.bridge,'static HRESULT SafeCreateDevice('))
        for source,signature in ((self.bridge,'static NVSDK_NGX_Result SafeEvaluateGuarded('),
                                 (self.host,'static NVSDK_NGX_Result SafeCreateFeature(')):
            body=extract(source,signature)
            self.assertNotIn('__except',body)
            self.assertNotIn('__try',body)
            self.assertIn('return fn(',body)
        with self.assertRaises(ValueError):m.patch_bridge(self.bridge)

    @unittest.skipUnless(os.environ.get('CXX'),'requires host C++ compiler')
    def test_actual_wait_on_warp_queue_completion(self):
        source=WARP_HARNESS.replace('// PRODUCTION',extract(self.bridge,'static bool NrWaitForGpu('))
        with tempfile.TemporaryDirectory(prefix='nr-carrier-warp-') as temp:
            cpp=Path(temp)/'warp.cpp';exe=Path(temp)/'warp.exe'
            cpp.write_text(source,encoding='utf-8')
            subprocess.run([os.environ['CXX'],'-std=c++17','-O2',str(cpp),'-o',str(exe),'-ld3d12','-ldxgi','-ldxguid','-lole32'],check=True)
            result=subprocess.run([str(exe)],capture_output=True,text=True,check=True,timeout=20)
            print(result.stdout.strip())

    @unittest.skipUnless(os.environ.get('CXX'),'requires host C++ compiler')
    def test_native_error_preserved_and_hardware_fault_leaves_child(self):
        functions='\n'.join((
            extract(self.bridge,'static NVSDK_NGX_Result SafeEvaluateGuarded('),
            extract(self.host,'static NVSDK_NGX_Result SafeCreateFeature(')))
        source=FAULT_HARNESS.replace('// PRODUCTION',functions)
        with tempfile.TemporaryDirectory(prefix='nr-carrier-fault-') as temp:
            cpp=Path(temp)/'fault.cpp';exe=Path(temp)/'fault.exe'
            cpp.write_text(source,encoding='utf-8')
            subprocess.run([os.environ['CXX'],'-std=c++17','-O2','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)],check=True)
            subprocess.run([str(exe),'normal'],check=True)
            for mode in ('evaluate','create'):
                result=subprocess.run([str(exe),mode],capture_output=True,timeout=15)
                self.assertEqual(result.returncode & 0xffffffff,0xc0000005)

    @unittest.skipUnless(os.environ.get('CXX'),'requires host C++ compiler')
    def test_actual_completion_submission_and_retirement_error_paths(self):
        functions='\n'.join(extract(self.bridge,s) for s in (
            'static void NrCarrierQuarantine(', 'static bool NrWaitForGpu(',
            'static bool BridgeDrainGpu()', 'static UINT64 EndCommands()\n{',
            'static void BridgeReleaseTextures()\n{'))
        source=HARNESS.replace('// PRODUCTION',functions)
        with tempfile.TemporaryDirectory(prefix='nr-carrier-completion-') as temp:
            cpp=Path(temp)/'completion.cpp';exe=Path(temp)/'completion.exe'
            cpp.write_text(source,encoding='utf-8')
            subprocess.run([os.environ['CXX'],'-std=c++17','-O2','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)],check=True)
            result=subprocess.run([str(exe)],capture_output=True,text=True,check=True)
            print(result.stdout.strip())

WARP_HARNESS=r'''
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <cstdint>
#include <cstdio>
// PRODUCTION
int main(){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    IDXGIFactory4 *factory=nullptr;IDXGIAdapter *adapter=nullptr;ID3D12Device *device=nullptr;
    ID3D12CommandQueue *queue=nullptr;ID3D12Fence *progress=nullptr;
    if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))return 1;
    if(FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))))return 2;
    if(FAILED(D3D12CreateDevice(adapter,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))))return 3;
    D3D12_COMMAND_QUEUE_DESC desc{};
    if(FAILED(device->CreateCommandQueue(&desc,IID_PPV_ARGS(&queue))))return 4;
    if(FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&progress))))return 5;
    if(NrWaitForGpu(progress,1,2))return 6; // no queue work: must time out
    if(progress->GetCompletedValue()!=0)return 7; // wait must not fabricate completion
    if(FAILED(queue->Signal(progress,1)))return 8;
    if(!NrWaitForGpu(progress,1,5000))return 9;
    if(!NrWaitForGpu(progress,1,0))return 10;
    if(NrWaitForGpu(nullptr,1,0))return 11;
    progress->Release();queue->Release();device->Release();adapter->Release();factory->Release();
    std::puts("PASS actual NrWaitForGpu WARP queue completion/timeout; no NVIDIA NR claim");
}
'''

FAULT_HARNESS=r'''
#include <windows.h>
#include <cstdint>
#include <cstring>
using NVSDK_NGX_Result=int;
struct ID3D12GraphicsCommandList{};struct NVSDK_NGX_Handle{};struct NVSDK_NGX_Parameter{};
using PFN_D3D12EvaluateFeature=int(*)(ID3D12GraphicsCommandList*,const NVSDK_NGX_Handle*,const NVSDK_NGX_Parameter*,void*);
using PFN_D3D12CreateFeature=int(*)(ID3D12GraphicsCommandList*,int,NVSDK_NGX_Parameter*,NVSDK_NGX_Handle**);
namespace nr_bridge_baseline {bool Bind(ID3D12GraphicsCommandList*){return true;}}
template<class... A>void Log(const char*,A...){}
// PRODUCTION
bool fault=false;
int Eval(ID3D12GraphicsCommandList*,const NVSDK_NGX_Handle*,const NVSDK_NGX_Parameter*,void*){
    if(fault)RaiseException(0xc0000005,EXCEPTION_NONCONTINUABLE,0,nullptr);
    return -71;
}
int Create(ID3D12GraphicsCommandList*,int,NVSDK_NGX_Parameter*,NVSDK_NGX_Handle**){
    if(fault)RaiseException(0xc0000005,EXCEPTION_NONCONTINUABLE,0,nullptr);
    return -72;
}
int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    DWORD code=99;fault=argc>1&&std::strcmp(argv[1],"normal")!=0;
    if(argc>1&&std::strcmp(argv[1],"create")==0)
        return SafeCreateFeature(Create,nullptr,nullptr,nullptr,&code)==-72&&!code?0:7;
    int r=SafeEvaluateGuarded(Eval,nullptr,nullptr,nullptr,&code);
    if(r!=-71||code)return 8;
    return SafeCreateFeature(Create,nullptr,nullptr,nullptr,&code)==-72&&!code?0:9;
}
'''

HARNESS=r'''
#include <cstdint>
#include <cstdio>
#include <cstdlib>
using UINT64=std::uint64_t;using ULONGLONG=std::uint64_t;using DWORD=unsigned;using UINT=unsigned;
#define FAILED(x) ((x)<0)
constexpr int NGX_SUCCESS=1;
static unsigned checks=0;
#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static ULONGLONG ticks=0;
ULONGLONG GetTickCount64(){return ticks;}
void Sleep(unsigned v){ticks+=v;}
template<class... A> void Log(const char*,A...){}
static int releases=0,closes=0,submits=0,release_calls=0;
struct Resource {void Release(){++releases;} };
struct ID3D12Fence {UINT64 done=0;UINT64 GetCompletedValue(){return done;} };
struct ID3D12CommandList {};
struct List:ID3D12CommandList {int close_result=0;int Close(){return close_result;} };
struct Queue {
    bool fail_signal=false,fail_wait=false,complete=true;
    int Signal(ID3D12Fence *f,UINT64 v){if(fail_signal)return -1;if(complete)f->done=v;return 0;}
    int Wait(ID3D12Fence*,UINT64){return fail_wait?-1:0;}
    void ExecuteCommandLists(unsigned,ID3D12CommandList**){++submits;}
};
struct Ctx {
    bool fail_signal=false;
    int Signal(ID3D12Fence*,UINT64){return fail_signal?-1:0;}
    void Flush(){}
};
struct ID3D12Resource {};
struct Params {void Set(const char*,ID3D12Resource*){} };
struct Pq {};
constexpr int SLOT_COUNT=4;
struct Bridge {
    static constexpr int kFrames=3;
    bool disabled=false,frame_ready=true;
    Queue *queue=nullptr;ID3D12Fence *fence12=nullptr,*fence_progress=nullptr,*fence11=nullptr;
    Ctx *ctx4=nullptr;List *list=nullptr;UINT64 fence_value=0,last_submitted=0,alloc_fence[3]={11,12,13};
    int frame_slot=0;Pq pq;Params *params=nullptr;
    Resource *depth_uav=nullptr,*depth_srv=nullptr,*depth_src=nullptr,*mv_uav=nullptr,*mv_srv=nullptr,*mv_src=nullptr;
    Resource *tex11[4]={},*tex12[4]={};void *shared[4]={};void *feature=nullptr;
    int (*release_feature)(void*)=nullptr;
}g_bridge;
bool g_nr_carrier_quarantined=false,g_nr_native_delivery=true;
void PqPassRelease(Pq*){}
void BridgeReleaseExposureFwd(){}
void CloseHandle(void*){++closes;}
// PRODUCTION
Queue queue;Ctx ctx;List list;ID3D12Fence shared,progress;Resource resource;int release_result=1;
int ReleaseFeature(void*){++release_calls;return release_result;}
void Reset(){
    ticks=0;releases=closes=submits=release_calls=0;g_nr_carrier_quarantined=false;g_nr_native_delivery=true;
    queue={};ctx={};list={};shared={};progress={};g_bridge={};release_result=1;
    g_bridge.queue=&queue;g_bridge.ctx4=&ctx;g_bridge.list=&list;
    g_bridge.fence12=g_bridge.fence11=&shared;g_bridge.fence_progress=&progress;
    g_bridge.feature=&resource;g_bridge.release_feature=ReleaseFeature;
    g_bridge.tex12[0]=&resource;g_bridge.shared[0]=&resource;
}
void Held(){CHECK(g_nr_carrier_quarantined&&g_bridge.disabled&&!g_nr_native_delivery);
    CHECK(releases==0&&closes==0&&g_bridge.feature==&resource&&g_bridge.tex12[0]==&resource);}
int main(){
    Reset();CHECK(NrWaitForGpu(&progress,0,3));CHECK(!NrWaitForGpu(nullptr,1,3));
    progress.done=UINT64_MAX;CHECK(!NrWaitForGpu(&progress,1,3));
    Reset();CHECK(!NrWaitForGpu(&progress,1,3)&&ticks==3);
    Reset();progress.done=2;CHECK(NrWaitForGpu(&progress,1,3)&&ticks==0);
    Reset();CHECK(EndCommands()==1&&submits==1&&g_bridge.alloc_fence[0]==1);
    Reset();list.close_result=-1;CHECK(EndCommands()==0&&submits==0);Held();
    Reset();queue.fail_signal=true;CHECK(EndCommands()==0&&submits==1&&g_bridge.alloc_fence[0]==UINT64_MAX);Held();
    Reset();ctx.fail_signal=true;BridgeReleaseTextures();Held();CHECK(g_bridge.alloc_fence[0]==11&&release_calls==0);
    Reset();queue.fail_wait=true;BridgeReleaseTextures();Held();CHECK(g_bridge.alloc_fence[0]==11&&release_calls==0);
    Reset();queue.fail_signal=true;BridgeReleaseTextures();Held();CHECK(g_bridge.alloc_fence[0]==11&&release_calls==0);
    Reset();queue.complete=false;BridgeReleaseTextures();Held();CHECK(ticks==5000&&g_bridge.alloc_fence[0]==11&&release_calls==0);
    // A quarantined carrier never repeats the timeout or releases on a later call.
    BridgeReleaseTextures();Held();CHECK(ticks==5000);
    Reset();queue.complete=false;progress.done=UINT64_MAX;BridgeReleaseTextures();Held();CHECK(ticks==0&&g_bridge.alloc_fence[0]==11);
    Reset();release_result=-1;BridgeReleaseTextures();Held();CHECK(release_calls==1);
    BridgeReleaseTextures();CHECK(release_calls==1);
    Reset();BridgeReleaseTextures();CHECK(releases==1&&closes==1&&release_calls==1&&g_bridge.feature==nullptr&&!g_nr_carrier_quarantined);
    std::printf("PASS %u production completion/retirement checks (fake queues, no GPU claim)\n",checks);
}
'''

if __name__=='__main__':unittest.main()
