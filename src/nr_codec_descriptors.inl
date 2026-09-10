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
    const auto dual=slot.dual_selected?nr_dual::dx12::GetSurfaces(slot.dual_selected->dual_spatial):nr_dual::dx12::Surfaces{};
    srv(2,slot.dual_selected&&!slot.dual_reduced?dual.output:slot.neural,DXGI_FORMAT_R16G16B16A16_FLOAT);
    srv(10,slot.dual_reduced?dual.input:nullptr,DXGI_FORMAT_R16G16B16A16_FLOAT);
    srv(11,slot.dual_reduced?dual.output:nullptr,DXGI_FORMAT_R16G16B16A16_FLOAT);
    srv(12,slot.dual_reduced?dual.low_delta:nullptr,DXGI_FORMAT_R32G32B32A32_FLOAT);
    srv(3,slot.motion_source,slot.motion_source?ID3D12Resource_GetDesc(slot.motion_source).Format:DXGI_FORMAT_R16G16_FLOAT);
    srv(4,slot.exposure_source,slot.exposure_source?ID3D12Resource_GetDesc(slot.exposure_source).Format:DXGI_FORMAT_R32_FLOAT);
    const DXGI_FORMAT edit_field_format = slot.edit_field
        ? ID3D12Resource_GetDesc(slot.edit_field).Format
        : DXGI_FORMAT_R32G32_FLOAT;
    srv(kCodecEditFieldSrvIndex,slot.edit_field,edit_field_format);
    srv(kCodecEditRestoreSrvIndex,slot.edit_horizontal,DXGI_FORMAT_R16G16_FLOAT);
    srv(kCodecLanczosSrvIndex,slot.lanczos_intermediate,DXGI_FORMAT_R16G16B16A16_FLOAT);
    srv(kCodecFaceSrvIndex,slot.face_mask,DXGI_FORMAT_R16G16B16A16_FLOAT);
    srv(kCodecRegionSrvIndex,slot.region_field,DXGI_FORMAT_R32G32B32A32_FLOAT);
    uav(kCodecSrvCount,slot.proxy,DXGI_FORMAT_R16G16B16A16_FLOAT);
    uav(kCodecResultUavIndex,slot.result,slot.result_uav_format);
    uav(kCodecMotionUavIndex,slot.motion_proxy,DXGI_FORMAT_R16G16_FLOAT);
    uav(kCodecEditFieldUavIndex,slot.edit_field,edit_field_format);
    uav(kCodecEditRestoreUavIndex,slot.edit_horizontal,DXGI_FORMAT_R16G16_FLOAT);
    uav(kCodecLanczosUavIndex,slot.lanczos_intermediate,DXGI_FORMAT_R16G16B16A16_FLOAT);
    D3D12_UNORDERED_ACCESS_VIEW_DESC raw={};raw.Format=DXGI_FORMAT_R32_TYPELESS;
    raw.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;raw.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;
    raw.Buffer.NumElements=kDiagnosticStatsUintCount;
    ID3D12Device_CreateUnorderedAccessView(device,slot.delta_stats,nullptr,&raw,at(kCodecStatsUavIndex));
    raw.Buffer.NumElements=4;
    ID3D12Device_CreateUnorderedAccessView(device,nullptr,nullptr,&raw,at(kCodecCaptureUavIndex));
}

// Update only the optional plane after the slot's exact mutation gate passed.
void WriteEditFieldDescriptors(ID3D12Device *device, const NrSlot &slot) {
    auto cpu = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(slot.heap);
    auto read = cpu;
    read.ptr += static_cast<SIZE_T>(g_codec.descriptor_size) * kCodecEditFieldSrvIndex;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv={};
    srv.Format=ID3D12Resource_GetDesc(slot.edit_field).Format; srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Texture2D.MipLevels=1;
    ID3D12Device_CreateShaderResourceView(device,slot.edit_field,&srv,read);
    cpu.ptr += static_cast<SIZE_T>(g_codec.descriptor_size) * kCodecEditFieldUavIndex;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav={};
    uav.Format=srv.Format; uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
    ID3D12Device_CreateUnorderedAccessView(device,slot.edit_field,nullptr,&uav,cpu);
}

void WriteEditRestoreDescriptors(ID3D12Device *device, const NrSlot &slot) {
    auto cpu=ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(slot.heap);
    auto read=cpu; read.ptr+=static_cast<SIZE_T>(g_codec.descriptor_size)*kCodecEditRestoreSrvIndex;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv={}; srv.Format=DXGI_FORMAT_R16G16_FLOAT;
    srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Texture2D.MipLevels=1;
    ID3D12Device_CreateShaderResourceView(device,slot.edit_horizontal,&srv,read);
    cpu.ptr+=static_cast<SIZE_T>(g_codec.descriptor_size)*kCodecEditRestoreUavIndex;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav={}; uav.Format=DXGI_FORMAT_R16G16_FLOAT;
    uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
    ID3D12Device_CreateUnorderedAccessView(device,slot.edit_horizontal,nullptr,&uav,cpu);
}

// Update only the optional plane after the slot's exact mutation gate passed.
void WriteLanczosDescriptors(ID3D12Device *device, const NrSlot &slot) {
    auto cpu = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(slot.heap);
    auto read = cpu;
    read.ptr += static_cast<SIZE_T>(g_codec.descriptor_size) * kCodecLanczosSrvIndex;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv={};
    srv.Format=DXGI_FORMAT_R16G16B16A16_FLOAT; srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Texture2D.MipLevels=1;
    ID3D12Device_CreateShaderResourceView(device,slot.lanczos_intermediate,&srv,read);
    cpu.ptr += static_cast<SIZE_T>(g_codec.descriptor_size) * kCodecLanczosUavIndex;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav={};
    uav.Format=DXGI_FORMAT_R16G16B16A16_FLOAT; uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
    ID3D12Device_CreateUnorderedAccessView(device,slot.lanczos_intermediate,nullptr,&uav,cpu);
}

// Post-SR takes its input from the real game Output and writes only the owned
// FP16 intermediate. Rebuild from resource descriptions, never copy from the
// shader-visible main heap. Called only while the slot is safe to mutate.
void WritePostSrSnapshotDescriptors(ID3D12Device *device, const NrSlot &slot) {
    WriteCodecDescriptorSet(device, slot, slot.snapshot_heap);
    auto cpu = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(slot.snapshot_heap);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = slot.result_format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    ID3D12Device_CreateShaderResourceView(device, slot.result, &srv, cpu);
    cpu.ptr += static_cast<SIZE_T>(g_codec.descriptor_size) * kCodecResultUavIndex;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    ID3D12Device_CreateUnorderedAccessView(device, slot.source, nullptr, &uav, cpu);
}
