// CPU-only check against the pinned, externally supplied official Luma header.
// No game shader/resource/device is created or represented as observed evidence.
#include <super_resolution.h>
#include "../../scripts/luma-ue-input-receipt.h"
#include <cassert>
#include <cstring>
static unsigned observed = 0;
static void Sink(const char* text) {
    ++observed;
    assert(std::strstr(text, "NR_UE_INPUT_RECEIPT v=1"));
    assert(std::strstr(text, "evaluated=0"));
    assert(std::strstr(text, "jitter=0.25,-0.25"));
    assert(!std::strstr(text, "0x1234"));
}
int main() {
    SR::SuperResolutionImpl::DrawData draw{};
    SR::SettingsData settings{};
    draw.render_width = settings.output_width = 1920;
    draw.render_height = settings.output_height = 1080;
    draw.jitter_x = 0.25f; draw.jitter_y = -0.25f;
    nr_luma_receipt::Site site{0, false, 1, 2, 3, 0, 1024, 0, 4, 8,
        false, false, reinterpret_cast<void*>(0x1234)};
    for (int i = 0; i < 5000; ++i)
        nr_luma_receipt::Observe(draw, settings, site, &Sink);
    assert(observed == 32);
    assert(draw.jitter_x == 0.25f && draw.output_color == nullptr);
}
