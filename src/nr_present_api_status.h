#pragma once

namespace nr_present_api_status {

// ReShade associates the overlay callback's effect runtime with its presenting
// swapchain. Keep this query stateless: a D3D12 game may also create auxiliary
// D3D11 devices, which must not latch the whole process as unsupported.
template <typename Runtime>
bool IsD3d11PresentRuntime(Runtime *runtime) noexcept {
    if (runtime == nullptr) return false;
    const auto *device = runtime->get_device();
    return device != nullptr &&
           device->get_api() == reshade::api::device_api::d3d11;
}

}  // namespace nr_present_api_status
