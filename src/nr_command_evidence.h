#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

// CPU-only observation. Caller owns synchronization; this never changes admission,
// publishes hooks, owns GPU objects, reads a GPU address or allocates memory.
namespace nr_command_evidence {
constexpr std::size_t kFirstCount = 16, kRecentCount = 8, kTextCapacity = 16384;
struct Event {
    char reason[80] = {};
    std::uint64_t tick = 0, token = 0, reset = 0, pipeline_writes = 0, dynamic = 0;
    std::uint32_t dirty = 0, meta_init = 0, meta_execute = 0, bindings = 0;
    bool admitted = false, registered = false, hooks = false, known = false;
    bool meta_known = false, meta_covered = false, null_baseline = false;
    bool expanded_tracking_requested = false;
    unsigned baseline = 0; // 0 unknown, 1 successful Reset, 2 ClearState
};
struct Bucket { Event first{}, latest{}; std::uint64_t count = 0; };
struct HookFailure {
    char stage[32] = {};
    int status = 0;
    unsigned request = 0, batch = 0;
    std::uint64_t tick = 0;
};
struct State {
    std::uint64_t total = 0, blocked = 0, admitted = 0, recoveries = 0;
    std::uint64_t transitions = 0, first_overflow = 0, hook_failures = 0;
    Bucket first[kFirstCount]{};
    Event latest{}, recent[kRecentCount]{};
    HookFailure first_hook{}, latest_hook{};
    std::size_t first_count = 0, recent_count = 0, next_recent = 0;
};
// Only fixed identifier characters are retained even if a future caller supplies
// an unexpected reason. No log line or private path is embedded in this record.
inline void Identifier(char *out, std::size_t capacity, const char *value) noexcept {
    if (!capacity) return;
    std::size_t n = 0;
    if (value) for (; value[n] && n + 1 < capacity; ++n) {
        const char c = value[n];
        out[n] = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_') ? c : '?';
    }
    out[n] = 0;
}
inline void Observe(State &s, const Event &e) noexcept {
    const bool change = !s.total || s.latest.admitted != e.admitted ||
                        std::strcmp(s.latest.reason, e.reason) != 0;
    if (s.total && !s.latest.admitted && e.admitted) ++s.recoveries;
    ++s.total;
    if (e.admitted) ++s.admitted; else ++s.blocked;
    std::size_t index = 0;
    for (; index < s.first_count; ++index)
        if (std::strcmp(s.first[index].first.reason, e.reason) == 0) break;
    if (index == s.first_count) {
        if (index < kFirstCount) { s.first[index].first = e; ++s.first_count; }
        else ++s.first_overflow;
    }
    if (index < kFirstCount) { ++s.first[index].count; s.first[index].latest = e; }
    if (change) {
        ++s.transitions;
        s.recent[s.next_recent] = e;
        s.next_recent = (s.next_recent + 1) % kRecentCount;
        if (s.recent_count < kRecentCount) ++s.recent_count;
    }
    s.latest = e;
}
inline void ObserveHookFailure(State &s, const char *stage, int status,
                               unsigned request, unsigned batch, std::uint64_t tick) noexcept {
    HookFailure event{};
    Identifier(event.stage, sizeof(event.stage), stage);
    event.status = status; event.request = request; event.batch = batch; event.tick = tick;
    if (!s.hook_failures) s.first_hook = event;
    ++s.hook_failures; s.latest_hook = event;
}
struct Writer {
    char *out; std::size_t capacity, used = 0; bool complete = true;
    template<class... Args> void Put(const char *format, Args... args) noexcept {
        if (!complete || !out || used >= capacity) { complete = false; return; }
        const int n = std::snprintf(out + used, capacity - used, format, args...);
        if (n < 0 || static_cast<std::size_t>(n) >= capacity - used) {
            out[used] = 0; complete = false;
        } else used += static_cast<std::size_t>(n);
    }
    void EventLine(const char *name, unsigned index, const Event &e, std::uint64_t count) noexcept {
        Put("commandEvidence.%s.%u=reason:%s count:%llu tick:%llu list:%llu baseline:%u reset:%llu pipelineWrites:%llu admitted:%u registered:%u hooks:%u known:%u null:%u expandedRequested:%u metaKnown:%u metaCovered:%u dirty:%u metaInit:%u metaExecute:%u bindings:%u dynamic:%llu\n",
            name, index, e.reason, static_cast<unsigned long long>(count),
            static_cast<unsigned long long>(e.tick), static_cast<unsigned long long>(e.token),
            e.baseline, static_cast<unsigned long long>(e.reset),
            static_cast<unsigned long long>(e.pipeline_writes), unsigned(e.admitted),
            unsigned(e.registered), unsigned(e.hooks), unsigned(e.known),
            unsigned(e.null_baseline), unsigned(e.expanded_tracking_requested),
            unsigned(e.meta_known), unsigned(e.meta_covered), e.dirty, e.meta_init,
            e.meta_execute, e.bindings, static_cast<unsigned long long>(e.dynamic));
    }
};
inline bool Format(const State &s, char *out, std::size_t capacity) noexcept {
    if (!out || !capacity) return false;
    out[0] = 0;
    Writer w{out, capacity};
    w.Put("commandEvidence.format=1\ncommandEvidence.available=1\n"
          "commandEvidence.scope=process-lifetime gate attempts; recent reason transitions, not every frame or GPU completion\n"
          "commandEvidence.dynamicCoverage=expandedRequested=0 means extra graphics setters are NOT observed; zero masks do not prove unused state\n"
          "commandEvidence.bindingsLegend=1:pso 2:dxr 4:computeRoot 8:graphicsRoot 16:resourceHeap 32:samplerHeap\n"
          "commandEvidence.baselineLegend=0:unknown 1:Reset 2:ClearState\n");
    w.Put("commandEvidence.total=%llu\ncommandEvidence.blocked=%llu\ncommandEvidence.admitted=%llu\n"
          "commandEvidence.recoveries=%llu\ncommandEvidence.transitions=%llu\ncommandEvidence.firstOverflow=%llu\n",
          static_cast<unsigned long long>(s.total), static_cast<unsigned long long>(s.blocked),
          static_cast<unsigned long long>(s.admitted), static_cast<unsigned long long>(s.recoveries),
          static_cast<unsigned long long>(s.transitions), static_cast<unsigned long long>(s.first_overflow));
    if (s.total) w.EventLine("latest", 0, s.latest, s.total);
    for (std::size_t i = 0; i < s.first_count; ++i) {
        w.EventLine("first", unsigned(i), s.first[i].first, s.first[i].count);
        // The latest record for each reason retains a late reset/coverage change
        // even when the global latest has already recovered to another reason.
        w.EventLine("lastForReason", unsigned(i), s.first[i].latest, s.first[i].count);
    }
    const auto begin = (s.next_recent + kRecentCount - s.recent_count) % kRecentCount;
    for (std::size_t i = 0; i < s.recent_count; ++i)
        w.EventLine("transition", unsigned(i), s.recent[(begin + i) % kRecentCount], 1);
    w.Put("commandEvidence.hookFailures=%llu\n", static_cast<unsigned long long>(s.hook_failures));
    if (s.hook_failures) for (unsigned i = 0; i < 2; ++i) {
        const auto &h = i ? s.latest_hook : s.first_hook;
        w.Put("commandEvidence.hook.%u=stage:%s status:%d request:%u batch:%u tick:%llu\n",
              i, h.stage, h.status, h.request, h.batch, static_cast<unsigned long long>(h.tick));
    }
    return w.complete;
}
static_assert(sizeof(State) < 12288, "bounded CPU evidence; allocate snapshots on worker heap");
} // namespace nr_command_evidence
