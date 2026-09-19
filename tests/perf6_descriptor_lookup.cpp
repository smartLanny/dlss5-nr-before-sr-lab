// Exercise actual production headers and the frozen owner-tested Perf5 oracle.
#define main perf3_fixture_main
#include "perf3_hotpaths.cpp"
#undef main

static int lookup_budget() {
    auto r=descriptor<CurrentDescriptor>();
    const nr_descriptor::View v{{1,1,0x900000},nr_descriptor::Kind::Uav,28,0,0,16,16,true,true,true};
    // Repeated writes, Finish, receipt validation and copy consumers reuse keys.
    // Warmup is not counted; a hit never authorizes a pending/stale receipt.
    for(unsigned i=0;i<256;++i)r->Finish(r->BeginWrite(0x100000+32*i,0,v));
#if PERF6_COUNT_WORK
    nr_descriptor::perf6_chain_checks=0;
#endif
    for(unsigned round=0;round<40;++round)for(unsigned i=0;i<256;++i) {
        auto w=r->BeginWrite(0x100000+32*i,0,v);
        require(!r->Cpu(0x100000+32*i,0),"cached pending write rejected");
        r->Finish(w);require(bool(r->Cpu(0x100000+32*i,0)),"cached complete write valid");
    }
#if PERF6_COUNT_WORK
    std::printf("slot Find chain checks=%llu budget=1024\n",static_cast<unsigned long long>(nr_descriptor::perf6_chain_checks));
    return nr_descriptor::perf6_chain_checks<=1024?0:3;
#else
    return 0;
#endif
}
static int sorted_budget() {
    std::vector<nr_descriptor::UnknownRange> ranges;
    for(unsigned i=0;i<2048;++i)ranges.push_back({Id{0x100000}+64*i,Id{0x10001f}+64*i,i/1024});
#if PERF6_COUNT_WORK
    nr_descriptor::perf6_range_compares=0;
#endif
    unsigned count=unsigned(ranges.size());require(nr_descriptor::NormalizeUnknownRanges(ranges.data(),count),"sorted normalization");
    require(count==2048,"address gaps retained");
#if PERF6_COUNT_WORK
    std::printf("sorted range comparisons=%llu budget=2200\n",static_cast<unsigned long long>(nr_descriptor::perf6_range_compares));
    return nr_descriptor::perf6_range_compares<=2200?0:3;
#else
    return 0;
#endif
}
template<class T> static std::vector<Id> cache_lifecycle_trace() {
    using R=typename T::Registry;auto p=std::make_unique<R>();auto& r=*p;std::vector<Id> out;
    r.ObserveResource({1,1,0x900000});
    r.ObserveHeap({1,1,0x910000,0x100000,0,65536,32,0,false});
    const typename T::View v{{1,1,0x900000},T::Kind::Uav,28,0,0,16,16,true,true,true};
    // Force bounded pool eviction and direct-cache conflicts; protected pending
    // slots cannot be recycled, and a stale Finish cannot change a replacement.
    auto pending=r.BeginWrite(0x100000,0,v);
    for(unsigned i=1;i<10000;++i) {
        const Id h=0x100000+32*i;auto w=r.BeginWrite(h,0,v);r.Finish(w);
        if(i%512==0){receipt(out,r.Cpu(h,0));receipt(out,r.Cpu(0x100000,0));}
    }
    r.Finish(pending);receipt(out,r.Cpu(0x100000,0));
    auto stale=r.BeginWrite(0x100000+32,0,v);
    r.RetireHeap(1);r.ObserveHeap({2,1,0x920000,0x100000,0,65536,32,0,false});
    r.Finish(r.BeginWrite(0x100000+32,0,v));r.Finish(stale);receipt(out,r.Cpu(0x100000+32,0));
    r.Invalidate();receipt(out,r.Cpu(0x100000+32,0));
    r.Finish(r.BeginWrite(0x100000+32,0,v));receipt(out,r.Cpu(0x100000+32,0));
    r.RetireResource(1);receipt(out,r.Cpu(0x100000+32,0));
    r.ObserveResource({2,1,0x900000});receipt(out,r.Cpu(0x100000+32,0));
    r.RetireDevice(1);receipt(out,r.Cpu(0x100000+32,0));
    const auto c=r.SnapshotCounters();out.insert(out.end(),{c.writes,c.overlaps,c.evictions,c.invalidations,c.cpu_slots.active});
    return out;
}
static void sort_equivalence() {
    std::mt19937 rng(2860318);
    for(unsigned round=0;round<150;++round) {
        std::vector<nr_descriptor::UnknownRange> a;std::vector<perf3_reference_descriptor::UnknownRange> b;
        for(unsigned i=0;i<64+round;++i) {
            const Id first=round==0?std::numeric_limits<Id>::max()-4096+i*16:1+rng()%8192;
            const Id last=first+rng()%16;const unsigned type=rng()%4;
            a.push_back({first,last,type});b.push_back({first,last,type});
        }
        if(round%3==0) {auto less=[](const auto& x,const auto& y){return x.type!=y.type?x.type<y.type:x.first!=y.first?x.first<y.first:x.last<y.last;};
            std::sort(a.begin(),a.end(),less);std::sort(b.begin(),b.end(),less);}
        if(round%5==0){std::reverse(a.begin(),a.end());std::reverse(b.begin(),b.end());}
        if(round%17==0){a.back().first=0;b.back().first=0;}
        unsigned ca=unsigned(a.size()),cb=unsigned(b.size());
        const bool va=nr_descriptor::NormalizeUnknownRanges(a.data(),ca),vb=perf3_reference_descriptor::NormalizeUnknownRanges(b.data(),cb);
        require(va==vb&&ca==cb,"sort return and count");
        for(unsigned i=0;i<ca;++i)require(a[i].first==b[i].first&&a[i].last==b[i].last&&a[i].type==b[i].type,"exact normalized range");
    }
    std::puts("PASS 150 sorted/reversed/random/invalid range differential cases");
}
template<class T> static void bench_lookup(const char* name,unsigned keys) {
    auto r=descriptor<T>();const typename T::View v{{1,1,0x900000},T::Kind::Uav,28,0,0,16,16,true,true,true};
    auto start=std::chrono::steady_clock::now();
    for(unsigned n=0;n<100000;++n)r->Finish(r->BeginWrite(0x100000+32*(n%keys),0,v));
    std::printf("BENCH writes %s keys=%u ms=%.3f\n",name,keys,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
}
int main(int argc,char** argv) {
    const std::string mode=argc>1?argv[1]:"semantics";
    if(mode=="--lookup-budget")return lookup_budget();
    if(mode=="--sorted-budget")return sorted_budget();
    if(mode=="--bench") {for(unsigned n:{1u,256u,4096u}){bench_lookup<OldDescriptor>("perf5",n);bench_lookup<CurrentDescriptor>("perf6",n);}return 0;}
    require(descriptor_trace<CurrentDescriptor>()==descriptor_trace<OldDescriptor>(),"all descriptor public receipts");
    require(cache_lifecycle_trace<CurrentDescriptor>()==cache_lifecycle_trace<OldDescriptor>(),"eviction/pending/heap-address-reuse/lifetime receipts");
    sort_equivalence();
#if PERF6_HAS_CACHE
    auto r=descriptor<CurrentDescriptor>();const auto old=r->SnapshotCounters();
    for(unsigned i=0;i<32;++i)(void)r->Cpu(0x100000,0);
    const auto now=r->SnapshotCounters();
    require(now.cpu_lookup_hits>old.cpu_lookup_hits,"real query hit counter");
    require(now.gpu_lookup_hits==old.gpu_lookup_hits,"CPU/GPU caches independent");
#endif
    std::puts("PASS exact descriptor/cache lifecycle differential, no game claim");return 0;
}
