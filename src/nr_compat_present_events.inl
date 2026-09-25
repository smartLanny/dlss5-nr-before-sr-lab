unsigned long long Collect(Context& c) {
    nr_present_capture::Collect();
    if(c.timeline.Poisoned())return UINT64_MAX;
    if(!c.fence){if(c.recorded){Poison(c,0xBAD00002u,"missing-completion-fence");return UINT64_MAX;}return 0;}
    const auto done=ID3D12Fence_GetCompletedValue(c.fence);
    if(done==UINT64_MAX||FAILED(ID3D12Device_GetDeviceRemovedReason(c.device))){Poison(c,0xBAD00002u,"completion/device");return UINT64_MAX;}
    for(auto& f:c.flights)if(f.fence&&done>=f.fence){
        if(f.adopted){++observation.completed;observation.completed_source=std::max(observation.completed_source,f.source);}
        if(f.adopted&&f.timing_resolved&&!f.capture_recorded&&f.config_generation==g_config_generation.load(std::memory_order_relaxed)&&f.timing_readback&&c.timestamp_frequency&&f.source>=observation.timing_source){
            unsigned long long values[16]{};void* data=nullptr;const D3D12_RANGE read{0,(5u+2u*c.passes)*sizeof(unsigned long long)};
            if(SUCCEEDED(ID3D12Resource_Map(f.timing_readback,0,&read,&data))&&data){
                std::memcpy(values,data,read.End);const D3D12_RANGE no_write{0,0};ID3D12Resource_Unmap(f.timing_readback,0,&no_write);
                bool ordered=true;for(unsigned i=1;i<5u+2u*c.passes;++i)ordered &= values[i]>=values[i-1];
                if(ordered){const double ms=1000.0/static_cast<double>(c.timestamp_frequency);
                    observation.timing_source=f.source;observation.timing_valid=true;
                    observation.capture_ms=(values[1]-values[0])*ms;observation.encode_ms=(values[2]-values[1])*ms;
                    std::fill(std::begin(observation.nr_ms),std::end(observation.nr_ms),0.0);
                    for(unsigned i=0;i<c.passes;++i)observation.nr_ms[i]=(values[4u+2u*i]-values[3u+2u*i])*ms;
                    observation.compose_ms=(values[3u+2u*c.passes]-values[2u+2u*c.passes])*ms;
                    observation.blit_ms=(values[4u+2u*c.passes]-values[3u+2u*c.passes])*ms;
                    observation.total_ms=(values[4u+2u*c.passes]-values[0])*ms;
                }
            }
        }
        f.timing_resolved=false;f.fence=0;f.adopted=false;ReleaseCom(f.backbuffer);
    }
    return done;
}
bool Submit(Context& c,unsigned slot,unsigned long long source,bool create,bool adopted) {
    auto& f=c.flights[slot];auto* list=f.list;
    if(FAILED(ID3D12GraphicsCommandList_Close(list))){Poison(c,0xBAD00002u,"Close");return false;}
    const auto fence=c.timeline.Reserve(slot);if(!fence){Poison(c,0xBAD00002u,"fence-exhausted");return false;}
    // Same ACTUAL presentation queue. No guessed queue or extra presenting swapchain.
    ID3D12CommandList* lists[]={reinterpret_cast<ID3D12CommandList*>(list)};
    c.recorded=true;f.fence=fence;f.source=source;f.adopted=adopted;
    ID3D12CommandQueue_ExecuteCommandLists(c.queue,1,lists);
    const HRESULT result=ID3D12CommandQueue_Signal(c.queue,c.fence,fence);
    if(FAILED(result)||!c.timeline.Submitted(slot,fence,create)){Poison(c,static_cast<unsigned>(result),"Signal-after-Execute");return false;}
    nr_present_capture::Submit(source,c.fence,fence);
    nr_source_runtime::CompletePresent(c.fence,fence);
    if(create)c.create_fence=fence;
    if(adopted){for(unsigned i=0;i<c.passes;++i)++observation.adopted[i];++observation.submitted;observation.submitted_source=source;SetStatus(Status::Submitted);
        last_recorded_tick.store(GetTickCount64(),std::memory_order_release);
        nr_processing_runtime::Adopted(nr_processing::Start::Present);}
    else SetStatus(create?Status::Creating:Status::Preparing);
    return true;
}
bool CreateLayer(Context& c,Layer& l,ID3D12GraphicsCommandList* list) {
    if(!Bind(c,l,l.context.params)){Poison(c,0xBAD00005u,"Create-parameter-contract");return false;}
    void* handle=nullptr;c.opaque_called=true;
    const auto result=CreateSignedNrFeature(list,l.context,SyntheticFeature(c),&handle);
    l.context.nr_handle=static_cast<NgxHandle*>(handle);
    if(result!=kNgxSuccess||!handle){Poison(c,result,"CreateFeature");return false;}
    for(const auto& other:c.layers)if(&other!=&l && other.context.nr_handle==l.context.nr_handle){
        Poison(c,0xBAD00005u,"Feature-instance-alias");return false;}
    return true;
}
bool Bootstrap(Context& c,unsigned slot,unsigned long long source) {
    auto* list=c.flights[slot].list;
    if(!c.uses_native_guides){
        Transition(list,c.unknown_depth,c.unknown_depth_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Transition(list,c.unknown_motion,c.unknown_motion_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Utility(c,list,c.unknown_guide_table,c.init_guides,c.work);
        UavBarrier(list,c.unknown_depth);UavBarrier(list,c.unknown_motion);
        Transition(list,c.unknown_depth,c.unknown_depth_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(list,c.unknown_motion,c.unknown_motion_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    Transition(list,c.image.proxy,c.image.proxy_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Utility(c,list,c.layers[0].init_input,c.init_color,c.work);UavBarrier(list,c.image.proxy);
    Transition(list,c.image.proxy,c.image.proxy_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(list,c.image.neural,c.image.neural_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Utility(c,list,c.layers[0].init_output,c.init_color,c.work);UavBarrier(list,c.image.neural);
    if(!CreateLayer(c,c.layers[0],list))return false;
    return Submit(c,slot,source,true,false); // no Evaluate before real Create completion
}
void DrawBackbuffer(Context& c,Flight& f) {
    auto* list=f.list;auto state=D3D12_RESOURCE_STATE_PRESENT;
    Transition(list,f.backbuffer,state,D3D12_RESOURCE_STATE_RENDER_TARGET);
    ID3D12DescriptorHeap* heaps[]={c.io};ID3D12GraphicsCommandList_SetDescriptorHeaps(list,1,heaps);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(list,c.util_root);
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(list,0,ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(c.io));
    const unsigned constants[]={c.source.width,c.source.height,static_cast<unsigned>(c.encoding),0};
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(list,1,4,constants,0);
    ID3D12GraphicsCommandList_SetPipelineState(list,c.blit);
    const auto rtv=ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(f.rtv);
    ID3D12GraphicsCommandList_OMSetRenderTargets(list,1,&rtv,FALSE,nullptr);
    const D3D12_VIEWPORT viewport{0,0,float(c.source.width),float(c.source.height),0,1};
    const D3D12_RECT rect{0,0,LONG(c.source.width),LONG(c.source.height)};
    ID3D12GraphicsCommandList_RSSetViewports(list,1,&viewport);ID3D12GraphicsCommandList_RSSetScissorRects(list,1,&rect);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(list,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_DrawInstanced(list,3,1,0,0);
    Transition(list,f.backbuffer,state,D3D12_RESOURCE_STATE_PRESENT);
}
bool Frame(Context& c,unsigned slot,ID3D12Resource* backbuffer,unsigned long long source) {
    auto& f=c.flights[slot];auto* list=f.list;
    f.timing_resolved=false;f.capture_recorded=false;f.config_generation=g_config_generation.load(std::memory_order_relaxed);Stamp(c,f,0);
    // Fence-free GPU scratch reuse is valid only on this serialized same queue;
    // allocator/parameter/RTV reuse was separately checked by Timeline::Free.
    auto bbstate=D3D12_RESOURCE_STATE_PRESENT;
    Transition(list,backbuffer,bbstate,D3D12_RESOURCE_STATE_COPY_SOURCE);
    Transition(list,c.raw,c.raw_state,D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12GraphicsCommandList_CopyResource(list,c.raw,backbuffer);
    Transition(list,backbuffer,bbstate,D3D12_RESOURCE_STATE_PRESENT);
    Transition(list,c.raw,c.raw_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    Transition(list,c.image.source,c.image.source_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Utility(c,list,c.io,c.convert,c.source);UavBarrier(list,c.image.source);
    Transition(list,c.image.source,c.image.source_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    Stamp(c,f,1);
    Transition(list,c.image.proxy,c.image.proxy_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Codec(c,list,false);UavBarrier(list,c.image.proxy);
    Transition(list,c.image.proxy,c.image.proxy_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Stamp(c,f,2);bool warmed=true;
    f.capture_recorded=nr_present_capture::BeginCapture(c,f,source);
    for(unsigned i=0;i<c.passes;++i){auto& l=c.layers[i];auto* parameters=l.evaluation_params[slot];
        if(!l.context.nr_handle){
            // Create later layers from an actual successful parent answer. The
            // helper owns the exported output state; do not initialize it behind
            // its back or guess creation completion from a Present count.
            auto& parent=i==1?c.image:c.layers[i-1].binding;
            auto state=static_cast<unsigned>(parent.neural_state);
            if(!nr_dual::dx12::RecordCreationInputs(c.spatial_pipelines,l.spatial,list,state)){
                Poison(c,0xBAD00005u,"prepare-later-Create");return false;}
            parent.neural_state=static_cast<D3D12_RESOURCE_STATES>(state);
            if(!CreateLayer(c,l,list))return false;
            return Submit(c,slot,source,true,false);
        }
        if(!Bind(c,l,parameters)){Poison(c,0xBAD00005u,"Evaluate-parameter-contract");return false;}
        ++observation.attempted[i];
        unsigned result=0;c.opaque_called=true;Stamp(c,f,3u+2u*i);
        if(i==0){
            Transition(list,c.image.neural,c.image.neural_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            result=Evaluate(&l,list,l.context.nr_handle,parameters);
            if(result!=kNgxSuccess){Poison(c,result,"Evaluate-primary");return false;}
            UavBarrier(list,c.image.neural);
            Transition(list,c.image.neural,c.image.neural_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }else{
            auto& parent=i==1?c.image:c.layers[i-1].binding;
            auto state=static_cast<unsigned>(parent.neural_state);
            nr_dual::Admission admission{};auto& previous=c.layers[i-1];
            admission.primary_feature=previous.context.nr_handle;admission.primary_params=previous.evaluation_params[slot];
            admission.secondary_feature=l.context.nr_handle;admission.secondary_params=parameters;
            admission.secondary_create_extent={l.context.width,l.context.height};
            admission.creation_completion_proven=c.timeline.Ready(ID3D12Fence_GetCompletedValue(c.fence));
            admission.recording_and_queue_admitted=!c.timeline.Poisoned()&&c.queue&&c.device;
            admission.dependencies_pinned=c.recorded&&c.layers[i].spatial&&f.backbuffer;
            admission.current_inputs_and_guides_bound=l.binding.proxy&&l.binding.neural&&l.frame_guides.depth&&l.frame_guides.motion;
            const auto result2=nr_dual::dx12::RecordSecond(c.spatial_pipelines,l.spatial,list,state,l.plan,admission,Evaluate,&l);
            parent.neural_state=static_cast<D3D12_RESOURCE_STATES>(state);
            if(!result2.HasSecond()){Poison(c,result2.ngx_result,"Evaluate-later");return false;}
            l.binding.neural_state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        }
        nr_present_capture::LayerBegin(c,f,i);
        Stamp(c,f,4u+2u*i);
        ++observation.evaluated[i];
        ++l.context.evaluations;
        l.context.guide_history.CommitOnEvaluate(l.frame_guides.signature,true);
        if(l.context.diagnostic_reset_requested)l.context.successful_evaluations_since_reset=0;
        ++l.context.successful_evaluations_since_reset;l.context.reset_retry_pending=false;
        warmed &= (c.uses_native_guides?l.context.successful_evaluations_since_reset:l.context.evaluations)>g_config.warmup_after_create;
    }
    c.previous_source=nr_source_runtime::CurrentPresentLease().source;
    if(!warmed)return Submit(c,slot,source,false,false);
    Transition(list,c.image.result,c.image.result_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Codec(c,list,true);UavBarrier(list,c.image.result);
    nr_present_capture::Finish(c,f);
    Transition(list,c.image.result,c.image.result_state,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    Stamp(c,f,3u+2u*c.passes);DrawBackbuffer(c,f);Stamp(c,f,4u+2u*c.passes);ResolveTiming(c,f);
    return Submit(c,slot,source,false,true);
}
bool Shape(const D3D12_RESOURCE_DESC& d) {
    return d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&d.Width&&d.Width<=16384&&d.Height&&d.Height<=16384&&
        d.MipLevels==1&&d.DepthOrArraySize==1&&d.SampleDesc.Count==1&&
        (d.Flags&D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)!=0;
}
bool FullRect(const reshade::api::rect* r,Extent e){return !r||(r->left==0&&r->top==0&&r->right==int(e.width)&&r->bottom==int(e.height));}
void OnPresent(reshade::api::command_queue* queue,reshade::api::swapchain* chain,
    const reshade::api::rect* source_rect,const reshade::api::rect* dest_rect,
    std::uint32_t dirty_count,const reshade::api::rect*) {
    if(inside)return;
    // The old Core present callback remains registered first and owns config/F6
    // polling. No new module scan, queue flush, model allocation or capture while
    // the master is disabled. Only finite in-flight retirement may remain.
    if(!g_enabled_fast.load(std::memory_order_acquire)&&!has_context.load(std::memory_order_acquire))return;
    if(!TryEnterCriticalSection(&g_chain_cs))return;
    struct Unlock {~Unlock(){LeaveCriticalSection(&g_chain_cs);}} unlock;
    Recursion recursion;
    if(retirement_active)return;
    if(current){const auto done=Collect(*current);if(done==UINT64_MAX)return;}
    if(!g_config.enabled||g_config.identity_use_original||
       !std::isfinite(g_config.transfer_strength)||g_config.transfer_strength<=0.000001f){
        settled_presents=0;
        if(current&&!RetireCurrent()){if(!current->timeline.Poisoned())SetStatus(Status::Draining);return;}
        SetStatus(Status::Off);return;
    }
    // DLSSG may own/pace a second presentation queue without an intercepted
    // Feature11 Evaluate. A recent SR receipt does not identify its generated
    // backbuffer. Do not queue a wait or mutate that surface. A supported
    // native-SR After path or exact FG-input consumer may handle the
    // application image before FG. Unsupported boundaries skip safely.
    if(g_fg_presentation_policy.Read().late_owned){
        if(current&&!RetireCurrent()){SetStatus(Status::Draining);return;}
        SetStatus(Status::FgBoundaryOwned);return;
    }
    const unsigned config=g_config_generation.load(std::memory_order_relaxed);
    if(stable_config!=config){stable_config=config;settled_presents=0;}
    if(!queue||!chain||!chain->get_device()||chain->get_device()->get_api()!=reshade::api::device_api::d3d12){SetStatus(Status::UnsupportedApi);return;}
    if(chain_overflow||LiveChains()!=1||std::find(chains.begin(),chains.end(),chain)==chains.end()){
        SetStatus(Status::UnsupportedSurface);return;
    }
    ID3D12Resource* buffer=nullptr;
    (void)NrAcquireSwapchainBuffer(chain,chain->get_current_back_buffer_index(),&buffer);
    struct BufferReference{ID3D12Resource*& value;~BufferReference(){ReleaseCom(value);}} buffer_reference{buffer};
    auto* native_queue=reinterpret_cast<ID3D12CommandQueue*>(queue->get_native());
    if(!buffer||!native_queue){SetStatus(Status::UnsupportedSurface);return;}
    const auto desc=ID3D12Resource_GetDesc(buffer);
    const Extent source{unsigned(desc.Width),desc.Height};
    const auto encoding=SurfaceEncoding(chain,desc.Format);
    // Resource::GetDevice is hooked by ReShade to return its proxy. Creating
    // private lists through that proxy and submitting them to the raw queue
    // mixes execution layers. Use the paired API-native device and queue.
    auto* device=reinterpret_cast<ID3D12Device*>(chain->get_device()->get_native());
    if(device)ID3D12Device_AddRef(device);
    ID3D12Device *resource_device=nullptr,*queue_device=nullptr;
    const bool identities=device&&SUCCEEDED(ID3D12Resource_GetDevice(buffer,IID_ID3D12Device,reinterpret_cast<void**>(&resource_device)))&&
        SUCCEEDED(ID3D12CommandQueue_GetDevice(native_queue,IID_ID3D12Device,reinterpret_cast<void**>(&queue_device)))&&
        IsSameRrDepthDevice(device,resource_device)&&IsSameRrDepthDevice(device,queue_device)&&ID3D12Device_GetNodeCount(device)==1&&
        ID3D12CommandQueue_GetDesc(native_queue).Type==D3D12_COMMAND_LIST_TYPE_DIRECT;
    ReleaseCom(resource_device);ReleaseCom(queue_device);
    nr_compat_present::Gate gate{};gate.master=gate.selected=true;gate.finite_strength=gate.nonzero_strength=true;
    gate.source_verified=nr_source_runtime::CanPresent(queue,chain,buffer);
    gate.application_present=gate.source_verified;
    struct ReleaseUnrecordedClaim {~ReleaseUnrecordedClaim(){nr_source_runtime::CancelPresentBeforeWork();}} claim_scope;
    gate.full_rect=Shape(desc)&&FullRect(source_rect,source)&&FullRect(dest_rect,source)&&dirty_count==0;
    gate.only_swapchain=true;gate.d3d12=true;gate.color_known=encoding!=Encoding::Unknown;gate.device_and_queue=identities;
    gate.other_nr=OtherNrOwnerActive();gate.hard_fault=g_lifecycle_tracking_uncertain.load(std::memory_order_acquire)||
        g_nr_failed_create_process_latched.load(std::memory_order_acquire)||
        g_feature_tracking_uncertain.load(std::memory_order_acquire);
    gate.prior_inline_drained=NativeNrDrained();
    if(gate.source_verified&&!nr_processing_runtime::PresentSelected())
        nr_processing_runtime::MissingInlineAtPresent(nr_source_runtime::PresentRetryToken());
    gate.selected=nr_processing_runtime::PresentSelected();
    const auto admitted=nr_compat_present::Admit(gate);
    if(admitted!=Status::Preparing){ReleaseCom(device);SetStatus(admitted);return;}
    const auto retry_token=nr_source_runtime::PresentRetryToken();
    if(!nr_processing_runtime::session.Ready(nr_processing::Start::Present,retry_token)){
        ReleaseCom(device);SetStatus(Status::RetryBackoff);return;
    }
    const auto reject_build=[retry_token](const char* reason){nr_processing_runtime::RejectBeforePresentWork(retry_token,reason);};
    const auto& source_lease=nr_source_runtime::CurrentPresentLease();
    const bool native_guides=NativeGuideAvailable(source_lease);
    if(!native_guides&&!source_lease.color_only){ReleaseCom(device);SetStatus(Status::MissingNativeGuides);return;}
    const auto work=nr_compat_present::WorkExtent(source,g_config.compat_percent);
    const unsigned passes=RequestedPasses(g_config);
    const unsigned numerator=1;
    const unsigned denominator=1;
    auto tuning=CurrentNrTuningSignature();tuning.input_filter=0;
    std::array<NrTuningSignature,kPassLimit> layer_tuning{};
    bool layer_tuning_changed=false;
    for(unsigned i=0;i<passes;++i){
        layer_tuning[i]=CurrentNrTuningSignature(i);layer_tuning[i].input_filter=0;
        if(current&&!SameNrTuningSignature(current->layer_tuning[i],layer_tuning[i]))layer_tuning_changed=true;
    }
    if(!nr_compat_present::Valid(work)){ReleaseCom(device);SetStatus(Status::UnsupportedSurface);return;}
    if(current&&(current->owner!=chain||current->queue!=native_queue||!IsSameRrDepthDevice(current->device,device)||
        !(current->source==source)||!(current->work==work)||current->encoding!=encoding||current->format!=desc.Format||
        current->passes!=passes||current->numerator!=numerator||current->denominator!=denominator||layer_tuning_changed||!SameNrTuningSignature(current->tuning,tuning)||
        current->motion_compensation!=g_config.present_mv_compensation||
        current->native_output.width!=source_lease.output_width||current->native_output.height!=source_lease.output_height||
        current->uses_native_guides!=native_guides||(native_guides&&!SameNrGuideStorageContract(current->native_contract,source_lease.guides.signature)))){
        const bool retired=RetireCurrent();ReleaseCom(device);
        if(!retired){if(!current->timeline.Poisoned())SetStatus(Status::Draining);return;}
        // Settings may have changed while the API retirement ran unlocked.
        // Resolve fresh source/tuning on the next event, do not use stale locals.
        SetStatus(Status::Preparing);return;
    }
    if(!current){
        if(!EnsureNrApi(device)||!EnsureCodec(device)){ReleaseCom(device);reject_build("runtime-or-codec-not-ready");SetStatus(Status::BuildFailure);return;}
        auto* c=new(std::nothrow) Context;
        if(!c){ReleaseCom(device);reject_build("present-context-allocation-failed");SetStatus(Status::Budget);return;}
        c->device=device;device=nullptr;c->queue=native_queue;ID3D12CommandQueue_AddRef(native_queue);
        c->owner=chain;c->source=source;c->work=work;c->encoding=encoding;c->format=desc.Format;
        c->native_output={source_lease.output_width,source_lease.output_height};
        c->motion_compensation=g_config.present_mv_compensation;
        c->uses_native_guides=native_guides;c->native_guides=source_lease.guides;c->native_contract=source_lease.guides.signature;
        if(next_generation==UINT64_MAX){ReleaseCom(c->queue);ReleaseCom(c->device);delete c;SetStatus(Status::BuildFailure);return;}
        c->passes=passes;c->numerator=numerator;c->denominator=denominator;c->tuning=tuning;c->generation=next_generation++;
        c->layer_tuning=layer_tuning;
        if(!Build(*c)){
            reject_build("present-resource-build-failed");
            current=c;has_context.store(true,std::memory_order_release);
            if(!RetireCurrent()){Poison(*current,0xBAD00005u,"partial-build-retirement");return;}
            SetStatus(Status::BuildFailure);return;
        }
        current=c;has_context.store(true,std::memory_order_release);failed_build_config=0;
        observation.timing_valid=false;observation.timing_source=0;
        observation.generation=c->generation;observation.bytes=c->bytes;observation.source_width=source.width;
        observation.source_height=source.height;observation.work_width=work.width;observation.work_height=work.height;observation.passes=passes;
    }
    ReleaseCom(device);
    auto& c=*current;
    if(c.uses_native_guides){
        c.native_guides=source_lease.guides;
        const auto& previous=c.previous_source;const auto& now=source_lease.source;
        if(!previous||previous.stream!=now.stream||previous.epoch!=now.epoch||previous.sequence==UINT64_MAX||previous.sequence+1!=now.sequence)
            c.native_guides.game_reset=1;
    }
    observation.color_only=!c.uses_native_guides;observation.reset_each_frame=!c.uses_native_guides;
    const auto done=Collect(c);if(done==UINT64_MAX)return;
    if(c.create_fence&&!c.timeline.Ready(done)){SetStatus(Status::Creating);return;}
    const int index=c.timeline.Free(done);if(index<0){SetStatus(Status::Busy);return;}
    const auto serial=nr_source_runtime::PresentSourceId();
    if(!serial){SetStatus(Status::SourceUnavailable);return;}
    auto& flight=c.flights[unsigned(index)];
    if(FAILED(ID3D12CommandAllocator_Reset(flight.allocator))||
       FAILED(ID3D12GraphicsCommandList_Reset(flight.list,flight.allocator,nullptr))){Poison(c,0xBAD00002u,"Reset-private-list");return;}
    if(!nr_source_runtime::MarkPresentRecorded(native_queue)){
        // A last-moment ownership change cancels an empty private recording.
        // Leave it closed so an Off transition can reuse the free flight.
        if(FAILED(ID3D12GraphicsCommandList_Close(flight.list))){Poison(c,0xBAD00002u,"Close-cancelled-private-list");return;}
        SetStatus(nr_source_runtime::PresentWorkBlockedByFg()?Status::FgBoundaryOwned:Status::Poisoned);return;
    }
    observation.source=serial;
    nr_processing_runtime::MarkRecorded();
    if(!c.layers[0].context.nr_handle){
        HMODULE self=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&OnPresent),&self)){Poison(c,0xBAD00002u,"module-lifetime-pin");return;}
        (void)Bootstrap(c,unsigned(index),serial);return;
    }
    flight.backbuffer=buffer;ID3D12Resource_AddRef(buffer);
    D3D12_RENDER_TARGET_VIEW_DESC rtv{};rtv.Format=c.format;rtv.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;
    ID3D12Device_CreateRenderTargetView(c.device,buffer,&rtv,ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(flight.rtv));
    (void)Frame(c,unsigned(index),buffer,serial);
}
void OnDestroySwapchain(reshade::api::swapchain* chain,bool) {
    if(inside)return;Guard guard;Recursion recursion;
    nr_source_runtime::RetireSwapchain(chain);
    for(auto& s:chains)if(s==chain)s=nullptr;
    if(!current||current->owner!=chain)return;
    auto& c=*current;
    // Resize must not race our own backbuffer references. This is a bounded
    // teardown wait, NOT a frame-path wait. Failed progress retains the whole
    // generation, making restart necessary instead of freeing live GPU memory.
    if(c.recorded&&!c.timeline.Drained(Collect(c))&&!c.timeline.Poisoned()){
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(!event){Poison(c,0xBAD00002u,"resize-event");return;}
        const auto hr=ID3D12Fence_SetEventOnCompletion(c.fence,c.timeline.Last(),event);
        const DWORD wait=SUCCEEDED(hr)?WaitForSingleObject(event,500):WAIT_FAILED;
        // A pending fence registration owns its event handle until completion.
        // Do not CloseHandle after timeout; that would leave a stale kernel handle.
        if(wait!=WAIT_OBJECT_0){Poison(c,0xBAD00002u,"resize-retirement-timeout");return;}
        CloseHandle(event);Collect(c);
    }
    if(!RetireCurrent())SetStatus(Status::Poisoned);else SetStatus(Status::Off);
    settled_presents=0;
}
bool TrySnapshot(Observation& out) {
    if(!TryEnterCriticalSection(&g_chain_cs))return false;
    out=observation;LeaveCriticalSection(&g_chain_cs);return true;
}
void Register(bool enable) {
    if(enable){
        nr_source_runtime::Register(true);
        reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
        reshade::register_event<reshade::addon_event::destroy_swapchain>(OnDestroySwapchain);
        reshade::register_event<reshade::addon_event::present>(OnPresent);
    }else{
        nr_source_runtime::Register(false);
        reshade::unregister_event<reshade::addon_event::present>(OnPresent);
        reshade::unregister_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
        reshade::unregister_event<reshade::addon_event::destroy_swapchain>(OnDestroySwapchain);
        // No blocking waits or NGX destruction under loader lock. Once any GPU
        // work was recorded the module is pinned; normal teardown uses the real
        // swapchain callback above. Process exit owns final reclamation.
    }
}
