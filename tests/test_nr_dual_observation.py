"""Focused NR2 observation and model-parameter continuity checks.

The first harness executes the new platform-free status aggregator.  The
second harness compiles the production tuning-signature function extracted
from nr_before_sr.cpp, then exercises two independent feature/parameter
owners through first creation, face-mask toggles, overall detail edits, and
reset.  Source extraction also checks that the real Bind/Create paths use the
context-owned signature and that the dual admission keeps feature/parameter
objects independent.  It does not claim that the model detects faces.
"""

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def definition(text: str, marker: str, semicolon: bool = False) -> str:
    start = text.index(marker)
    brace = text.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end + int(semicolon)]


OBSERVATION_HARNESS = r'''
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include "nr_dual_observation.hpp"

using namespace nr_dual_observation;
unsigned checks = 0;
void Check(bool ok, const char *label) {
    ++checks;
    if (!ok) throw std::runtime_error(label);
}

FrameResult R(std::uint64_t tick, std::uint32_t config, std::uint64_t route,
              bool eligible, bool evaluated, bool warmup, bool adopted) {
    FrameResult r{};
    r.tick_ms = tick; r.config_generation = config;
    r.route = {route, true}; r.enabled = true;
    r.eligible_primary = eligible;
    r.secondary_evaluate_success = evaluated;
    r.warmup_discarded = warmup;
    r.final_adopted = adopted;
    return r;
}

SnapshotQuery Q(std::uint64_t tick, std::uint32_t config, std::uint64_t route,
                bool enabled = true) {
    SnapshotQuery q{};
    q.tick_ms = tick; q.config_generation = config;
    q.route = {route, true}; q.enabled = enabled;
    return q;
}

int main() {
    Aggregator a;
    Check(a.SnapshotUi(Q(0, 1, 7)).state == UiState::WaitingForFrame,
          "enabled route waits before the first eligible frame");

    // NGX success during warm-up is an evaluation receipt, never an adopted
    // final answer, even if a malformed caller also sets final_adopted.
    auto warm = R(0, 1, 7, true, true, true, true);
    a.Record(warm);
    auto s = a.SnapshotUi(Q(100, 1, 7));
    Check(s.state == UiState::Preparing && s.counts.eligible_primary == 1 &&
              s.counts.secondary_evaluate_success == 1 &&
              s.counts.warmup_discarded == 1 && s.counts.final_adopted == 0,
          "warm-up success is preparing and not adopted");

    a.Record(R(100, 1, 7, true, true, false, true));
    s = a.SnapshotUi(Q(200, 1, 7));
    Check(s.state == UiState::Intermittent &&
              s.counts.final_adopted == 1 && s.counts.eligible_primary == 2,
          "partial adoption is intermittent");
    a.Record(R(200, 1, 7, true, true, false, true));
    s = a.SnapshotUi(Q(250, 1, 7));
    Check(s.state == UiState::Intermittent && s.counts.final_adopted == 2 &&
              s.counts.eligible_primary == 3,
          "partial adoption remains intermittent");
    a.Record(R(300, 1, 7, true, true, false, true));
    s = a.SnapshotUi(Q(350, 1, 7));
    Check(s.state == UiState::Intermittent && s.counts.final_adopted == 3 &&
              s.counts.eligible_primary == 4,
          "one warm-up frame keeps the window intermittent");
    a.Record(R(400, 1, 7, true, true, false, true));
    s = a.SnapshotUi(Q(450, 1, 7));
    Check(s.state == UiState::Intermittent && s.counts.final_adopted == 4 &&
              s.counts.eligible_primary == 5,
          "the warm-up denominator remains visible in the window");

    // A new 500 ms bucket does not carry old counts forward.
    a.Record(R(700, 1, 7, true, true, false, true));
    s = a.SnapshotUi(Q(701, 1, 7));
    Check(s.state == UiState::Active && s.counts.eligible_primary == 1 &&
              s.counts.final_adopted == 1,
          "500 ms window starts with current receipts");

    // Querying a new key hides old active data even before the quiet Record
    // arrives; the quiet Record then establishes the new empty window.
    s = a.SnapshotUi(Q(710, 2, 7));
    Check(s.state == UiState::WaitingForFrame &&
              s.counts.eligible_primary == 0,
          "config mismatch cannot expose old window");
    auto quiet = R(710, 2, 7, false, false, false, false);
    quiet.enabled = true;
    a.Record(quiet);
    s = a.SnapshotUi(Q(720, 2, 7));
    Check(s.state == UiState::WaitingForFrame &&
              s.counts.final_adopted == 0,
          "new config quiet record clears the window");

    // A route identity change has the same boundary semantics.
    a.Record(R(800, 2, 8, true, true, false, true));
    s = a.SnapshotUi(Q(801, 2, 8));
    Check(s.state == UiState::Active && s.counts.eligible_primary == 1,
          "new valid route starts its own window");

    Aggregator pending;
    pending.Record(R(0, 3, 9, true, true, true, false));
    Check(pending.SnapshotUi(Q(100, 3, 9)).state == UiState::Preparing &&
              pending.SnapshotUi(Q(100, 3, 9)).no_adoption_age_ms == 100,
          "initial no-adoption period is preparing");
    pending.Record(R(1000, 3, 9, true, true, true, false));
    pending.Record(R(2000, 3, 9, true, true, true, false));
    auto pending_snapshot = pending.SnapshotUi(Q(2001, 3, 9));
    Check(pending_snapshot.state == UiState::NotYetApplied &&
              pending_snapshot.no_adoption_age_ms == 2001,
          "continuous eligible frames without adoption become not-yet-applied");
    pending.Record(R(2100, 3, 9, true, true, false, true));
    pending_snapshot = pending.SnapshotUi(Q(2101, 3, 9));
    Check(pending_snapshot.no_adoption_age_ms == 0 &&
              pending_snapshot.state == UiState::Intermittent,
          "an adopted result clears the no-adoption streak");

    Aggregator stale;
    stale.Record(R(0, 4, 10, true, true, false, true));
    s = stale.SnapshotUi(Q(2101, 4, 10));
    Check(s.state == UiState::WaitingForFrame && s.counts.eligible_primary == 0,
          "an adopted route with no recent processing waits for a frame");

    Aggregator failed;
    auto bad = R(10, 5, 11, false, false, false, false);
    bad.fatal = true; bad.fatal_code = 0xBAD00001u;
    failed.Record(bad);
    s = failed.SnapshotUi(Q(11, 5, 11));
    Check(s.state == UiState::Failed && s.fatal_code == 0xBAD00001u,
          "fatal is immediate");
    failed.Record(R(20, 6, 11, false, false, false, false));
    Check(failed.SnapshotUi(Q(21, 6, 11)).state == UiState::WaitingForFrame,
          "new config clears the fatal latch");

    Check(UiStateName(UiState::NotYetApplied) != nullptr,
          "stable state names are available to the UI adapter");
    std::printf("NR2 observation: %u checks, 0 failures\n", checks);
    return 0;
}
'''


FACE_HARNESS = r'''
#include <cstdio>
#include <string>
#include <stdexcept>
#include <unordered_map>

enum class NrJitterMode : int { Legacy = 0, GameInputPixel = 1 };
TUNING_TYPE
struct ChainConfig {
    float intensity = 1.2f;
    float local_tone = 1.0f;
    float local_structure = 1.0f;
    float skin_structure = -1.0f;
    bool auto_mask = false;
    unsigned int style = 0;
    bool ui_correction = false;
    NrJitterMode jitter_mode = NrJitterMode::Legacy;
    unsigned int nr_input_filter = 0;
};
ChainConfig g_config{};
TUNING_FUNCTION
SAME_TUNING_FUNCTION

struct ParameterValue {
    enum class Kind { Float, Int, UInt } kind = Kind::Int;
    float float_value = 0;
    int int_value = 0;
    unsigned int uint_value = 0;
};
struct NgxParams {
    std::unordered_map<std::string, ParameterValue> values;
};

void SetFloat(NgxParams *params, const char *key, float value) {
    params->values[key] = {ParameterValue::Kind::Float, value, 0, 0};
}
void SetInt(NgxParams *params, const char *key, int value) {
    params->values[key] = {ParameterValue::Kind::Int, 0, value, 0};
}
void SetUInt(NgxParams *params, const char *key, unsigned int value) {
    params->values[key] = {ParameterValue::Kind::UInt, 0, 0, value};
}

struct NrContext {
    NgxParams *params = nullptr;
    NrTuningSignature tuning_signature{};
    bool jitter_binding_written = false;
};

// This body is the exact trailing model-parameter setter segment extracted
// from production BindNrParameters. The harness supplies only the existing
// parameter-writer and context seams; it does not recreate the setter logic.
bool BindNrParameterTuning(NrContext &context) {
    NgxParams *params = context.params;
    if (params == nullptr) return false;
BIND_SETTERS
}

struct Layer {
    int feature_owner = 0;
    NgxParams params{};
    NrContext context{};
};

bool ApplyToBoth(Layer &primary, Layer &secondary) {
    primary.context.params = &primary.params;
    secondary.context.params = &secondary.params;
    return BindNrParameterTuning(primary.context) &&
        BindNrParameterTuning(secondary.context);
}

const ParameterValue &Value(const Layer &layer, const char *key) {
    return layer.params.values.at(key);
}
bool SameFloat(const Layer &a, const Layer &b, const char *key, float value) {
    return Value(a, key).kind == ParameterValue::Kind::Float &&
        Value(b, key).kind == ParameterValue::Kind::Float &&
        Value(a, key).float_value == value && Value(b, key).float_value == value;
}
bool SameInt(const Layer &a, const Layer &b, const char *key, int value) {
    return Value(a, key).kind == ParameterValue::Kind::Int &&
        Value(b, key).kind == ParameterValue::Kind::Int &&
        Value(a, key).int_value == value && Value(b, key).int_value == value;
}
bool SameUInt(const Layer &a, const Layer &b, const char *key,
             unsigned int value) {
    return Value(a, key).kind == ParameterValue::Kind::UInt &&
        Value(b, key).kind == ParameterValue::Kind::UInt &&
        Value(a, key).uint_value == value && Value(b, key).uint_value == value;
}
void Check(bool ok, const char *label, unsigned &checks) {
    ++checks;
    if (!ok) throw std::runtime_error(label);
}

int main() {
    unsigned checks = 0;
    Layer primary{1, {}, {}}, secondary{2, {}, {}};
    primary.context.tuning_signature = CurrentNrTuningSignature();
    secondary.context.tuning_signature = primary.context.tuning_signature;
    const auto first = CurrentNrTuningSignature();
    Check(ApplyToBoth(primary, secondary) &&
              SameNrTuningSignature(first, primary.context.tuning_signature) &&
              SameFloat(primary, secondary, "DLSSNR.Intensity", first.intensity) &&
              SameFloat(primary, secondary, "DLSSNR.LocalToneStrength", first.local_tone) &&
              SameFloat(primary, secondary, "DLSSNR.LocalStructureStrength",
                        first.local_structure) &&
              SameFloat(primary, secondary, "DLSSNR.SkinStructureStrength", -1.0f) &&
              SameInt(primary, secondary, "DLSSNR.UseAutoMask", 0),
          "production setter binds the first signature to both layers", checks);
    Check(&primary.feature_owner != &secondary.feature_owner &&
              &primary.params != &secondary.params &&
              &primary.context != &secondary.context,
          "first creation has independent feature, parameter, and context owners", checks);

    // AutoMask on carries the saved zero skin value through the production
    // setters for both independently-owned parameter blocks.
    g_config.skin_structure = 0.0f;
    g_config.auto_mask = true;
    const auto face_on = CurrentNrTuningSignature();
    primary.context.tuning_signature = face_on;
    secondary.context.tuning_signature = face_on;
    Check(ApplyToBoth(primary, secondary) && face_on.auto_mask &&
              face_on.skin_structure == 0.0f &&
              SameInt(primary, secondary, "DLSSNR.UseAutoMask", 1) &&
              SameFloat(primary, secondary, "DLSSNR.SkinStructureStrength", 0.0f),
          "AutoMask on reaches both layers with Skin=0", checks);

    g_config.local_tone = 1.35f;
    g_config.local_structure = .72f;
    const auto detail = CurrentNrTuningSignature();
    primary.context.tuning_signature = detail;
    secondary.context.tuning_signature = detail;
    Check(ApplyToBoth(primary, secondary) && detail.local_tone == 1.35f &&
              detail.local_structure == .72f &&
              SameFloat(primary, secondary, "DLSSNR.LocalToneStrength", 1.35f) &&
              SameFloat(primary, secondary, "DLSSNR.LocalStructureStrength", .72f) &&
              SameFloat(primary, secondary, "DLSSNR.SkinStructureStrength", 0.0f) &&
              SameInt(primary, secondary, "DLSSNR.UseAutoMask", 1),
          "overall detail edit keeps the face value on both layers", checks);
    Check(primary.params.values.count("DLSSNR.SkinStructureStrength") == 1 &&
              secondary.params.values.count("DLSSNR.SkinStructureStrength") == 1 &&
              SameUInt(primary, secondary, "DLSSNR.Style", detail.style) &&
              SameUInt(primary, secondary, "DLSSNR.UICorrection",
                       detail.ui_correction ? 1u : 0u),
          "new model parameter keys are written independently", checks);

    // Reset returns to the actual current config semantics and does not leave
    // either parameter block with pre-reset face/detail fields.
    g_config = {};
    const auto reset = CurrentNrTuningSignature();
    primary.context.tuning_signature = reset;
    secondary.context.tuning_signature = reset;
    Check(ApplyToBoth(primary, secondary) && !reset.auto_mask &&
              reset.skin_structure == -1.0f &&
              reset.local_tone == 1.0f && reset.local_structure == 1.0f &&
              SameFloat(primary, secondary, "DLSSNR.SkinStructureStrength", -1.0f) &&
              SameInt(primary, secondary, "DLSSNR.UseAutoMask", 0) &&
              SameFloat(primary, secondary, "DLSSNR.LocalToneStrength", 1.0f) &&
              SameFloat(primary, secondary, "DLSSNR.LocalStructureStrength", 1.0f),
          "reset synchronizes both independent layers", checks);
    Check(!SameNrTuningSignature(face_on, reset),
          "reset actually changes the model signature after the edits", checks);
    std::printf("Face/model continuity: %u checks, 0 failures\n", checks);
    return 0;
}
'''


RUNTIME_HARNESS = r'''
#include <atomic>
#include <cstdio>
#include <stdexcept>
#include "nr_dual_observation.hpp"

enum class NrJitterMode : int { Legacy = 0, GameInputPixel = 1 };
struct NrTuningSignature {
    float intensity = 1.2f;
    float local_tone = 1.0f;
    float local_structure = 1.0f;
    float skin_structure = -1.0f;
    bool auto_mask = false;
    unsigned int style = 0;
    bool ui_correction = false;
    NrJitterMode jitter_mode = NrJitterMode::Legacy;
    unsigned int input_filter = 0;
};
struct ChainConfig {
    bool enabled = false;
    unsigned int nr_passes = 1;
    unsigned int warmup_after_create = 2;
};
struct NrContext {
    unsigned long long instance_id = 0;
    unsigned long long parent_instance_id = 0;
    unsigned long long feature_generation = 0;
    unsigned int config_generation = 0;
    unsigned int evaluations = 0;
    unsigned int successful_evaluations_since_reset = 0;
    unsigned int width = 0, height = 0;
    int mode = 0;
    unsigned layer_index = 0;
    bool tuning_signature_initialized = false;
    NrTuningSignature tuning_signature{};
};
ChainConfig g_config{};
std::atomic<unsigned> g_config_generation{1};
struct CriticalSection {};
CriticalSection g_chain_cs;
unsigned long long test_tick = 0;
void EnterCriticalSection(CriticalSection *) {}
void LeaveCriticalSection(CriticalSection *) {}
unsigned long long GetTickCount64() { return test_tick; }
namespace nr_dual_runtime { std::atomic<unsigned> fatal_result{0}; }
#include "nr_dual_status_runtime.inl"

using namespace nr_dual_observation;
using namespace nr_dual_status_runtime;
unsigned checks = 0;
void Check(bool ok, const char *label) {
    ++checks;
    if (!ok) throw std::runtime_error(label);
}

int main() {
    g_config.enabled = true; g_config.nr_passes = 2;
    NrContext primary{}; primary.instance_id = 100; primary.mode = 2;
    NrContext secondary_a{}; secondary_a.layer_index = 1;
    NrContext secondary_b{}; secondary_b.layer_index = 1;

    Check(SnapshotUi().state == UiState::WaitingForFrame,
          "runtime snapshot starts waiting");
    RecordObservation(primary, nullptr, true, false, false, false);
    Check(SnapshotUi().state == UiState::Preparing,
          "eligible primary with no second result is preparing");
    ++test_tick;
    RecordObservation(primary, &secondary_a, true, true, true, true);
    auto s = SnapshotUi();
    Check(s.state == UiState::Preparing && s.counts.final_adopted == 0 &&
              s.counts.warmup_discarded == 1,
          "warm-up secondary success is never adopted");
    ++test_tick;
    RecordObservation(primary, &secondary_b, true, true, true, false);
    s = SnapshotUi();
    Check(s.state == UiState::Intermittent && s.counts.final_adopted == 1,
          "changing secondary address does not change primary route key");

    // Config is read at the locked call site, so a toggle hides the prior
    // window immediately even before another frame arrives.
    g_config_generation.store(2);
    g_config.nr_passes = 1;
    s = SnapshotUi();
    Check(s.state == UiState::Off && s.counts.eligible_primary == 0,
          "NR2 off is immediate from current config");
    g_config.nr_passes = 2;
    Check(SnapshotUi().state == UiState::WaitingForFrame,
          "new config waits before a new observation");
    primary.instance_id = 101;
    ++test_tick;
    RecordObservation(primary, nullptr, false, false, false, false);
    Check(SnapshotUi().state == UiState::WaitingForFrame,
          "new primary instance starts an empty route");

    // Raw fatal_result is authoritative without waiting for another Record.
    nr_dual_runtime::fatal_result.store(0xBAD00001u);
    s = SnapshotUi();
    Check(s.state == UiState::Failed && s.fatal_code == 0xBAD00001u,
          "raw dual fatal is immediate in UI snapshot");
    nr_dual_runtime::fatal_result.store(0);

    // Capture actual secondary identity fields after a successful Evaluate.
    g_config_generation.store(3); primary.instance_id = 200;
    RecordObservation(primary, nullptr, false, false, false, false);
    NrContext aux{};
    aux.layer_index = 1; aux.instance_id = 300; aux.parent_instance_id = 200;
    aux.mode = 2; aux.feature_generation = 44; aux.config_generation = 3;
    aux.evaluations = 8; aux.successful_evaluations_since_reset = 5;
    aux.width = 960; aux.height = 540; aux.tuning_signature.auto_mask = true;
    aux.tuning_signature.skin_structure = .65f;
    aux.tuning_signature_initialized = true;
    CaptureSecondaryIdentity(aux);
    const auto identity = SnapshotSecondaryIdentity();
    Check(identity.valid && identity.instance_id == 300 &&
              identity.feature_generation == 44 && identity.config_generation == 3 &&
              identity.evaluation == 8 && identity.since_reset == 5 &&
              identity.warmed_up && identity.tuning.auto_mask &&
              identity.tuning.skin_structure == .65f,
          "secondary identity captures actual context receipt");
    g_config_generation.store(4);
    Check(!SnapshotSecondaryIdentity().valid,
          "secondary identity is hidden after config generation change");
    std::printf("Runtime adapter: %u checks, 0 failures\n", checks);
    return 0;
}
'''


SERIALIZATION_HARNESS = r'''
#include <cstdio>
#include <stdexcept>
#include <string>
#include "nr_dual_observation.hpp"

enum class NrJitterMode : int { Legacy = 0, GameInputPixel = 1 };
struct NrTuningSignature {
    float intensity = 1.2f, local_tone = 1.0f, local_structure = 1.0f;
    float skin_structure = -1.0f;
    bool auto_mask = false;
    unsigned int style = 0;
    bool ui_correction = false;
    NrJitterMode jitter_mode = NrJitterMode::Legacy;
    unsigned int input_filter = 0;
};
namespace nr_dual_status_runtime {
struct SecondaryIdentity {
    bool valid = false;
    nr_dual_observation::Route route{};
    unsigned long long instance_id = 0, feature_generation = 0;
    unsigned config_generation = 0, evaluation = 0, since_reset = 0;
    unsigned width = 0, height = 0;
    bool warmed_up = false;
    NrTuningSignature tuning{};
};
struct SnapshotPair {
    nr_dual_observation::Snapshot ui{};
    SecondaryIdentity secondary{};
};
SnapshotPair pair{};
SnapshotPair SnapshotUiAndSecondary() { return pair; }
}
struct FeedbackSession { std::string text; };
template<class... Args>
void FeedbackAppend(FeedbackSession &s, const char *format, Args... args) {
    char buffer[32768] = {};
    const int written = std::snprintf(buffer, sizeof(buffer), format, args...);
    if (written > 0) s.text.append(buffer, static_cast<std::size_t>(written));
}
SERIALIZATION_HELPER
void Run(FeedbackSession *s) {
    FeedbackAppendDualSnapshot(*s, "test");
}
void Check(bool ok, const char *label) {
    if (!ok) throw std::runtime_error(label);
}
int main() {
    using namespace nr_dual_observation;
    using namespace nr_dual_status_runtime;
    pair.ui.state = UiState::Active;
    pair.ui.counts.eligible_primary = 7;
    pair.ui.counts.secondary_evaluate_success = 8;
    pair.ui.counts.warmup_discarded = 2;
    pair.ui.counts.final_adopted = 6;
    pair.ui.processing_age_ms = 31; pair.ui.no_adoption_age_ms = 32;
    pair.ui.window_start_ms = 900;
    pair.ui.config_generation = 44; pair.ui.route = {0x1234, true};
    pair.secondary.valid = true; pair.secondary.route = {0x1234, true};
    pair.secondary.instance_id = 300; pair.secondary.feature_generation = 55;
    pair.secondary.config_generation = 44; pair.secondary.evaluation = 8;
    pair.secondary.since_reset = 5; pair.secondary.width = 960; pair.secondary.height = 540;
    pair.secondary.warmed_up = true; pair.secondary.tuning.intensity = 1.7f;
    pair.secondary.tuning.local_tone = .8f; pair.secondary.tuning.local_structure = .72f;
    pair.secondary.tuning.skin_structure = .65f; pair.secondary.tuning.auto_mask = true;
    pair.secondary.tuning.style = 2; pair.secondary.tuning.ui_correction = true;
    FeedbackSession session; Run(&session);
    Check(session.text.find("dual.snapshot.state=active\n") != std::string::npos,
          "serialized state name");
    Check(session.text.find("dual.snapshot.eligiblePrimary=7\n") != std::string::npos &&
              session.text.find("dual.snapshot.secondaryEvaluate=8\n") != std::string::npos &&
              session.text.find("dual.snapshot.warmupDiscarded=2\n") != std::string::npos &&
              session.text.find("dual.snapshot.finalAdopted=6\n") != std::string::npos,
          "serialized five-hundred-ms counters");
    Check(session.text.find("dual.snapshot.processingAgeMs=31\n") != std::string::npos &&
              session.text.find("dual.snapshot.noAdoptionAgeMs=32\n") != std::string::npos &&
              session.text.find("dual.snapshot.config=44\n") != std::string::npos &&
              session.text.find("dual.snapshot.routeId=4660\n") != std::string::npos,
          "serialized snapshot timing and route");
    Check(session.text.find("dual.secondaryIdentity.instance=300\n") != std::string::npos &&
              session.text.find("dual.secondaryIdentity.config=44\n") != std::string::npos &&
              session.text.find("dual.secondaryIdentity.evaluation=8\n") != std::string::npos &&
              session.text.find("dual.secondaryIdentity.sinceReset=5\n") != std::string::npos &&
              session.text.find("dual.secondaryIdentity.extent=960x540\n") != std::string::npos,
          "serialized secondary identity");
    Check(session.text.find("dual.secondaryIdentity.tuning.skinStructure=0.649999976\n") != std::string::npos &&
              session.text.find("dual.secondaryIdentity.tuning.autoMask=1\n") != std::string::npos &&
              session.text.find("dual.secondaryIdentity.tuning.style=2\n") != std::string::npos,
          "serialized actual tuning fields");
    std::printf("F8 dual serialization: 5 checks, 0 failures\n");
    return 0;
}
'''


def source_checks(core: str, dual_runtime: str, dual_pass: str) -> None:
    find_create = definition(core, "NrContext *FindOrCreateContext(")
    bind = definition(core, "bool BindNrParameters(")
    create = definition(core, "NgxResult CreateSignedNrFeature(")

    assert "NrTuningSignature requested_tuning = CurrentNrTuningSignature();" in find_create
    assert "empty->tuning_signature = requested_tuning;" in find_create
    assert "layer_index" in find_create and "parent_instance" in find_create

    binding_lines = {
        'intensity': 'SetFloat(params, "DLSSNR.Intensity", tuning.intensity);',
        'local_tone': 'SetFloat(params, "DLSSNR.LocalToneStrength", tuning.local_tone);',
        'local_structure': 'SetFloat(params, "DLSSNR.LocalStructureStrength", tuning.local_structure);',
        'skin_structure': 'SetFloat(params, "DLSSNR.SkinStructureStrength", tuning.skin_structure);',
        'auto_mask': 'SetInt(params, "DLSSNR.UseAutoMask", tuning.auto_mask ? 1 : 0);',
        'style': 'SetUInt(params, "DLSSNR.Style", tuning.style);',
        'ui_correction': 'SetUInt(params, "DLSSNR.UICorrection", tuning.ui_correction ? 1u : 0u);',
    }
    for field, line in binding_lines.items():
        assert line in bind, f"BindNrParameters lost {field} from context tuning"
        assert f"tuning.{field}" in create, f"CreateSignedNrFeature lost {field} receipt"
    assert 'const NrTuningSignature &tuning = context.tuning_signature;' in bind
    assert 'const NrTuningSignature &tuning = context.tuning_signature;' in create

    # The dual route supplies distinct handles/parameter blocks to the real
    # admission contract.  These checks are against the production route code,
    # not a reconstructed two-layer fake.
    for line in (
        'admission.primary_feature=primary.nr_handle;',
        'admission.secondary_feature=aux.nr_handle;',
        'admission.primary_params=primary.params;',
        'admission.secondary_params=aux.params;',
    ):
        assert line in dual_runtime
    assert 'a.primary_feature != a.secondary_feature' in dual_pass
    assert 'a.primary_params != a.secondary_params' in dual_pass


def default_checks() -> None:
    dual = (ROOT / "src/nr_dual_pass.hpp").read_text(encoding="utf-8")
    recommended = (ROOT / "src/nr_recommended_picture.inl").read_text(encoding="utf-8")
    ini = (ROOT / "config/nr_before_sr.ini").read_text(encoding="utf-8")
    settings = definition(dual, "struct Settings")
    assert re.search(r"passes\s*=\s*1", settings)
    assert re.search(r"secondary_scale\s*\{\s*1\s*,\s*2\s*\}", settings)
    assert "next.nr_passes=1;next.nr_second_numerator=1;next.nr_second_denominator=2;" in recommended
    values = dict(re.findall(r"^\s*(NRPasses|NRSecondScaleNumerator|NRSecondScaleDenominator)\s*=\s*(\d+)\s*$", ini, re.MULTILINE))
    assert values == {"NRPasses": "1", "NRSecondScaleNumerator": "1", "NRSecondScaleDenominator": "2"}


def compile_and_run(compiler: str, source_text: str, substitutions: dict[str, str], label: str) -> None:
    for token in sorted(substitutions, key=len, reverse=True):
        value = substitutions[token]
        source_text = source_text.replace(token, value)
    with tempfile.TemporaryDirectory(prefix=f"nr-{label}-") as temporary:
        directory = Path(temporary)
        source = directory / f"{label}.cpp"
        exe = directory / f"{label}.exe"
        source.write_text(source_text, encoding="utf-8")
        subprocess.run([
            compiler, "-std=c++20", "-O0", "-Wall", "-Wextra", "-Werror", "-static",
            "-I", str(ROOT / "src"), str(source), "-o", str(exe),
        ], check=True)
        subprocess.run([str(exe)], check=True, timeout=20)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default=os.environ.get("CXX", "clang++"))
    args = parser.parse_args()
    compiler = shutil.which(args.compiler) or args.compiler
    if not Path(compiler).exists() and shutil.which(compiler) is None:
        raise SystemExit(f"C++ compiler not found: {args.compiler}")

    default_checks()
    core = (ROOT / "src/nr_before_sr.cpp").read_text(encoding="utf-8-sig")
    dual_runtime = (ROOT / "src/nr_dual_runtime.inl").read_text(encoding="utf-8")
    dual_pass = (ROOT / "src/nr_dual_pass.hpp").read_text(encoding="utf-8")
    feedback = (ROOT / "src/nr_feedback_capture.inl").read_text(encoding="utf-8")
    assert "%s.dual.status=%u" in feedback
    assert "nr_dual_status_runtime::SnapshotUiAndSecondary()" in feedback
    assert "dual.snapshot.eligiblePrimary=%llu" in feedback
    assert "dual.snapshot.secondaryEvaluate=%llu" in feedback
    assert "dual.snapshot.warmupDiscarded=%llu" in feedback
    assert "dual.snapshot.finalAdopted=%llu" in feedback
    assert "dual.snapshot.processingAgeMs=%llu" in feedback
    assert "dual.snapshot.noAdoptionAgeMs=%llu" in feedback
    assert "dual.secondaryIdentity.tuning.skinStructure=%.9g" in feedback
    serialization_helper = definition(
        feedback, "void FeedbackAppendDualSnapshot(FeedbackSession &s,")
    source_checks(core, dual_runtime, dual_pass)

    tuning_type = definition(core, "struct NrTuningSignature", True)
    tuning_function = definition(core, "NrTuningSignature CurrentNrTuningSignature(")
    same_tuning = definition(core, "bool SameNrTuningSignature(")
    bind_definition = definition(core, "bool BindNrParameters(")
    setter_start = bind_definition.index(
        "const NrTuningSignature &tuning = context.tuning_signature;")
    setter_end = bind_definition.index("return true;", setter_start) + len("return true;")
    binding_setters = bind_definition[setter_start:setter_end]
    assert "SetFloat(params, \"DLSSNR.Intensity\", tuning.intensity);" in binding_setters
    assert "SetInt(params, \"DLSSNR.UseAutoMask\", tuning.auto_mask ? 1 : 0);" in binding_setters
    assert "SetUInt(params, \"DLSSNR.UICorrection\", tuning.ui_correction ? 1u : 0u);" in binding_setters
    compile_and_run(compiler, OBSERVATION_HARNESS, {}, "observation")
    compile_and_run(compiler, FACE_HARNESS, {
        "TUNING_TYPE": tuning_type,
        "TUNING_FUNCTION": tuning_function,
        "SAME_TUNING_FUNCTION": same_tuning,
        "BIND_SETTERS": binding_setters,
    }, "face")
    compile_and_run(compiler, RUNTIME_HARNESS, {}, "runtime")
    compile_and_run(compiler, SERIALIZATION_HARNESS,
                    {"SERIALIZATION_HELPER": serialization_helper}, "serialization")
    print("NR2 defaults, observation aggregation, and source-backed face/model continuity checks passed")


if __name__ == "__main__":
    main()
