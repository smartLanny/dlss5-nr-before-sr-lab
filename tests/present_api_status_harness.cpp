#include <cstdio>

namespace reshade::api {
enum class device_api { d3d11, d3d12, vulkan };
}

#include "../src/nr_present_api_status.h"

struct FakeDevice {
    reshade::api::device_api api = reshade::api::device_api::d3d12;
    reshade::api::device_api get_api() const noexcept { return api; }
};

struct FakeRuntime {
    FakeDevice *device = nullptr;
    FakeDevice *get_device() noexcept { return device; }
};

int main() {
    unsigned failures = 0;
    const auto check = [&failures](bool condition, const char *label) {
        if (!condition) {
            ++failures;
            std::printf("FAIL: %s\n", label);
        }
    };

    check(!nr_present_api_status::IsD3d11PresentRuntime<FakeRuntime>(nullptr),
          "null runtime remains unknown");
    FakeRuntime no_device{};
    check(!nr_present_api_status::IsD3d11PresentRuntime(&no_device),
          "runtime without a device remains unknown");

    FakeDevice d3d11{reshade::api::device_api::d3d11};
    FakeDevice d3d12{reshade::api::device_api::d3d12};
    FakeDevice vulkan{reshade::api::device_api::vulkan};
    FakeRuntime d3d11_runtime{&d3d11};
    FakeRuntime d3d12_runtime{&d3d12};
    FakeRuntime vulkan_runtime{&vulkan};
    check(nr_present_api_status::IsD3d11PresentRuntime(&d3d11_runtime),
          "presenting D3D11 runtime is unsupported");
    check(!nr_present_api_status::IsD3d11PresentRuntime(&d3d12_runtime),
          "presenting D3D12 runtime is supported");
    check(!nr_present_api_status::IsD3d11PresentRuntime(&vulkan_runtime),
          "other API is not mislabeled as D3D11");

    // An auxiliary D3D11 runtime must not poison a later/current D3D12 query.
    (void)nr_present_api_status::IsD3d11PresentRuntime(&d3d11_runtime);
    check(!nr_present_api_status::IsD3d11PresentRuntime(&d3d12_runtime),
          "D3D11 observation does not latch across runtimes");

    std::printf("present API status: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
