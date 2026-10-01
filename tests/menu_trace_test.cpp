#include <cassert>
#include <thread>
#include <atomic>
#include "nr_menu_trace.h"

int main() {
    using namespace nr_menu_trace;
    Trace t;
    Frame a{};a.render_w=1920;a.render_h=1080;a.work_w=1280;a.work_h=720;
    a.output_w=3840;a.output_h=2160;a.feature=13;a.feature_generation=2;
    NrGuideSignature g{};g.motion_bound_width=1280;g.motion_bound_height=720;
    t.Observe(100,a,g,{});
    assert(!t.Copy(100).marked && t.Copy(100).count==0);
    t.Mark(1000);
    t.Observe(1010,a,g,{1,2,3});
    Frame b=a;b.render_w=1600;b.work_w=1066;
    t.Observe(1100,b,g,{1,2,3});
    t.Observe(1200,a,g,{1,2,3});
    auto s=t.Copy(1200);
    assert(s.count==4 && s.events[0].kind==Kind::Esc && s.events[2].frame_mask==(Render|Work) && s.events[3].frame_mask==(Render|Work));
    assert(s.events[3].ms-s.events[1].ms<500);
    auto changed=g;changed.motion_bound_width=1279;
    assert(!SameNrGuideStorageContract(g,changed));
    t.Observe(1250,a,changed,{1,2,3});
    s=t.Copy(1250);assert(s.count==5 && s.events[4].storage_mask==MotionBound);
    auto mapped=changed;mapped.depth_x=8;
    assert(SameNrGuideStorageContract(changed,mapped) && !SameNrGuideMappingContract(changed,mapped));
    t.Observe(1260,a,mapped,{1,2,3});
    s=t.Copy(1260);assert(s.events[5].storage_mask==0 && s.events[5].mapping_mask==DepthOrigin);
    t.Observe(1270,a,mapped,{4,2,3});
    s=t.Copy(1270);assert(s.count==7 && s.resource_rotations==1 && s.events[6].kind==Kind::ResourceRotation && s.events[6].storage_mask==0);
    t.Note(1280,Kind::Create,a);t.Note(1281,Kind::CreateFailed,a,123);
    t.Note(1282,Kind::Retire,a,3);t.Note(1283,Kind::Reset,a,5);
    t.Success(1284,true);t.Note(1285,Kind::EvaluateFailed,a,6,true);
    s=t.Copy(1285);assert(s.creates==1 && s.create_failed==1 && s.retires==1 && s.resets==1);
    assert(s.evaluations==2 && s.successful==1 && s.failed==1 && s.reset_success==1 && s.reset_rejected==1);
    t.Mark(1290);s=t.Copy(1290);assert(s.presses==2 && s.repeated_presses==1 && s.deadline==3290);
    assert(s.events[s.count-1].kind==Kind::Esc && s.events[s.count-1].ms==1290);
    assert(!t.Active(3291));t.Observe(3291,b,g,{});assert(t.Copy(3291).count==s.count);
    t.Mark(4000);s=t.Copy(4000);assert(s.count==1 && !s.has_frame && s.observations==0 && s.presses==1);
    auto history=t.CopyHistory(4000);assert(history.count==2 && history.windows[0].expired && history.windows[1].started==4000);
    for(unsigned i=0;i<100;++i)t.Note(4010+i,Kind::Bypass,a,i);
    s=t.Copy(4200);assert(s.count==kCapacity && s.overflow==101-kCapacity);
    assert(s.events[0].reason==36 && s.events[kCapacity-1].reason==99);
    std::atomic<bool> done{false};
    std::thread writer([&]{for(unsigned i=0;i<1000;++i)t.Note(4200+i,Kind::Bypass,a,i);done=true;});
    while(!done){auto copy=t.Copy(4500);assert(copy.count<=kCapacity);}
    writer.join();s=t.Copy(5300);assert(s.count==kCapacity && s.overflow==1101-kCapacity);
    t.Mark(7000);t.Mark(10000);t.Mark(13000);
    history=t.CopyHistory(13000);assert(history.count==3 && history.windows[0].started==7000 && history.windows[2].started==13000);
    Frame route=a;route.route=2;route.carrier_key=1;
    t.Observe(13010,route,g,{});route.route=1;t.Observe(13020,route,changed,{});
    auto split=t.Copy(13020);assert(split.events[1].kind==Kind::First && split.events[2].kind==Kind::First && split.events[2].storage_mask==0);
    auto before=a,after=a;before.work_w=1280;after.work_w=1200;
    t.ContractChange(13030,before,after,g,changed);
    split=t.Copy(13030);assert(split.events[3].frame_mask==Work && split.events[3].storage_mask==MotionBound);

    // A legal double-buffered source rotates every 16 ms. It must not evict
    // earlier Create/Retire/Reset evidence or turn source-frame growth into a
    // false contract event. Another route receives its own first rotation.
    Trace rotations;
    Frame primary=a;primary.carrier_key=42;primary.route=2;primary.layer=0;
    primary.source_known=true;primary.source_stream=7;primary.source_epoch=9;primary.source_frame=1;
    rotations.Mark(20000);
    rotations.Observe(20001,primary,g,{10,20,30});
    rotations.Note(20002,Kind::Create,primary);
    rotations.Note(20003,Kind::Retire,primary,3);
    rotations.Note(20004,Kind::Reset,primary,5);
    for(unsigned i=0;i<100;++i){
        primary.source_frame=i+2;
        const bool alternate=(i&1)==0;
        rotations.Observe(20016+16*i,primary,g,{alternate?11u:10u,alternate?21u:20u,30});
    }
    auto rotated=rotations.Copy(21600);
    assert(rotated.observations==101 && rotated.resource_rotations==100 &&
           rotated.resource_rotation_union_mask==(DepthResource|MotionResource));
    assert(rotated.count==6 && rotated.overflow==0 && rotated.creates==1&&rotated.retires==1&&rotated.resets==1);
    assert(rotated.events[2].kind==Kind::Create && rotated.events[3].kind==Kind::Retire &&
           rotated.events[4].kind==Kind::Reset && rotated.events[5].kind==Kind::ResourceRotation);
    assert(rotated.events[5].coalesced_count==100 &&
           rotated.events[5].resource_mask==(DepthResource|MotionResource) &&
           rotated.events[5].ms==20016 && rotated.events[5].last_ms==21600);
    for(unsigned i=0;i<rotated.count;++i)assert(rotated.events[i].frame_mask==0);
    Frame post=primary;post.route=1;post.source_frame=1;
    rotations.Observe(21616,post,g,{40,50,60});
    post.source_frame=2;rotations.Observe(21632,post,g,{41,50,60});
    rotated=rotations.Copy(21632);
    assert(rotated.count==8 && rotated.events[6].kind==Kind::First &&
           rotated.events[7].kind==Kind::ResourceRotation &&
           rotated.events[7].coalesced_count==1 && rotated.events[7].resource_mask==DepthResource &&
           rotated.events[5].coalesced_count==100 && rotated.overflow==0);
    Frame other_source=primary;other_source.source_stream=8;other_source.source_frame=1;
    rotations.Observe(21648,other_source,g,{70,80,90});
    other_source.source_frame=2;rotations.Observe(21664,other_source,g,{71,80,90});
    rotated=rotations.Copy(21664);
    assert(rotated.count==10 && rotated.events[8].kind==Kind::First &&
           rotated.events[9].kind==Kind::ResourceRotation &&
           rotated.events[9].coalesced_count==1 && rotated.events[5].coalesced_count==100);
    rotations.Mark(24000);
    rotations.Observe(24001,primary,g,{10,20,30});
    rotations.Observe(24016,primary,g,{11,20,30});
    const auto next=rotations.Copy(24016);
    const auto prior=rotations.CopyHistory(24016);
    assert(next.count==3 && next.events[2].kind==Kind::ResourceRotation && next.events[2].coalesced_count==1);
    assert(prior.count==2 && prior.windows[0].events[5].coalesced_count==100 &&
           prior.windows[0].overflow==0);
}
