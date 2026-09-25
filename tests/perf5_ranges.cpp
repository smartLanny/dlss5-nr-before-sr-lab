// Real production registry and the frozen Perf4 reference, including the
// heap/range fallback rather than only the earlier favourable sparse case.
#define PERF4_MAIN perf4_fixture_main
#include "perf4_hotpaths.cpp"
#undef PERF4_MAIN
#include <type_traits>

template<class T> static unsigned prepare(std::vector<typename T::Range>& ranges) {
    unsigned count=static_cast<unsigned>(ranges.size());
#if PERF5_NORMALIZE
    if constexpr(std::is_same_v<T,CurrentMany>)
        require(nr_descriptor::NormalizeUnknownRanges(ranges.data(),count),"prepare owned ranges");
#endif
    return count;
}
template<class T> static std::vector<typename T::Range> scopes(unsigned mode) {
    std::vector<typename T::Range> ranges;
    for(unsigned i=0;i<256;++i) {
        if(mode==0)ranges.push_back({heap_base(100+i),heap_base(700+i)+31,0});
        else if(mode==1)ranges.push_back({heap_base(0),heap_base(many_heaps-1)+2016,i%2?2u:0u});
        else if(mode==2)ranges.push_back({heap_base(800+i%32)+1,heap_base(800+i%32)+65,i%2?2u:0u});
        else ranges.push_back({heap_base((i*73)%many_heaps)+31,heap_base((i*73)%many_heaps)+96,i%2?2u:0u});
    }
    std::reverse(ranges.begin(),ranges.end());return ranges;
}
template<class T> static std::vector<Id> indexed_trace() {
    auto p=many_registry<T>();auto& r=*p;std::vector<Id> out;
    const typename T::View v{{1,1,1},T::Kind::Uav,28,0,0,8,8,true,true,true};
    for(unsigned mode=0;mode<4;++mode) {
        auto ranges=scopes<T>(mode), nested=scopes<T>((mode+1)%4);
        const unsigned n=prepare<T>(ranges),m=prepare<T>(nested);
        typename T::Scope a,b;
        const auto pending=r.BeginWrite(heap_base(850)+32,0,v);
        r.BeginUnknownCopy(a,ranges.data(),n);r.BeginUnknownCopy(b,nested.data(),m);
        for(unsigned i=0;i<many_heaps;i+=7)receipt(out,r.Cpu(heap_base(i)+32,i%2?2u:0u));
        r.EndUnknownCopy(a);r.Finish(pending);
        for(unsigned i=0;i<many_heaps;i+=7)receipt(out,r.Cpu(heap_base(i)+32,i%2?2u:0u));
        // Changing live membership while indexed scopes exist must not retain
        // stale heap-index pointers or permit an earlier ticket to republish.
        r.RetireHeap(851);r.RetireHeap(852);
        require(r.ObserveHeap({9001+mode,1,0x900000+mode,heap_base(850)+1,0,16,31,0,false}),"new overlapping heap");
        r.EndUnknownCopy(b);
        for(unsigned i=0;i<many_heaps;i+=7)for(unsigned slot=0;slot<4;++slot)
            receipt(out,r.Cpu(heap_base(i)+32*slot,i%2?2u:0u));
        const auto counters=r.SnapshotCounters();
        out.insert(out.end(),{counters.writes,counters.copies,counters.unknown_writes,counters.invalidations,
            counters.scoped_copies,counters.global_copies,counters.heaps_retired});
    }
    // Raw API clients are still supported, including malformed/global scopes.
    typename T::Range invalid{100,99,0};typename T::Scope bad;
    r.BeginUnknownCopy(bad,&invalid,1);receipt(out,r.Cpu(heap_base(3),2));r.EndUnknownCopy(bad);
    return out;
}
template<class T>static std::vector<Id> stride_gap_trace() {
    auto r=std::make_unique<typename T::Registry>();std::vector<Id> out;
    r->ObserveResource({1,1,1});
    const typename T::View v{{1,1,1},T::Kind::Uav,28,0,0,8,8,true,true,true};
    const Id base=0x80000000;
    require(r->ObserveHeap({1,1,1,base,0,256,32,0,false}),"aligned heap");
    require(r->ObserveHeap({2,1,2,base+1,0,256,32,0,false}),"offset overlapping heap");
    std::vector<typename T::Range> ranges;
    for(unsigned i=0;i<128;++i) {
        r->Finish(r->BeginWrite(base+32*i,0,v));r->Finish(r->BeginWrite(base+32*i+1,0,v));
        ranges.push_back({base+32*i+1,base+32*i+1,0});
    }
    const unsigned n=prepare<T>(ranges);typename T::Scope scope;
    r->BeginUnknownCopy(scope,ranges.data(),n);
    for(unsigned i=0;i<128;++i) {
        require(bool(r->Cpu(base+32*i,0)),"stride gaps must remain known during a native copy");
        receipt(out,r->Cpu(base+32*i,0));receipt(out,r->Cpu(base+32*i+1,0));
    }
    r->EndUnknownCopy(scope);
    for(unsigned i=0;i<128;++i) {
        require(bool(r->Cpu(base+32*i,0))&&!r->Cpu(base+32*i+1,0),"only exact addresses become unknown");
        receipt(out,r->Cpu(base+32*i,0));receipt(out,r->Cpu(base+32*i+1,0));
    }
    return out;
}
#if PERF5_NORMALIZE
static void normalized_union() {
    using R=nr_descriptor::UnknownRange;
    const Id high=std::numeric_limits<Id>::max();
    for(unsigned seed=0;seed<64;++seed) {
        std::mt19937 rng(seed+500);std::vector<R> raw;
        for(unsigned i=0;i<256;++i) {const Id a=1+rng()%4096;raw.push_back({a,a+rng()%128,unsigned(rng()%4)});}
        raw.push_back({high-127,high,0});raw.push_back({high-200,high-127,0});
        raw.push_back({high-300,high-201,2});
        auto copy=raw;unsigned n=unsigned(copy.size());
        require(nr_descriptor::NormalizeUnknownRanges(copy.data(),n),"normalization succeeds");
        for(unsigned i=1;i<n;++i) {
            require(copy[i-1].type<=copy[i].type,"type order");
            if(copy[i-1].type==copy[i].type)require(copy[i-1].last<copy[i].first-1,"coalesced union is disjoint");
        }
        for(unsigned type=0;type<5;++type)for(unsigned k=0;k<5000;++k) {
            const Id address=k<4500?k:high-(k-4500);
            bool a=false,b=false;
            for(const auto& r:raw)a|=r.type==type&&r.first<=address&&address<=r.last;
            for(unsigned i=0;i<n;++i){const auto& r=copy[i];b|=r.type==type&&r.first<=address&&address<=r.last;}
            require(a==b,"exact membership preserved, including high addresses and gaps");
        }
    }
    R malformed[]={{90,100,0},{1,0,0},{20,30,0}};
    const auto first=malformed[0];unsigned n=3;
    require(!nr_descriptor::NormalizeUnknownRanges(malformed,n)&&n==3&&malformed[0].first==first.first,"malformed input not sorted or silently dropped");
    n=1;require(!nr_descriptor::NormalizeUnknownRanges(nullptr,n),"missing array rejected");
    n=0;require(nr_descriptor::NormalizeUnknownRanges(nullptr,n),"empty array accepted");
    std::puts("PASS 64 exact range-union cases; byte-address gaps, types, max-address and invalid inputs");
}
#endif
#if PERF5_NORMALIZE
static void counter_coverage() {
    auto r=many_registry<CurrentMany>();
    auto ranges=scopes<CurrentMany>(1);const unsigned count=prepare<CurrentMany>(ranges);
    nr_descriptor::UnknownCopy scope;r->BeginUnknownCopy(scope,ranges.data(),count);r->EndUnknownCopy(scope);
    auto counters=r->SnapshotCounters();
    require(counters.ordered_copy_scopes==1 && counters.ordered_copy_ranges==2 &&
        counters.ordered_fallbacks==2 && counters.raw_copy_scopes==0,"ordered fallback counter records actual Begin/End paths");
    nr_descriptor::UnknownRange raw[]={{heap_base(0),heap_base(many_heaps-1)+2016,2},
                                      {heap_base(0),heap_base(many_heaps-1)+2016,0}};
    r->BeginUnknownCopy(scope,raw,2);r->EndUnknownCopy(scope);
    counters=r->SnapshotCounters();
    require(counters.raw_copy_scopes==1 && counters.linear_fallbacks==2,"raw fallback remains visible and supported");
    std::puts("PASS ordered and raw fallback counters, scope reuse and cleanup");
}
#endif
static int fallback_budget() {
    auto r=many_registry<CurrentMany>();auto ranges=scopes<CurrentMany>(0);
    const unsigned n=prepare<CurrentMany>(ranges);
#if PERF5_COUNT
    nr_descriptor::perf5_predicates=0;
#endif
    nr_descriptor::UnknownCopy scope;r->BeginUnknownCopy(scope,ranges.data(),n);r->EndUnknownCopy(scope);
#if PERF5_COUNT
    const auto work=nr_descriptor::perf5_predicates;
    std::printf("dense overlapping range predicates=%llu prepared_count=%u budget=12000\n",static_cast<unsigned long long>(work),n);
    return work<=12000?0:3;
#else
    return 0;
#endif
}
template<class T>static void timed(const char* version,unsigned mode) {
    auto r=many_registry<T>();const auto input=scopes<T>(mode);
    auto start=std::chrono::steady_clock::now();
    for(unsigned repeat=0;repeat<200;++repeat) {
        auto ranges=input;const unsigned n=prepare<T>(ranges); // include sorting cost
        typename T::Scope scope;r->BeginUnknownCopy(scope,ranges.data(),n);r->EndUnknownCopy(scope);
    }
    std::printf("BENCH %s mode=%u ms=%.3f (includes per-copy range preparation)\n",version,mode,
        std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
}
int main(int argc,char**argv) {
    const std::string mode=argc>1?argv[1]:"semantics";
    if(mode=="--fallback-budget")return fallback_budget();
    if(mode=="--bench") {
        for(unsigned i=0;i<4;++i){timed<OldMany>("perf4",i);timed<CurrentMany>("candidate",i);}return 0;
    }
    require(indexed_trace<CurrentMany>()==indexed_trace<OldMany>(),"prepared range public receipts equal raw Perf4");
    require(stride_gap_trace<CurrentMany>()==stride_gap_trace<OldMany>(),"fragmented range cap and overlapping stride-gap receipts match");
    require(descriptor_trace<CurrentDescriptor>()==descriptor_trace<OldDescriptor>(),"legacy nested raw registry behavior unchanged");
#if PERF5_NORMALIZE
    normalized_union();counter_coverage();
#endif
    std::puts("PASS range preparation/retirement/pending/nested exact differential");return 0;
}
