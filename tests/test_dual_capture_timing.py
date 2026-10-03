"""CPU contract for the actual dual timing module and private capture Active.

D3D12 calls are counted API seams; no device, GPU, NGX or game is executed.
The complete production timing inl is compiled, including its late Active
declaration. Active itself is extracted unchanged from the production worker.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

CPP = r'''
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <stdexcept>
using UINT64=std::uint64_t;
using SIZE_T=std::size_t;
#define FAILED(x) ((x)<0)
struct ID3D12Device {};
struct ID3D12QueryHeap {};
struct ID3D12Resource { UINT64 ticks[14]{}; };
struct ID3D12Fence { UINT64 completed=10; };
struct ID3D12CommandQueue {};
struct ID3D12GraphicsCommandList {};
struct D3D12_RANGE { SIZE_T Begin,End; };
struct D3D12_QUERY_HEAP_DESC { unsigned Type=0,Count=0; };
struct D3D12_RESOURCE_DESC {
 unsigned Dimension=0; UINT64 Width=0;
 unsigned Height=0,DepthOrArraySize=0,MipLevels=0;
 struct {unsigned Count=0;} SampleDesc;
 unsigned Layout=0;
};
struct D3D12_HEAP_PROPERTIES {
 unsigned Type=0,CreationNodeMask=0,VisibleNodeMask=0;
};
constexpr unsigned D3D12_QUERY_HEAP_TYPE_TIMESTAMP=1,D3D12_QUERY_TYPE_TIMESTAMP=1,
 D3D12_RESOURCE_DIMENSION_BUFFER=1,D3D12_TEXTURE_LAYOUT_ROW_MAJOR=1,
 D3D12_HEAP_TYPE_READBACK=1,D3D12_HEAP_FLAG_NONE=0,D3D12_RESOURCE_STATE_COPY_DEST=1;
constexpr int IID_ID3D12QueryHeap=0,IID_ID3D12Resource=0;
unsigned stamps=0,resolves=0,maps=0,checks=0;
template<class... A> int ID3D12Device_CreateQueryHeap(A...){return -1;}
template<class... A> int ID3D12Device_CreateCommittedResource(A...){return -1;}
void ID3D12GraphicsCommandList_EndQuery(ID3D12GraphicsCommandList*,ID3D12QueryHeap*,unsigned,unsigned){++stamps;}
void ID3D12GraphicsCommandList_ResolveQueryData(ID3D12GraphicsCommandList*,ID3D12QueryHeap*,unsigned,unsigned,unsigned,ID3D12Resource*,SIZE_T){++resolves;}
UINT64 ID3D12Fence_GetCompletedValue(ID3D12Fence* f){return f->completed;}
int ID3D12CommandQueue_GetTimestampFrequency(ID3D12CommandQueue*,UINT64* f){*f=10000;return 0;}
int ID3D12Resource_Map(ID3D12Resource* r,unsigned,const D3D12_RANGE*,void** p){++maps;*p=r->ticks;return 0;}
void ID3D12Resource_Unmap(ID3D12Resource*,unsigned,const D3D12_RANGE*){}
struct Queue { ID3D12Fence* fence=nullptr;ID3D12CommandQueue* queue=nullptr; };
struct NrSlot {
 bool mutable_slot=true,dual_time_pending=false,dual_time_stable=false,
      dual_time_active=true,dual_time_reduced=true,dual_reduced=true;
 unsigned dual_time_generation=1,dual_full_timing_layer=0,dual_time_mask=16383,
          dual_capture_index=~0u;
 UINT64 dual_time_fence=3,dual_time_token=17;
 Queue* dual_time_queue=nullptr;
 ID3D12Resource* dual_time_readback=nullptr;
 ID3D12QueryHeap* dual_time_heap=nullptr;
 NrSlot* dual_selected=nullptr;
};
bool CanMutateSlotLocked(NrSlot& s){return s.mutable_slot;}
bool ReadLifecycleToken(ID3D12GraphicsCommandList*,unsigned long long& token){token=17;return true;}
unsigned StageCaptureStatus(){return 0;}
namespace nr_f8 {bool IsActive(){return false;}}
namespace nr_replay {
 struct Recorder {static constexpr unsigned Invalid=~0u;};
 bool Active(){return false;}
}
namespace nr_full {constexpr unsigned kMaxPasses=5;}
namespace nr_dual_runtime {std::atomic<unsigned> fatal_result{0},second_completed{0};}
std::atomic<unsigned> g_config_generation{1};
#ifndef NR_TIMING_TEST_NO_PRIVATE
#define NR_HAS_PRIVATE_RESIDUAL_SR 1
namespace nr_private_sr_capture {
 struct Worker {std::atomic<unsigned> state{0},active_runs{0},leases{0};} worker;
}
#endif
#include "nr_dual_timing.inl"
// Match the Core include order: this definition comes after the timing module.
#ifndef NR_TIMING_TEST_NO_PRIVATE
namespace nr_private_sr_capture {
@ACTUAL_ACTIVE@
}
#endif
void Check(bool value,const char* label){++checks;if(!value)throw std::runtime_error(label);}
void Capture(unsigned state,unsigned runs,unsigned leases){
#ifndef NR_TIMING_TEST_NO_PRIVATE
 nr_private_sr_capture::worker.state=state;
 nr_private_sr_capture::worker.active_runs=runs;
 nr_private_sr_capture::worker.leases=leases;
#else
 (void)state;(void)runs;(void)leases;
#endif
}
NrSlot Ready(Queue& q,ID3D12Resource& r,ID3D12QueryHeap& heap,NrSlot& selected){
 NrSlot s;s.dual_time_queue=&q;s.dual_time_readback=&r;
 s.dual_time_heap=&heap;s.dual_selected=&selected;return s;
}
int main(){try{
 using namespace nr_dual_runtime;
 ID3D12GraphicsCommandList list;ID3D12Fence fence;ID3D12CommandQueue queue;
 Queue q{&fence,&queue};ID3D12Resource readback;ID3D12QueryHeap heap;NrSlot selected;
 for(unsigned i=0;i<14;++i)readback.ticks[i]=100+i;
 Capture(0,0,0);
 auto normal=Ready(q,readback,heap,selected);
 ResolveTiming(&list,normal,true);
 Check(normal.dual_time_pending&&normal.dual_time_stable&&!normal.dual_time_active,
       "CAPTURE_TIMING_OFF_NORMAL");
 Check(resolves==1&&stamps==0,"CAPTURE_TIMING_OFF_GPU_CALLS");
 CollectTiming(normal);
 Check(time_samples==1&&second_completed==1,"CAPTURE_TIMING_OFF_SAMPLE");
 auto full=Ready(q,readback,heap,selected);full.dual_time_mask=3u<<4;full.dual_full_timing_layer=2;
 ResolveLayerTiming(&list,full,true);CollectTiming(full);
 Check(full_time[1].samples==1,"CAPTURE_TIMING_OFF_FULL_SAMPLE");
#ifndef NR_TIMING_TEST_NO_PRIVATE
 const unsigned scenarios[][3]={{1,0,0},{2,1,0},{2,0,1},{3,0,1}};
 for(const auto& scenario:scenarios){
  Capture(scenario[0],scenario[1],scenario[2]);
  Check(nr_private_sr_capture::Active(),"CAPTURE_TIMING_ACTIVE_WINDOW");
  const auto before_resolves=resolves,before_stamps=stamps,before_maps=maps;
  auto primary=Ready(q,readback,heap,selected);
  ResolveTiming(&list,primary,true);
  Check(!primary.dual_time_pending&&!primary.dual_time_stable&&!primary.dual_time_active&&primary.dual_time_mask==0,
        "CAPTURE_TIMING_PRIMARY_EXCLUDED");
  auto layer=Ready(q,readback,heap,selected);layer.dual_time_mask=3u<<4;layer.dual_full_timing_layer=2;
  ResolveLayerTiming(&list,layer,true);
  // The actual full-chain adoption predicate cannot restore an excluded layer.
  layer.dual_time_stable=layer.dual_time_pending&&StageCaptureStatus()==0&&!nr_f8::IsActive()&&!nr_replay::Active();
  Check(!layer.dual_time_pending&&!layer.dual_time_stable&&!layer.dual_time_active&&layer.dual_time_mask==0,
        "CAPTURE_TIMING_FULL_EXCLUDED");
  CollectTiming(primary);CollectTiming(layer);
  Check(resolves==before_resolves&&stamps==before_stamps&&maps==before_maps,
        "CAPTURE_TIMING_NO_LATE_GPU_OR_MAP");
  Check(time_samples==1&&full_time[1].samples==1,"CAPTURE_TIMING_NO_CAPTURE_SAMPLES");
 }
 // A lawful completed old recording is retained even while a writer lease exists.
 Capture(2,0,1);
 auto old=Ready(q,readback,heap,selected);
 old.dual_time_active=false;old.dual_time_pending=old.dual_time_stable=true;
 CollectTiming(old);
 Check(time_samples==2&&second_completed==2,"CAPTURE_TIMING_OLD_SAMPLE_RETAINED");
 // Once the ready worker has no run or lease, ordinary sampling resumes.
 Capture(2,0,0);Check(!nr_private_sr_capture::Active(),"CAPTURE_TIMING_IDLE_WORKER");
#endif
 auto resumed=Ready(q,readback,heap,selected);
 ResolveTiming(&list,resumed,true);CollectTiming(resumed);
 Check(time_samples>=2,"CAPTURE_TIMING_RESUMED");
 std::printf("PASS dual capture timing checks=%u CPU-only\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
'''


def definition(text, marker):
    start = text.index(marker)
    brace = text.index("{", start)
    end, depth = brace + 1, 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


class DualCaptureTiming(unittest.TestCase):
    def run_module(self, *, no_private=False, remove_guard=None):
        compiler = os.environ.get("CXX") or os.environ.get("DLSS5_TEST_CXX") or shutil.which("clang++")
        if not compiler or not Path(compiler).is_file():
            self.skipTest("Host C++ compiler unavailable")
        timing = (ROOT / "src/nr_dual_timing.inl").read_text(encoding="utf-8-sig")
        active = definition((ROOT / "src/nr_private_sr_capture.inl").read_text(encoding="utf-8-sig"), "bool Active()")
        if remove_guard:
            body = definition(timing, remove_guard)
            self.assertEqual(body.count("if(nr_private_sr_capture::Active()){"), 1)
            timing = timing.replace(body, body.replace("if(nr_private_sr_capture::Active()){", "if(false){"), 1)
        with tempfile.TemporaryDirectory(prefix="nr-dual-capture-timing-") as td:
            folder = Path(td)
            (folder / "nr_dual_timing.inl").write_text(timing, encoding="utf-8")
            cpp, exe = folder / "test.cpp", folder / "test.exe"
            cpp.write_text(CPP.replace("@ACTUAL_ACTIVE@", active), encoding="utf-8")
            command = [compiler, "-std=c++17", "-O2", "-static", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(exe)]
            if no_private:
                command.append("-DNR_TIMING_TEST_NO_PRIVATE=1")
            compiled = subprocess.run(command, capture_output=True, text=True, timeout=90)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            return run.returncode, run.stdout + run.stderr

    def test_capture_and_writer_backlog_are_excluded(self):
        code, log = self.run_module()
        self.assertEqual(code, 0, log)
        self.assertIn("PASS dual capture timing", log)

    def test_without_private_sr_has_no_capture_symbol_dependency(self):
        code, log = self.run_module(no_private=True)
        self.assertEqual(code, 0, log)

    def test_missing_primary_guard_negative_control(self):
        code, log = self.run_module(remove_guard="void ResolveTiming(")
        self.assertEqual(code, 1, log)
        self.assertIn("CAPTURE_TIMING_PRIMARY_EXCLUDED", log)

    def test_missing_layer_guard_negative_control(self):
        code, log = self.run_module(remove_guard="void ResolveLayerTiming(")
        self.assertEqual(code, 1, log)
        self.assertIn("CAPTURE_TIMING_FULL_EXCLUDED", log)


if __name__ == "__main__":
    unittest.main()
