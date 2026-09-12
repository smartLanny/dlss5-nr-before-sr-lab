namespace nr_dual_runtime {
struct CodecSet {ID3D12PipelineState* pipelines[6]={};};
CodecSet codecs[2]{};
const char* const codec_entries[]={"CombineCS","CombineV2CS","BuildEditFieldCS","BuildColorEditFieldCS","CombineCS","CombineV2CS"};
bool EnsureCodecSet(unsigned domain,bool face) {
    if(domain>1||!g_codec.device||!g_codec.root)return false;
    auto& set=codecs[domain];
    for(unsigned i=0;i<(face?6u:4u);++i) {
        if(set.pipelines[i])continue;
        auto* shader=CompileCodecShader(codec_entries[i],i>=4,static_cast<int>(domain));
        if(!shader)return false;
        D3D12_COMPUTE_PIPELINE_STATE_DESC d{};d.pRootSignature=g_codec.root;
        d.CS={ID3D10Blob_GetBufferPointer(shader),ID3D10Blob_GetBufferSize(shader)};
        const auto hr=ID3D12Device_CreateComputePipelineState(g_codec.device,&d,IID_ID3D12PipelineState,reinterpret_cast<void**>(&set.pipelines[i]));
        ReleaseCom(shader);if(FAILED(hr))return false;
    }
    return true;
}
ID3D12PipelineState* CodecPipeline(ID3D12PipelineState* original,bool face,unsigned domain) {
    if(domain>1)return original;
    auto& set=codecs[domain];unsigned index=6;
    if(original==g_codec.combine||original==g_codec.combine_face)index=face?4:0;
    if(original==g_codec.combine_v2||original==g_codec.combine_v2_face)index=face?5:1;
    if(original==g_codec.build_edit_field)index=2;
    if(original==g_codec.build_color_edit_field)index=3;
    // Other kernels consume the existing edit field, not the Neural texture.
    return index<6?set.pipelines[index]:original;
}
}
