#pragma once
#include <cstdint>
#include <limits>

// One bounded historical receipt. Identities come from actual NR evaluation,
// feature/context lifetime and command recording; none is a game view id.
namespace nr_work_receipt {
enum class Route : unsigned { Unknown, NativePostSr, NativePostRr, NativePreNr };
struct Extent { unsigned width=0,height=0; };
struct Receipt {
    Route route=Route::Unknown;
    Extent render{},work{},output{};
    std::uint64_t nr_evaluation=0,command_token=0,feature_generation=0,nr_instance=0;
    std::uint64_t slot_use_serial=0,recorded_ms=0;
    unsigned config_generation=0,history_generation=0;
    std::uint64_t queue_identity=0,fence_value=0,submitted_ms=0,completed_observed_ms=0;
    bool valid=false,submission_observed=false,completion_observed=false;
};
inline bool Record(Receipt& target, Receipt next) {
    const Extent limit=next.route==Route::NativePreNr?next.render:next.output;
    if ((next.route!=Route::NativePostSr && next.route!=Route::NativePostRr &&
         next.route!=Route::NativePreNr) ||
        !next.render.width || !next.render.height || !next.work.width || !next.work.height ||
        !next.output.width || !next.output.height || next.work.width>limit.width ||
        next.work.height>limit.height || !next.nr_evaluation || !next.command_token ||
        !next.feature_generation || !next.nr_instance || !next.slot_use_serial || !next.recorded_ms)
        return false;
    next.queue_identity=next.fence_value=next.submitted_ms=next.completed_observed_ms=0;
    next.submission_observed=next.completion_observed=false;
    next.valid=true;
    target=next;
    return true;
}
inline void Submitted(Receipt& value,std::uint64_t token,std::uint64_t queue,
                      std::uint64_t fence,std::uint64_t now) {
    if (!value.valid || value.submission_observed || token!=value.command_token ||
        !queue || !fence || fence==std::numeric_limits<std::uint64_t>::max() ||
        now<value.recorded_ms) return;
    value.queue_identity=queue;value.fence_value=fence;value.submitted_ms=now;
    value.submission_observed=true;
}
inline void ObserveCompletion(Receipt& value,std::uint64_t queue,
                              std::uint64_t completed,std::uint64_t now) {
    if (!value.valid || !value.submission_observed || value.completion_observed ||
        queue!=value.queue_identity || completed==std::numeric_limits<std::uint64_t>::max() ||
        completed<value.fence_value || now<value.submitted_ms) return;
    value.completion_observed=true;value.completed_observed_ms=now;
}
inline const char* RouteName(Route route) {
    return route==Route::NativePostSr?"native-post-sr":
           route==Route::NativePostRr?"native-post-rr":
           route==Route::NativePreNr?"native-pre-nr-recorded":"unknown";
}
} // namespace nr_work_receipt
