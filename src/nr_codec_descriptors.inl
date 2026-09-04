// Recreate descriptors from owned resources. A shader-visible heap is never a
// CPU copy source (D3D12 CopyDescriptorsSimple contract).
void WriteCodecDescriptorSet(ID3D12Device *device, const NrSlot &slot,
                             ID3D12DescriptorHeap *heap) {
    auto at=[&](UINT index) { auto h=ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
        h.ptr+=static_cast<SIZE_T>(g_codec.descriptor_size)*index;return h; };
    auto srv=[&](UINT i,ID3D12Resource *resource,DXGI_FORMAT format) {
        D3D12_SHADER_RESOURCE_VIEW_DESC d={};d.Format=format;d.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;d.Texture2D.MipLevels=1;
        ID3D12Device_CreateShaderResourceView(device,resource,&d,at(i)); };
    auto uav=[&](UINT i,ID3D12Resource *resource,DXGI_FORMAT format) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC d={};d.Format=format;d.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
        ID3D12Device_CreateUnorderedAccessView(device,resource,nullptr,&d,at(i)); };
    srv(0,slot.source,slot.source_format);
    srv(1,slot.proxy,DXGI_FORMAT_R16G16B16A16_FLOAT);
    srv(2,slot.neural,DXGI_FORMAT_R16G16B16A16_FLOAT);
    srv(3,slot.motion_source,slot.motion_source?ID3D12Resource_GetDesc(slot.motion_source).Format:DXGI_FORMAT_R16G16_FLOAT);
    srv(4,slot.exposure_source,slot.exposure_source?ID3D12Resource_GetDesc(slot.exposure_source).Format:DXGI_FORMAT_R32_FLOAT);
    uav(kCodecSrvCount,slot.proxy,DXGI_FORMAT_R16G16B16A16_FLOAT);
    uav(kCodecResultUavIndex,slot.result,slot.result_uav_format);
    uav(kCodecMotionUavIndex,slot.motion_proxy,DXGI_FORMAT_R16G16_FLOAT);
    D3D12_UNORDERED_ACCESS_VIEW_DESC raw={};raw.Format=DXGI_FORMAT_R32_TYPELESS;
    raw.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;raw.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;
    raw.Buffer.NumElements=kDiagnosticStatsUintCount;
    ID3D12Device_CreateUnorderedAccessView(device,slot.delta_stats,nullptr,&raw,at(kCodecStatsUavIndex));
    raw.Buffer.NumElements=4;
    ID3D12Device_CreateUnorderedAccessView(device,nullptr,nullptr,&raw,at(kCodecCaptureUavIndex));
}
