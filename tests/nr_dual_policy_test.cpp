#include "nr_dual_pass.hpp"
#include "nr_dual_lifetime.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
#include <string>
using namespace nr_dual;
static unsigned checks = 0;
void Check(bool value, const char* message) {
    ++checks; if (!value) throw std::runtime_error(message); // Active in Release/NDEBUG, too.
}
struct FakeBackend {
    bool valid = true, poisoned = false;
    std::uint32_t prior_failure = 0;
    std::uint32_t result = 1;
    std::vector<std::string> calls;
    bool PreviouslyPoisoned() const { return poisoned; }
    std::uint32_t PriorFailureResult() const { return prior_failure; }
    bool ValidateNoCommands(const Plan&) { calls.push_back("validate"); return valid; }
    void RecordDownsample() { calls.push_back("downsample"); }
    void PrepareOutputForEvaluate() { calls.push_back("write"); }
    std::uint32_t EvaluateOnce() { calls.push_back("evaluate"); return result; }
    void PoisonWithoutFurtherGpuCommands(std::uint32_t r) { poisoned = true; prior_failure = r; calls.push_back("poison"); }
    void MakeOutputReadable() { calls.push_back("read"); }
};
Admission Ready(const Plan& p) {
    static int handles[4];
    return {&handles[0], &handles[1], &handles[2], &handles[3], p.secondary, true, true, true, true};
}
int main() {
    try {
        RasterRules rules;
        auto full = MakePlan({2, {1, 1}}, {1920, 1080}, rules);
        Check(bool(full) && full.path == Path::DirectSecond, "100% direct");
        Check(full.secondary == Extent{1920,1080} && !full.rounded, "100% exact extent");
        auto half = MakePlan({2, {1, 2}}, {1920, 1080}, rules);
        Check(bool(half) && half.secondary == Extent{960,540}, "50% extent");
        Check(half.path == Path::MatchedResidual, "reduced route");
        auto two_thirds = MakePlan({2, {2, 3}}, {1920,1080}, rules);
        Check(two_thirds.secondary == Extent{1280,720} && !two_thirds.rounded, "exact 2/3");
        Check(MakePlan({2,{3,4}}, {1920,1080}, rules).secondary == Extent{1440,810}, "75%");
        Check(MakePlan({2,{1,4}}, {1920,1080}, rules).secondary == Extent{480,270}, "25%");
        Check(!MakePlan({2,{24,100}}, {1920,1080}, rules), "below range rejected");
        Check(!MakePlan({2,{101,100}}, {1920,1080}, rules), "above range rejected");
        Check(!MakePlan({2,{1,0}}, {1920,1080}, rules), "zero denominator");
        Check(!MakePlan({3,{1,1}}, {1920,1080}, rules), "no arbitrary pass count");
        Check(!MakePlan({2,{1,1}}, {0,1080}, rules), "zero primary");
        rules.alignment = {8,8};
        auto rounded = MakePlan({2,{1,2}}, {1920,1080}, rules);
        Check(rounded.secondary == Extent{960,544} && rounded.rounded, "report legal rounding");
        Check(MakePlan({2,{1,1}}, {1920,1080}, rules).secondary == Extent{1920,1080}, "do not round 100%");
        rules.minimum = {512,512};
        Check(!MakePlan({2,{1,4}}, {1920,1080}, rules), "respect caller runtime minimum");
        rules = {};
        for (unsigned w=1; w<513; w+=7) for (unsigned h=1; h<257; h+=11) {
            for (Scale s : {Scale{1,1}, Scale{3,4}, Scale{2,3}, Scale{1,2}, Scale{1,4}}) {
                auto p = MakePlan({2,s}, {w,h}, rules);
                Check(bool(p), "valid geometry sweep");
                Check(p.secondary.width <= w && p.secondary.height <= h, "no supersample");
                Check(std::uint64_t(p.secondary.width)*4 >= w &&
                      std::uint64_t(p.secondary.height)*4 >= h, "bounded downsample kernel");
            }
        }
        ManualSettings ui;
        ui.draft = {2,{50,100}};
        Check(ui.applied.passes == 1 && ui.revision == 0, "drag is not applied");
        Check(ui.Commit() && ui.revision == 1 && SameScale(ui.applied.secondary_scale,{1,2}), "apply once");
        Check(ui.Commit() && ui.revision == 1, "unchanged commit not rebuild");
        ui.draft.secondary_scale = {0,0};
        Check(!ui.Commit() && ui.revision == 1, "invalid UI draft retained without activation");
        ui.RevertDraft(); Check(SameScale(ui.draft.secondary_scale,{1,2}), "cancel draft");
        AuxHistory history;
        LayerKey key{1, 10, 7, 20, {960,540}};
        Check(history.NeedsReset(key,1,false), "cold reset");
        Check(history.CommitSuccess(key,1), "success commit");
        Check(!history.NeedsReset(key,2,false), "continuous parent");
        Check(history.NeedsReset(key,3,false), "skipped secondary resets");
        Check(history.NeedsReset(key,1,false), "duplicate parent not continuation");
        Check(history.NeedsReset(key,2,true), "camera/source reset");
        auto changed = key; ++changed.primary_instance;
        Check(history.NeedsReset(changed,2,false), "parent rebuild invalidates child");
        changed = key; ++changed.secondary_generation;
        Check(history.NeedsReset(changed,2,false), "secondary generation invalidates history");
        changed = key; ++changed.primary_input_epoch;
        Check(history.NeedsReset(changed,2,false), "parent input kernel/phase generation");
        changed = key; changed.size = {1280,720};
        Check(history.NeedsReset(changed,2,false), "secondary size invalidates history");
        history.Invalidate(); Check(history.NeedsReset(key,2,false), "explicit invalidate");
        changed = key; changed.layer=0;
        Check(!history.CommitSuccess(changed,2), "primary cannot occupy secondary ledger");

        { FakeBackend b; auto out=RecordPreparedSecond(full,Ready(full),b);
          Check(out.selection==Selection::DirectSecond && out.gpu_work_recorded, "direct selection");
          Check(b.calls == std::vector<std::string>{"validate","write","evaluate","read"}, "100% no resize/no additive roundtrip"); }
        { FakeBackend b; auto out=RecordPreparedSecond(half,Ready(half),b);
          Check(out.selection==Selection::FusedResidual && out.HasSecond(), "low selection");
          Check(b.calls == std::vector<std::string>{"validate","downsample","write","evaluate","read"}, "one second Evaluate, no full-size compose dispatch"); }
        { FakeBackend b; b.result=0xBAD00005u; auto out=RecordPreparedSecond(half,Ready(half),b);
          Check(out.selection==Selection::NoneAfterOpaqueFailure && !out.HasSecond(), "opaque error no image");
          Check(b.calls == std::vector<std::string>{"validate","downsample","write","evaluate","poison"}, "no GPU commands/retry after opaque failure");
          const auto prior=b.calls; b.result=1;
          auto again=RecordPreparedSecond(half,Ready(half),b);
          Check(again.selection==Selection::NoneAfterOpaqueFailure && !again.gpu_work_recorded, "poison remains fatal on subsequent invocation");
          Check(b.calls==prior && again.ngx_result==0xBAD00005u, "poison cannot become a silent primary fallback/retry"); }
        { FakeBackend b; b.valid=false; auto out=RecordPreparedSecond(half,Ready(half),b);
          Check(out.selection==Selection::Primary && !out.gpu_work_recorded, "preflight safe fallback");
          Check(b.calls==std::vector<std::string>{"validate"}, "preflight no commands"); }
        for(unsigned failure=0; failure<7; ++failure) {
            FakeBackend b; auto a=Ready(full);
            switch(failure) {
            case 0: a.secondary_feature=a.primary_feature; break;
            case 1: a.secondary_params=a.primary_params; break;
            case 2: a.creation_completion_proven=false; break;
            case 3: a.recording_and_queue_admitted=false; break;
            case 4: a.dependencies_pinned=false; break;
            case 5: a.current_inputs_and_guides_bound=false; break;
            case 6: a.secondary_create_extent={960,540}; break;
            }
            auto out=RecordPreparedSecond(full,a,b);
            Check(out.selection==Selection::Primary && !out.gpu_work_recorded && b.calls.empty(), "invalid admission no GPU/NGX calls");
        }
        { FakeBackend b; auto p=MakePlan({1,{1,2}}, {1920,1080}, rules);
          auto out=RecordPreparedSecond(p,{},b);
          Check(out.selection==Selection::Primary && b.calls.empty(), "disabled exact no-work"); }
        {
            struct Context;
            struct Slot {void* dual_spatial=nullptr;Context* dual_owner=nullptr;};
            struct Context {Slot slots[8]{};unsigned handle=0;};
            Context active;active.handle=123;active.slots[3].dual_spatial=&active;active.slots[3].dual_owner=&active;
            Context deferred=active;RebindSlotOwners(deferred);active={};active.handle=456;
            Check(deferred.slots[3].dual_owner==&deferred&&deferred.slots[3].dual_owner->handle==123,"first deferred move rebases owner away from reused active entry");
            Context worker=deferred;RebindSlotOwners(worker);deferred={};deferred.handle=789;
            Check(worker.slots[3].dual_owner==&worker&&worker.slots[3].dual_owner->handle==123,"worker move rebases owner away from reused queue entry");
            Check(worker.slots[0].dual_owner==nullptr,"empty slots are not assigned false owners");
        }
        std::cout << "PASS " << checks << " assertions (portable policy and fake backend; not GPU/NGX)\n";
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
