#include <cstdio>
#include <cstdlib>

#include "../src/nr_f8_runtime.inl"

namespace {

struct Config {
    unsigned int model = 7;
    float transfer_strength = 1.0f;
    bool region_protection = false;
    bool jitter = false;
};

struct Fake {
    unsigned int apply_count = 0;
    unsigned int raw_request_count = 0;
    unsigned int restore_count = 0;
    unsigned int package_count = 0;
    bool package_partial = false;
    bool fail_apply = false;
    unsigned int generation = 10;
    unsigned int requested_slots[3][nr_f8_runtime::kFrames] = {};
    bool requested[3][nr_f8_runtime::kFrames] = {};
};

Fake *g_fake = nullptr;

bool ApplyGroup(unsigned int group, nr_f8_runtime::Mode,
                const nr_f8_runtime::Options &, unsigned int *generation) {
    ++g_fake->apply_count;
    if (g_fake->fail_apply) return false;
    *generation = g_fake->generation + group;
    return true;
}

bool RequestRawSlot(unsigned int group, unsigned int slot) {
    ++g_fake->raw_request_count;
    if (group < 3 && slot < nr_f8_runtime::kFrames) {
        g_fake->requested[group][slot] = true;
        g_fake->requested_slots[group][slot] = slot;
    }
    return true;
}

bool Restore(const void *) {
    ++g_fake->restore_count;
    return true;
}

void Package(bool partial) {
    ++g_fake->package_count;
    g_fake->package_partial = partial;
}

void Check(bool condition, const char *what) {
    if (!condition) {
        std::fprintf(stderr, "f8 runtime check failed: %s\n", what);
        std::exit(1);
    }
}

nr_f8_runtime::Callbacks Callbacks() {
    nr_f8_runtime::Callbacks callbacks = {};
    callbacks.apply_group = ApplyGroup;
    callbacks.request_raw_slot = RequestRawSlot;
    callbacks.restore = Restore;
    callbacks.package = Package;
    return callbacks;
}

nr_f8_runtime::Identity Identity(unsigned long long frame,
                                 unsigned long long instance = 99,
                                 unsigned int generation = 10,
                                 unsigned int width = 1920,
                                 unsigned int height = 1080) {
    nr_f8_runtime::Identity value = {};
    value.frame = frame;
    value.instance = instance;
    value.generation = generation;
    value.width = width;
    value.height = height;
    value.warmed = true;
    return value;
}

void FeedRaw(nr_f8_runtime::Session<Config> &session,
             const nr_f8_runtime::Identity &identity, unsigned int slot) {
    nr_f8_runtime::RawSample sample = {};
    sample.identity = identity;
    sample.slot = slot;
    sample.complete = true;
    session.raw_sample(sample);
}

void FeedCompleteGroup(nr_f8_runtime::Session<Config> &session,
                       unsigned long long first_frame,
                       unsigned int generation) {
    session.applied_identity(Identity(first_frame, 99, generation));
    for (unsigned int i = 0; i < nr_f8_runtime::kFrames; ++i) {
        const nr_f8_runtime::Identity identity =
            Identity(first_frame + i, 99, generation);
        session.successful_frame(identity, false);
        FeedRaw(session, identity, i);
    }
}

void TestCompleteSequence() {
    Fake fake = {};
    g_fake = &fake;
    nr_f8_runtime::Session<Config> session;
    nr_f8_runtime::Options options = {};
    options.issue = 3;
    options.intensity = 1.5f;
    options.intensity_override = true;
    options.region_protection = true;
    options.jitter_experiment = true;
    Check(session.request(options), "request accepted");
    Check(!session.request(options), "duplicate request rejected");
    Config original = {};
    original.transfer_strength = 0.75f;
    Check(session.begin(original, Callbacks(), 1000), "begin accepted");
    Check(session.state() == nr_f8_runtime::State::Running, "running state");
    Check(fake.apply_count == 1 && fake.raw_request_count == 0,
          "native group has no raw requests");
    session.screenshot_saved(0);
    Check(session.tick(4000), "native group advances after countdown");
    Check(fake.apply_count == 2 &&
              fake.raw_request_count == nr_f8_runtime::kFrames,
          "second group requests all immutable slots");
    FeedCompleteGroup(session, 200, 11);
    Check(session.frame_count(1) == nr_f8_runtime::kFrames &&
              session.raw_count(1) == nr_f8_runtime::kFrames,
          "second group has 16 matched frames and raw samples");
    Check(session.tick(7000), "second group advances");
    Check(fake.apply_count == 3 &&
              fake.raw_request_count == nr_f8_runtime::kFrames * 2,
          "third group requests another immutable slot set");
    FeedCompleteGroup(session, 300, 12);
    Check(session.tick(11000), "complete sequence enters restoring");
    Check(session.state() == nr_f8_runtime::State::Restoring,
          "restoring state");
    Check(fake.restore_count == 0, "restore is not performed under session lock");
    Check(Callbacks().restore(&session.original()), "restore callback succeeds");
    session.restore_finished(true);
    Check(session.state() == nr_f8_runtime::State::Packaging,
          "packaging state");
    Check(fake.package_count == 1 && !fake.package_partial,
          "complete package requested");
    session.packaged(true);
    Check(session.state() == nr_f8_runtime::State::Complete,
          "complete state");
    Check(session.original().transfer_strength == 0.75f,
          "original config retained");
}

void TestGapResetAndLateRaw() {
    Fake fake = {};
    g_fake = &fake;
    nr_f8_runtime::Session<Config> session;
    Check(session.request({}), "gap request");
    Check(session.begin({}, Callbacks(), 0), "gap begin");
    session.screenshot_saved(0);
    session.tick(3000); // native group -> NR-to-SR
    session.applied_identity(Identity(10, 99, 11));
    session.successful_frame(Identity(10, 99, 11), false);
    FeedRaw(session, Identity(10, 99, 11), 0);
    session.successful_frame(Identity(12, 99, 11), false);
    Check(session.partial() && session.frame_count(1) == 1,
          "frame gap marks partial and starts a new segment");
    Check(session.raw_count(1) == 0, "gap clears old raw matches");
    FeedRaw(session, Identity(10, 99, 11), 0);
    Check(session.partial() && session.raw_count(1) == 0,
          "late old raw cannot attach to new segment");
    session.successful_frame(Identity(13, 99, 11), true);
    Check(session.partial() && session.frame_count(1) == 0,
          "reset breaks the current segment");
    session.successful_frame(Identity(14, 99, 11), false);
    Check(session.frame_count(1) == 1, "post-reset frames remain observable");
}

void TestCancelAndTimeoutRestore() {
    Fake fake = {};
    g_fake = &fake;
    nr_f8_runtime::Session<Config> cancelled;
    Check(cancelled.request({}), "cancel request");
    Check(cancelled.begin({}, Callbacks(), 100), "cancel begin");
    cancelled.cancel();
    Check(cancelled.state() == nr_f8_runtime::State::Restoring,
          "cancel enters restore");
    cancelled.restore_finished(false);
    Check(fake.package_count == 1 && fake.package_partial,
          "cancel package is partial");
    cancelled.packaged(false);
    Check(cancelled.state() == nr_f8_runtime::State::Partial,
          "cancel ends partial");

    fake = {};
    nr_f8_runtime::Session<Config> timed_out;
    Check(timed_out.request({}), "timeout request");
    Check(timed_out.begin({}, Callbacks(), 0), "timeout begin");
    Check(timed_out.tick(nr_f8_runtime::Session<Config>::kGroupTimeoutMs),
          "timeout enters restore");
    Check(timed_out.state() == nr_f8_runtime::State::Restoring,
          "timeout restore state");
    Check(Callbacks().restore(&timed_out.original()), "timeout restore callback succeeds");
    timed_out.restore_finished(true);
    timed_out.packaged(false);
    Check(timed_out.state() == nr_f8_runtime::State::Partial,
          "timeout ends partial");
}

void TestCancelBeforeStart() {
    Fake fake = {};
    g_fake = &fake;
    nr_f8_runtime::Session<Config> session;
    Check(session.request({}), "pre-start cancel request");
    session.cancel();
    Check(session.state() == nr_f8_runtime::State::Idle,
          "pre-start cancel closes the pending session");
    Check(fake.restore_count == 0, "pre-start cancel does not restore config");
}

void TestApplyFailure() {
    Fake fake = {};
    fake.fail_apply = true;
    g_fake = &fake;
    nr_f8_runtime::Session<Config> session;
    Check(session.request({}), "failed apply request");
    Check(!session.begin({}, Callbacks(), 0), "failed apply rejected");
    Check(session.state() == nr_f8_runtime::State::Restoring && session.partial(),
          "failed apply restores partial");
}

} // namespace

int main() {
    TestCompleteSequence();
    TestGapResetAndLateRaw();
    TestCancelAndTimeoutRestore();
    TestCancelBeforeStart();
    TestApplyFailure();
    std::puts("f8 runtime harness: PASS");
    return 0;
}
