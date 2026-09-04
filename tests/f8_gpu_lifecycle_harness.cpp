#define NR_F8_GPU_LIFECYCLE_ONLY 1
#include "../src/nr_f8_gpu.inl"
#include <cstdio>
#include <cstdlib>

namespace {
using namespace nr_f8_gpu;

void Check(bool value, const char *message) {
    if (!value) {
        std::fprintf(stderr, "f8 gpu lifecycle check failed: %s\n", message);
        std::exit(1);
    }
}

TokenIdentity Key(unsigned long long request, unsigned int frame) {
    TokenIdentity key = {};
    key.request = request;
    key.group = 1;
    key.frame_index = frame;
    key.identity.frame_id = 100 + frame;
    key.identity.instance_id = 9;
    key.identity.config_generation = 4;
    key.identity.warmed_up = true;
    return key;
}

void TestSixteenAndFence() {
    TokenLedger ledger;
    for (unsigned int i = 0; i < kCapacity; ++i) {
        const int slot = ledger.Arm(Key(77, i));
        Check(slot >= 0, "sixteen frames reserve");
        Check(ledger.Record(static_cast<unsigned int>(slot), 500 + i),
              "record immutable command token");
    }
    Check(ledger.Count(TokenState::Recorded) == kCapacity, "all records remain held");
    Check(ledger.Arm(Key(77, 99)) < 0, "ring rejects overwrite while in flight");
    Check(ledger.Submit(500, reinterpret_cast<void *>(1), 1000),
          "submit exact queue fence");
    Check(!ledger.Ready(0, reinterpret_cast<void *>(1), 999, true),
          "wrong fence rejected");
    Check(!ledger.Ready(0, reinterpret_cast<void *>(2), 1000, true),
          "wrong queue rejected");
    Check(ledger.Ready(0, reinterpret_cast<void *>(1), 1000, true),
          "exact fence completes");
    Check(ledger.Reclaim(0), "completed entry reclaimed");
    Check(ledger.Arm(Key(77, 99)) >= 0, "reclaimed entry reusable");
}

void TestDelayedInvalidation() {
    TokenLedger ledger;
    const int slot = ledger.Arm(Key(88, 0));
    Check(slot >= 0 && ledger.Record(static_cast<unsigned int>(slot), 700),
          "delayed record");
    Check(ledger.Invalidate(700, false) == 1, "uncertain invalidation retained");
    Check(!ledger.Ready(static_cast<unsigned int>(slot), reinterpret_cast<void *>(3), 1, true),
          "unsubmitted invalidation cannot fabricate completion");
    Check(ledger.Submit(700, reinterpret_cast<void *>(3), 11),
          "delayed submission gets exact fence");
    Check(ledger.Ready(static_cast<unsigned int>(slot), reinterpret_cast<void *>(3), 11, true),
          "delayed submitted fence completes");
    Check(ledger.Reclaim(static_cast<unsigned int>(slot)), "delayed entry reclaimed");

    const int abandoned = ledger.Arm(Key(88, 1));
    Check(abandoned >= 0 && ledger.Record(static_cast<unsigned int>(abandoned), 701),
          "abandoned record");
    Check(ledger.Invalidate(701, true) == 1, "proven unsubmitted invalidation");
    Check(ledger.Reclaim(static_cast<unsigned int>(abandoned)),
          "proven unsubmitted entry reclaimed");
}
} // namespace

int main() {
    TestSixteenAndFence();
    TestDelayedInvalidation();
    std::puts("f8 gpu lifecycle harness: PASS");
    return 0;
}
