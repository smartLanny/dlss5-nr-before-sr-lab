#pragma once
#include "nr_edit_dx12.hpp"
namespace nr_post_residual_dx12 {
enum class Domain:unsigned {Unknown, SdrEncodedFp16};
struct Rect {unsigned x=0,y=0,width=0,height=0;};
struct Pair {
    nr_edit_dx12::View base{},candidate{};
    Rect base_rect{},candidate_rect{};
    nr_private_sr::Source base_source{},candidate_source{};
    // Explicit producer proof, not inferred from format or a scene-linear flag.
    Domain domain=Domain::Unknown;
    bool same_encoding_verified=false;
    unsigned work_width=0,work_height=0;
    float carrier_range=4;
};
struct Constants {
    unsigned full_size[2]{},work_size[2]{},base_origin[2]{},candidate_origin[2]{};
    unsigned target_origin[2]{},reserved_origin[2]{};
    float carrier_range=4;
    unsigned pair_valid=0,reconstruction_succeeded=0,reserved=0;
};
static_assert(sizeof(Constants)==64);
struct Encoded {
    nr_edit_dx12::Answer dispatch{};
    Pair pair{};
    nr_edit_dx12::View carrier{},tail{};
    nr_private_sr::Recording recording{};
    explicit operator bool()const{return dispatch.recorded;}
};
// All views are pinned by the existing immutable dispatch packets. Caller owns
// command-state restoration, exact submission receipts and recording retirement.
Encoded Encode(nr_edit_dx12::Kernels*,ID3D12GraphicsCommandList*,nr_private_sr::Recording,
               const Pair&,nr_edit_dx12::View carrier,nr_edit_dx12::View tail);
struct Reconstruction {
    nr_edit_dx12::View carrier{},target{};
    Rect target_rect{};
    nr_private_sr::Recording recording{};
    nr_private_sr::Source source{};
    nr_edit_dx12::Token encoded_token{};
    // Target may be the old candidate after Encode's synchronized read. It must
    // already contain Z; a refused contract or invalid pixel leaves it intact.
    bool succeeded=false,target_contains_candidate=false;
};
nr_edit_dx12::Answer Compose(nr_edit_dx12::Kernels*,ID3D12GraphicsCommandList*,
                            const Encoded&,const Reconstruction&);
}
