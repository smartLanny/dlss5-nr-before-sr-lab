#pragma once
#include <cstdint>
// Reconstruction transaction control shared by #258/#259. Instantiated inside
// the existing Core ownership/source lock; this does not create a second hook.
namespace nr_edit_flow {
constexpr std::uint32_t success=1,invalid=0xBAD00005u;
enum class Phase {None,Declined,Ready,Recorded,Composed,Fatal};
struct Step {Phase phase=Phase::Declined;std::uint32_t error=0;bool recorded=false;};
struct Result {
    std::uint32_t result=invalid;
    unsigned native_calls=0;
    bool private_recorded=false,composed=false,fatal=false;
};
template<class Adapter>
Result Execute(Adapter& a) {
    Result out;
    auto fail=[&](std::uint32_t error) {
        out.result=error&&error!=success?error:invalid;out.fatal=true;
        a.QuarantineAndAbandon();return out;
    };
    bool prepared=false,before=false;
    if(a.Eligible()) {
        before=a.CaptureBeforeState();
        if(before) {
            Step image=a.ProducePrivateImage();out.private_recorded|=image.recorded;
            if(image.phase==Phase::Fatal)return fail(image.error);
            if(image.phase==Phase::Ready){
                Step edit=a.PrepareEdit();out.private_recorded|=edit.recorded;
                if(edit.phase==Phase::Fatal)return fail(edit.error);
                prepared=edit.phase==Phase::Ready;
            }
            if(!a.RestoreBeforeState())return fail(invalid);
        }
    }
    out.result=a.CallNative();++out.native_calls;
    if(out.result!=success) {
        a.CancelPendingEdit();return out;
    }
    if(!prepared)return out;
    if(!a.PostIdentityStillMatches()||!a.CaptureAfterState()){
        a.CancelPendingEdit();return out;
    }
    const Step edit=a.FinishEdit(out.result);out.private_recorded|=edit.recorded;
    if(edit.phase==Phase::Fatal)return fail(edit.error);
    out.composed=edit.phase==Phase::Composed;
    // Snapshot is from AFTER native, never replay pre-native descriptor state.
    if(!a.RestoreAfterState())return fail(invalid);
    if(!out.composed)a.CancelPendingEdit();
    return out;
}
}
