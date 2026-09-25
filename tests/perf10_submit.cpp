// Actual OnExecute and GuideCopiesSubmitted bodies with deterministic native
// queue doubles. No NVIDIA inference; the existing transport WARP remains required.
#include <atomic>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <random>
#include <chrono>
#include <string>
#include "nr_source_frame.hpp"
using namespace nr_source_frame;
using HRESULT=long;using UINT64=std::uint64_t;
#define FAILED(X) ((X)<0)
struct ID3D12CommandList{};using ID3D12GraphicsCommandList=ID3D12CommandList;
struct ID3D12CommandQueue{bool fail=false;};struct ID3D12Fence{unsigned id=0;};
struct Copy{Source source{};Recording recording{};ID3D12Fence* producer_fence=nullptr;Id producer_value=0,queue=0;bool recorded=false,submitted=false,poisoned=false;std::array<char,2048> payload{};};
struct Diagnostic{unsigned signal_failure=0,submitted=0;};
#define ENV \
Copy guide_copies[8]{};Diagnostic guide_receipt_diagnostics{}; \
std::atomic<bool> guides_awaiting_submission{false},source_receipts_pending{false}; \
namespace nr_processing_runtime {std::atomic<bool> work_enabled{true};} \
unsigned lock_depth=0;struct Lock{Lock(){++lock_depth;}~Lock(){--lock_depth;}};Lineage lineage; \
unsigned collects=0,queue_reads=0,record_reads=0,guide_visits=0;std::vector<Id> signals; \
void CollectGuideInputs(){++collects;} \
Recording RecordingOf(ID3D12GraphicsCommandList* p){++record_reads;Id x=reinterpret_cast<Id>(p);return x?Recording{x,1+(x>>8),1+(x&3),1}:Recording{};} \
Id QueueOf(ID3D12CommandQueue* p){++queue_reads;return p?7:0;} \
HRESULT ID3D12CommandQueue_Signal(ID3D12CommandQueue* q,ID3D12Fence* f,Id value){signals.push_back(Id(f->id)*1000+value);return q->fail?-1:0;}
namespace baseline {ENV
#include "submit_old.inl"
}
namespace candidate {ENV
#include "submit_current.inl"
}
#undef ENV
static void check(bool v,const char* why){if(!v){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
static ID3D12CommandList* ptr(unsigned n){return reinterpret_cast<ID3D12CommandList*>(Id(n+1)*16);}
static bool eq(Label a,Label b){return a.source==b.source&&a.role==b.role&&a.queue==b.queue&&a.submission==b.submission&&a.submitted==b.submitted&&a.content==b.content&&a.exact_write==b.exact_write&&a.tainted==b.tainted&&a.processed==b.processed&&a.fg_consumed==b.fg_consumed&&a.encoding==b.encoding;}
static void prepare(unsigned seed,bool empty=false){
 std::mt19937 rng(seed);baseline::lineage={};candidate::lineage={};baseline::guide_receipt_diagnostics={};candidate::guide_receipt_diagnostics={};baseline::signals.clear();candidate::signals.clear();
 bool pending=false;
 static ID3D12Fence fences[8]{{1},{2},{3},{4},{5},{6},{7},{8}};
 for(unsigned i=0;i<8;++i){Copy c;c.recording=baseline::RecordingOf(ptr(rng()%12));c.recorded=!empty&&bool(rng()%2);c.submitted=bool(rng()%2);c.poisoned=bool(rng()%3==0);c.producer_fence=rng()%5?&fences[i]:nullptr;c.producer_value=rng()%17==0?UINT64_MAX:rng()%40;
  baseline::guide_copies[i]=candidate::guide_copies[i]=c;pending|=c.recorded&&!c.submitted&&!c.poisoned;}
 // Conservative true is allowed after Reset. False is never allowed with a
 // published pending copy: production capture sets true under the same lock.
 baseline::guides_awaiting_submission=candidate::guides_awaiting_submission=pending||(!empty&&seed%7==0);
 baseline::nr_processing_runtime::work_enabled=candidate::nr_processing_runtime::work_enabled=empty||seed%5!=0;
 for(unsigned i=0;i<12;++i){auto r=baseline::RecordingOf(ptr(i));Resource v{100+i,1,1,0};Label l;l.source={20,1,1+i};l.role=Role::Application;
  baseline::lineage.Set(r,v,l);candidate::lineage.Set(r,v,l);}
 baseline::source_receipts_pending=candidate::source_receipts_pending=true;
 baseline::collects=candidate::collects=baseline::queue_reads=candidate::queue_reads=baseline::record_reads=candidate::record_reads=baseline::guide_visits=candidate::guide_visits=0;
}
static void same(){
 check(baseline::collects==candidate::collects&&baseline::queue_reads==candidate::queue_reads&&baseline::record_reads==candidate::record_reads,"collection/identity side effect");
 check(baseline::signals==candidate::signals,"fence signal order/value");check(baseline::guides_awaiting_submission==candidate::guides_awaiting_submission&&baseline::source_receipts_pending==candidate::source_receipts_pending,"pending summaries");
 check(baseline::guide_receipt_diagnostics.submitted==candidate::guide_receipt_diagnostics.submitted&&baseline::guide_receipt_diagnostics.signal_failure==candidate::guide_receipt_diagnostics.signal_failure,"diagnostics");
 for(unsigned i=0;i<8;++i){auto a=baseline::guide_copies[i],b=candidate::guide_copies[i];check(a.recorded==b.recorded&&a.recording==b.recording&&a.poisoned==b.poisoned&&a.submitted==b.submitted&&a.producer_value==b.producer_value&&a.queue==b.queue,"guide state");}
 for(unsigned i=0;i<12;++i)check(eq(baseline::lineage.Lookup({100+i,1,1,0}),candidate::lineage.Lookup({100+i,1,1,0})),"submitted source evidence");
}
static void semantics(){std::mt19937 rng(10277);
 for(unsigned round=0;round<5000;++round){prepare(round);std::array<ID3D12CommandList*,256> lists{};unsigned count=rng()%25;for(unsigned i=0;i<count;++i)lists[i]=rng()%7?ptr(rng()%12):nullptr;
  ID3D12CommandQueue queue{bool(round%9==0)};auto* q=round%13?&queue:nullptr;auto* ls=round%17?lists.data():nullptr;
  bool success=round%11!=0;if(round%19==0)count=257;
  baseline::OnExecute(q,count,ls,success);candidate::OnExecute(q,count,ls,success);same();
  baseline::OnExecute(q,count,ls,success);candidate::OnExecute(q,count,ls,success);same();}
 std::puts("PASS 5000 mixed submitted/poisoned/overflow/failed-signal/disabled/duplicate-list batches and replay, exact fence/queue/source results");}
static int budget(){prepare(1,true);std::array<ID3D12CommandList*,12> lists{};for(unsigned i=0;i<12;++i)lists[i]=ptr(i);ID3D12CommandQueue q;
 for(unsigned n=0;n<1000;++n)candidate::OnExecute(&q,12,lists.data(),true);
 std::printf("empty-guide payload visits=%u over 12000 list submissions\n",candidate::guide_visits);return candidate::guide_visits==0?0:3;}
int main(int argc,char**argv){if(argc>1&&std::string(argv[1])=="--budget")return budget();semantics();}
