// Exact Core shader/constant ABI on private owned images. No counterfeit
// Feature1 recovery token, native-output probe or native-SR success counter.
void Codec(Context& c,ID3D12GraphicsCommandList* list,bool combine) {
    auto& slot=c.image;const bool hdr=c.encoding!=Encoding::Srgb;
    auto policy=ResolveNrColorPolicy(1,hdr,hdr?CodecColorMode::Automatic:CodecColorMode::LinearSdr,
        true,true,g_config.color_strength,g_config.preserve_lighting);
    ApplyLightingLockPolicy(policy,1,g_config.lighting_lock);
    ApplyRequestedUnverifiedHdrColor(policy,g_config.allow_unverified_hdr_color,g_config.color_strength,
        g_config.transfer_strength,g_config.color_protection);
    CodecConstants k{};k.width=combine?c.source.width:c.work.width;k.height=combine?c.source.height:c.work.height;
    k.proxy_scale=g_config.proxy_scale;k.transfer_strength=g_config.transfer_strength;k.source_to_canonical=1;
    k.color_strength=policy.effective_color_strength;k.max_luma_ratio=g_config.max_luma_ratio;
    k.passthrough=hdr?0u:16u;
    if(combine&&g_config.high_strength_protection)k.passthrough|=1024u;
    if(combine&&g_config.layered_enhancement)k.passthrough|=2048u;
    if(combine&&g_config.independent_chroma_composition)k.passthrough|=4096u;
    const bool light=combine&&!g_config.generic_residual&&!g_config.region_enabled;
    if(light){k.passthrough|=262144u;if(g_config.light_control_mode==1u)k.passthrough|=524288u;}
    k.pre_exposure=1;k.source_width=c.source.width;k.source_height=c.source.height;
    k.model_width=c.work.width;k.model_height=c.work.height;k.proxy_curve=g_config.proxy_curve;k.frame_confidence=1;
    k.lighting_lock=combine?g_config.lighting_lock:0;k.edge_guard=combine?g_config.edge_guard:0;
    k.detail_stability=combine?g_config.detail_stability:0;
    k.layer_tone=g_config.layer_tone;k.layer_structure=g_config.layer_structure;
    k.layer_bright=g_config.layer_bright;k.layer_shadow=g_config.layer_shadow;
    if(light){k.light_broad=g_config.light_broad;k.light_dark=g_config.light_dark;k.light_reflection=g_config.light_reflection;
        k.light_structure=g_config.light_structure;k.light_glow=g_config.light_glow;k.light_preview=g_config.light_preview;}
    if(combine&&g_config.generic_residual){std::copy(g_config.generic_gains,g_config.generic_gains+5,k.region_gains);k.region_flags=32u;}
    // Engine segmentation and independent face masks are unavailable on this
    // color-only source. Do not reinterpret those controls as zero-valued guides.
    if(combine&&slot.dual_reduced){k.dual_low_width=slot.dual_low_width;k.dual_low_height=slot.dual_low_height;
        k.dual_residual=1;k.dual_model_domain=slot.dual_model_domain;}
    ID3D12DescriptorHeap* heaps[]={slot.heap};ID3D12GraphicsCommandList_SetDescriptorHeaps(list,1,heaps);
    ID3D12GraphicsCommandList_SetComputeRootSignature(list,g_codec.root);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list,0,ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(slot.heap));
    ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list,1,kCodecRootConstantCount,&k,0);
    auto* shader=combine?g_codec.combine_v2:g_codec.encode_v2;
    if(combine&&slot.dual_reduced)shader=nr_dual_runtime::CodecPipeline(shader,false,slot.dual_model_domain);
    ID3D12GraphicsCommandList_SetPipelineState(list,shader);
    ID3D12GraphicsCommandList_Dispatch(list,(k.width+7)/8,(k.height+7)/8,1);
}
NrGuideBinding SyntheticGuides(const Layer& l,bool hdr) {
    NrGuideBinding g{};g.depth=l.depth;g.motion=l.motion;g.game_reset=1;
    auto& s=g.signature;s.depth_width=s.motion_source_width=s.motion_bound_width=l.context.width;
    s.depth_height=s.motion_source_height=s.motion_bound_height=l.context.height;
    s.depth_physical_width=s.motion_physical_width=s.color_physical_width=l.context.width;
    s.depth_physical_height=s.motion_physical_height=s.color_physical_height=l.context.height;
    s.depth_format=DXGI_FORMAT_R32_FLOAT;s.motion_format=DXGI_FORMAT_R16G16_FLOAT;s.color_format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    s.normalized_create_flags=hdr?1:0;s.depth_inverted=0;s.motion_scale_x=s.motion_scale_y=1;
    g.has_motion_scale_x=g.has_motion_scale_y=g.valid_motion_scale_x=g.valid_motion_scale_y=true;
    g.jitter.known=false; // not a fabricated game-phase value
    return g;
}
FeatureRecord SyntheticFeature(const Context& c){FeatureRecord f{};f.feature_id=18;f.generation=c.generation;f.create_flags=c.encoding==Encoding::Srgb?0:1;return f;}
bool NativeGuideAvailable(const nr_source_runtime::PresentLease& lease) {
    const auto& g=lease.guides;
    return lease.guides_owned&&lease.guides_current&&g.depth&&g.motion&&
        !g.signature.linear_depth&&!g.signature.resample_motion&&
        g.signature.depth_width&&g.signature.depth_height&&
        g.signature.motion_bound_width&&g.signature.motion_bound_height&&
        g.valid_motion_scale_x&&g.valid_motion_scale_y;
}
NrGuideBinding LayerGuides(const Context& c,const Layer& l) {
    if(!c.uses_native_guides)return SyntheticGuides(l,c.encoding!=Encoding::Srgb);
    auto guides=c.native_guides;auto& signature=guides.signature;
    // Preserve the established post-NR guide units. Low-resolution MV is
    // expressed by the actual guide extent/create flag, not a second scaling.
    signature.normalized_create_flags &= ~3;
    signature.normalized_create_flags |= c.encoding==Encoding::Srgb?0:1;
    if(signature.motion_bound_width!=l.context.width||signature.motion_bound_height!=l.context.height)
        signature.normalized_create_flags|=2;
    return guides;
}
bool Bind(Context& c,Layer& l,NgxParams* params) {
    auto* saved=l.context.params;l.context.params=params;
    l.frame_guides=LayerGuides(c,l);
    auto feature=SyntheticFeature(c);feature.create_flags=l.context.bound_create_flags;
    const bool ok=BindNrParameters(l.context,l.binding,l.frame_guides,feature,
        l.context.width,l.context.height,ColorContract{},!c.uses_native_guides||l.context.evaluations==0,nullptr);
    l.context.params=saved;return ok;
}

std::uint32_t Evaluate(void* user,ID3D12GraphicsCommandList* list,const void* handle,const void* params) {
    auto& layer=*static_cast<Layer*>(user);
    NgxParams* owned_params=nullptr;
    for(auto* block:layer.evaluation_params)if(block==params)owned_params=block;
    if(handle!=layer.context.nr_handle||!owned_params)return 0xBAD00005u;
    ScopedSrwExclusive lock(&g_nr_api_lock);
    ScopedPrivateNrCall call(PrivateNrOperation::Evaluate,layer.context.nr_handle);
    ScopedNgxLayer scope(NgxLayer::PrivateBridge);
    return g_nr_api.bridge_evaluate(g_nr_api.signed_evaluate,list,layer.context.nr_handle,owned_params,nullptr);
}
bool RetireProof(void* user){
    auto& c=*static_cast<Context*>(user);
    return !c.timeline.Poisoned() && (!c.recorded ||
        (c.fence && c.timeline.Drained(ID3D12Fence_GetCompletedValue(c.fence))));
}
bool Release(Context*& ptr) {
    if(!ptr)return true;
    auto& c=*ptr;
    if(c.timeline.Poisoned() || (c.recorded&&!RetireProof(&c)))return false;
    if(!nr_present_capture::Retire(c))return false;
    // No GPU dependency is freed until every model Release and parameter destroy
    // succeeds. A returned error retains the remaining generation, no retry.
    for(unsigned i=0;i<c.passes;++i){auto& l=c.layers[i];ScopedSrwExclusive lock(&g_nr_api_lock);
        if(l.context.nr_handle){ScopedPrivateNrCall call(PrivateNrOperation::Release,l.context.nr_handle);
            ScopedNgxLayer private_layer(NgxLayer::PrivateBridge);
            const auto identity=FindFeature(l.context.nr_handle);
            const auto r=g_nr_api.bridge_release(g_nr_api.signed_release,l.context.nr_handle);
            if(r!=kNgxSuccess){c.timeline.Poison();c.retirement_error=r;return false;}
            ForgetObservedFeature(l.context.nr_handle,identity.generation);l.context.nr_handle=nullptr;}
        for(auto& p:l.evaluation_params)if(p){const auto r=g_nr_api.destroy(p);if(r!=kNgxSuccess){c.timeline.Poison();c.retirement_error=r;return false;}p=nullptr;}
        if(l.context.params){const auto r=g_nr_api.destroy(l.context.params);if(r!=kNgxSuccess){c.timeline.Poison();c.retirement_error=r;return false;}l.context.params=nullptr;}
    }
    for(auto& f:c.flights){ReleaseCom(f.backbuffer);ReleaseCom(f.timing_readback);ReleaseCom(f.rtv);ReleaseCom(f.list);ReleaseCom(f.allocator);}
    for(unsigned i=0;i<c.passes;++i){auto& l=c.layers[i];
        if(l.spatial&&!nr_dual::dx12::DestroySpatialSlotAfterRetirement(l.spatial,RetireProof,&c))return false;
        ReleaseCom(l.init_input);ReleaseCom(l.init_output);ReleaseCom(l.depth);ReleaseCom(l.motion);}
    if(c.spatial_pipelines&&!nr_dual::dx12::DestroyPipelinesAfterRetirement(c.spatial_pipelines,RetireProof,&c))return false;
    ReleaseCom(c.image.heap);ReleaseCom(c.image.source);ReleaseCom(c.image.result);ReleaseCom(c.image.proxy);ReleaseCom(c.image.neural);
    ReleaseCom(c.raw);ReleaseCom(c.io);ReleaseCom(c.blit);ReleaseCom(c.convert);ReleaseCom(c.init);ReleaseCom(c.init_color);ReleaseCom(c.util_root);
    ReleaseCom(c.timestamps);ReleaseCom(c.fence);ReleaseCom(c.queue);ReleaseCom(c.device);delete ptr;ptr=nullptr;has_context.store(false,std::memory_order_release);return true;
}
// The original Core destroys NGX Features outside the chain lock. Preserve
// that lock order rather than blocking a model worker which needs Core callbacks.
// Call only with exactly one acquired chain-lock level and a serialized event.
bool RetireCurrent() {
    if(!current)return true;
    if(!RetireProof(current))return false;
    Context* retired=current;current=nullptr;retirement_active=true;
    LeaveCriticalSection(&g_chain_cs);
    const bool released=Release(retired);
    EnterCriticalSection(&g_chain_cs);
    current=retired;retirement_active=false;has_context.store(current!=nullptr,std::memory_order_release);
    if(!released&&current&&current->timeline.Poisoned())
        Poison(*current,current->retirement_error?current->retirement_error:0xBAD00002u,"retirement-API-error");
    return released;
}
template<class Config> unsigned RequestedPasses(const Config& config) {
    if constexpr(requires {config.nr_full_reference;config.nr_full_passes;})
        if(config.nr_full_reference)return std::clamp(config.nr_full_passes,2u,kPassLimit);
    return config.nr_passes==2?2:1;
}
template<class Config> bool FullReference(const Config& config) {
    if constexpr(requires {config.nr_full_reference;})return config.nr_full_reference;
    return false;
}
bool AllocateParameters(Context& c,NgxParams*& p) {
    ScopedSrwExclusive lock(&g_nr_api_lock);
    const auto result=g_nr_api.allocate(&p);
    if(result!=kNgxSuccess||!p){
        if(p)Poison(c,result,"ambiguous-parameter-allocation");
        return false;
    }
    unsigned aliases=0;
    for(const auto& l:c.layers){aliases+=l.context.params==p;
        for(const auto* q:l.evaluation_params)aliases+=q==p;}
    if(aliases!=1){Poison(c,0xBAD00005u,"parameter-allocation-alias");return false;}
    return true;
}
bool Build(Context& c) {
    const auto fp16=DXGI_FORMAT_R16G16B16A16_FLOAT;
    if(!Texture(c,c.source,c.format,D3D12_RESOURCE_FLAG_NONE,&c.raw)||
       !Texture(c,c.source,fp16,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,&c.image.source)||
       !Texture(c,c.source,fp16,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,&c.image.result)||
       !Texture(c,c.work,fp16,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,&c.image.proxy)||
       !Texture(c,c.work,fp16,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,&c.image.neural))return false;
    nr_dual_runtime::InitializeSourceFormats(c.image);c.image.result_format=fp16;
    c.image.codec_color_mode=c.encoding==Encoding::Srgb?CodecColorMode::LinearSdr:CodecColorMode::Automatic;
    c.image.replay_capture_index=nr_replay::Recorder::Invalid;
    c.image.dual_capture_index=nr_replay::Recorder::Invalid;
    if(!MakePipelines(c)||!Table(c,&c.io,c.image.source))return false;
    if(FAILED(ID3D12Device_CreateFence(c.device,0,D3D12_FENCE_FLAG_NONE,IID_ID3D12Fence,reinterpret_cast<void**>(&c.fence))))return false;
    for(auto& f:c.flights){
        if(FAILED(ID3D12Device_CreateCommandAllocator(c.device,D3D12_COMMAND_LIST_TYPE_DIRECT,IID_ID3D12CommandAllocator,reinterpret_cast<void**>(&f.allocator)))||
           FAILED(ID3D12Device_CreateCommandList(c.device,0,D3D12_COMMAND_LIST_TYPE_DIRECT,f.allocator,nullptr,IID_ID3D12GraphicsCommandList,reinterpret_cast<void**>(&f.list)))||
           FAILED(ID3D12GraphicsCommandList_Close(f.list)))return false;
        D3D12_DESCRIPTOR_HEAP_DESC d{};d.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;d.NumDescriptors=1;
        if(FAILED(ID3D12Device_CreateDescriptorHeap(c.device,&d,IID_ID3D12DescriptorHeap,reinterpret_cast<void**>(&f.rtv))))return false;
    }
    // Optional timestamp evidence: failed instrumentation never enables a route.
    D3D12_QUERY_HEAP_DESC q{};q.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;q.Count=kFlightCount*16;
    if(SUCCEEDED(ID3D12CommandQueue_GetTimestampFrequency(c.queue,&c.timestamp_frequency))&&c.timestamp_frequency&&
       SUCCEEDED(ID3D12Device_CreateQueryHeap(c.device,&q,IID_ID3D12QueryHeap,reinterpret_cast<void**>(&c.timestamps)))){
        bool timing_ready=true;
        for(auto& f:c.flights){D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
            d.Width=16*sizeof(unsigned long long);d.Height=d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;
            d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;D3D12_HEAP_PROPERTIES h{};
            h.Type=D3D12_HEAP_TYPE_READBACK;h.CreationNodeMask=h.VisibleNodeMask=1;
            timing_ready &= SUCCEEDED(ID3D12Device_CreateCommittedResource(c.device,&h,D3D12_HEAP_FLAG_NONE,&d,
                D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_ID3D12Resource,reinterpret_cast<void**>(&f.timing_readback)));}
        if(!timing_ready){for(auto& f:c.flights)ReleaseCom(f.timing_readback);ReleaseCom(c.timestamps);}
    }
    if(c.passes>1){std::string error;c.spatial_pipelines=nr_dual::dx12::CreatePipelines(c.device,error);if(!c.spatial_pipelines)return false;}
    for(unsigned i=0;i<c.passes;++i){auto& l=c.layers[i];Extent e=c.work;
        if(i){const bool reduced=c.passes==2&&(c.numerator!=c.denominator);
            l.plan=nr_dual::MakePlan({2,{reduced?c.numerator:1u,reduced?c.denominator:1u}},
                {c.work.width,c.work.height},{{1,1},{16384,16384},{1,1}});
            if(!l.plan)return false;
            ID3D12Resource* parent=i==1?c.image.neural:c.layers[i-1].binding.neural;
            std::string error;l.spatial=nr_dual::dx12::CreateSpatialSlot(c.spatial_pipelines,parent,l.plan,
                nr_dual::ModelDomain::SrgbProxy,kOwnedBudget-c.bytes,error,IsSameRrDepthDevice);
            if(!l.spatial)return false;
            const auto bytes=nr_dual::dx12::SlotBytes(l.spatial);
            if(!nr_compat_present::Fits(c.bytes,bytes,kOwnedBudget))return false;c.bytes+=bytes;
            const auto surfaces=nr_dual::dx12::GetSurfaces(l.spatial);
            l.binding.proxy=surfaces.input;l.binding.neural=surfaces.output;
            e={surfaces.secondary_extent.width,surfaces.secondary_extent.height};
        }else{l.binding.proxy=c.image.proxy;l.binding.neural=c.image.neural;}
        auto& model=l.context;model.width=e.width;model.height=e.height;model.mode=1;
        model.tuning_signature=c.tuning;model.tuning_signature_initialized=true;
        model.bound_create_flags=LayerGuides(c,l).signature.normalized_create_flags;model.bound_perf_quality=5;
        model.codec_color_mode=c.image.codec_color_mode;model.codec_color_mode_initialized=true;
        if(!Texture(c,e,DXGI_FORMAT_R32_FLOAT,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,&l.depth)||
           !Texture(c,e,DXGI_FORMAT_R16G16_FLOAT,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,&l.motion)||
           !Table(c,&l.init_input,l.binding.proxy,l.depth,l.motion)||
           !Table(c,&l.init_output,l.binding.neural,l.depth,l.motion)||!AllocateParameters(c,model.params))return false;
        for(auto& p:l.evaluation_params)if(!AllocateParameters(c,p))return false;
    }
    D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap.NumDescriptors=kCodecDescriptorCount;heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if(FAILED(ID3D12Device_CreateDescriptorHeap(c.device,&heap,IID_ID3D12DescriptorHeap,reinterpret_cast<void**>(&c.image.heap))))return false;
    if(c.passes>1){const auto surfaces=nr_dual::dx12::GetSurfaces(c.layers[c.passes-1].spatial);
        const bool reduced=c.passes==2&&c.layers[1].plan.path==nr_dual::Path::MatchedResidual;
        WriteCodecDescriptorSet(c.device,c.image,c.image.heap,nullptr,&surfaces,reduced);
        c.image.dual_reduced=reduced;c.image.dual_low_width=surfaces.secondary_extent.width;
        c.image.dual_low_height=surfaces.secondary_extent.height;c.image.dual_model_domain=static_cast<unsigned>(surfaces.domain);
        if(reduced&&!nr_dual_runtime::EnsureCodecSet(static_cast<unsigned>(surfaces.domain),false))return false;
    }else WriteCodecDescriptorSet(c.device,c.image,c.image.heap);
    observation.bytes=c.bytes;return true;
}
