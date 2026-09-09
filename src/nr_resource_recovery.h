#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

// Value-only recovery evidence. The owner serializes access and supplies real
// lifecycle identities; this ledger never calls NGX, submits work, waits, or
// changes the native return value. A view evidence_id identifies genuinely new
// evidence, not the number of native calls which have repeated the same view.
namespace nr_resource_recovery {
struct Scope {
    std::uint64_t device = 0, device_generation = 0;
    std::uint64_t feature = 0, feature_generation = 0, config_generation = 0;
    std::uint32_t mode = 0;
};
inline bool operator==(const Scope& a, const Scope& b) noexcept {
    return a.device == b.device && a.device_generation == b.device_generation &&
        a.feature == b.feature && a.feature_generation == b.feature_generation &&
        a.config_generation == b.config_generation && a.mode == b.mode;
}
struct Key {
    Scope scope{};
    std::uint64_t resource = 0, resource_generation = 0;
    std::uint32_t subresource = 0, role = 0;
};
inline bool operator==(const Key& a, const Key& b) noexcept {
    return a.scope == b.scope && a.resource == b.resource &&
        a.resource_generation == b.resource_generation &&
        a.subresource == b.subresource && a.role == b.role;
}
struct ViewSignature {
    std::uint32_t srv = 0, uav = 0, provenance = 0;
    std::uint64_t evidence_id = 0;
};
inline bool operator==(const ViewSignature& a, const ViewSignature& b) noexcept {
    return a.srv == b.srv && a.uav == b.uav && a.provenance == b.provenance &&
        a.evidence_id == b.evidence_id;
}
struct Carrier {
    std::uint64_t frame_id = 0, ordinal = 0;
    explicit operator bool() const noexcept { return frame_id && ordinal; }
};
inline bool operator==(const Carrier& a, const Carrier& b) noexcept {
    return a.frame_id == b.frame_id && a.ordinal == b.ordinal;
}
struct Token {
    Key key{};
    ViewSignature signature{};
    std::uint64_t attempt_id = 0;
    Carrier carrier{};
    explicit operator bool() const noexcept { return attempt_id && static_cast<bool>(carrier); }
};
inline bool operator==(const Token& a, const Token& b) noexcept {
    return a.key == b.key && a.signature == b.signature &&
        a.attempt_id == b.attempt_id && a.carrier == b.carrier;
}
struct Target {
    std::uint64_t resource = 0, resource_generation = 0;
    std::uint32_t subresource = 0;
    explicit operator bool() const noexcept { return resource != 0; }
};
inline bool operator==(const Target& a, const Target& b) noexcept {
    return a.resource == b.resource && a.resource_generation == b.resource_generation &&
        a.subresource == b.subresource;
}
enum class WritebackTargetPolicy { SameResource, Explicit };
struct RetirementProof {
    bool recording_idle = false, gpu_idle = false, readbacks_collected = false;
};
enum class SemanticState { Waiting, Exact, Unique, Rejected };
enum class State { Unknown, Waiting, Resolved, Pending, RecoverySucceeded, Latched };
enum class Stage : unsigned {
    Descriptor, NrEvaluate, NativeSrEvaluate, TransferRecorded, Submitted,
    ActualStorageRead, NrCreate, ParameterBinding, ResourceAllocation, Count
};
enum class FailureKind { None, Reported, WrongTarget, NonFinite, WritebackMismatch };
struct Failure {
    bool present = false;
    Stage stage = Stage::Descriptor;
    std::int32_t result = 0;
    FailureKind kind = FailureKind::None;
    Token token{};
};
struct Resolution {
    State state = State::Unknown;
    std::uint64_t attempt_id = 0;
    bool can_attempt = false, capacity_unknown = false;
};
struct RecoveryEvidence {
    State state = State::Unknown;
    SemanticState semantic = SemanticState::Waiting;
    std::uint64_t attempt_id = 0, proof_attempt_id = 0, proof_frame_id = 0;
    std::uint64_t proof_carrier_ordinal = 0, proof_age = 0;
    std::uint64_t window_first_ordinal = 0, window_last_ordinal = 0;
    std::uint64_t window_first_frame_id = 0, window_last_frame_id = 0;
    bool was_waiting = false, correlated_window = false;
    bool descriptor_verified = false, writeback_verified = false;
    bool capacity_unknown = false, frame_evidence_lost = false;
    bool first_failure_historical = false;
    unsigned nr_success_streak = 0, native_success_streak = 0, submitted_count = 0;
    Target proof_target{};
    Failure first_failure{};
};
struct WritebackProof {
    std::uint64_t resource = 0, resource_generation = 0;
    std::uint32_t subresource = 0;
    bool actual_storage_read = false, finite = false, matches_expected = false;
    std::int32_t result = 0;
};

template <std::size_t InterpretationCapacity = 64, std::size_t FrameCapacity = 128,
          std::size_t ScopeCapacity = 16>
class Ledger {
    static_assert(InterpretationCapacity && FrameCapacity >= 3 && ScopeCapacity,
                  "recovery needs bounded interpretation, scope and frame storage");
    struct Event { bool seen = false, success = false; std::int32_t result = 0; };
    struct Entry {
        bool used = false, was_waiting = false;
        Key key{};
        ViewSignature signature{};
        SemanticState semantic = SemanticState::Waiting;
        std::uint64_t attempt_id = 0, first_ordinal = 0, descriptor_ordinal = 0;
        Token proof{};
        Target proof_target{};
        Failure failure{};
    };
    struct Frame {
        bool used = false, ended = false, used_nr = false, conflict = false;
        bool target_frozen = false, target_conflict = false, retired_complete = false;
        Scope scope{};
        std::uint32_t role = 0;
        Carrier carrier{};
        Token token{};
        Target target{};
        std::array<Event, static_cast<unsigned>(Stage::Count)> events{};
        std::uint64_t age = 0;
    };
    struct Window {
        bool valid = false;
        // Each member was independently complete; these are not global counts.
        std::array<Token, 3> members{};
    };
    struct Stream {
        bool used = false, evidence_lost = false;
        Scope scope{};
        std::uint32_t role = 0;
        Carrier latest{};
        std::uint64_t break_ordinal = 0;
        Window window{};
        Failure first_failure{};
    };
    std::array<Entry, InterpretationCapacity> entries_{};
    std::array<Frame, FrameCapacity> frames_{};
    std::array<Stream, ScopeCapacity> streams_{};
    std::uint64_t next_attempt_ = 1, next_age_ = 1;

    static bool Ready(SemanticState s) noexcept {
        return s == SemanticState::Exact || s == SemanticState::Unique;
    }
    static bool Valid(const Key& key) noexcept {
        return key.scope.device && key.scope.feature && key.resource;
    }
    Entry* FindEntry(const Key& key, const ViewSignature& signature) noexcept {
        for (auto& e : entries_) if (e.used && e.key == key && e.signature == signature) return &e;
        return nullptr;
    }
    const Entry* FindEntry(const Key& key, const ViewSignature& signature) const noexcept {
        for (const auto& e : entries_) if (e.used && e.key == key && e.signature == signature) return &e;
        return nullptr;
    }
    Stream* FindStream(const Scope& scope, std::uint32_t role) noexcept {
        for (auto& s : streams_) if (s.used && s.scope == scope && s.role == role) return &s;
        return nullptr;
    }
    const Stream* FindStream(const Scope& scope, std::uint32_t role) const noexcept {
        for (const auto& s : streams_) if (s.used && s.scope == scope && s.role == role) return &s;
        return nullptr;
    }
    Stream* EnsureStream(const Scope& scope, std::uint32_t role) noexcept {
        if (!scope.device || !scope.feature) return nullptr;
        if (auto* s = FindStream(scope, role)) return s;
        for (auto& s : streams_) if (!s.used) {
            s = {}; s.used = true; s.scope = scope; s.role = role; return &s;
        }
        return nullptr;
    }
    Frame* FindFrame(const Scope& scope, std::uint32_t role, Carrier carrier) noexcept {
        for (auto& f : frames_) if (f.used && f.scope == scope && f.role == role &&
                                    f.carrier == carrier) return &f;
        return nullptr;
    }
    const Frame* FindOrdinal(const Stream& s, std::uint64_t ordinal) const noexcept {
        for (const auto& f : frames_) if (f.used && f.scope == s.scope && f.role == s.role &&
                                        f.carrier.ordinal == ordinal) return &f;
        return nullptr;
    }
    static bool Succeeded(const Frame& f, Stage stage) noexcept {
        const auto& e = f.events[static_cast<unsigned>(stage)]; return e.seen && e.success;
    }
    bool Complete(const Frame& f) const noexcept {
        if (!f.used || !f.ended || !f.used_nr || f.conflict || f.target_conflict || !f.target || !f.token) return false;
        if (f.retired_complete) return true;
        const auto* e = FindEntry(f.token.key, f.token.signature);
        return e && Ready(e->semantic) && !e->failure.present && e->attempt_id == f.token.attempt_id &&
            Succeeded(f, Stage::Descriptor) && Succeeded(f, Stage::NrEvaluate) &&
            Succeeded(f, Stage::NativeSrEvaluate) && Succeeded(f, Stage::Submitted);
    }
    static void Break(Stream& s, std::uint64_t ordinal) noexcept {
        if (ordinal > s.break_ordinal) s.break_ordinal = ordinal;
        if (s.window.valid && s.window.members[0].carrier.ordinal <= s.break_ordinal) s.window = {};
    }
    Frame* AllocateFrame(Stream& s, Carrier carrier) noexcept {
        Frame* target = nullptr;
        for (auto& f : frames_) if (!f.used) { target = &f; break; }
        if (!target) {
            target = &frames_[0];
            for (auto& f : frames_) if (f.age < target->age) target = &f;
            // An unfinished overwritten frame can no longer prove continuity.
            // Completed snapshots remain exact evidence even after frame reuse.
            if (!Complete(*target) && !(target->ended && !target->used_nr)) {
                if (auto* old = FindStream(target->scope, target->role)) {
                    old->evidence_lost = true; Break(*old, target->carrier.ordinal);
                }
            }
        }
        if (next_age_ == std::numeric_limits<std::uint64_t>::max()) return nullptr;
        *target = {}; target->used = true; target->scope = s.scope; target->role = s.role;
        target->carrier = carrier; target->age = next_age_++; return target;
    }
    void Refresh(Frame& f) noexcept {
        if (!Complete(f)) return;
        auto* s = FindStream(f.scope, f.role);
        auto* e = FindEntry(f.token.key, f.token.signature);
        if (!s || !e) return;
        if (Succeeded(f, Stage::ActualStorageRead) && Succeeded(f, Stage::TransferRecorded) &&
            (!e->proof || f.carrier.ordinal > e->proof.carrier.ordinal)) {
            e->proof = f.token; e->proof_target = f.target;
        }
        // Out-of-order submissions may complete the first, middle or last
        // member. Inspect only the three possible windows containing this frame.
        const auto first = f.carrier.ordinal > 2 ? f.carrier.ordinal - 2 : 1;
        for (auto start = first; start <= f.carrier.ordinal; ++start) {
            if (start <= s->break_ordinal || start > std::numeric_limits<std::uint64_t>::max() - 2) continue;
            Window candidate; bool complete = true;
            for (unsigned i = 0; i != 3; ++i) {
                const auto* member = FindOrdinal(*s, start + i);
                if (!member || !Complete(*member)) { complete = false; break; }
                candidate.members[i] = member->token;
            }
            if (complete && (!s->window.valid || start > s->window.members[0].carrier.ordinal)) {
                candidate.valid = true; s->window = candidate;
            }
        }
    }
    bool RecordSuccess(const Token& token, Stage stage, std::int32_t result) noexcept {
        auto* e = FindEntry(token.key, token.signature);
        auto* f = FindFrame(token.key.scope, token.key.role, token.carrier);
        if (!e || e->failure.present || e->attempt_id != token.attempt_id ||
            !f || f->conflict || !(f->token == token)) return false;
        auto& event = f->events[static_cast<unsigned>(stage)];
        if (event.seen) return event.success && event.result == result;
        event = {true, true, result};
        if (stage == Stage::Submitted) {
            f->target_frozen = true;
            if (!f->target || f->target_conflict)
                if (auto* s = FindStream(f->scope, f->role)) Break(*s, f->carrier.ordinal);
        }
        if (stage == Stage::Descriptor && token.carrier.ordinal > e->descriptor_ordinal)
            e->descriptor_ordinal = token.carrier.ordinal;
        Refresh(*f); return true;
    }
    void RetireEntry(Entry& entry) noexcept {
        for (auto& f : frames_) if (f.used && f.token && f.token.key == entry.key &&
            f.token.signature == entry.signature && f.token.attempt_id == entry.attempt_id)
            f.retired_complete = Complete(f);
        entry = {};
    }
public:
    // Resolve is also called while waiting. Neither observing native calls nor
    // resolving the same interpretation consumes a new attempt identity.
    Resolution Resolve(const Key& key, const ViewSignature& signature, SemanticState semantic) noexcept {
        if (!Valid(key)) return {};
        auto* e = FindEntry(key, signature);
        if (!e) {
            bool waited = semantic == SemanticState::Waiting;
            for (const auto& prior : entries_) if (prior.used && prior.key == key && prior.was_waiting) waited = true;
            for (auto& vacant : entries_) if (!vacant.used) {
                e = &vacant; *e = {}; e->used = true; e->key = key; e->signature = signature;
                e->was_waiting = waited; break;
            }
            if (!e) return {State::Unknown, 0, false, true};
        }
        e->semantic = semantic;
        e->was_waiting = e->was_waiting || semantic == SemanticState::Waiting;
        if (e->failure.present) return {State::Latched, e->attempt_id, false, false};
        if (!Ready(semantic) || !signature.srv) return {State::Waiting, e->attempt_id, false, false};
        return {e->attempt_id ? State::Pending : State::Resolved, e->attempt_id, true, false};
    }

    // Call once per actual classified native carrier, including NR bypasses.
    // Ordinals are per scope and role; calls to another Feature cannot add gaps.
    Carrier ObserveCarrier(const Scope& scope, std::uint32_t role, std::uint64_t frame_id) noexcept {
        auto* s = EnsureStream(scope, role);
        if (!s || !frame_id || s->latest.ordinal == std::numeric_limits<std::uint64_t>::max()) return {};
        Carrier carrier{frame_id, s->latest.ordinal + 1};
        if (!AllocateFrame(*s, carrier)) return {};
        s->latest = carrier; return carrier;
    }

    // Begin is called immediately before actual descriptor programming / NR
    // work. Repeated frames of this interpretation use one stable attempt_id.
    Token Begin(const Key& key, const ViewSignature& signature, Carrier carrier,
                WritebackTargetPolicy target_policy = WritebackTargetPolicy::SameResource) noexcept {
        auto* e = FindEntry(key, signature);
        auto* s = FindStream(key.scope, key.role);
        auto* f = FindFrame(key.scope, key.role, carrier);
        if (!e || !s || !f || !(s->latest == carrier) || f->ended || f->conflict ||
            e->failure.present || !Ready(e->semantic) || !signature.srv) return {};
        if (f->token) {
            if (f->token.key == key && f->token.signature == signature) return f->token;
            f->conflict = true; Break(*s, carrier.ordinal); return {};
        }
        if (!e->attempt_id) {
            if (next_attempt_ == std::numeric_limits<std::uint64_t>::max()) return {};
            e->attempt_id = next_attempt_++; e->first_ordinal = carrier.ordinal;
        }
        f->token = {key, signature, e->attempt_id, carrier};
        if (target_policy == WritebackTargetPolicy::SameResource)
            f->target = {key.resource, key.resource_generation, key.subresource};
        return f->token;
    }

    // PRE binds its owned Result for this frame; it need not equal the semantic
    // input Key or the Result selected by another frame of the same attempt.
    // POST may use Begin's default same-resource target. Binding is frozen at
    // its first explicit assignment, or automatically before submit/readback.
    bool BindWritebackTarget(const Token& token, Target target) noexcept {
        auto* e = FindEntry(token.key, token.signature);
        auto* f = FindFrame(token.key.scope, token.key.role, token.carrier);
        if (!e || e->failure.present || e->attempt_id != token.attempt_id || !f ||
            f->conflict || f->target_conflict || !(f->token == token)) return false;
        const bool too_late = f->events[static_cast<unsigned>(Stage::Submitted)].seen ||
                              f->events[static_cast<unsigned>(Stage::ActualStorageRead)].seen;
        if (!target || too_late || (f->target_frozen && !(f->target == target))) {
            f->target_frozen = true; f->target_conflict = true;
            if (auto* s = FindStream(f->scope, f->role)) Break(*s, f->carrier.ordinal);
            return false;
        }
        f->target = target; f->target_frozen = true; return true;
    }

    bool EndCarrier(const Scope& scope, std::uint32_t role, Carrier carrier, bool used_nr) noexcept {
        auto* f = FindFrame(scope, role, carrier);
        auto* s = FindStream(scope, role);
        if (!f || !s) return false;
        if (f->ended) return f->used_nr == used_nr;
        f->ended = true; f->used_nr = used_nr;
        if (!used_nr) Break(*s, carrier.ordinal);
        Refresh(*f); return true;
    }

    // Successful ActualStorageRead is accepted only through RecordWriteback;
    // a raw boolean must never bypass frame target validation.
    // Missing evidence is not a false outcome; do not Record an unperformed step.
    bool Record(const Token& token, Stage stage, bool success, std::int32_t result = 0) noexcept {
        if (stage >= Stage::Count || !token) return false;
        if (!success) return ReportFailure(token, stage, result);
        if (stage == Stage::ActualStorageRead) return false;
        return RecordSuccess(token, stage, result);
    }

    bool RecordWriteback(const Token& token, const WritebackProof& proof) noexcept {
        if (!proof.actual_storage_read) return false;
        auto* e = FindEntry(token.key, token.signature);
        auto* f = FindFrame(token.key.scope, token.key.role, token.carrier);
        if (!e || e->failure.present || e->attempt_id != token.attempt_id || !f ||
            f->conflict || f->target_conflict || !(f->token == token)) return false;
        f->target_frozen = true;
        if (!f->target) {
            if (auto* s = FindStream(f->scope, f->role)) Break(*s, f->carrier.ordinal);
            return false;
        }
        if (proof.resource != f->target.resource || proof.resource_generation != f->target.resource_generation ||
            proof.subresource != f->target.subresource)
            return ReportFailure(token, Stage::ActualStorageRead, proof.result, FailureKind::WrongTarget);
        if (!proof.finite) return ReportFailure(token, Stage::ActualStorageRead, proof.result, FailureKind::NonFinite);
        if (!proof.matches_expected)
            return ReportFailure(token, Stage::ActualStorageRead, proof.result, FailureKind::WritebackMismatch);
        return RecordSuccess(token, Stage::ActualStorageRead, proof.result);
    }

    bool ReportFailure(const Token& token, Stage stage, std::int32_t result,
                       FailureKind kind = FailureKind::Reported) noexcept {
        if (!token || stage >= Stage::Count) return false;
        auto* e = FindEntry(token.key, token.signature);
        if (!e || e->attempt_id != token.attempt_id) return false;
        // Failure is conservative even when its old frame was evicted. A
        // success from such a token is rejected by Record's exact frame lookup.
        if (e->failure.present) return true;
        e->failure = {true, stage, result, kind, token};
        if (auto* f = FindFrame(token.key.scope, token.key.role, token.carrier)) {
            if (f->token == token) f->events[static_cast<unsigned>(stage)] = {true, false, result};
        }
        if (auto* s = FindStream(token.key.scope, token.key.role)) {
            if (!s->first_failure.present) s->first_failure = e->failure;
            Break(*s, s->latest.ordinal);
        }
        return true;
    }

    RecoveryEvidence Read(const Key& key, const ViewSignature& signature) const noexcept {
        RecoveryEvidence r;
        const auto* e = FindEntry(key, signature);
        if (!e) { r.capacity_unknown = true; return r; }
        r.semantic = e->semantic; r.was_waiting = e->was_waiting;
        r.attempt_id = e->attempt_id; r.first_failure = e->failure;
        const auto* s = FindStream(key.scope, key.role);
        if (!r.first_failure.present && s && s->first_failure.present) {
            r.first_failure = s->first_failure; r.first_failure_historical = true;
        }
        if (e->failure.present) { r.state = State::Latched; return r; }
        if (!Ready(e->semantic) || !signature.srv) { r.state = State::Waiting; return r; }
        if (!e->attempt_id) { r.state = State::Resolved; return r; }
        r.state = State::Pending;
        if (!s) { r.capacity_unknown = true; return r; }
        r.frame_evidence_lost = s->evidence_lost;
        r.descriptor_verified = e->descriptor_ordinal > s->break_ordinal;
        if (e->proof && e->proof.attempt_id == e->attempt_id && e->proof.key == key &&
            e->proof.signature == signature) {
            r.writeback_verified = e->proof.carrier.ordinal > s->break_ordinal;
            r.proof_attempt_id = e->proof.attempt_id;
            r.proof_target = e->proof_target;
            r.proof_frame_id = e->proof.carrier.frame_id; r.proof_carrier_ordinal = e->proof.carrier.ordinal;
            r.proof_age = s->latest.ordinal >= e->proof.carrier.ordinal ? s->latest.ordinal - e->proof.carrier.ordinal : 0;
        }
        if (s->window.valid && s->window.members[0].carrier.ordinal > s->break_ordinal &&
            s->window.members[2].carrier.ordinal >= e->first_ordinal) {
            r.correlated_window = true;
            r.window_first_ordinal = s->window.members[0].carrier.ordinal;
            r.window_last_ordinal = s->window.members[2].carrier.ordinal;
            r.window_first_frame_id = s->window.members[0].carrier.frame_id;
            r.window_last_frame_id = s->window.members[2].carrier.frame_id;
            r.nr_success_streak = r.native_success_streak = r.submitted_count = 3;
        } else {
            // Pending diagnostics use only a single exact consecutive window.
            // They cannot combine a native count, NR count and another submit.
            const Frame* latest = nullptr;
            for (const auto& f : frames_) if (f.used && f.scope == key.scope && f.role == key.role &&
                f.ended && f.carrier.ordinal > s->break_ordinal &&
                (!latest || f.carrier.ordinal > latest->carrier.ordinal)) latest = &f;
            bool nr_chain = true, native_chain = true;
            if (latest) for (unsigned i = 0; i != 3 && latest->carrier.ordinal > i; ++i) {
                const auto ordinal = latest->carrier.ordinal - i;
                const auto* f = FindOrdinal(*s, ordinal);
                if (!f || !f->ended || !f->used_nr || f->conflict || ordinal <= s->break_ordinal) break;
                if (!i) { r.window_last_ordinal = ordinal; r.window_last_frame_id = f->carrier.frame_id; }
                r.window_first_ordinal = ordinal; r.window_first_frame_id = f->carrier.frame_id;
                nr_chain = nr_chain && Succeeded(*f, Stage::NrEvaluate);
                native_chain = native_chain && Succeeded(*f, Stage::NativeSrEvaluate);
                if (nr_chain) ++r.nr_success_streak;
                if (native_chain) ++r.native_success_streak;
                if (Succeeded(*f, Stage::Submitted)) ++r.submitted_count;
            }
        }
        if (r.correlated_window && r.descriptor_verified && r.writeback_verified)
            r.state = State::RecoverySucceeded;
        return r;
    }

    // Waiting/ready metadata with no Begin has never owned NR work. It can be
    // forgotten while native carriers are in flight without claiming GPU idle
    // or weakening any actual-attempt/failure lifetime obligation.
    bool ForgetUnattempted(const Key& key) noexcept {
        bool forgotten = false;
        for (auto& e : entries_) if (e.used && e.key == key && !e.attempt_id && !e.failure.present) {
            e = {}; forgotten = true;
        }
        return forgotten;
    }

    // The owner certifies these three independent facts from its real recording,
    // GPU retirement and readback collection state, not from this ledger's
    // Submitted event. Reclaiming successful transient entries does not erase
    // immutable completed carrier evidence used by another resource in the ring.
    bool RetireInactiveResource(const Key& key, RetirementProof proof) noexcept {
        if (!proof.recording_idle || !proof.gpu_idle || !proof.readbacks_collected) return false;
        for (const auto& e : entries_) if (e.used && e.key == key && !e.failure.present) {
            for (const auto& f : frames_) if (f.used && f.token && f.token.key == e.key &&
                f.token.signature == e.signature && f.token.attempt_id == e.attempt_id &&
                !f.ended) return false;
        }
        bool retired = false;
        for (auto& e : entries_) if (e.used && e.key == key && !e.failure.present) {
            RetireEntry(e); retired = true;
        }
        return retired;
    }

    // Actual resource retirement may reclaim nonfailed interpretations. Failed
    // latches survive both this and inactive rebinding until Feature/Scope
    // retirement, even when no reliable native resource-destruction hook exists.
    void RetireResource(const Scope& scope, std::uint64_t resource, std::uint64_t generation) noexcept {
        for (auto& e : entries_) if (e.used && e.key.scope == scope &&
            e.key.resource == resource && e.key.resource_generation == generation && !e.failure.present) RetireEntry(e);
    }
    void RetireScope(const Scope& scope) noexcept {
        for (auto& e : entries_) if (e.used && e.key.scope == scope) e = {};
        for (auto& f : frames_) if (f.used && f.scope == scope) f = {};
        for (auto& s : streams_) if (s.used && s.scope == scope) s = {};
    }
};
using ResourceRecoveryLedger = Ledger<>;
inline const char* StateName(State state) noexcept {
    switch (state) {
    case State::Waiting: return "waiting-semantics";
    case State::Resolved: return "semantics-resolved";
    case State::Pending: return "recovery-pending";
    case State::RecoverySucceeded: return "recovery-succeeded";
    case State::Latched: return "interpretation-failed-latched";
    default: return "recovery-evidence-unknown";
    }
}
inline const char* StageName(Stage stage) noexcept {
    switch (stage) {
    case Stage::NrCreate: return "nr-create";
    case Stage::ParameterBinding: return "parameter-binding";
    case Stage::ResourceAllocation: return "resource-allocation";
    case Stage::Descriptor: return "descriptor-programming";
    case Stage::NrEvaluate: return "nr-evaluate";
    case Stage::NativeSrEvaluate: return "native-sr-evaluate";
    case Stage::TransferRecorded: return "transfer-recording";
    case Stage::Submitted: return "real-submission";
    case Stage::ActualStorageRead: return "actual-storage-read";
    default: return "unknown";
    }
}
} // namespace nr_resource_recovery
