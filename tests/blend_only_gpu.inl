// Included after Gpu. Synthetic state policy + real production restore + actual WARP pixels.
// Native-hook observation is covered separately by pristine_null_native.inl.
struct BlendOnlyPixel { float channels[4]; };
BlendOnlyPixel RunBlendOnlyCase(Gpu &gpu, bool null_default, bool inject,
                                 const float *override_values = nullptr) {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    const char shader[] = R"(
float4 VS(uint id : SV_VertexID) : SV_Position {
    float2 p = id == 0 ? float2(-1,-1) : id == 1 ? float2(3,-1) : float2(-1,3);
    return float4(p,0,1);
}
float4 PS() : SV_Target { return float4(1,1,1,1); }
)";
    auto root = gpu.Root(nullptr,0);
    auto vs = Compile(shader,"VS","vs_5_0"), ps = Compile(shader,"PS","ps_5_0");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
    description.pRootSignature=root.p;
    description.VS={vs->GetBufferPointer(),vs->GetBufferSize()};
    description.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    description.SampleMask=UINT_MAX;
    description.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;
    description.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
    description.RasterizerState.DepthClipEnable=TRUE;
    description.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    description.NumRenderTargets=1;description.RTVFormats[0]=DXGI_FORMAT_R32G32B32A32_FLOAT;
    description.SampleDesc.Count=1;
    auto &blend=description.BlendState.RenderTarget[0];
    blend.BlendEnable=TRUE;blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    blend.SrcBlend=D3D12_BLEND_BLEND_FACTOR;blend.DestBlend=D3D12_BLEND_ZERO;blend.BlendOp=D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha=D3D12_BLEND_BLEND_FACTOR;blend.DestBlendAlpha=D3D12_BLEND_ZERO;blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;
    Com<ID3D12PipelineState> pipeline;
    Check(gpu.device->CreateGraphicsPipelineState(&description,IID_ID3D12PipelineState,pipeline.address()),"Create blend-only graphics pipeline");
    D3D12_RESOURCE_DESC texture{};
    texture.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;texture.Width=texture.Height=1;
    texture.DepthOrArraySize=texture.MipLevels=texture.SampleDesc.Count=1;
    texture.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;texture.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    auto target=gpu.Resource(texture,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_RENDER_TARGET);
    auto heap=gpu.Heap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV,1,false);
    auto rtv=heap->GetCPUDescriptorHandleForHeapStart();
    gpu.device->CreateRenderTargetView(target.p,nullptr,rtv);
    const float zero[4]={0,0,0,0};
    gpu.list->ClearRenderTargetView(rtv,zero,0,nullptr);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 size=0;
    gpu.device->GetCopyableFootprints(&texture,0,1,0,&footprint,nullptr,nullptr,&size);
    auto result=gpu.Buffer(size,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    float expected[4]={null_default?1.f:.25f,null_default?1.f:.5f,null_default?1.f:.75f,1.f};
    if (override_values) std::memcpy(expected, override_values, sizeof(expected));
    gpu.list->ClearState(nullptr);
    // Native raw-NULL hook is exercised by pristine_null_native.inl; this
    // synthetic producer uses the same production canonicalizer before WARP.
    gpu.list->OMSetBlendFactor(CommandStateBlendFactorValues(null_default?nullptr:expected));
    auto state=std::make_unique<NativeCommandState>();
    state->known=true;state->null_pipeline_baseline=true;
    state->null_guard_covered=true;state->null_guard_reset_observed=true;
    state->dynamic.recorded=kDynamicBlend;
    std::memcpy(state->dynamic.blend,expected,sizeof(expected));
    if (inject) {
        Require(CanAutoRestoreRasterNull(*state),"Finite blend-only automatic policy refused verified fixture");
        gpu.list->SetPipelineState(gpu.plugin_pipeline.p);
        gpu.list->OMSetBlendFactor(zero);
        RestoreNativeCommandState(gpu.list.p,*state);
    }
    // The game's later draw supplies its own raster bindings, but deliberately
    // does NOT bind blend again: only correct restoration can produce the expected pixels.
    gpu.list->SetGraphicsRootSignature(root.p);gpu.list->SetPipelineState(pipeline.p);
    D3D12_VIEWPORT viewport{0,0,1,1,0,1};D3D12_RECT scissor{0,0,1,1};
    gpu.list->RSSetViewports(1,&viewport);gpu.list->RSSetScissorRects(1,&scissor);
    gpu.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    gpu.list->OMSetRenderTargets(1,&rtv,FALSE,nullptr);
    gpu.list->DrawInstanced(3,1,0,0);
    gpu.Barrier(target.p,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION from{},to{};
    from.pResource=target.p;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.pResource=result.p;to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=footprint;
    gpu.list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    gpu.Submit();
    void *mapped=nullptr;D3D12_RANGE range{0,16};Check(result->Map(0,&range,&mapped),"Map blend-only pixel");
    bool correct=true;const auto *actual=static_cast<const float *>(mapped);
    for(unsigned i=0;i<4;++i)correct=correct && actual[i]==expected[i];
    BlendOnlyPixel pixel{};
    std::memcpy(pixel.channels, actual, sizeof(pixel.channels));
    std::printf("Blend-only WARP nullDefault=%d inject=%d actual=(%.3f,%.3f,%.3f,%.3f)\n",null_default,inject,actual[0],actual[1],actual[2],actual[3]);
    result->Unmap(0,nullptr);ReleaseNativeCommandState(*state);
    // Unit-range cases retain the independent analytic oracle. For finite
    // out-of-unit factors, compare against an actual uninjected native draw:
    // do not assume how a device/format performs blend arithmetic or clamping.
    if (!override_values) Require(correct,"Restored blend-only state produced wrong pixels");
    return pixel;
}

void RunBlendOnly(Gpu &gpu, bool null_default, bool inject) {
    RunBlendOnlyCase(gpu, null_default, inject);
    if (null_default || !inject) return;
    const float cases[][4] = {
        {-0.f, -1.f, -2.f, -3.f}, // #99 dev3 observed state (not game replay).
        {-0.125f, 0.25f, 1.25f, 2.f},
        {4.f, -2.f, 0.5f, 1.f},
    };
    for (const auto &values : cases) {
        const auto native = RunBlendOnlyCase(gpu, false, false, values);
        const auto restored = RunBlendOnlyCase(gpu, false, true, values);
        Require(std::memcmp(native.channels, restored.channels, sizeof(native.channels)) == 0,
                "Restored finite blend-only state differs from native pixels");
    }
    std::puts("FINITE_BLEND_NATIVE_PIXEL_ORACLE: 3 paired finite out-of-unit cases passed");
}
