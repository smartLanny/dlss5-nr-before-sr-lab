"""Compile actual transformed Feeder submission/drain bodies with injected faults."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from tests.multi_api.test_external_provider_recovery import module, ROOT


class CompletionTests(unittest.TestCase):
    def test_production_submit_and_drain_failures(self):
        feeder = os.environ.get('NR_FEEDER_SOURCE')
        cxx = os.environ.get('CXX')
        if not feeder or not cxx:
            self.skipTest('NR_FEEDER_SOURCE and CXX required for production Windows fault harness')
        transform = module('patch-feeder-external-nr')
        transform.assert_feeder_commit(Path(feeder))
        text = transform.patch_feed_cpp((Path(feeder) / 'src/dlss5-feed.cpp').read_text(encoding='utf-8'))
        def body(signature):
            start = text.index(signature + '\n{\n')
            end = text.index('\n}\n', start) + 3
            return text[start:end]
        end_commands = body('static UINT64 EndCommands()')
        drain = body('static bool DrainGpu()')
        preamble = r'''
#include <cstdint>
#include <cstdio>
#include <cstdlib>
using UINT64=std::uint64_t; using HRESULT=std::int32_t;
constexpr int D3D12_QUERY_TYPE_TIMESTAMP=0;
#define FAILED(hr) ((hr)<0)
struct ID3D12CommandList {};
struct List: ID3D12CommandList {
    HRESULT closed=0; int closes=0;
    HRESULT Close(){++closes;return closed;}
    template<class... T> void EndQuery(T...){}
    template<class... T> void ResolveQueryData(T...){}
};
struct Queue {
    HRESULT result=0; int executed=0, signals=0;
    void ExecuteCommandLists(int,ID3D12CommandList**){++executed;}
    HRESULT Signal(void*,UINT64){++signals;return result;}
};
struct Feed { static constexpr int kFrames=3; };
struct State {
    List *list; Queue *queue; void *fence12; void *fence_event;
    void *ts_heap=nullptr,*ts_read=nullptr;
    int frame_slot=0,width=1,height=1,color_fmt=0,output_fmt=0,home_pitch=0;
    UINT64 home_slice=0, fence_value=0,alloc_fence[3]={};
    unsigned in_pitch[4]={}; bool mask_ok=false;
} g;
static bool nr_external_gpu_quarantined=false;
static int disabled=0, failed=0;
void FeedDisable(const char*){++disabled;}
void FeedFail(const char*){++failed;}
void FeedDrainInfoQueue(const char*){}
void CK(const char*){}
template<class... T>void Log(const char*,T...){}
namespace nr_external_provider_completion {
enum class Result { Complete, SignalFailed, Removed, EventFailed, Timeout };
Result injected=Result::Complete;
Result SignalAndWait(Queue*,void*,void*,UINT64,unsigned){return injected;}
}
static unsigned checks=0;
void Check(bool b){++checks;if(!b){std::printf("failed %u\n",checks);std::exit(1);}}
'''
        exercise = r'''
int main(){
    List list; Queue queue; int fence=0,event=0;
    g={&list,&queue,&fence,&event};
    list.closed=-1;
    Check(EndCommands()==0 && queue.executed==0 && queue.signals==0 && failed==1);
    list.closed=0; queue.result=-1;
    Check(EndCommands()==0 && queue.executed==1 && disabled==1);
    Check(nr_external_gpu_quarantined && g.alloc_fence[g.frame_slot]==1);
    const auto slot=g.frame_slot;
    using namespace nr_external_provider_completion;
    for(auto failure:{Result::SignalFailed,Result::Timeout,Result::EventFailed,Result::Removed}){
        injected=failure;
        Check(!DrainGpu());
        Check(nr_external_gpu_quarantined && g.alloc_fence[slot]==1);
    }
    injected=Result::Complete;
    Check(DrainGpu() && !nr_external_gpu_quarantined && g.alloc_fence[slot]==0);
    queue.result=0;
    const auto before=g.fence_value;
    Check(EndCommands()==before+1 && g.alloc_fence[slot]==before+1);
    std::printf("actual Feeder EndCommands/DrainGpu: %u assertions passed (fault injection, not GPU)\n",checks);
}
'''
        with tempfile.TemporaryDirectory(prefix='nr-feeder-completion-') as tmp:
            cpp = Path(tmp) / 'completion.cpp'
            exe = Path(tmp) / 'completion.exe'
            cpp.write_text('#include <initializer_list>\n' + preamble + end_commands + drain + exercise, encoding='utf-8')
            subprocess.run([cxx, '-std=c++20', '-O2', str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
