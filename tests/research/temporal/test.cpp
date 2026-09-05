#include "research/temporal_provider.h"
#include <cstdio>
#include <limits>
using namespace nr::research::temporal;
static int checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (false)
static Frame Good() {
    Frame f;
    f.api = Api::D3D12; f.origin = Origin::NativeDlss;
    f.render = {2560, 1440}; f.output = {3840, 2160}; f.frame = 10; f.adapter = 1;
    f.color = {{2560,1440},{0,0,2560,1440},10,1,true,true};
    f.depth = f.color; f.motion = f.color;
    f.destination = {{3840,2160},{0,0,3840,2160},10,1,true,true};
    f.enabled = f.offlineAllowed = f.nativeFrame = f.queueVerified = true;
    f.linearColorVerified = f.depthConventionVerified = f.motionConventionVerified = true;
    f.exposureVerified = true; f.preExposure = 1;
    f.motionToRenderX = 2560; f.motionToRenderY = -1440;
    f.jitterX = 0.5f; f.jitterY = -0.5f;
    return f;
}
int main() {
    auto f = Good(); Grid g;
    CHECK(Validate(f) == Reject::None);
    CHECK(MapToNr(f,{1920,1080},g) == Reject::None);
    CHECK(g.motionX == 1920 && g.motionY == -1080);
    CHECK(g.jitterX == .375f && g.jitterY == -.375f);
    CHECK(MapToNr(f,{0,1080},g) == Reject::GridInvalid && g.work.width == 0);
    CHECK(MapToNr(f,{3000,1440},g) == Reject::GridInvalid);
    CHECK(!Fits({0xffffffffu,0,2,1},{0xffffffffu,1}));
    CHECK(Fits({16,8,2560,1440},{2600,1500}));
#define REJECT(change, why) do { f = Good(); change; CHECK(Validate(f) == Reject::why); } while(false)
    REJECT(f.enabled=false, Disabled);
    REJECT(f.offlineAllowed=false, ProtectedSession);
    REJECT(f.otherOwner=true, OtherOwner);
    REJECT(f.rr=true, RayReconstruction);
    REJECT(f.api=Api::D3D11, ApiUnsupported);
    REJECT(f.nativeFrame=false, IdentityUnknown);
    REJECT(f.queueVerified=false, IdentityUnknown);
    REJECT(f.render.width=4000, ExtentInvalid);
    REJECT(f.color.frame=9, ResourceInvalid);
    REJECT(f.depth.adapter=2, ResourceInvalid);
    REJECT(f.motion.typedViewVerified=false, ResourceInvalid);
    REJECT(f.destination.stateAndLifetimeVerified=false, ResourceInvalid);
    REJECT(f.motionToRenderX=0, MissingSemantics);
    REJECT(f.jitterX=std::numeric_limits<float>::quiet_NaN(), MissingSemantics);
    REJECT(f.motionJittered=true, MotionJittered);
    REJECT(f.preExposure=0, ExposureUnsupported);
    REJECT(f.exposureVerified=false, ExposureUnsupported);
    Fsr31Observation fsr{Good(),3,1,2560,-1440,false}; Frame normalized;
    CHECK(NormalizeFsr31(fsr,normalized) == Reject::None);
    CHECK(normalized.origin == Origin::Fsr31 && normalized.motionToRenderY == -1440);
    fsr.apiMinor=0;
    CHECK(NormalizeFsr31(fsr,normalized) == Reject::VersionUnsupported && !normalized.enabled);
    fsr.apiMinor=1; fsr.autoExposure=true;
    CHECK(NormalizeFsr31(fsr,normalized) == Reject::ExposureUnsupported);
    fsr.autoExposure=false; fsr.frame.motionAtOutput=true;
    CHECK(NormalizeFsr31(fsr,normalized) == Reject::MissingSemantics);
    fsr.frame.motionAtOutput=false; fsr.frame.color.active.x=1;
    CHECK(NormalizeFsr31(fsr,normalized) == Reject::SubrectUnsupported);
    for (std::uint32_t n = 1; n <= 2560; ++n) {
        CHECK(MapToNr(Good(),{n,1},g) == Reject::None);
        CHECK(std::isfinite(g.motionX) && g.motionX > 0);
    }
    std::printf("temporal: %d checks passed\n", checks);
}
