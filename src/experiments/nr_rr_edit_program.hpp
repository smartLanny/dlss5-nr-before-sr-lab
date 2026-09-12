#pragma once
#include "nr_edit_dx12_dispatch.hpp"
#include "nr_edit_frame_contract.hpp"
#include "nr_edit_shader_constants.hpp"
namespace nr_edit::dx12 {
struct RrProgram;
// Views must be owned canonical zero-origin single-level textures, with the
// actual types declared in nr_rr_edit.hlsl. Original/edited are R, clean/final O.
// Each frame keeps a distinct binding object alive through its final submission.
struct RrImages {View edited,original,motion,hardware_depth,previous_edit,previous_depth,
    clean,reprojection,edit,view_depth,expected_previous_depth,history_depth,final;};
struct RrRecorded {SameFramePair pair;FrameKey key{};ID3D12GraphicsCommandList* list=nullptr;RrImages images{};RrShaderConstants constants{};dispatch::Ticket tickets[3]{};bool post_attempted=false;};
RrProgram* CreateRr(ID3D12Device*,const wchar_t* shader,char* error,std::size_t);
// The Core must already have successfully generated E_R using its real NR1/NR2.
// Before records guide preparation + accumulation. It does not call/change RR.
bool RecordRrBefore(RrProgram*,RrRecorded&,ID3D12GraphicsCommandList*,const FrameKey&,
                    const RrImages&,const RrShaderConstants&);
// Call only after original RR succeeded once, with current saved input/state
// contracts. The output is private; Core copyback occurs after its final protection.
bool RecordRrAfter(RrProgram*,RrRecorded&,ID3D12GraphicsCommandList*,const FrameKey&,
                   const RrImages&,const RrShaderConstants&,bool native_success);
// Required even when no post pass was recorded: the first two passes still exist.
bool SubmitRr(RrProgram*,RrRecorded&,ID3D12CommandQueue*,ID3D12Fence*,std::uint64_t,std::uint64_t queue_identity);
void QuarantineRr(RrProgram*);
bool DestroyRrAfterRetirement(RrProgram*&);
}
