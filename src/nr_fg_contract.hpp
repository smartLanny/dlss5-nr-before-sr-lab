#pragma once
// Public API spelling, frozen against NVIDIA/DLSS
// 374959484e79a640feaba44c93ac8cfb0a03f5b5/include/nvsdk_ngx_defs_dlssg.h.
// Optional getters stay optional. These names never authorize pointer/queue use.
namespace nr_fg_contract {
inline constexpr const char* frame="DLSSG.BackbufferFrameID";
inline constexpr const char* count="DLSSG.MultiFrameCount";
inline constexpr const char* index="DLSSG.MultiFrameIndex";
inline constexpr const char* backbuffer="DLSSG.Backbuffer";
inline constexpr const char* depth="DLSSG.Depth";
inline constexpr const char* motion="DLSSG.MVecs";
inline constexpr const char* hudless="DLSSG.HUDLess";
inline constexpr const char* ui="DLSSG.UI";
inline constexpr const char* ui_alpha="DLSSG.UIAlpha";
inline constexpr const char* distortion="DLSSG.BidirectionalDistortionField";
inline constexpr const char* generated="DLSSG.OutputInterpolated";
inline constexpr const char* real="DLSSG.OutputReal";
inline constexpr const char* reset="DLSSG.Reset";
inline constexpr const char* paused="DLSSG.NotRenderingGameFrames";
inline constexpr const char* hdr="DLSSG.ColorBuffersHDR";
inline constexpr const char* inverted="DLSSG.DepthInverted";
inline constexpr const char* mv_x="DLSSG.MvecScaleX";
inline constexpr const char* mv_y="DLSSG.MvecScaleY";
inline constexpr const char* jitter_x="DLSSG.JitterOffsetX";
inline constexpr const char* jitter_y="DLSSG.JitterOffsetY";
}
