#include "nr_descriptor_provenance.h"
#include "r2_descriptor_generated.h"
#include <cassert>
#include <iostream>
#include <random>
#include <memory>
#include <thread>
#include <vector>
using Id=std::uint64_t;
using A=nr_descriptor::Registry<16,128,64,128>;
using B=r2_descriptor::Registry<16,128,64,128>;
template<class X,class Y> void same(const X& x,const Y& y){
    assert(bool(x)==bool(y));assert(unsigned(x.reason)==unsigned(y.reason));
    assert(x.shader_visible==y.shader_visible);
    if(x){assert(x.stamp.heap==y.stamp.heap&&x.stamp.index==y.stamp.index&&x.stamp.revision==y.stamp.revision&&x.stamp.epoch==y.stamp.epoch);
        assert(x.view.format==y.view.format&&x.view.resource.id==y.view.resource.id);}
}
int main(){unsigned seeds=6,rounds=20000;
    for(unsigned seed=0;seed<seeds;++seed){
        A a;B b;Id ids[8]{};Id next=1;std::mt19937 rng(0x252183+seed);
        a.ObserveResource({50,7,999999});b.ObserveResource({50,7,999999});
        const auto create=[&](unsigned i){ids[i]=next++;bool gpu=i>=4;Id cpu=1000+i*1000;
            assert(a.ObserveHeap({ids[i],7,100000+i,cpu,gpu?1000000+cpu:0,64,4,0,gpu}));
            assert(b.ObserveHeap({ids[i],7,100000+i,cpu,gpu?1000000+cpu:0,64,4,0,gpu}));};
        for(unsigned i=0;i<8;++i)create(i);
        for(unsigned n=0;n<rounds;++n){unsigned i=rng()%8,j=rng()%64,op=rng()%7;Id dest=1000+i*1000+4*j;
            if(op<2){unsigned f=10+rng()%2;
                auto wa=a.BeginWrite(dest,0,{{50,7,999999},nr_descriptor::Kind::Uav,f,0,0,64,64,true,true,true});
                auto wb=b.BeginWrite(dest,0,{{50,7,999999},r2_descriptor::Kind::Uav,f,0,0,64,64,true,true,true});
                if(op==1){a.RetireHeap(ids[i]);b.RetireHeap(ids[i]);create(i);}
                a.Finish(wa);b.Finish(wb);
            }else if(op==2){a.RetireHeap(ids[i]);b.RetireHeap(ids[i]);create(i);
            }else if(op==3){Id src=1000+(rng()%4)*1000+4*(rng()%64);
                auto wa=a.BeginCopy(dest,src,0);auto wb=b.BeginCopy(dest,src,0);a.Finish(wa);b.Finish(wb);
            }else if(op==4 || op==5){
                Id first=dest+(rng()%4),last=first+rng()%512;unsigned type=(rng()%6)?0:2;
                nr_descriptor::UnknownRange ra[]{ {first,last,type},{1000,1012,0} };
                r2_descriptor::UnknownRange rb[]{{first,last,type},{1000,1012,0}};
                nr_descriptor::UnknownCopy ca;r2_descriptor::UnknownCopy cb;
                a.BeginUnknownCopy(ca,ra,2);b.BeginUnknownCopy(cb,rb,2);
                same(a.Cpu(dest,0),b.Cpu(dest,0));
                if(op==5){a.RetireHeap(ids[i]);b.RetireHeap(ids[i]);create(i);}
                a.EndUnknownCopy(ca);b.EndUnknownCopy(cb);
            }else{
                nr_descriptor::UnknownCopy ca;r2_descriptor::UnknownCopy cb;
                a.BeginUnknownCopy(ca,nullptr,1);b.BeginUnknownCopy(cb,nullptr,1);
                same(a.Cpu(dest,0),b.Cpu(dest,0));a.EndUnknownCopy(ca);b.EndUnknownCopy(cb);
            }
            for(unsigned k=0;k<8;++k){Id cpu=1000+k*1000+4*(rng()%64);same(a.Cpu(cpu,0),b.Cpu(cpu,0));
                if(k>=4)same(a.Gpu(1000000+cpu,0),b.Gpu(1000000+cpu,0));}
            const auto x=a.SnapshotCounters();const auto y=b.SnapshotCounters();
            assert(x.writes==y.writes&&x.copies==y.copies&&x.evictions==y.evictions&&x.invalidations==y.invalidations&&x.unknown_writes==y.unknown_writes);
            assert(x.scoped_copies==y.scoped_copies&&x.global_copies==y.global_copies&&x.heaps_retired==y.heaps_retired);
            assert(x.cpu_slots.active==y.cpu_slots.active&&x.gpu_slots.active==y.gpu_slots.active);
        }
        a.RetireDevice(7);b.RetireDevice(7);assert(a.SnapshotCounters().cpu_slots.active==0&&a.SnapshotCounters().gpu_slots.active==0);
    }
    // Actual threads, full mirror enabled; separate descriptor lanes, shared
    // production mutex/index and repeated pending-write heap retirement.
    A shared;shared.ObserveResource({50,7,999999});std::vector<std::thread> threads;
    for(unsigned k=0;k<4;++k)threads.emplace_back([&,k]{
        const Id base=1000+k*1000;Id id=100000+k*10000;
        for(unsigned n=0;n<2000;++n,++id){
            shared.ObserveHeap({id,7,100000+k,base,0,64,4,0,false});
            Id dst[4]{base,base+4,base+8,base+12},src[4]{base+16,base+16,base+16,base+16};
            shared.Finish(shared.BeginWrite(base+16,0,{{50,7,999999},nr_descriptor::Kind::Uav,10,0,0,64,64,true,true,true}));
            nr_descriptor::Write ws[4];shared.BeginCopies(4,dst,src,0,ws);shared.FinishCopies(4,ws);
            assert(shared.Cpu(base,0));
            nr_descriptor::UnknownRange range{base+1,base+12,0};nr_descriptor::UnknownCopy scope;
            shared.BeginUnknownCopy(scope,&range,1);assert(shared.Cpu(base,0));assert(!shared.Cpu(base+4,0));
            shared.EndUnknownCopy(scope);shared.RetireHeap(id);assert(!shared.Cpu(base,0));
        }
    });
    for(auto& thread:threads)thread.join();const auto stats=shared.SnapshotCounters();
    assert(stats.heaps_retired==8000&&stats.cpu_slots.active==0&&stats.copy_begin_batches==8000);
#if NR_DESCRIPTOR_PROFILE
    assert(stats.profile_enabled&&stats.copy_begin_cpu_ns>0&&stats.copy_finish_cpu_ns>0&&stats.heap_retire_cpu_ns>0&&stats.unknown_cpu_ns>0);
#else
    assert(!stats.profile_enabled&&stats.copy_begin_cpu_ns==0&&stats.copy_finish_cpu_ns==0&&stats.heap_retire_cpu_ns==0&&stats.unknown_cpu_ns==0);
#endif
    std::cout<<"R3/R2 lifecycle-range differential operations="<<seeds*rounds<<" full-mirror threaded lifecycles=8000 profile="<<stats.profile_enabled<<"\n";
}
