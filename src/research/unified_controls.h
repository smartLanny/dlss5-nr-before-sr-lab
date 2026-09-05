#pragma once
#include "framegen_control.h"
#include <cstdint>

namespace nr::research::controls {
struct Extent { std::uint32_t width = 0, height = 0; };
inline bool Same(Extent a, Extent b) { return a.width==b.width && a.height==b.height; }
inline bool Valid(Extent e) { return e.width && e.height; }
// Fixed-point UI values avoid NaN and ambiguous percentages. Zero R means Game.
struct ScaleRequest { std::uint32_t renderPermille=0, nrPermille=1000; };
struct ScaleCaps {
    bool controllerVerified=false, liveResize=false, gameDrsActive=false;
    std::uint32_t alignX=1, alignY=1;
};
struct Observation {
    Extent output, render;
    std::uint64_t frame=0, resources=0;
    bool sameFrameGuidesVerified=false;
};
enum class ScaleReason {
    None, ObservationUnknown, InvalidScale, InvalidAlignment,
    RenderUnavailable, RestartRequired, GameDrsOwnsScale, CaptureLocked,
    Pending, OutputChanged, StaleFrame, TimedOut, ResourceChangeUnproven
};
struct ScalePlan {
    ScaleReason reason=ScaleReason::ObservationUnknown;
    Extent output, render, nr;
    bool changesRender=false;
};
inline bool Scaled(Extent source, std::uint32_t permille, std::uint32_t ax,
                   std::uint32_t ay, Extent& result) {
    result={};
    if (!Valid(source) || !permille || permille>1000 || !ax || !ay || ax>256 || ay>256)
        return false;
    const auto width=(std::uint64_t{source.width}*permille/1000/ax)*ax;
    const auto height=(std::uint64_t{source.height}*permille/1000/ay)*ay;
    if (!width || !height) return false;
    result={static_cast<std::uint32_t>(width),static_cast<std::uint32_t>(height)};
    return true;
}
inline bool Valid(const Observation& o) {
    return Valid(o.output) && Valid(o.render) && o.frame && o.resources &&
        o.sameFrameGuidesVerified && o.render.width<=o.output.width &&
        o.render.height<=o.output.height;
}
inline ScalePlan Plan(ScaleRequest r, ScaleCaps c, Observation o, bool captureLocked) {
    ScalePlan p;
    if (!Valid(o)) return p;
    if (r.nrPermille<500 || r.nrPermille>1000 ||
        (r.renderPermille && (r.renderPermille<250 || r.renderPermille>1000))) {
        p.reason=ScaleReason::InvalidScale; return p;
    }
    if (!c.alignX || !c.alignY || c.alignX>256 || c.alignY>256) {
        p.reason=ScaleReason::InvalidAlignment; return p;
    }
    if (captureLocked) { p.reason=ScaleReason::CaptureLocked; return p; }
    p.output=o.output; p.render=o.render;
    if (r.renderPermille) {
        if (!c.controllerVerified) { p.reason=ScaleReason::RenderUnavailable; return p; }
        if (c.gameDrsActive) { p.reason=ScaleReason::GameDrsOwnsScale; return p; }
        if (!Scaled(o.output,r.renderPermille,c.alignX,c.alignY,p.render)) {
            p.reason=ScaleReason::InvalidScale; return p;
        }
        p.changesRender=!Same(p.render,o.render);
        if (p.changesRender && !c.liveResize) { p.reason=ScaleReason::RestartRequired; return p; }
    }
    // NR is independent. Game-owned R can stay unchanged in Feeder/old games.
    if (!Scaled(p.render,r.nrPermille,1,1,p.nr)) { p.reason=ScaleReason::InvalidScale; return p; }
    p.reason=ScaleReason::None; return p;
}
// Single control-thread. Backend acknowledgement alone never commits R/N.
// Caller applies NR only after Observe() succeeds, then calls CommitNr(success).
class ScaleTransaction {
public:
    bool Begin(const ScalePlan& p, const Observation& before, std::uint64_t revision) {
        if (p.reason!=ScaleReason::None || !Valid(before) || !revision || revision<=revision_ ||
            !Same(p.output,before.output) || !Valid(p.render) || !Valid(p.nr) ||
            p.nr.width>p.render.width || p.nr.height>p.render.height ||
            p.render.width>p.output.width || p.render.height>p.output.height ||
            p.changesRender != !Same(p.render,before.render)) return false;
        plan_=p; before_=before; revision_=revision; pending_=true; ready_=false; return true;
    }
    ScaleReason Observe(const Observation& now) {
        ready_=false;
        if (!pending_) return ScaleReason::Pending;
        if (now.frame>before_.frame && now.frame-before_.frame>120) {
            pending_=false; return ScaleReason::TimedOut;
        }
        if (!Valid(now)) return ScaleReason::ObservationUnknown;
        if (!Same(now.output,plan_.output)) { pending_=false; return ScaleReason::OutputChanged; }
        if (now.frame<=before_.frame) return ScaleReason::StaleFrame;
        if (!Same(now.render,plan_.render)) return ScaleReason::Pending;
        if (now.resources<before_.resources ||
            (plan_.changesRender && now.resources==before_.resources))
            return ScaleReason::ResourceChangeUnproven;
        ready_=true; return ScaleReason::None;
    }
    bool NeedsHistoryReset() const {
        return ready_ && (!hasApplied_ || !Same(appliedR_,plan_.render) || !Same(appliedN_,plan_.nr));
    }
    bool CommitNr(bool succeeded) {
        if (!pending_ || !ready_ || !succeeded) return false;
        appliedR_=plan_.render; appliedN_=plan_.nr; hasApplied_=true;
        pending_=ready_=false; return true;
    }
    void Cancel() { pending_=ready_=false; }
    bool Pending() const { return pending_; }
    Extent AppliedRender() const { return appliedR_; }
    Extent AppliedNr() const { return appliedN_; }
private:
    ScalePlan plan_{}; Observation before_{};
    Extent appliedR_{},appliedN_{};
    std::uint64_t revision_=0;
    bool pending_=false,ready_=false,hasApplied_=false;
};
// Shared view-model for the existing overlay and Quickbar. Not a replacement of
// NRUI V1's binary layout. Get/Set ABI integration must be additive/versioned.
struct Request { ScaleRequest scale; fg::Request framegen; };
struct MenuModel {
    ScalePlan scale;
    fg::Decision framegen;
    Extent observedRender, observedOutput;
    bool trueRenderControlAvailable=false;
};
inline MenuModel Resolve(const Request& r, const ScaleCaps& sc, const Observation& o,
                         const fg::Caps& fc, const fg::Conditions& conditions) {
    return {Plan(r.scale,sc,o,conditions.captureLocked),fg::Decide(r.framegen,fc,conditions),
        o.render,o.output,Valid(o) && sc.controllerVerified && !sc.gameDrsActive};
}
} // namespace nr::research::controls
