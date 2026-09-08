// Actual Windows resource admission for the FFX->NR descriptor adapter.
#define wmain unused_direct_sr_wmain
#include <direct_sr_gpu.cpp>
#undef wmain
#include "src/interop/nr_fsr31_external_frame.h"
#include <cassert>

int wmain() {
    auto ctx=MakeDevice(true); auto f=MakeFrame(ctx,false); BindFrame(f,ctx);
    const auto nr=Texture(ctx.device.Get(),kRenderWidth,kRenderHeight,
        DXGI_FORMAT_R16G16B16A16_FLOAT,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    nr_fsr31_dlss::Quality quality{};
    quality.min_width=1;quality.min_height=1;
    quality.max_width=kRenderWidth;quality.max_height=kRenderHeight;
    nr_fsr31_external::Lease lease{42,1,1,1,ctx.queue.Get(),ctx.fence.Get(),0,NrGuideProvenance::Synthetic};
    NrExternalFrameV1 result{};
    auto build=[&]{return nr_fsr31_external::BuildFrame(ctx.device.Get(),f.create,f.dispatch,quality,nr.Get(),lease,result);};
    using S=nr_fsr31_dlss::Status;
    assert(build()==S::UnsupportedResource && result.provider_id==0); // No dropped exposure texture.
    f.dispatch.exposure={}; // This scalar-exposure fixture explicitly declares no texture.
    assert(build()==S::Ready);
    assert(result.color.resource==reinterpret_cast<std::uint64_t>(f.color.Get()));
    assert(result.output.resource==reinterpret_cast<std::uint64_t>(nr.Get()));
    assert(result.color.provenance==NrGuideProvenance::Synthetic);
    assert(result.color.jitter_x==.25f && result.color.jitter_y==-.25f);
    assert(result.motion.motion_scale_x==f.dispatch.motionVectorScale.x);
    assert(result.lease_flags & static_cast<unsigned>(NrExternalLeaseFlags::ResetTemporal));
    f.dispatch.reset=false; assert(build()==S::Ready);
    assert(!(result.lease_flags & static_cast<unsigned>(NrExternalLeaseFlags::ResetTemporal)));
    lease.provenance=NrGuideProvenance::Unknown;assert(build()==S::UnsupportedFlags);
    lease.provenance=NrGuideProvenance::Native;lease.epoch=0;assert(build()==S::MissingInput);
    lease.epoch=1;f.dispatch.renderSize.width=kRenderWidth-1;assert(build()==S::InvalidExtent);
    f.dispatch.renderSize.width=kRenderWidth;
    assert(nr_fsr31_external::BuildFrame(ctx.device.Get(),f.create,f.dispatch,quality,
        f.color.Get(),lease,result)==S::AliasedOutput);
    puts("FFX-to-NR real resource admission passed; no GPU dispatch or game claim");
}
