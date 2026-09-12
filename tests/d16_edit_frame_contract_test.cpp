#include "nr_edit_shader_constants.hpp"
#include "nr_edit_frame_contract.hpp"
#include <cassert>
#include <iostream>
#include <limits>
using namespace nr_edit;
int main() {
    FrameKey k{1,2,3,4,5,6,7,8,9,10,11,640,360,1920,1080};
    SameFramePair p;
    assert(p.Consume(k,true)==PairResult::Empty);
    assert(!p.Arm(k,false));assert(p.Consume(k,true)==PairResult::Consumed);
    assert(!p.Arm({},true));assert(p.Arm(k,true));
    assert(p.Consume(k,true)==PairResult::Ready);
    assert(p.Consume(k,true)==PairResult::Consumed);
    auto changed=k;changed.source++;
    assert(p.Arm(k,true));assert(p.Consume(changed,true)==PairResult::Mismatch);
    assert(p.Consume(k,true)==PairResult::Consumed);
    assert(p.Arm(k,true));assert(p.Consume(k,false)==PairResult::NativeFailed);
    for(unsigned i=0;i<15;++i) {
        changed=k;
        switch(i){
        case 0:++changed.stream;break;case 1:++changed.epoch;break;
        case 2:++changed.source;break;case 3:++changed.view;break;
        case 4:++changed.device;break;case 5:++changed.queue;break;
        case 6:++changed.recording;break;case 7:++changed.recording_generation;break;
        case 8:++changed.output_lifetime;break;case 9:++changed.configuration;break;
        case 10:++changed.color_domain;break;case 11:++changed.render_width;break;
        case 12:++changed.render_height;break;case 13:++changed.output_width;break;
        case 14:++changed.output_height;break;
        }
        assert(p.Arm(k,true));assert(p.Consume(changed,true)==PairResult::Mismatch);
    }
    p.Begin();assert(p.Consume(k,true)==PairResult::Empty);
    HistoryReceipt previous{1,2,10,4,5,6,7,640,360,true};
    auto current=previous;current.source=14;
    assert(HistoryMatches(previous,current,10,true,false)); // gap + correct reference
    assert(!HistoryMatches(previous,current,13,true,false)); // previous captured != accepted
    assert(!HistoryMatches(previous,current,10,false,false));
    assert(!HistoryMatches(previous,current,10,true,true));
    current.configuration++;assert(!HistoryMatches(previous,current,10,true,false));
    current=previous;current.source=14;current.view++;assert(!HistoryMatches(previous,current,10,true,false));
    current=previous;current.source=14;current.finite_exposure=false;
    assert(!HistoryMatches(previous,current,10,true,false));
    FenceReceipt f{6,20,30,true,false};
    assert(!CanRetire(f,{6,20,29}));assert(CanRetire(f,{6,20,30}));
    assert(!CanRetire(f,{6,20,std::numeric_limits<std::uint64_t>::max()}));
    assert(!CanRetire(f,{7,20,100}));assert(!CanRetire(f,{6,21,100}));
    f.poisoned=true;assert(!CanRetire(f,{6,20,100}));f.poisoned=false;f.submitted=false;
    assert(!CanRetire(f,{6,20,100}));
    std::cout<<"edit contract: seam identity, one-shot consumption, reference pairing and retirement passed\n";
}
