// CPU-only tests of the actual descriptor and lineage headers. The reference
// headers are generated from the frozen, owner-tested Lock2 Git revision.
#include "nr_descriptor_provenance.h"
#include "nr_source_frame.hpp"
#include "perf3_reference_descriptor.h"
#include "perf3_reference_source.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <string>
#include <vector>

static void require(bool ok,const char* message) {
    if(!ok){std::fprintf(stderr,"FAIL %s\n",message);std::exit(1);}
}
using Id=std::uint64_t;
struct CurrentDescriptor {
    using Registry=nr_descriptor::Registry<32,64,8192,8192>;
    using Range=nr_descriptor::UnknownRange;using Scope=nr_descriptor::UnknownCopy;
    using View=nr_descriptor::View;using Kind=nr_descriptor::Kind;
};
struct OldDescriptor {
    using Registry=perf3_reference_descriptor::Registry<32,64,8192,8192>;
    using Range=perf3_reference_descriptor::UnknownRange;using Scope=perf3_reference_descriptor::UnknownCopy;
    using View=perf3_reference_descriptor::View;using Kind=perf3_reference_descriptor::Kind;
};
template<class T> auto descriptor() {
    auto r=std::make_unique<typename T::Registry>();
    r->ObserveResource({1,1,0x900000});
    require(r->ObserveHeap({1,1,0x910000,0x100000,0,4096,32,0,false}),"CPU heap");
    require(r->ObserveHeap({2,1,0x920000,0x200000,0x400000,4096,32,0,true}),"GPU heap");
    const typename T::View v{{1,1,0x900000},T::Kind::Uav,28,0,0,16,16,true,true,true};
    for(unsigned i=0;i<4096;++i)for(Id base:{0x100000ULL,0x200000ULL})
        r->Finish(r->BeginWrite(base+32*i,0,v));
    return r;
}
template<class R> static void receipt(std::vector<Id>& out,const R& r) {
    // Include UNKNOWN receipt payloads, not just successful observations.
    out.insert(out.end(),{Id(r.reason),r.shader_visible,r.stamp.heap,r.stamp.index,
        r.stamp.epoch,r.stamp.revision,r.view.known,Id(r.view.kind),r.view.resource.id,
        r.view.resource.device,r.view.resource.address,r.view.format,r.view.mip,r.view.plane,
        r.view.width,r.view.height,r.view.typed,r.view.shape_known});
}
template<class T> static std::vector<Id> descriptor_trace() {
    auto p=descriptor<T>();auto& r=*p;std::vector<Id> out;
    const typename T::View v{{1,1,0x900000},T::Kind::Uav,28,0,0,16,16,true,true,true};
    std::mt19937 rng(2770318);
    for(unsigned n=0;n<800;++n) {
        const unsigned index=rng()%3500;const Id h=0x100000+32*index;
        auto pending=r.BeginWrite(h,0,v); // invalidation must defeat a late Finish
        receipt(out,r.Cpu(h,0));
        typename T::Range ranges[]={{h+(n%3),h+32*(1+rng()%600),0},
                                    {h+32,h+32*3,0},{0x200001,0x200061,0}};
        typename T::Scope a,b;
        r.BeginUnknownCopy(a,ranges,3);r.BeginUnknownCopy(b,ranges+1,1);
        receipt(out,r.Cpu(h,0));receipt(out,r.Cpu(h+32,0));
        r.EndUnknownCopy(a);r.Finish(pending);
        receipt(out,r.Cpu(h+32,0));r.EndUnknownCopy(b);
        receipt(out,r.Cpu(h,0));receipt(out,r.Cpu(h+32,0));receipt(out,r.Gpu(0x400020,0));
        r.Finish(r.BeginWrite(h,0,v));receipt(out,r.Cpu(h,0));
    }
    // Live overlapping heaps, wrong types, unaligned endpoints, high addresses,
    // global malformed ranges, and retirement during an in-flight scope.
    require(r.ObserveHeap({3,1,0x930000,0x100001,0,64,31,0,false}),"overlap observed");
    require(r.ObserveHeap({4,1,0x940000,0x100000,0,64,32,2,false}),"different type observed");
    const Id high=std::numeric_limits<Id>::max()-4095;
    require(r.ObserveHeap({5,1,0x950000,high,0,128,32,0,false}),"high handles accepted");
    r.Finish(r.BeginWrite(high+32,0,v));
    typename T::Range ranges[]={{0x100000,0x100200,0},{high+1,high+63,0}};
    typename T::Scope scope;r.BeginUnknownCopy(scope,ranges,2);
    receipt(out,r.Cpu(high+32,0));receipt(out,r.Cpu(0x100020,2));
    r.RetireHeap(3);r.EndUnknownCopy(scope);receipt(out,r.Cpu(high+32,0));
    r.BeginUnknownCopy(scope,nullptr,1);receipt(out,r.Cpu(0x100000,0));r.EndUnknownCopy(scope);
    r.RetireDevice(1);const auto c=r.SnapshotCounters();
    out.insert(out.end(),{c.writes,c.copies,c.unknown_writes,c.overlaps,c.evictions,c.invalidations,
        c.resources_retired,c.heaps_retired,c.scoped_copies,c.global_copies,c.cpu_slots.active,c.gpu_slots.active});
    return out;
}
static int descriptor_budget() {
    auto r=descriptor<CurrentDescriptor>();const auto before=r->SnapshotCounters();
    nr_descriptor::UnknownRange range{0x100020,0x100100,0};nr_descriptor::UnknownCopy scope;
    require(bool(r->Cpu(0x100000,0)),"unrelated source initially known");
    r->BeginUnknownCopy(scope,&range,1);
    require(bool(r->Cpu(0x100000,0))&&!r->Cpu(0x100020,0),"only targeted slots pending");
    r->EndUnknownCopy(scope);
    require(bool(r->Cpu(0x100000,0))&&!r->Cpu(0x100020,0),"unknown values cannot reappear");
    const auto visits=r->SnapshotCounters().unknown_slot_visits-before.unknown_slot_visits;
    std::printf("descriptor narrow range visits=%llu (budget 128)\n",static_cast<unsigned long long>(visits));
    return visits<=128?0:3;
}

template<class L> static void label(std::vector<Id>& out,const L& v) {
    out.insert(out.end(),{v.source.stream,v.source.epoch,v.source.sequence,Id(v.role),v.queue,v.submission,
        v.submitted,v.content,v.exact_write,v.tainted,v.processed,v.fg_consumed,v.encoding});
}
struct CurrentSource {using Lineage=nr_source_frame::Lineage;using Recording=nr_source_frame::Recording;
    using Resource=nr_source_frame::Resource;using Label=nr_source_frame::Label;using Role=nr_source_frame::Role;};
struct OldSource {using Lineage=perf3_reference_source::Lineage;using Recording=perf3_reference_source::Recording;
    using Resource=perf3_reference_source::Resource;using Label=perf3_reference_source::Label;using Role=perf3_reference_source::Role;};
template<class T> static std::vector<Id> lineage_trace() {
    auto p=std::make_unique<typename T::Lineage>();auto& l=*p;
    std::vector<Id> out;std::mt19937 rng(2800318);Id resets[70]{};
    for(auto& reset:resets)reset=1;
    for(unsigned n=0;n<4000;++n) {
        unsigned index=rng()%70;const typename T::Recording r{index+1,index+100,resets[index],1};
        const typename T::Resource dst{1+rng()%20,1+(n/1000),1,0},src{1+rng()%20,1+(n/1000),1,0};
        typename T::Label v{};v.source={1,1,1+n};v.role=(n%3)?T::Role::Application:T::Role::Generated;
        v.processed=(n%7==0);v.fg_consumed=(n%11==0);
        switch(rng()%12) {
        case 0:out.push_back(l.Set(r,dst,v));break;
        case 1:out.push_back(l.Copy(r,dst,src,true));break;
        case 2:out.push_back(l.Invalidate(r,dst));break;
        case 3:out.push_back(l.Clear(r,dst));break;
        case 4:out.push_back(l.InvalidateDevice(r));break;
        case 5:out.push_back(l.Submit(r,(n%5)?10:11,n%9!=0));break;
        case 6:l.Reset(r);++resets[index];break;
        case 7:l.ForgetList(r.list);break;
        case 8:l.Forget(dst);break;
        case 9:l.RetireSource(1,1);break;
        case 10:l.Presented(dst,v,10);break;
        case 11:l.DescribeSurface(dst,1+n%3);break;
        }
        label(out,l.Lookup(dst,r));label(out,l.Lookup(dst));
        out.insert(out.end(),{l.TracksList(r.list,r.lifetime),l.PendingList(r),l.AnyPending()});
    }
    // Invalid keys must retain the reference's search order; copied caches
    // contain relative indices and cannot point back into the original object.
    out.push_back(l.Submit({},10,true));
    l.Reset({});
    auto copied=std::make_unique<typename T::Lineage>(l);
    const typename T::Recording fresh{999,999,1,1};
    const typename T::Resource output{999,1,1,0};
    out.push_back(copied->Invalidate(fresh,output));
    out.push_back(copied->Submit(fresh,10,true));
    label(out,copied->Lookup(output));label(out,l.Lookup(output));
    return out;
}
static int lineage_budget() {
    auto l=std::make_unique<nr_source_frame::Lineage>();
    for(Id i=1;i<=64;++i)require(l->InvalidateDevice({i,i,1,1}),"fill batch table");
#if PERF3_COUNT_SCANS
    nr_source_frame::perf3_batch_scan_visits=0;
#endif
    for(unsigned n=0;n<4000;++n)for(Id i=61;i<=64;++i)
        require(l->InvalidateDevice({i,i,1,1}),"idempotent invalidation retained");
#if PERF3_COUNT_SCANS
    const auto visits=nr_source_frame::perf3_batch_scan_visits;
    std::printf("lineage full-table visits=%llu (budget 256)\n",static_cast<unsigned long long>(visits));
    return visits<=256?0:3;
#else
    return 0;
#endif
}

template<class T> static void descriptor_bench(const char* version,unsigned width) {
    auto r=descriptor<T>();const auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<1000;++i) {
        typename T::Range range{0x100000+32*(i%4),0x100000+32*(i%4+width-1),0};
        typename T::Scope scope;r->BeginUnknownCopy(scope,&range,1);r->EndUnknownCopy(scope);
    }
    const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    std::printf("BENCH descriptor %s width=%u ms=%.4f visits=%llu\n",version,width,ms,
        static_cast<unsigned long long>(r->SnapshotCounters().unknown_slot_visits));
}
template<class T> static void lineage_bench(const char* version) {
    auto l=std::make_unique<typename T::Lineage>();
    for(Id i=1;i<=64;++i)require(l->InvalidateDevice({i,i,1,1}),"benchmark warmup");
    const auto start=std::chrono::steady_clock::now();
    for(unsigned n=0;n<100000;++n)for(Id i=61;i<=64;++i)require(l->InvalidateDevice({i,i,1,1}),"benchmark append");
    std::printf("BENCH lineage %s ms=%.4f\n",version,
        std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
}
int main(int argc,char** argv) {
    const std::string mode=argc>1?argv[1]:"semantics";
    if(mode=="--descriptor-budget")return descriptor_budget();
    if(mode=="--lineage-budget")return lineage_budget();
    if(mode=="--bench") {
        for(unsigned width:{1u,8u,512u,4096u}){descriptor_bench<OldDescriptor>("lock2",width);descriptor_bench<CurrentDescriptor>("perf3",width);}
        lineage_bench<OldSource>("lock2");lineage_bench<CurrentSource>("perf3");return 0;
    }
    require(descriptor_trace<CurrentDescriptor>()==descriptor_trace<OldDescriptor>(),"descriptor receipt/counter differential");
    require(lineage_trace<CurrentSource>()==lineage_trace<OldSource>(),"lineage recording/submission/lifetime differential");
    std::puts("PASS descriptor nested/pending/alignment/overlap/retirement and 4000 lineage state transitions");
}
