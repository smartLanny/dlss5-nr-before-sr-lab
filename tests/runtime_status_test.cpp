#include "../src/nr_runtime_status.h"
#include <cassert>
int main() {
    using namespace nr_runtime_status;
    Sample a{1, 10, 10, 8, 3, true, false, true};
    Sample b{1, 15, 15, 12, 3, true, false, true};
    assert(Active(a, b));
    assert(!Active({}, b)); // Old cumulative totals cannot prove this interval.
    assert(!Active(a, a)); // Idle/stalled chain.
    auto bad = b; bad.epoch++; assert(!Active(a, bad));
    bad = b; bad.config++; assert(!Active(a, bad));
    bad = b; bad.enabled = false; assert(!Active(a, bad));
    bad = b; bad.bypassed = true; assert(!Active(a, bad));
    bad = b; bad.coherent = false; assert(!Active(a, bad));
    bad = b; bad.nr_evaluations = a.nr_evaluations; assert(!Active(a, bad));
    bad = b; bad.transfers = a.transfers; assert(!Active(a, bad));
}
