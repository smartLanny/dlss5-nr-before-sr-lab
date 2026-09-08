#include "parameter_plan.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
using namespace nr::nvfp4;
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) throw std::runtime_error(#x); } while (0)
int main() {
    ParameterPlan p;
    const std::vector<std::uint8_t> bytes(64, 0xA5);
    const std::vector<Patch> patches{{8, BufferId::weights, 16, 32, 16}, {24, BufferId::input, 0, 64, 16}};
    CHECK(ParameterPlan::Build(bytes, patches, p) == Error::none);
    Bindings b{};
    b[0] = {0x1000, 48}; b[1] = {0x2000, 64};
    std::vector<std::uint8_t> output(p.size(), 0xCC);
    CHECK(p.Bind(b, output.data(), output.size()) == Error::none);
    CHECK(output[8] == 0x10 && output[9] == 0x10 && output[24] == 0 && output[25] == 0x20);
    CHECK(output[7] == 0xA5 && output[16] == 0xA5 && output[63] == 0xA5);
    auto good = output;
    b[0].bytes = 47;
    CHECK(p.Bind(b, output.data(), output.size()) == Error::range);
    CHECK(output == good);
    b[0] = {0x1001, 48}; CHECK(p.Bind(b, output.data(), output.size()) == Error::alignment);
    b[0] = {UINT64_MAX - 15, 64}; CHECK(p.Bind(b, output.data(), output.size()) == Error::address_overflow);
    b[0] = {0x1000, 48}; b[1] = {};
    CHECK(p.Bind(b, output.data(), output.size()) == Error::buffer_missing);
    CHECK(output == good);
    b[1] = {0x2000, 64};
    CHECK(p.Bind(b, output.data(), output.size()-1) == Error::output_size);
    auto bad = patches; bad[1].offset = 12;
    CHECK(ParameterPlan::Build(bytes, bad, p) == Error::overlap);
    bad = patches; bad[1].required_bytes = 0;
    CHECK(ParameterPlan::Build(bytes, bad, p) == Error::invalid_patch);
    bad = patches; bad[1].alignment = 3;
    CHECK(ParameterPlan::Build(bytes, bad, p) == Error::invalid_patch);
    bad = patches; bad[1].offset = 60;
    CHECK(ParameterPlan::Build(bytes, bad, p) == Error::invalid_patch);
    CHECK(p.Bind(b, output.data(), output.size()) == Error::none); // Failed Build is atomic.
    auto* address = output.data();
    for (unsigned i=0; i<10000; ++i) {
        CHECK(p.Bind(b, output.data(), output.size()) == Error::none);
        CHECK(address == output.data() && output == good);
    }
    SegmentTransaction a;
    CHECK(!a.Prepare(true, true, false)); CHECK(a.original_is_legal());
    CHECK(!a.BeginRecording());
    SegmentTransaction c;
    CHECK(c.Prepare(true, true, true)); CHECK(c.BeginRecording());
    c.Fail(); CHECK(c.state() == SegmentTransaction::State::quarantined);
    CHECK(!c.original_is_legal()); CHECK(!c.Retire(1, UINT64_MAX, true));
    SegmentTransaction d;
    CHECK(d.Prepare(true, true, true)); CHECK(d.BeginRecording()); CHECK(d.Submitted(2, 7));
    CHECK(!d.Retire(1, 100, true)); CHECK(!d.Retire(2, 6, true)); CHECK(!d.Retire(2, 7, false));
    CHECK(d.Retire(2, 7, true));
    std::cout << "PASS " << checks << " host checks; no GPU was used\n";
}
