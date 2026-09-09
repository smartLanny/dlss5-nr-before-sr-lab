// Native D3D12 observation shares the existing serialized MinHook publisher.
// These two requests have their own bounded batch; they do not consume the
// command-list batch or alter a descriptor supplied by the caller.
CommandStateHookFamily g_nr_format_srv_hooks, g_nr_format_uav_hooks;
template <unsigned I> void STDMETHODCALLTYPE NrFormatCreateSrv(ID3D12Device* device,
        ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* desc,
        D3D12_CPU_DESCRIPTOR_HANDLE destination) {
    using Fn = decltype(ID3D12DeviceVtbl::CreateShaderResourceView);
    reinterpret_cast<Fn>(g_nr_format_srv_hooks.originals[I])(device, resource, desc, destination);
    auto* scope = g_nr_native_format_scope;
    if (!scope || scope->device != device || resource != scope->resources[0] || !resource) return;
    const bool valid = desc && desc->ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2D &&
        desc->Shader4ComponentMapping == D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING &&
        desc->Texture2D.MostDetailedMip == 0 &&
        (desc->Texture2D.MipLevels == 1 || desc->Texture2D.MipLevels == UINT_MAX) &&
        desc->Texture2D.PlaneSlice == 0 && desc->Texture2D.ResourceMinLODClamp == 0;
    scope->views[0].Observe(desc ? static_cast<unsigned>(desc->Format) : 0, valid);
}
template <unsigned I> void STDMETHODCALLTYPE NrFormatCreateUav(ID3D12Device* device,
        ID3D12Resource* resource, ID3D12Resource* counter,
        const D3D12_UNORDERED_ACCESS_VIEW_DESC* desc, D3D12_CPU_DESCRIPTOR_HANDLE destination) {
    using Fn = decltype(ID3D12DeviceVtbl::CreateUnorderedAccessView);
    reinterpret_cast<Fn>(g_nr_format_uav_hooks.originals[I])(device, resource, counter, desc, destination);
    auto* scope = g_nr_native_format_scope;
    if (!scope || scope->device != device || resource != scope->resources[1] || !resource) return;
    const bool valid = !counter && desc && desc->ViewDimension == D3D12_UAV_DIMENSION_TEXTURE2D &&
        desc->Texture2D.MipSlice == 0 && desc->Texture2D.PlaneSlice == 0;
    scope->views[1].Observe(desc ? static_cast<unsigned>(desc->Format) : 0, valid);
}
void* const g_nr_format_srv_detours[4] = {
    reinterpret_cast<void*>(&NrFormatCreateSrv<0>), reinterpret_cast<void*>(&NrFormatCreateSrv<1>),
    reinterpret_cast<void*>(&NrFormatCreateSrv<2>), reinterpret_cast<void*>(&NrFormatCreateSrv<3>)};
void* const g_nr_format_uav_detours[4] = {
    reinterpret_cast<void*>(&NrFormatCreateUav<0>), reinterpret_cast<void*>(&NrFormatCreateUav<1>),
    reinterpret_cast<void*>(&NrFormatCreateUav<2>), reinterpret_cast<void*>(&NrFormatCreateUav<3>)};
bool EnsureNrNativeFormatHooks(ID3D12Device* device) {
    if (!device) return false;
    auto receipt = NrFormatGetDevice(device);
    if (receipt.identity) return receipt.hooks; // A failed publication stays unknown for this device.
    receipt.identity = g_nr_format_next_identity.fetch_add(1, std::memory_order_relaxed);
    CommandStateHookRequest requests[] = {
        {&g_nr_format_srv_hooks, reinterpret_cast<void*>(device->lpVtbl->CreateShaderResourceView), g_nr_format_srv_detours},
        {&g_nr_format_uav_hooks, reinterpret_cast<void*>(device->lpVtbl->CreateUnorderedAccessView), g_nr_format_uav_detours}};
    receipt.hooks = PublishCommandStateHooks(requests, 2);
    if (FAILED(ID3D12Device_SetPrivateData(device, kNrFormatDevice, sizeof(receipt), &receipt))) return false;
    return receipt.hooks;
}
void OnNrFormatInitDevice(reshade::api::device* device) {
    if (device && device->get_api() == reshade::api::device_api::d3d12)
        EnsureNrNativeFormatHooks(reinterpret_cast<ID3D12Device*>(device->get_native()));
}
