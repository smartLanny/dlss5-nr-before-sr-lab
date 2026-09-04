#include <cstdio>
#include <cstdlib>

#include "../src/nr_f8_raw_ring.inl"

namespace {
using nr_f8_raw_ring::Identity;
using nr_f8_raw_ring::Ring;

void Check(bool condition, const char *what) {
    if (!condition) {
        std::fprintf(stderr, "f8 raw ring check failed: %s\n", what);
        std::exit(1);
    }
}

Identity Id(unsigned long long frame) {
    Identity id = {};
    id.frame = frame;
    id.instance = 7;
    id.generation = 3;
    id.width = 1920;
    id.height = 1080;
    return id;
}

void TestSixteenImmutableSlots() {
    Ring ring;
    for (unsigned int slot = 0; slot < nr_f8_raw_ring::kCapacity; ++slot)
        Check(ring.Reserve(41, slot, Id(100 + slot)), "reserve all 16 slots");
    Check(ring.ReservedCount() == 16, "all reservations are visible");
    Check(!ring.Reserve(41, 0, Id(999)), "in-flight slot cannot be overwritten");
    for (unsigned int slot = 0; slot < nr_f8_raw_ring::kCapacity; ++slot)
        Check(ring.Submit(41, slot, 1000 + slot), "submit immutable fence");
    Check(!ring.Complete(41, 0, 999, Id(100)), "wrong fence rejected");
    Check(!ring.Complete(41, 0, 1000, Id(101)), "wrong identity rejected");
    Check(ring.Complete(41, 0, 1000, Id(100)), "exact completion accepted");
    Check(!ring.Complete(41, 0, 1000, Id(100)), "duplicate completion rejected");
    Check(!ring.Reclaim(41, 1, 1001), "uncompleted slot cannot reclaim");
    Check(ring.Reclaim(41, 0, 1000), "completed slot reclaimed");
    Check(ring.Reserve(41, 0, Id(200)), "reclaimed slot can be reused");
    Check(ring.Get(0).identity.frame == 200, "reused slot has new immutable identity");
    Check(!ring.Complete(41, 0, 1000, Id(100)), "old completion cannot hit reused slot");
}

void TestRequestIdentity() {
    Ring ring;
    Check(ring.Reserve(9, 0, Id(1)), "request nine reserve");
    Check(!ring.Submit(8, 0, 2), "old request cannot submit");
    Check(ring.Submit(9, 0, 2), "current request submits");
    Check(!ring.Complete(8, 0, 2, Id(1)), "old request cannot complete");
    Check(ring.Complete(9, 0, 2, Id(1)), "current request completes");
}
} // namespace

int main() {
    TestSixteenImmutableSlots();
    TestRequestIdentity();
    std::puts("f8 raw ring harness: PASS");
    return 0;
}
