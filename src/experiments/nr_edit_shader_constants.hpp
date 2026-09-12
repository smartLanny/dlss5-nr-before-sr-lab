#pragma once
#include <cstddef>
#include <cstdint>
namespace nr_edit {
struct alignas(16) RrShaderConstants {
    std::uint32_t render_size[2]{},output_size[2]{};
    float input_pre_exposure=1,output_pre_exposure=1,new_sample_rate=.08f,depth_absolute_tolerance=.01f;
    float depth_relative_tolerance=.01f,static_motion_tolerance_pixels=1;
    std::uint32_t history_valid=0,frame_pair_valid=0;
    float current_projection_ab[2]{},previous_projection_ab[2]{};
    // Column-vector multiplication, serialized row-major. Must match sampled raster phase.
    float current_clip_to_previous_clip[16]{};
    std::uint32_t camera_mapping_verified=0,explicit_previous_depth_verified=0,force_identity=0,padding=0;
};
struct alignas(16) DeferredShaderConstants {
    std::uint32_t render_size[2]{},output_size[2]{};
    float input_pre_exposure=1,output_pre_exposure=1,carrier_range=4;
    std::uint32_t frame_pair_valid=0,private_sr_succeeded=0,force_identity=0,padding0=0,padding1=0;
};
static_assert(sizeof(RrShaderConstants)==144);
static_assert(offsetof(RrShaderConstants,current_clip_to_previous_clip)==64);
static_assert(offsetof(RrShaderConstants,camera_mapping_verified)==128);
static_assert(sizeof(DeferredShaderConstants)==48);
static_assert(offsetof(DeferredShaderConstants,private_sr_succeeded)==32);
// Constants/descriptors must be immutable until every actual consumer retires.
// Zero defaults deliberately do not authorize model history, a source pair or SR success.
} // namespace nr_edit
