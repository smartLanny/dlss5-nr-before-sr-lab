#include "../src/nr_colour_lab_policy.h"
#include <cassert>
#include <limits>
using namespace nr_colour_lab;
static_assert(kDefaultMode == 1);
static_assert(!AllowUnverified(0, false));
static_assert(AllowUnverified(0, true));
static_assert(AllowUnverified(1, false));
static_assert(AllowUnverified(1, true));
static_assert(!AllowUnverified(2, false));
static_assert(!AllowUnverified(2, true));
static_assert(!AllowUnverified(-1, true));
static_assert(!AllowUnverified(3, true));
static_assert(!AllowUnverified(std::numeric_limits<int>::max(), true));
int main() {
    for (int i = -100; i <= 100; ++i) {
        assert(AllowUnverified(i, false) == (i == 1));
        assert(AllowUnverified(i, true) == (i == 0 || i == 1));
    }
}
