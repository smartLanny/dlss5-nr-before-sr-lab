// C-interface D3D12 adapter, included inside the core's private namespace.
// The portable header is included OUTSIDE that namespace by the generator.
DXGI_FORMAT NrSceneReadFormat(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R32G32B32A32_TYPELESS: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default: return format;
    }
}
bool NrIsFloatSceneStorage(DXGI_FORMAT format) {
    return nr_format_access::IsFloatColor(static_cast<unsigned>(NrSceneReadFormat(format)));
}
DXGI_FORMAT NrResolveSceneSrv(ID3D12Device* device, const D3D12_RESOURCE_DESC& desc) {
    if(!device || desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
       desc.DepthOrArraySize!=1 || desc.MipLevels!=1 || desc.SampleDesc.Count!=1)
        return DXGI_FORMAT_UNKNOWN;
    nr_format_access::Request r;
    r.resource=static_cast<unsigned>(desc.Format);
    r.permit_float_interpretation=true;
    r.deny_shader_resource=(desc.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)!=0;
    const auto plan=nr_format_access::Resolve(r,[device](unsigned format,nr_format_access::Usage usage) {
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
        support.Format=static_cast<DXGI_FORMAT>(format);
        const HRESULT hr=ID3D12Device_CheckFeatureSupport(device,D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support));
        if(FAILED(hr)) return false;
        if(usage==nr_format_access::Usage::ShaderLoad)
            return (support.Support1&D3D12_FORMAT_SUPPORT1_TEXTURE2D)!=0 &&
                   (support.Support1&D3D12_FORMAT_SUPPORT1_SHADER_LOAD)!=0;
        return (support.Support1&D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW)!=0 &&
               (support.Support2&D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE)!=0;
    });
    return plan?static_cast<DXGI_FORMAT>(plan.srv):DXGI_FORMAT_UNKNOWN;
}
