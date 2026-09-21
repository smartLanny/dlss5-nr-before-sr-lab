#include "../src/interop/nr_external_provider.h"
#include <cassert>
#include <cstddef>
#include <cstdio>
using namespace nr_external_provider;
static_assert(sizeof(NrExternalFrameV1)==448 && sizeof(NrExternalResourceV1)==72);
static_assert(offsetof(NrExternalFrameV1,color)==56 && offsetof(NrExternalFrameV1,command_list)==416);
NrExternalFrameV1 Frame(){
    NrExternalFrameV1 f;f.provider_id=42;f.view_id=3;f.epoch=1;f.frame_id=1;
    f.command_list=10;f.queue=11;f.fence=12;f.fence_value=1;
    f.lease_flags=static_cast<unsigned>(NrExternalLeaseFlags::CommandListOwnedByProducer)|
        static_cast<unsigned>(NrExternalLeaseFlags::QueueFenceRequired);
    f.color.resource=1;f.color.width=f.color.rect_w=1920;f.color.height=f.color.rect_h=1080;
    f.color.provenance=NrGuideProvenance::Synthetic;f.output=f.color;f.output.resource=2;
    f.depth=f.color;f.depth.resource=3;f.motion=f.color;f.motion.resource=4;return f;
}
int main(){
    ConfigSnapshot cfg{true,1.f,false,false,2};Session s;auto f=Frame();
    assert(!Admit(s,cfg,f).admit);
    assert(ClaimOwnership(s,cfg,42)&&!ClaimOwnership(s,cfg,43));
    auto first=Admit(s,cfg,f);assert(first.admit&&first.run_nr&&first.history_reset);
    assert(!Admit(s,cfg,f).admit); // same content cannot consume again
    auto before=s;++f.frame_id;auto next=Admit(s,cfg,f);assert(next.admit&&!next.history_reset);
    RollbackAdmit(s,before,NrExternalProviderReason::RuntimeUnavailable);
    assert(Admit(s,cfg,f).history_reset); // only policy rollback; recorded Claims remain authoritative
    cfg.master_enabled=false;++f.frame_id;auto off=Admit(s,cfg,f);assert(off.admit&&!off.run_nr);
    cfg.master_enabled=true;++f.frame_id;assert(Admit(s,cfg,f).history_reset);
    cfg.competing_nr=true;++f.frame_id;assert(!Admit(s,cfg,f).admit);cfg.competing_nr=false;
    cfg.observer_uncertain=true;assert(!Admit(s,cfg,f).admit);cfg.observer_uncertain=false;
    ++f.epoch;assert(Admit(s,cfg,f).reason==NrExternalProviderReason::EpochMismatch);
    ReleaseOwnership(s,43);assert(s.owned);ReleaseOwnership(s,42);assert(!s.owned);
    assert(ClaimOwnership(s,cfg,42));assert(Admit(s,cfg,f).history_reset);
    auto bad=f;bad.epoch=0;assert(!FrameHeaderOk(bad));bad=f;bad.abi=0;assert(!FrameHeaderOk(bad));
    bad=f;bad.color.rect_x=0xffffffffu;assert(!ValidateLeases(bad));
    bad=f;bad.output.resource=bad.color.resource;assert(!ValidateLeases(bad));
    NrExternalSubmitResultV1 result;FillSubmitResult(off,f,false,result);
    assert(!result.writeback_performed&&result.reason==NrExternalProviderReason::Disabled);
    ReleaseOwnership(s,42);NrExternalStatusV1 status;FillStatus(s,cfg,status);assert(status.reason==NrExternalProviderReason::Starting);
    std::puts("PASS external ABI/admission: ownership, duplicate, OFF/resume, observer/native conflict, resize epoch, release and malformed leases; CPU only");
}
