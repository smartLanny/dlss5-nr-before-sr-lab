// Inside nr_fg_runtime, after Session/Draw. Each table owns immutable resource
// identities; per-frame native alpha is copied to the same ordered scratch.
bool BuildHud(Session& s,const NativeInputs& original){
    if(!original.hudless)return true;
    auto& c=s.images;const auto alpha_format=original.ui_alpha?ID3D12Resource_GetDesc(original.ui_alpha).Format:DXGI_FORMAT_UNKNOWN;
    if(s.hud_root)return s.alpha_format==alpha_format;
    if(!io::Texture(c,c.source,c.format,D3D12_RESOURCE_FLAG_NONE,&s.full_raw)||
       !io::Texture(c,c.source,c.format,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,&s.scene_encoded))return false;
    if(original.ui_alpha&&!io::Texture(c,c.source,alpha_format,D3D12_RESOURCE_FLAG_NONE,&s.alpha_raw))return false;
    s.alpha_format=alpha_format;
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,5,0,0,0};
    D3D12_ROOT_PARAMETER parameters[2]{};parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].DescriptorTable={1,&range};parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameters[1].Constants={0,0,4};
    D3D12_ROOT_SIGNATURE_DESC root{};root.NumParameters=2;root.pParameters=parameters;
    ID3DBlob *serialized=nullptr,*error=nullptr;
    auto result=D3D12SerializeRootSignature(&root,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&error);ReleaseCom(error);
    if(SUCCEEDED(result))result=ID3D12Device_CreateRootSignature(c.device,0,ID3D10Blob_GetBufferPointer(serialized),ID3D10Blob_GetBufferSize(serialized),IID_ID3D12RootSignature,reinterpret_cast<void**>(&s.hud_root));
    ReleaseCom(serialized);if(FAILED(result))return false;
    const std::string source=std::string(kCompatPresentShader)+kFgHudShader;
    ID3DBlob *pixel=nullptr,*vertex=io::CompileUtility("FullscreenVS","vs_5_0");
    result=D3DCompile(source.data(),source.size(),"own-fg-hud",nullptr,nullptr,"HudPS","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&pixel,&error);
    if(FAILED(result)&&error)Log("FG HUD shader: %s",static_cast<const char*>(ID3D10Blob_GetBufferPointer(error)));
    ReleaseCom(error);if(FAILED(result)||!vertex){ReleaseCom(pixel);ReleaseCom(vertex);return false;}
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};desc.pRootSignature=s.hud_root;
    desc.VS={ID3D10Blob_GetBufferPointer(vertex),ID3D10Blob_GetBufferSize(vertex)};desc.PS={ID3D10Blob_GetBufferPointer(pixel),ID3D10Blob_GetBufferSize(pixel)};
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;desc.SampleMask=UINT_MAX;
    desc.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;desc.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;desc.RasterizerState.DepthClipEnable=TRUE;
    desc.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;desc.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;desc.NumRenderTargets=1;desc.RTVFormats[0]=c.format;desc.SampleDesc.Count=1;
    result=ID3D12Device_CreateGraphicsPipelineState(c.device,&desc,IID_ID3D12PipelineState,reinterpret_cast<void**>(&s.hud_pso));ReleaseCom(pixel);ReleaseCom(vertex);if(FAILED(result))return false;
    D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;heap.NumDescriptors=5;heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if(FAILED(ID3D12Device_CreateDescriptorHeap(c.device,&heap,IID_ID3D12DescriptorHeap,reinterpret_cast<void**>(&s.hud_heap))))return false;
    ID3D12Resource* textures[]={c.raw,c.image.source,c.image.result,s.full_raw,s.alpha_raw};
    const DXGI_FORMAT formats[]={c.format,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R16G16B16A16_FLOAT,c.format,original.ui_alpha?alpha_format:DXGI_FORMAT_R8_UNORM};
    auto cpu=ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(s.hud_heap);const auto stride=ID3D12Device_GetDescriptorHandleIncrementSize(c.device,heap.Type);
    for(unsigned i=0;i<5;++i){D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Format=formats[i];view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;view.Texture2D.MipLevels=1;view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        ID3D12Device_CreateShaderResourceView(c.device,textures[i],&view,cpu);cpu.ptr+=stride;}
    return true;
}
void CaptureHud(Session& s,ID3D12GraphicsCommandList* list,const NativeInputs& input){
    if(!input.hudless)return;
    auto state=static_cast<D3D12_RESOURCE_STATES>(input.color_state);Transition(list,input.color,state,D3D12_RESOURCE_STATE_COPY_SOURCE);
    Transition(list,s.full_raw,s.full_state,D3D12_RESOURCE_STATE_COPY_DEST);ID3D12GraphicsCommandList_CopyResource(list,s.full_raw,input.color);
    Transition(list,input.color,state,static_cast<D3D12_RESOURCE_STATES>(input.color_state));Transition(list,s.full_raw,s.full_state,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    if(input.ui_alpha){state=static_cast<D3D12_RESOURCE_STATES>(input.ui_alpha_state);Transition(list,input.ui_alpha,state,D3D12_RESOURCE_STATE_COPY_SOURCE);
        Transition(list,s.alpha_raw,s.alpha_state,D3D12_RESOURCE_STATE_COPY_DEST);ID3D12GraphicsCommandList_CopyResource(list,s.alpha_raw,input.ui_alpha);
        Transition(list,input.ui_alpha,state,static_cast<D3D12_RESOURCE_STATES>(input.ui_alpha_state));Transition(list,s.alpha_raw,s.alpha_state,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);}
}
void DrawHud(Session& s,ID3D12GraphicsCommandList* list,const NativeInputs& input,ID3D12DescriptorHeap* rtv){
    auto& c=s.images;auto state=static_cast<D3D12_RESOURCE_STATES>(input.color_state);Transition(list,input.color,state,D3D12_RESOURCE_STATE_RENDER_TARGET);
    ID3D12DescriptorHeap* heaps[]={s.hud_heap};ID3D12GraphicsCommandList_SetDescriptorHeaps(list,1,heaps);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(list,s.hud_root);ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(list,0,ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(s.hud_heap));
    const unsigned constants[]={c.source.width,c.source.height,static_cast<unsigned>(c.encoding),input.ui_alpha?1u:0u};
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(list,1,4,constants,0);ID3D12GraphicsCommandList_SetPipelineState(list,s.hud_pso);
    const auto target=ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv);ID3D12GraphicsCommandList_OMSetRenderTargets(list,1,&target,FALSE,nullptr);
    const D3D12_VIEWPORT viewport{0,0,float(c.source.width),float(c.source.height),0,1};const D3D12_RECT rect{0,0,LONG(c.source.width),LONG(c.source.height)};
    ID3D12GraphicsCommandList_RSSetViewports(list,1,&viewport);ID3D12GraphicsCommandList_RSSetScissorRects(list,1,&rect);ID3D12GraphicsCommandList_IASetPrimitiveTopology(list,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);ID3D12GraphicsCommandList_DrawInstanced(list,3,1,0,0);
    Transition(list,input.color,state,static_cast<D3D12_RESOURCE_STATES>(input.color_state));
}
