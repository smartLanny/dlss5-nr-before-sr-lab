// SRV/UAV creation uses the existing format hooks. The remaining writers share
// their publisher and forward the original API once with unchanged arguments.
CommandStateHookFamily g_descriptor_heap_hooks,g_descriptor_rtv_hooks,g_descriptor_cbv_hooks,
    g_descriptor_copy_hooks,g_descriptor_copy_simple_hooks;
template<unsigned I> HRESULT STDMETHODCALLTYPE NrDescriptorCreateHeap(ID3D12Device *device,
        const D3D12_DESCRIPTOR_HEAP_DESC *desc,REFIID iid,void **output) {
    using Fn=decltype(ID3D12DeviceVtbl::CreateDescriptorHeap);
    const auto result=reinterpret_cast<Fn>(g_descriptor_heap_hooks.originals[I])(device,desc,iid,output);
    if(SUCCEEDED(result) && output && *output) {
        ID3D12DescriptorHeap *heap=nullptr;
        if(SUCCEEDED(IUnknown_QueryInterface(static_cast<IUnknown *>(*output),IID_ID3D12DescriptorHeap,
                reinterpret_cast<void **>(&heap))) && heap) {
            NrDescriptorTrackHeap(device,heap);ID3D12DescriptorHeap_Release(heap);
        }
    }
    return result;
}
template<unsigned I> void STDMETHODCALLTYPE NrDescriptorCreateRtv(ID3D12Device *device,
        ID3D12Resource *resource,const D3D12_RENDER_TARGET_VIEW_DESC *desc,D3D12_CPU_DESCRIPTOR_HANDLE destination) {
    const auto write=NrDescriptorBeginRtv(device,resource,desc,destination);
    using Fn=decltype(ID3D12DeviceVtbl::CreateRenderTargetView);
    reinterpret_cast<Fn>(g_descriptor_rtv_hooks.originals[I])(device,resource,desc,destination);
    g_nr_descriptors.Finish(write);
}
template<unsigned I> void STDMETHODCALLTYPE NrDescriptorCreateCbv(ID3D12Device *device,
        const D3D12_CONSTANT_BUFFER_VIEW_DESC *desc,D3D12_CPU_DESCRIPTOR_HANDLE destination) {
    const auto write=NrDescriptorObserveDeviceLayer(device)?
        g_nr_descriptors.BeginWrite(destination.ptr,0,{}):nr_descriptor::Write{};
    using Fn=decltype(ID3D12DeviceVtbl::CreateConstantBufferView);
    reinterpret_cast<Fn>(g_descriptor_cbv_hooks.originals[I])(device,desc,destination);
    g_nr_descriptors.Finish(write);
}
template<unsigned I> void STDMETHODCALLTYPE NrDescriptorCopyDescriptors(ID3D12Device *device,UINT destinations,
        const D3D12_CPU_DESCRIPTOR_HANDLE *destination,const UINT *destination_sizes,UINT sources,
        const D3D12_CPU_DESCRIPTOR_HANDLE *source,const UINT *source_sizes,D3D12_DESCRIPTOR_HEAP_TYPE type) {
    NrDescriptorCopy copy;
    NrDescriptorBeginCopy(copy,device,destinations,destination,destination_sizes,sources,source,source_sizes,type);
    using Fn=decltype(ID3D12DeviceVtbl::CopyDescriptors);
    reinterpret_cast<Fn>(g_descriptor_copy_hooks.originals[I])(device,destinations,destination,destination_sizes,
        sources,source,source_sizes,type);
}
template<unsigned I> void STDMETHODCALLTYPE NrDescriptorCopyDescriptorsSimple(ID3D12Device *device,UINT count,
        D3D12_CPU_DESCRIPTOR_HANDLE destination,D3D12_CPU_DESCRIPTOR_HANDLE source,D3D12_DESCRIPTOR_HEAP_TYPE type) {
    NrDescriptorCopy copy;
    NrDescriptorBeginCopy(copy,device,1,&destination,&count,1,&source,&count,type);
    using Fn=decltype(ID3D12DeviceVtbl::CopyDescriptorsSimple);
    reinterpret_cast<Fn>(g_descriptor_copy_simple_hooks.originals[I])(device,count,destination,source,type);
}
#define NR_DESCRIPTOR_DETOURS(NAME) void *const g_descriptor_detours_##NAME[4]={ \
    reinterpret_cast<void *>(&NrDescriptor##NAME<0>),reinterpret_cast<void *>(&NrDescriptor##NAME<1>), \
    reinterpret_cast<void *>(&NrDescriptor##NAME<2>),reinterpret_cast<void *>(&NrDescriptor##NAME<3>)}
NR_DESCRIPTOR_DETOURS(CreateHeap);
NR_DESCRIPTOR_DETOURS(CreateRtv);
NR_DESCRIPTOR_DETOURS(CreateCbv);
NR_DESCRIPTOR_DETOURS(CopyDescriptors);
NR_DESCRIPTOR_DETOURS(CopyDescriptorsSimple);
#undef NR_DESCRIPTOR_DETOURS
std::mutex g_nr_descriptor_publication_mutex;
bool EnsureNrDescriptorHooks(ID3D12Device *device,bool canonical_native) {
    if(!device)return false;
    std::lock_guard guard(g_nr_descriptor_publication_mutex);
    const auto existing=NrDescriptorDeviceCoverage(device);
    if(existing.device && existing.native_address)
        return existing.ready && (!canonical_native || existing.native_address==reinterpret_cast<nr_descriptor::Id>(device));
    // A proxy's shared ready bit is not proof that its vtable sees raw handles.
    // The init-device callback identifies the actual native observation layer.
    if(!canonical_native)return false;
    const auto format=NrFormatGetDevice(device);
    NrDescriptorDeviceStatus status;status.device=format.identity;
    status.native_address=reinterpret_cast<nr_descriptor::Id>(device);
    if(!format.identity || !format.hooks)return false;
    if(!NrDescriptorObjectIdentity(reinterpret_cast<ID3D12Object *>(device),NrDescriptorObject::Device,format.identity))return false;
    CommandStateHookRequest requests[]={
        {&g_descriptor_heap_hooks,reinterpret_cast<void *>(device->lpVtbl->CreateDescriptorHeap),g_descriptor_detours_CreateHeap},
        {&g_descriptor_rtv_hooks,reinterpret_cast<void *>(device->lpVtbl->CreateRenderTargetView),g_descriptor_detours_CreateRtv},
        {&g_descriptor_cbv_hooks,reinterpret_cast<void *>(device->lpVtbl->CreateConstantBufferView),g_descriptor_detours_CreateCbv},
        {&g_descriptor_copy_hooks,reinterpret_cast<void *>(device->lpVtbl->CopyDescriptors),g_descriptor_detours_CopyDescriptors},
        {&g_descriptor_copy_simple_hooks,reinterpret_cast<void *>(device->lpVtbl->CopyDescriptorsSimple),g_descriptor_detours_CopyDescriptorsSimple}};
    status.ready=PublishCommandStateHooks(requests,static_cast<unsigned>(std::size(requests)),
        "optional descriptor provenance incomplete; format admission unchanged");
    if(FAILED(ID3D12Device_SetPrivateData(device,kNrDescriptorDeviceHooks,sizeof(status),&status)))return false;
    return status.ready;
}
