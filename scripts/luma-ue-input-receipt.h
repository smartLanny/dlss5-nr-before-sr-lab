#pragma once
// Our bounded diagnostic only. Include after the fixed official super_resolution.h.
#include <d3d11.h>
#include <array>
#include <format>
#include <mutex>
#include <string>

namespace nr_luma_receipt {
// This records the real SR Draw return only, not observed GPU completion/pixels.
template<class Sink>
void ObserveResult(unsigned long long shader, unsigned long long frame,
                   bool draw_succeeded, Sink sink) noexcept {
    try {
        static std::mutex mutex;
        static unsigned rows = 0;
        const std::lock_guard lock(mutex);
        if (rows >= 32) return;
        const auto text = std::format(
            "NR_UE_SR_RESULT v=1 row={} shader={:08x} drawFrame={} drawSucceeded={} gpuObserved=0",
            ++rows, shader, frame, draw_succeeded ? 1 : 0);
        sink(text.c_str());
    } catch (...) { /* Diagnostic failure cannot replace the original SR result. */ }
}

struct Site {
    unsigned long long shader;
    bool compute;
    int source_slot, depth_slot, velocity_slot, global_cb_slot;
    size_t cb_bytes;
    int projection_index, view_size_index, clip_previous_index;
    bool camera_cut, output_changed;
    const void* context;
};

template<class Sink>
void Observe(const SR::SuperResolutionImpl::DrawData& d,
             const SR::SettingsData& s, const Site& site, Sink sink) noexcept {
    // Logging must never change original TAA/SR control flow, including allocation failures.
    try {
        static std::mutex mutex;
        static unsigned long long calls = 0;
        static unsigned rows = 0;
        static bool previous_reset = false;
        static std::string previous_shape;
        static std::array<const void*, 128> identities{};
        const std::lock_guard lock(mutex);
        if (rows >= 32) return;
        ++calls;
        auto id = [](const void* pointer) -> unsigned {
            if (!pointer) return 0;
            for (unsigned i = 0; i < identities.size(); ++i) {
                if (identities[i] == pointer) return i + 1;
                if (!identities[i]) { identities[i] = pointer; return i + 1; }
            }
            return 0; // Bounded map exhausted: unknown, never print a raw address.
        };
        auto shape = std::format("shader={:08x} stage={} slots={},{},{} cb={}:{} offsets={},{},{} render={}x{} output={}x{}",
            site.shader, site.compute ? "cs" : "ps", site.source_slot, site.depth_slot,
            site.velocity_slot, site.global_cb_slot, site.cb_bytes, site.projection_index,
            site.view_size_index, site.clip_previous_index, d.render_width, d.render_height,
            s.output_width, s.output_height);
        const bool changed = shape != previous_shape;
        const bool reset_edge = d.reset && !previous_reset;
        previous_shape = shape;
        previous_reset = d.reset;
        if (calls > 4 && !changed && !reset_edge && calls % 120 != 0) return;
        ++rows;
        auto describe = [&](ID3D11Resource* resource) {
            D3D11_TEXTURE2D_DESC desc{};
            ID3D11Texture2D* texture = nullptr;
            if (resource && SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D),
                    reinterpret_cast<void**>(&texture)))) {
                texture->GetDesc(&desc);
                texture->Release();
            }
            return std::format("{}:{}x{}:fmt{}:samples{}", id(resource), desc.Width,
                desc.Height, unsigned(desc.Format), desc.SampleDesc.Count);
        };
        // Session IDs identify observed addresses only, not resource lifetime or view ownership.
        const auto text = std::format(
            "NR_UE_INPUT_RECEIPT v=1 row={} call={} contextSession={} {} color={} output={} depth={} motion={} exposure={} bias={} alpha={} jitter={},{} mvScale={},{} reset={} cameraCut={} outputChanged={} preExposure={} hdr={} reversedDepth={} mvsJittered={} autoExposure={} near={} far={} fov={} delta={} drawFrame={} sharpness={} evaluated=0",
            rows, calls, id(site.context), shape, describe(d.source_color), describe(d.output_color),
            describe(d.depth_buffer), describe(d.motion_vectors), describe(d.exposure),
            id(d.bias_mask), id(d.transparency_alpha), d.jitter_x, d.jitter_y,
            s.mvs_x_scale, s.mvs_y_scale, d.reset, site.camera_cut, site.output_changed,
            d.pre_exposure, s.hdr, s.inverted_depth, s.mvs_jittered, s.auto_exposure,
            d.near_plane, d.far_plane, d.vert_fov, d.time_delta, d.frame_index, d.user_sharpness);
        sink(text.c_str());
    } catch (...) { /* Read-only diagnostics are optional; retain the original call. */ }
}
}
