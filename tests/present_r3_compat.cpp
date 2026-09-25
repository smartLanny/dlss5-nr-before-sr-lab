#include "nr_late_present_receipt.hpp"
#include "baseline_receipt_generated.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>
#include <random>
using namespace nr_late_present;
Candidate good(unsigned w=1920,unsigned h=1080,Id stream=1){
    return {stream,1,100,100,100,100,7,9,10,5,w,h,true,true,true,true,false,false,false,false};
}
int main(){unsigned cases=0;
    Request q{7,3840,2160,5,false,false,false};
    {
        Candidate a[]={good(1920,1080,1),good(3840,2160,2)};
        assert(Select(q,a,2).index==1);std::swap(a[0],a[1]);assert(Select(q,a,2).index==0);++cases;
    }
    {
        Candidate a[]={good(1920,1080,1),good(1280,720,2),good(3840,2160,3)};
        std::array<unsigned,3> order{0,1,2};
        do {Candidate b[]={a[order[0]],a[order[1]],a[order[2]]};auto s=Select(q,b,3);
            assert(s&&b[s.index].stream==3);++cases;}while(std::next_permutation(order.begin(),order.end()));
    }
    {Candidate a[]={good(3840,2160,1),good(3840,2160,2),good()};
        assert(Select(q,a,3).decision==Decision::Ambiguous);++cases;}
    {Candidate a[]={good(1920,1080,1),good(1280,720,2)};
        assert(Select(q,a,2).decision==Decision::Ambiguous);++cases;}
    {Candidate a[]={good(3840,2160,1),good(1920,1080,2)};a[0].consumed=true;
        assert(Select(q,a,2).index==1);++cases;}
    // Actual production hint: no DLSS producer has existed, then appears late.
    {nr_present_mapping::Demands<8> d;
        for(unsigned i=0;i<10000;++i)assert(d.Needed(7,5,{1920,1080}));++cases;
        d.Publish(10,7,5,{3840,2160});assert(d.Needed(7,5,{1920,1080}));++cases;
        d.Publish(11,7,5,{800,600});assert(d.Needed(7,5,{1920,1080}));++cases;
        assert(d.Needed(7,6,{1920,1080}));++cases; // mode/F6 generation changed
        d.Forget(10);assert(!d.Needed(7,5,{1920,1080}));++cases; // only positively incompatible extent
        d.Publish(10,7,5,{3840,2160});assert(d.Needed(7,5,{1920,1080}));++cases;
        d.Publish(10,7,5,{3840,1600});assert(!d.Needed(7,5,{1920,1080}));++cases;
        d.Publish(10,7,5,{3840,2160});assert(d.Needed(7,5,{1920,1080}));++cases;
        d.Forget(10);d.Forget(11);assert(d.Needed(7,5,{1920,1080}));++cases;
        for(unsigned i=1;i<=20;++i)d.Publish(i,7,5,{800,600});
        assert(d.Needed(7,5,{1920,1080}));++cases; // overflow is not rejection
        for(unsigned i=1;i<=20;++i)d.Forget(i);
        d.Publish(100,7,5,{3840,2160});assert(d.Needed(7,5,{1920,1080}));++cases;
        assert(d.Needed(8,5,{1920,1080}));++cases; // unrelated device
        d.Publish(100,7,5,{0,0});assert(d.Needed(7,5,{1920,1080}));++cases;
    }
    // D18 exact-match accepted outcomes remain accepted with the SAME source
    // even when additional fully admissible scaled streams are appended.
    std::mt19937 rng(0xD183);unsigned trials=0;
    for(unsigned n=0;n<20000;++n){
        std::array<Candidate,8> a{};std::array<baseline_late_present::Candidate,8> b{};
        for(unsigned j=0;j<8;++j){a[j]=good(j<4?3840:1920,j<4?2160:1080,1+rng()%3);
            auto& c=a[j];c.recorded=(rng()%5)!=0;c.consumed=(rng()%7)==0;
            c.submitted=(rng()%7)!=0;c.latest_sequence=100+rng()%3;
            b[j]={c.stream,c.epoch,c.sequence,c.latest_sequence,c.opportunity,c.latest_device_opportunity,
                c.device,c.producer_queue,c.producer_fence_value,c.config_generation,c.output_width,c.output_height,
                c.recorded,c.native_result_known,c.native_succeeded,c.submitted,c.consumed,c.inline_processed,c.fg_consumed,c.poisoned};
        }
        baseline_late_present::Request original{7,3840,2160,5,false,false,false};
        auto old=baseline_late_present::Select(original,b.data(),b.size());auto now=Select(q,a.data(),a.size());
        if(old){assert(now&&now.index==old.index&&now.age==old.age);++trials;}
        if(old.decision==baseline_late_present::Decision::Ambiguous)assert(now.decision==Decision::Ambiguous);
    }
    assert(trials>100);
    std::cout<<"R3 compatibility scenarios="<<cases<<" randomized inventories=20000 exact-accepted-preserved="<<trials<<"\n";
}
