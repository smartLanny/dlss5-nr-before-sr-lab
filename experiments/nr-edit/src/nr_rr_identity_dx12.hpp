#pragma once
// B2 isolated identity entry. Not installed in ProcessRrPost or enabled by an INI.
// The caller supplies the existing recording lease and command-state envelope;
// this module has no submission ledger, model, history, Init, or feature factory.
#include "nr_edit_dx12.hpp"
namespace nr_rr_identity {
using Recording=nr_private_sr::Recording;
using Source=nr_private_sr::Source;
using Id=nr_private_sr::Id;
using Result=nr_private_sr::Result;
enum class Reason {Disabled, Contract, Recording, State, Alias, Parameters,
    Device, Format, Budget, Busy, Lease, Ready, RestoreFailed, OpaqueFailure};
struct Frame {
    Recording recording{};
    Source source{};
    Id view=0,module_generation=0,feature_generation=0;
    nr_edit_dx12::View color{};
    unsigned render_width=0,render_height=0,output_width=0,output_height=0;
    unsigned base_x=0,base_y=0;
    // A controlled host may declare its own input contract. A native adapter
    // must derive these facts from B1 evidence, never a non-null pointer.
    bool native_contract_known=false,transparency_absent_known=false;
    bool all_color_aliases_known=false,single_layer=false,fg_off_known=false;
    float pre_exposure=0,exposure_scale=0;
};
struct LeaseAdapter {
    void* user=nullptr;
    bool (*current)(void*,const Frame&)=nullptr;
    // Acquires the caller's immutable lease BEFORE the first copy command.
    // It pins both resources and returns an opaque lease, or records nothing.
    void* (*record)(void*,const Frame&,ID3D12Resource*,ID3D12Resource*)=nullptr;
    // Publish BEFORE entering native. An opaque-consumed lease must never use
    // a copy-only Reset/discard rule. Failure quarantines without calling native.
    bool (*native_consumer)(void*,void*)=nullptr;
    // Optional observed state of the native-consumed copy. Unknown means the
    // next accepted frame replaces it after safe lease retirement, never guesses.
    bool (*copy_read_state)(void*,void*,unsigned*)=nullptr;
    bool (*healthy)(void*,void*)=nullptr;
    // True only after recording release AND exact queue/fence completion (or
    // a separately proved copy-only discard). No frame age/timeout substitute.
    bool (*mutable_now)(void*,void*)=nullptr;
    void (*release)(void*,void*)=nullptr;
    void (*quarantine)(void*,void*)=nullptr;
};
struct Envelope {
    void* user=nullptr;
    bool (*capture)(void*,ID3D12GraphicsCommandList*)=nullptr;
    bool (*restore)(void*)=nullptr;
};
struct Parameters {
    void* object=nullptr;
    // Native NGX typed and void overloads are separate observed states. The
    // adapter preserves exact Get result codes, including missing/type errors.
    Result (*read)(void*,const char*,bool typed,void**)=nullptr;
    void (*write)(void*,const char*,bool typed,void*)=nullptr;
};
using Native=Result(*)(void*,ID3D12GraphicsCommandList*,Parameters);
struct Outcome {
    Result result=0;
    Reason reason=Reason::Contract;
    unsigned native_calls=0;
    bool copied=false,substituted=false,restored=false;
};
class Context;
// Cold allocation only; no plane is allocated until an enabled, proven frame.
Context* Create(ID3D12Device*,LeaseAdapter);
bool Retire(Context*&); // Does not wait; false retains all leased storage.
Outcome Evaluate(Context*,bool enabled,ID3D12GraphicsCommandList*,const Frame&,
    Parameters,Envelope,std::uint64_t available_bytes,Native,void* native_user);
}
