// Reuse the actual-header differential fixtures, not a second implementation.
#define main perf3_fixture_main
#include "perf3_hotpaths.cpp"
#undef main

struct CurrentMany { using Registry=nr_descriptor::Registry<4096,32,8192,8192>;
    using Range=nr_descriptor::UnknownRange;using Scope=nr_descriptor::UnknownCopy;
    using View=nr_descriptor::View;using Kind=nr_descriptor::Kind; };
struct OldMany { using Registry=perf3_reference_descriptor::Registry<4096,32,8192,8192>;
    using Range=perf3_reference_descriptor::UnknownRange;using Scope=perf3_reference_descriptor::UnknownCopy;
    using View=perf3_reference_descriptor::View;using Kind=perf3_reference_descriptor::Kind; };
static constexpr unsigned many_heaps=1024;
static Id heap_base(unsigned i){return 0x10000000ULL+Id(i)*4096;}
template<class T> auto many_registry() {
    auto r=std::make_unique<typename T::Registry>();
    r->ObserveResource({1,1,1});
    typename T::View v{{1,1,1},T::Kind::Uav,28,0,0,8,8,true,true,true};
    for(unsigned i=0;i<many_heaps;++i) {
        require(r->ObserveHeap({i+1,1,0x800000+i,heap_base(i),0,64,32,i%2?2u:0u,false}),"many heaps");
        for(unsigned j=0;j<4;++j)r->Finish(r->BeginWrite(heap_base(i)+32*j,i%2?2u:0u,v));
    }
    return r;
}
template<class T> static std::vector<Id> many_trace() {
    auto p=many_registry<T>();auto& r=*p;std::vector<Id> out;
    for(unsigned round=0;round<6;++round) {
        std::vector<typename T::Range> ranges;
        for(unsigned i=0;i<128;++i) {
            const unsigned h=(i+800)%many_heaps;
            Id first=heap_base(h)+(i%3==0?1:32);
            ranges.push_back({first,first+63,round%2?2u:0u});
        }
        if(round==2)ranges.push_back({heap_base(0),heap_base(many_heaps-1)+2016,0});
        if(round==3)for(unsigned n=0;n<128;++n)ranges.push_back(ranges[0]);
        if(round==4)r.RetireHeap(901);
        typename T::Scope a,b;
        r.BeginUnknownCopy(a,ranges.data(),static_cast<unsigned>(ranges.size()));
        r.BeginUnknownCopy(b,ranges.data()+20,40);
        for(unsigned i=0;i<many_heaps;i+=3)receipt(out,r.Cpu(heap_base(i)+32,i%2?2u:0u));
        r.EndUnknownCopy(a);r.EndUnknownCopy(b);
        for(unsigned i=0;i<many_heaps;++i)for(unsigned j=0;j<4;++j)receipt(out,r.Cpu(heap_base(i)+32*j,i%2?2u:0u));
    }
    return out;
}
static int outer_budget() {
    auto r=many_registry<CurrentMany>();std::vector<nr_descriptor::UnknownRange> ranges;
    // Most heaps are irrelevant; many disjoint ranges hit the far end.
    for(unsigned n=0;n<128;++n)ranges.push_back({heap_base(800+n)+32,heap_base(800+n)+64,n%2?2u:0u});
#if PERF4_COUNT_WORK
    nr_descriptor::perf4_heap_tests=0;
#endif
    nr_descriptor::UnknownCopy scope;r->BeginUnknownCopy(scope,ranges.data(),unsigned(ranges.size()));r->EndUnknownCopy(scope);
#if PERF4_COUNT_WORK
    const auto work=nr_descriptor::perf4_heap_tests;
    std::printf("outer heap/range candidate tests=%llu (budget 2048)\n",static_cast<unsigned long long>(work));
    return work<=2048?0:3;
#else
    return 0;
#endif
}
template<class T> static void prime_values(typename T::Lineage& l) {
    for(Id i=1;i<=256;++i) {
        typename T::Label v{};v.source={1,1,i};v.role=(i%3)?T::Role::Application:T::Role::Generated;
        v.tainted=(i%5==0);v.fg_consumed=(i%7==0);v.encoding=unsigned(i%4);
        l.Presented({i,1,1,0},v,10);
    }
}
static int saturation_budget() {
    auto l=std::make_unique<nr_source_frame::Lineage>();prime_values<CurrentSource>(*l);
    for(Id i=1;i<=64;++i)require(l->InvalidateDevice({i,i,1,1}),"saturation fill");
#if PERF4_COUNT_WORK
    nr_source_frame::perf4_unknown_writes=0;nr_source_frame::perf4_batch_tests=0;
#endif
    for(unsigned n=0;n<4000;++n)for(Id i=65;i<69;++i)
        require(!l->InvalidateDevice({i,i,1,1}),"full table remains fail closed");
    auto result=l->Lookup({1,1,1,0});require(!result.source && result.tainted && !result.submitted,"source erased, sticky flags retained");
#if PERF4_COUNT_WORK
    std::printf("saturation label writes=%llu batch tests=%llu (budgets 256/1024)\n",
        static_cast<unsigned long long>(nr_source_frame::perf4_unknown_writes),static_cast<unsigned long long>(nr_source_frame::perf4_batch_tests));
    return nr_source_frame::perf4_unknown_writes<=256 && nr_source_frame::perf4_batch_tests<=1024?0:3;
#else
    return 0;
#endif
}
template<class T> static std::vector<Id> saturation_trace() {
    auto l=std::make_unique<typename T::Lineage>();prime_values<T>(*l);std::vector<Id> out;
    const typename T::Resource resource{1,1,1,0};
    typename T::Label v{};v.source={8,9,10};v.role=T::Role::Application;
    for(Id i=1;i<=64;++i)out.push_back(l->Set({i,i,1,1},{i,1,1,0},v));
    for(unsigned step=0;step<3000;++step) {
        const Id n=65+step%12;
        const typename T::Recording reject{n,n,1,1};
        out.push_back(l->InvalidateDevice(reject));
        if(step%13==0)l->Presented(resource,v,10);
        if(step%17==0)out.push_back(l->Submit({1,1,1,1},10,step%2));
        if(step%19==0)l->RetireSource(0,0); // retired default labels must be recanonicalized
        if(step%23==0)l->DescribeSurface(resource,unsigned(step%4));
        if(step%29==0)l->RetireSource(8,9);
        if(step%31==0)l->Forget({2,1,1,0});
        out.push_back(l->Invalidate(reject,resource));
        label(out,l->Lookup(resource));label(out,l->Lookup({2,1,1,0}));
        if(step%127==0)for(Id i=1;i<=256;++i)label(out,l->Lookup({i,1,1,0}));
    }
    // Negative misses cannot survive freeing/republishing a slot; positive
    // cache entries must still check reset/lifetime/device identity.
    for(Id i=65;i<81;++i) {
        l->Reset({1,1,1,1});l->ForgetList(i-1);
        const typename T::Recording r{i,i,1,1};
        out.push_back(l->Set(r,resource,v));out.push_back(l->Submit(r,10,true));label(out,l->Lookup(resource));
        out.push_back(l->InvalidateDevice({i+1000,i+1000,1,1}));label(out,l->Lookup(resource));
    }
    auto copied=std::make_unique<typename T::Lineage>(*l);
    copied->Presented(resource,v,11);copied->InvalidateDevice({9999,9999,1,1});
    label(out,copied->Lookup(resource));label(out,l->Lookup(resource));
    out.push_back(l->Submit({},10,true));return out;
}
template<class T> static void bench_many(const char* name,bool dense) {
    auto r=many_registry<T>();std::vector<typename T::Range> ranges;
    for(unsigned n=0;n<128;++n) {
        if(dense)ranges.push_back({heap_base(0),heap_base(many_heaps-1)+2016,n%2?2u:0u});
        else ranges.push_back({heap_base(800+n)+32,heap_base(800+n)+64,n%2?2u:0u});
    }
    auto start=std::chrono::steady_clock::now();
    for(unsigned n=0;n<300;++n){typename T::Scope scope;r->BeginUnknownCopy(scope,ranges.data(),unsigned(ranges.size()));r->EndUnknownCopy(scope);}
    std::printf("BENCH outer %s dense=%u ms=%.3f\n",name,unsigned(dense),std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
}
template<class T> static void bench_saturated(const char* name) {
    auto l=std::make_unique<typename T::Lineage>();prime_values<T>(*l);
    for(Id i=1;i<=64;++i)require(l->InvalidateDevice({i,i,1,1}),"bench fill");
    auto start=std::chrono::steady_clock::now();
    for(unsigned n=0;n<25000;++n)for(Id i=65;i<69;++i)require(!l->InvalidateDevice({i,i,1,1}),"bench failed recording");
    std::printf("BENCH saturation %s ms=%.3f\n",name,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
}
int main(int argc,char** argv) {
    const std::string mode=argc>1?argv[1]:"semantics";
    if(mode=="--outer-budget")return outer_budget();
    if(mode=="--saturation-budget")return saturation_budget();
    if(mode=="--bench") {
        for(bool dense:{false,true}){bench_many<OldMany>("perf3",dense);bench_many<CurrentMany>("perf4",dense);}
        bench_saturated<OldSource>("perf3");bench_saturated<CurrentSource>("perf4");return 0;
    }
    require(many_trace<CurrentMany>()==many_trace<OldMany>(),"many-heap exact receipt differential");
    require(saturation_trace<CurrentSource>()==saturation_trace<OldSource>(),"saturation, republish, retirement, reset differential");
    require(descriptor_trace<CurrentDescriptor>()==descriptor_trace<OldDescriptor>(),"legacy descriptor differential");
    require(lineage_trace<CurrentSource>()==lineage_trace<OldSource>(),"legacy lineage differential");
    std::puts("PASS many heaps/ranges, repeated saturation with republishing, and legacy exact public behavior");return 0;
}
