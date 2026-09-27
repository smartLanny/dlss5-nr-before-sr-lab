// Independent synthetic comparison: zero model MV, model flow, or external temporal only.
#include <fstream>
#include NR_FLOW_PRODUCER_HEADER
namespace flow_model {
using namespace sr_host;
std::vector<char> Bytes(Host& h,ID3D12Resource* texture,D3D12_RESOURCE_STATES state){
    const auto desc=ID3D12Resource_GetDesc(texture);D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 size=0;
    ID3D12Device_GetCopyableFootprints(h.device,&desc,0,1,0,&footprint,nullptr,nullptr,&size);
    auto* readback=h.Buffer(size,D3D12_HEAP_TYPE_READBACK);h.Begin();if(state!=D3D12_RESOURCE_STATE_COPY_SOURCE)h.Barrier(texture,state,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION to{},from{};to.pResource=readback;to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=footprint;
    from.pResource=texture;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;ID3D12GraphicsCommandList_CopyTextureRegion(h.list,&to,0,0,0,&from,nullptr);
    if(state!=D3D12_RESOURCE_STATE_COPY_SOURCE)h.Barrier(texture,D3D12_RESOURCE_STATE_COPY_SOURCE,state);h.Submit();h.Wait();
    char* mapped=nullptr;Hr(ID3D12Resource_Map(readback,0,nullptr,(void**)&mapped),"byte oracle map");std::vector<char> data(size_t(desc.Width)*desc.Height*8);
    for(unsigned y=0;y<desc.Height;++y)std::memcpy(data.data()+y*size_t(desc.Width)*8,mapped+footprint.Offset+y*footprint.Footprint.RowPitch,size_t(desc.Width)*8);
    ID3D12Resource_Unmap(readback,0,nullptr);return data;
}
// Independent CPU scene producer: positive viewport jitter shifts the rendered
// image right/down, hence raster values sample the unshifted scene at p-jitter.
void Scene(Host& h,ID3D12Resource* texture,float jitter_x,float jitter_y,bool normal=false){
    const auto d=ID3D12Resource_GetDesc(texture);D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 size=0;
    ID3D12Device_GetCopyableFootprints(h.device,&d,0,1,0,&fp,nullptr,nullptr,&size);auto* upload=h.Buffer(size,D3D12_HEAP_TYPE_UPLOAD);
    char* mapped=nullptr;Hr(ID3D12Resource_Map(upload,0,nullptr,reinterpret_cast<void**>(&mapped)),"scene upload map");
    const auto half=[](float value){if(value==0)return static_cast<unsigned short>(0);int e=0;std::frexp(value,&e);--e;
        return static_cast<unsigned short>(unsigned(e+15)*1024+unsigned(std::nearbyint(std::ldexp(value,10-e)))-1024);};
    for(unsigned y=0;y<d.Height;++y)for(unsigned x=0;x<d.Width;++x){
        const float u=std::clamp((x+.5f-jitter_x)/float(d.Width),0.f,1.f),v=std::clamp((y+.5f-jitter_y)/float(d.Height),0.f,1.f);
        const float color[]={normal?0.f:.125f+.75f*u,normal?0.f:.125f+.75f*v,normal?1.f:.25f,normal?.5f:1.f};
        unsigned short pixel[4]{};for(unsigned k=0;k<4;++k)pixel[k]=half(color[k]);std::memcpy(mapped+y*fp.Footprint.RowPitch+x*8,pixel,8);
    }
    ID3D12Resource_Unmap(upload,0,nullptr);h.Begin();h.Barrier(texture,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION to{},from{};to.pResource=texture;to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    from.pResource=upload;from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=fp;
    ID3D12GraphicsCommandList_CopyTextureRegion(h.list,&to,0,0,0,&from,nullptr);h.Barrier(texture,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    h.Submit();h.Wait();
}
namespace present=nr_compat_present_runtime;
// Controlled assembly reuses the actual signed NR model/codec/spatial helpers.
// It does not call Present or fabricate a Present source. The native source and
// the original Core Capture/Finish/submit/Reset path remain those of this host.
struct RealNrChain {
    present::Context* c=nullptr;unsigned completed_frames=0;bool created_this_frame=false;
    void Build(Host& h,unsigned layers,unsigned nw,unsigned nh,unsigned ow,unsigned oh,const NrGuideBinding& guide){
        Require(EnsureNrApi(h.device)&&EnsureCodec(h.device),"real NR API and complete Core codec");
        nr_layers::Migrate(g_config.nr_layers,layers,nr_layers::Primary(g_config));
        for(unsigned i=1;i<layers;++i){auto& l=g_config.nr_layers[i-1];l.intensity=1.f+.1f*i;l.local_tone=.8f+.03f*i;l.local_structure=.7f+.03f*i;}
        c=new present::Context;c->device=h.device;ID3D12Device_AddRef(h.device);c->queue=h.queue;ID3D12CommandQueue_AddRef(h.queue);
        c->source={ow,oh};c->work={nw,nh};c->format=DXGI_FORMAT_R16G16B16A16_FLOAT;c->encoding=present::Encoding::ScRgb;
        c->passes=layers;c->numerator=c->denominator=1;c->generation=1;c->uses_native_guides=true;c->native_guides=guide;
        c->tuning=CurrentNrTuningSignature();c->tuning.input_filter=0;
        for(unsigned i=0;i<layers;++i){c->layer_tuning[i]=CurrentNrTuningSignature(i);c->layer_tuning[i].input_filter=0;}
        Require(present::Build(*c),"real independent signed NR layers Build");
    }
    bool Record(Host& h,ID3D12Resource* base,ID3D12Resource* final,const NrGuideBinding& guide){
        c->native_guides=guide;created_this_frame=false;auto* list=h.list;auto& image=c->image;
        auto base_state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        Transition(list,base,base_state,D3D12_RESOURCE_STATE_COPY_SOURCE);Transition(list,image.source,image.source_state,D3D12_RESOURCE_STATE_COPY_DEST);
        ID3D12GraphicsCommandList_CopyResource(list,image.source,base);Transition(list,base,base_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(list,image.source,image.source_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(list,image.proxy,image.proxy_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);present::Codec(*c,list,false);UavBarrier(list,image.proxy);
        Transition(list,image.proxy,image.proxy_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        for(unsigned i=0;i<c->passes;++i){auto& l=c->layers[i];auto* params=l.evaluation_params[0];
            if(!l.context.nr_handle){
                if(!i){Transition(list,image.neural,image.neural_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    present::Utility(*c,list,l.init_output,c->init_color,c->work);UavBarrier(list,image.neural);}
                else{auto& parent=i==1?image:c->layers[i-1].binding;auto state=unsigned(parent.neural_state);
                    Require(nr_dual::dx12::RecordCreationInputs(c->spatial_pipelines,l.spatial,list,state),"real preceding layer initializes next Create");parent.neural_state=D3D12_RESOURCE_STATES(state);}
                Require(present::CreateLayer(*c,l,list),"real signed Feature18 Create");created_this_frame=true;return false;
            }
            Require(present::Bind(*c,l,params),"same-source N guides bound to each real model");
            if(!i){Transition(list,image.neural,image.neural_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                Ngx(present::Evaluate(&l,list,l.context.nr_handle,params),"real signed primary Feature18 Evaluate");
                UavBarrier(list,image.neural);Transition(list,image.neural,image.neural_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);}
            else{auto& parent=i==1?image:c->layers[i-1].binding;auto state=unsigned(parent.neural_state);auto& previous=c->layers[i-1];
                nr_dual::Admission a{};a.primary_feature=previous.context.nr_handle;a.primary_params=previous.evaluation_params[0];
                a.secondary_feature=l.context.nr_handle;a.secondary_params=params;a.secondary_create_extent={l.context.width,l.context.height};
                a.creation_completion_proven=c->timeline.Ready(ID3D12Fence_GetCompletedValue(c->fence));
                a.recording_and_queue_admitted=c->queue==h.queue&&!c->timeline.Poisoned();a.dependencies_pinned=c->recorded&&l.spatial;
                a.current_inputs_and_guides_bound=l.binding.proxy&&l.binding.neural&&l.frame_guides.depth&&l.frame_guides.motion;
                auto result=nr_dual::dx12::RecordSecond(c->spatial_pipelines,l.spatial,list,state,l.plan,a,present::Evaluate,&l);
                Require(result.HasSecond(),"real full previous output feeds next independent Feature18");parent.neural_state=D3D12_RESOURCE_STATES(state);
                l.binding.neural_state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;}
            ++l.context.evaluations;l.context.guide_history.CommitOnEvaluate(l.frame_guides.signature,true);
            if(l.context.diagnostic_reset_requested)l.context.successful_evaluations_since_reset=0;
            ++l.context.successful_evaluations_since_reset;l.context.reset_retry_pending=false;
        }
        Transition(list,image.result,image.result_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);present::Codec(*c,list,true);UavBarrier(list,image.result);
        Transition(list,image.result,image.result_state,D3D12_RESOURCE_STATE_COPY_SOURCE);
        h.Barrier(final,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);ID3D12GraphicsCommandList_CopyResource(list,final,image.result);
        h.Barrier(final,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Transition(list,image.result,image.result_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);++completed_frames;return true;
    }
    void SubmittedAndWait(Host& h){
        if(!c)return;const auto value=c->timeline.Reserve(0);Require(value,"NR exact fence reservation");
        Hr(ID3D12CommandQueue_Signal(h.queue,c->fence,value),"NR host exact post-execution fence");
        Require(c->timeline.Submitted(0,value,created_this_frame),"NR actual Signal receipt");c->recorded=true;
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Hr(ID3D12Fence_SetEventOnCompletion(c->fence,value,event),"NR completion event");
        Require(WaitForSingleObject(event,30000)==WAIT_OBJECT_0,"NR exact completion");CloseHandle(event);
    }
    void Verify(Host& h){
        Require(c&&completed_frames==6,"six complete real NR chain Z producers");
        std::vector<char> previous;
        for(unsigned i=0;i<c->passes;++i){auto& l=c->layers[i];
            const auto input=Bytes(h,l.binding.proxy,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),output=Bytes(h,l.binding.neural,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            Require(input!=output,"each real model changes its own input");if(i)Require(input==previous,"full previous NR answer, not only last residual, feeds next layer");
            previous=output;std::ofstream f("real-nr-layer-"+std::to_string(i+1)+".fp16",std::ios::binary);f.write(output.data(),output.size());
            for(unsigned j=0;j<i;++j)Require(l.context.nr_handle!=c->layers[j].context.nr_handle&&l.context.params!=c->layers[j].context.params,"independent real Feature18 identities");
        }
        Require(present::RetireProof(c),"all NR exact fences drained");
    }
    void Destroy(){Require(present::Release(c)&&!c,"real signed NR release after host recording ended and all fences");}
};

ID3D12Resource* Texture(Host& h,unsigned w,unsigned height,DXGI_FORMAT format){
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=height;
    d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Format=format;
    ID3D12Resource* r=nullptr;Hr(ID3D12Device_CreateCommittedResource(h.device,&heap,D3D12_HEAP_FLAG_NONE,&d,
        D3D12_RESOURCE_STATE_COPY_SOURCE,nullptr,IID_ID3D12Resource,(void**)&r),"flow snapshot");h.owned.push_back(r);return r;
}
void Upload(Host& h,ID3D12Resource* r,const void* pixels,unsigned bpp,D3D12_RESOURCE_STATES state){
    auto d=ID3D12Resource_GetDesc(r);D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 size=0;
    ID3D12Device_GetCopyableFootprints(h.device,&d,0,1,0,&fp,nullptr,nullptr,&size);auto* upload=h.Buffer(size,D3D12_HEAP_TYPE_UPLOAD);
    char* mapped=nullptr;Hr(ID3D12Resource_Map(upload,0,nullptr,(void**)&mapped),"input upload");
    for(unsigned y=0;y<d.Height;++y)std::memcpy(mapped+y*fp.Footprint.RowPitch,(const char*)pixels+y*d.Width*bpp,d.Width*bpp);
    ID3D12Resource_Unmap(upload,0,nullptr);h.Begin();h.Barrier(r,state,D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION to{},from{};to.pResource=r;to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    from.pResource=upload;from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=fp;
    ID3D12GraphicsCommandList_CopyTextureRegion(h.list,&to,0,0,0,&from,nullptr);h.Barrier(r,D3D12_RESOURCE_STATE_COPY_DEST,state);h.Submit();h.Wait();
}
float texture_noise(int x,int y,int scale){
    const unsigned px=unsigned(x+65536),py=unsigned(y+65536),ix=px/scale,iy=py/scale;
    auto hash=[](unsigned a,unsigned b){unsigned z=a*0x9e3779b9u^b*0x85ebca6bu;z^=z>>16;z*=0x7feb352du;z^=z>>15;z*=0x846ca68bu;z^=z>>16;return float(z&255u);};
    const float fx=float(px%scale)/scale,fy=float(py%scale)/scale;
    return (hash(ix,iy)*(1-fx)+hash(ix+1,iy)*fx)*(1-fy)+(hash(ix,iy+1)*(1-fx)+hash(ix+1,iy+1)*fx)*fy;
}
unsigned short HalfPositive(float v){if(v==0)return 0;int e=0;std::frexp(v,&e);--e;return (e+15)*1024+unsigned(std::nearbyint(std::ldexp(v,10-e)))-1024;}
int Run(const wchar_t* bridge,bool flow_mode,bool external_mode){
    using namespace nr::final_residual;InitializeCriticalSection(&g_chain_cs);Host h;
    GetCurrentDirectoryW(MAX_PATH,g_log_path);wcscat_s(g_log_path,L"\\model-flow.log");
    auto dll=LoadLibraryW(bridge);Require(dll,"bootstrap library");
    auto bootstrap=reinterpret_cast<unsigned(*)(ID3D12Device*)>(GetProcAddress(dll,"NRChain_Bootstrap"));Require(bootstrap,"bootstrap export");Ngx(bootstrap(h.device),"sole bootstrap");
    Require(EnsureNrApi(h.device),"real Core NR API");Require(EnsureCodec(h.device),"real Core codec");
    constexpr unsigned w=512,height=384,nw=256,nh=192;
    auto* snapshot=Texture(h,w,height,DXGI_FORMAT_R8G8B8A8_UNORM);
    auto* base=h.Plane(w,height,DXGI_FORMAT_R16G16B16A16_FLOAT,false);
    auto* final=h.Plane(w,height,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
    auto* depth=h.Plane(nw,nh,DXGI_FORMAT_R32_FLOAT,false);
    auto* motion=h.Plane(nw,nh,DXGI_FORMAT_R32G32_FLOAT,false);
    NrGuideBinding guide{};guide.depth=depth;guide.motion=motion;auto& sig=guide.signature;
    sig.depth_width=sig.motion_source_width=sig.motion_bound_width=sig.depth_physical_width=sig.motion_physical_width=sig.color_physical_width=nw;
    sig.depth_height=sig.motion_source_height=sig.motion_bound_height=sig.depth_physical_height=sig.motion_physical_height=sig.color_physical_height=nh;
    sig.depth_format=DXGI_FORMAT_R32_FLOAT;sig.motion_format=DXGI_FORMAT_R32G32_FLOAT;sig.color_format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    sig.motion_scale_x=sig.motion_scale_y=1;guide.valid_motion_scale_x=guide.valid_motion_scale_y=true;
    sig.depth_inverted=0;sig.normalized_create_flags=1;guide.game_reset=0;
    RealNrChain chain;chain.Build(h,1,nw,nh,w,height,guide);
    AmdOfProducer producer(h.device,h.queue,w,height);Require(producer.initialized(),producer.status());HistoryContract history;
    std::unique_ptr<RecordedTemporal> external;
    if(external_mode){
        external=std::make_unique<RecordedTemporal>(h.device,h.queue,w,height,Backend::AmdOpticalFlow,Scope::WholeChain,512ull<<20);
        Require(external->initialized(),"actual external accumulator");
    }
    Id external_recording=0;unsigned external_submissions=0;
    unsigned usable=0;for(unsigned seq=1;seq<=16;++seq){
        std::vector<unsigned char> rgba(w*height*4);std::vector<unsigned short> fp16(w*height*4);
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<w;++x){
            const float xx=float(int(x)-int(seq-1)*4),yy=float(int(y)-int(seq-1)*2);
            float v=(.60f*texture_noise(int(xx),int(yy),8)+.25f*texture_noise(int(xx),int(yy),32)+.15f*texture_noise(int(xx),int(yy),2))/255.f;
            auto c=unsigned(std::clamp(v,0.f,1.f)*255+.5f);for(unsigned k=0;k<4;++k){rgba[(y*w+x)*4+k]=k==3?255:c;fp16[(y*w+x)*4+k]=HalfPositive(k==3?1.f:c/255.f);}
        }
        Upload(h,snapshot,rgba.data(),4,D3D12_RESOURCE_STATE_COPY_SOURCE);Upload(h,base,fp16.data(),8,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Frame frame{{1,1,seq},{1,1,1,w,height,Backend::AmdOpticalFlow},seq*1000ull,false};auto plan=history.prepare(frame);Require(plan.work(),"flow source plan");
        Require(producer.dispatch(frame,plan,snapshot)==Submit::Submitted,"actual AMD flow dispatch");GpuFlow flow;Require(producer.poll(flow),"flow pending fence");
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Hr(ID3D12Fence_SetEventOnCompletion(flow.completion,flow.value,event),"flow completion");Require(WaitForSingleObject(event,30000)==WAIT_OBJECT_0,"exact flow wait");CloseHandle(event);
        Require(producer.poll(flow)&&flow.receipt.status==FlowStatus::Ready,"completed actual flow");history.observe_flow(plan.token,flow.receipt);
        auto raw=Bytes(h,flow.vectors,D3D12_RESOURCE_STATE_COPY_SOURCE);std::vector<float> vectors(nw*nh*2,0.f);double epe=0;unsigned measured=0;
        if(flow.receipt.motion_usable){++usable;for(unsigned y=0;y<nh;++y)for(unsigned x=0;x<nw;++x){float v[2];std::memcpy(v,raw.data()+((2*y+1)*w+2*x+1)*8,8);
            Require(std::isfinite(v[0])&&std::isfinite(v[1]),"finite same-source flow");if(x>32&&y>32&&x<nw-32&&y<nh-32){epe+=std::hypot(v[0]+4,v[1]+2);++measured;}
            if(flow_mode){vectors[(y*nw+x)*2]=v[0]*float(nw)/w;vectors[(y*nw+x)*2+1]=v[1]*float(nh)/height;}}}
        Upload(h,motion,vectors.data(),8,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);guide.game_reset=seq==1?1:0;
        h.Begin();
        // The actual Host::Begin Reset ended the previous borrowed recording.
        if(external&&external_recording){external->end_recording(external_recording);external_recording=0;}
        const bool ready=chain.Record(h,base,final,guide);
        if(ready&&external){
            RecordedInputs in;
            in.frame={{33,1,seq},{1,1,kResidualLinearScene,w,height,Backend::AmdOpticalFlow,Scope::WholeChain,{0,0,w,height,0,1}},seq*16667ull,false};
            in.frame.source_to_scene=1;in.settings.enabled=true;
            in.original=base;in.original_state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            in.candidate=final;in.candidate_state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;in.candidate_format=DXGI_FORMAT_R16G16B16A16_FLOAT;
            Require(external->record(h.list,in)==RecordResult::Recorded,"actual external-only recording after unchanged NR");
            external_recording=seq;
        }
        h.Submit();h.Wait();chain.SubmittedAndWait(h);
        if(external&&external_recording){
            Require(external->submitted(h.fence,h.value,external_recording),"external actual consumer submission");
            ++external_submissions;
        }
        Require(producer.release_output(plan.token,h.fence,h.value),"all real model readers completed before flow release");
        Require(history.finish(plan.token,plan.history_generation,ResidualStatus::SubmittedAdopted)==Completion::Adopted,"experiment source/model-output adoption");
        if(ready){auto pixels=Bytes(h,final,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);std::ofstream f("frame-"+std::to_string(seq)+".fp16",std::ios::binary);f.write(pixels.data(),pixels.size());}
        int actual_reset=-1;if(ready)Require(GetInt(chain.c->layers[0].evaluation_params[0],"DLSSNR.Reset",&actual_reset),"read actual model Reset");
        std::ofstream flow_dump("flow-"+std::to_string(seq)+".rg32",std::ios::binary);flow_dump.write(raw.data(),raw.size());
        std::printf("FLOW source=%u usable=%d mode=%s epe=%.6f ready=%d model_reset=%d age=%u scd_bits=%u scd_score=%.8f\n",seq,flow.receipt.motion_usable,external_mode?"external":flow_mode?"amd":"zero",measured?epe/measured:0,ready,actual_reset,flow.frames_since_reset,flow.scene_history_bits,flow.scene_change);std::fflush(stdout);
    }
    Require(usable>0,"actual AMD usable frames exercised");h.Begin();
    if(external&&external_recording){external->end_recording(external_recording);external_recording=0;}
    Hr(ID3D12GraphicsCommandList_Close(h.list),"end replayable test recording");
    if(external){
        Require(external_submissions==15&&external->releasable(),"external-only exact completed recordings retire");
        std::printf("EXTERNAL submissions=%u bytes=%llu; same unchanged model MV and NR workload\n",external_submissions,external->stats().bytes);
        external.reset();
    }
    chain.Destroy();
    std::printf("PASS real model flow experiment usable=%u; quality assessed separately, no privateSR/game; external group is a separate consumer\n",usable);std::fflush(nullptr);ExitProcess(0);
}
}
int wmain(int argc,wchar_t**argv){if(argc!=3)return 2;try{return flow_model::Run(argv[1],std::wcscmp(argv[2],L"amd")==0,std::wcscmp(argv[2],L"external")==0);}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());std::fflush(nullptr);ExitProcess(1);}}
