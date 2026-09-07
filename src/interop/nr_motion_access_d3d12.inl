// Included before codec descriptor reconstruction, C-interface D3D12.
// Only FLOAT interpretations are accepted for a float2 shader; no UINT fallback.
DXGI_FORMAT NrMotionReadFormat(DXGI_FORMAT format) {
    switch(static_cast<unsigned>(format)) {
    case 1: case 2: return static_cast<DXGI_FORMAT>(2);   // RGBA32
    case 9: case 10: return static_cast<DXGI_FORMAT>(10); // RGBA16
    case 15: case 16: return static_cast<DXGI_FORMAT>(16);// RG32
    case 33: case 34: return static_cast<DXGI_FORMAT>(34);// RG16
    default: return DXGI_FORMAT_UNKNOWN;
    }
}
bool NrCanReadMotion(ID3D12Device* device, const D3D12_RESOURCE_DESC& desc) {
    if(!device || desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
       desc.DepthOrArraySize!=1 || desc.MipLevels<1 || desc.SampleDesc.Count!=1 ||
       (desc.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)!=0) return false;
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
    support.Format=NrMotionReadFormat(desc.Format);
    if(support.Format==DXGI_FORMAT_UNKNOWN) return false;
    return SUCCEEDED(ID3D12Device_CheckFeatureSupport(device,D3D12_FEATURE_FORMAT_SUPPORT,
        &support,sizeof(support))) && (support.Support1&D3D12_FORMAT_SUPPORT1_TEXTURE2D)!=0 &&
        (support.Support1&D3D12_FORMAT_SUPPORT1_SHADER_LOAD)!=0;
}
