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

// Cost-only harness: common CPU scene preparation and common upload are excluded;
// every temporal-specific GPU copy/AMD conversion/dispatch/readback-copy is included.
int Cost(const wchar_t* bridge,unsigned mode,unsigned layers,unsigned w,unsigned height){
 using namespace nr::final_residual;InitializeCriticalSection(&g_chain_cs);Host h;
 GetCurrentDirectoryW(MAX_PATH,g_log_path);wcscat_s(g_log_path,L"\\cost.log");
 auto dll=LoadLibraryW(bridge);Require(dll,"bootstrap library");
 auto bootstrap=reinterpret_cast<unsigned(*)(ID3D12Device*)>(GetProcAddress(dll,"NRChain_Bootstrap"));Require(bootstrap,"bootstrap export");Ngx(bootstrap(h.device),"sole bootstrap");
 Require(w>=128&&height>=128&&w<=4096&&height<=4096&&w%2==0&&height%2==0,"bounded even output extent");
 const unsigned nw=w/2,nh=height/2;
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
 RealNrChain chain;chain.Build(h,layers,nw,nh,w,height,guide);
 const auto backend=mode==1?Backend::NativeMotion:Backend::AmdOpticalFlow;
 std::unique_ptr<RecordedTemporal> temporal;
 if(mode){temporal=std::make_unique<RecordedTemporal>(h.device,h.queue,w,height,backend,Scope::WholeChain,1024ull<<20);Require(temporal->initialized(),temporal->status());}
 struct GuideCopy{ID3D12Resource* texture=nullptr;ID3D12Resource* upload=nullptr;D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};};
 GuideCopy extra[2];UINT64 extra_bytes=0;
 if(mode==1)for(unsigned k=0;k<2;++k){auto& e=extra[k];e.texture=h.Plane(nw,nh,k?DXGI_FORMAT_R32G32_FLOAT:DXGI_FORMAT_R32_FLOAT,false);
  const auto d=ID3D12Resource_GetDesc(e.texture);UINT64 bytes=0;ID3D12Device_GetCopyableFootprints(h.device,&d,0,1,0,&e.fp,nullptr,nullptr,&bytes);
  e.upload=h.Buffer(bytes,D3D12_HEAP_TYPE_UPLOAD);extra_bytes+=bytes+ID3D12Device_GetResourceAllocationInfo(h.device,0,1,&d).SizeInBytes;
  char* ptr=nullptr;Hr(ID3D12Resource_Map(e.upload,0,nullptr,(void**)&ptr),"native input staging");
  for(unsigned y=0;y<nh;++y)for(unsigned x=0;x<nw;++x){const float v[2]={k?-2.f:.5f,-1.f};std::memcpy(ptr+y*e.fp.Footprint.RowPitch+x*(k?8:4),v,k?8:4);}ID3D12Resource_Unmap(e.upload,0,nullptr);
 }
 D3D12_QUERY_HEAP_DESC qd{};qd.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;qd.Count=3;ID3D12QueryHeap* queries=nullptr;
 Hr(ID3D12Device_CreateQueryHeap(h.device,&qd,IID_ID3D12QueryHeap,(void**)&queries),"cost timestamps");auto* times=h.Buffer(24,D3D12_HEAP_TYPE_READBACK);
 UINT64 frequency=0;Hr(ID3D12CommandQueue_GetTimestampFrequency(h.queue,&frequency),"timestamp frequency");Id lease=0;unsigned produced=0;
 for(unsigned seq=1;produced<40&&seq<50;++seq){
  std::vector<unsigned short> pixels(w*height*4);
  for(unsigned y=0;y<height;++y)for(unsigned x=0;x<w;++x){int xx=int(x)-int(seq-1)*4,yy=int(y)-int(seq-1)*2;
   float v=(.60f*texture_noise(xx,yy,8)+.25f*texture_noise(xx,yy,32)+.15f*texture_noise(xx,yy,2))/255.f;
   for(unsigned c=0;c<4;++c)pixels[(y*w+x)*4+c]=HalfPositive(c==3?1.f:std::clamp(v,0.f,1.f));}
  Upload(h,base,pixels.data(),8,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  // Common upload completed; do not retain forty full-resolution CPU upload buffers.
  ReleaseCom(h.owned.back());h.owned.pop_back();guide.game_reset=seq==1?1:0;
  h.Begin();if(temporal&&lease){temporal->end_recording(lease);lease=0;}
  ID3D12GraphicsCommandList_EndQuery(h.list,queries,D3D12_QUERY_TYPE_TIMESTAMP,0);
  const bool ready=chain.Record(h,base,final,guide);
  ID3D12GraphicsCommandList_EndQuery(h.list,queries,D3D12_QUERY_TYPE_TIMESTAMP,1);
  if(ready&&temporal){
   if(mode==1)for(auto& e:extra){h.Barrier(e.texture,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION to{},from{};to.pResource=e.texture;to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    from.pResource=e.upload;from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=e.fp;
    ID3D12GraphicsCommandList_CopyTextureRegion(h.list,&to,0,0,0,&from,nullptr);h.Barrier(e.texture,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);}
   RecordedInputs in;in.settings.enabled=true;in.settings.mode=mode==3?Mode::LowFrequency:Mode::Adaptive;
   in.frame={{33,1,seq},{1,Id(1)<<32,kResidualLinearScene,w,height,backend},seq*16667ull,false};
   in.original=base;in.original_state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
   in.candidate=final;in.candidate_state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;in.candidate_format=DXGI_FORMAT_R16G16B16A16_FLOAT;
   if(mode==1){in.native={in.frame.source,extra[1].texture,DXGI_FORMAT_R32G32_FLOAT,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,0,0,nw,nh,2,2,true};
    in.native.depth=extra[0].texture;in.native.depth_source=in.frame.source;in.native.depth_phase_proven=true;}
   Require(temporal->record(h.list,in)==RecordResult::Recorded,temporal->status());lease=seq;
  }
  ID3D12GraphicsCommandList_EndQuery(h.list,queries,D3D12_QUERY_TYPE_TIMESTAMP,2);
  ID3D12GraphicsCommandList_ResolveQueryData(h.list,queries,D3D12_QUERY_TYPE_TIMESTAMP,0,3,times,0);
  h.Submit();h.Wait();chain.SubmittedAndWait(h);
  if(lease)Require(temporal->submitted(h.fence,h.value,lease),"cost temporal exact completion");
  if(ready){++produced;UINT64* t=nullptr;Hr(ID3D12Resource_Map(times,0,nullptr,(void**)&t),"cost timestamps read");
   double nr=(t[1]-t[0])*1000./frequency,added=(t[2]-t[1])*1000./frequency;ID3D12Resource_Unmap(times,0,nullptr);
   std::printf("COST mode=%u layers=%u produced=%u warmup=%u nr_ms=%.9f added_ms=%.9f total_ms=%.9f temporal_bytes=%llu\n",mode,layers,produced,produced<=8,nr,added,nr+added,temporal?temporal->stats().bytes+extra_bytes:0);}
 }
 Require(produced==40,"8 warmup plus32 stable frames");auto capture=Bytes(h,final,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
 std::ofstream f("final.fp16",std::ios::binary);f.write(capture.data(),capture.size());f.close();h.Begin();if(lease)temporal->end_recording(lease);
 Hr(ID3D12GraphicsCommandList_Close(h.list),"cost final recording reset");if(temporal)Require(temporal->releasable(),"cost temporal retirement");temporal.reset();chain.Destroy();ReleaseCom(queries);
 std::fflush(nullptr);ExitProcess(0);
}
}
int wmain(int argc,wchar_t**argv){if(argc!=6)return 2;try{return flow_model::Cost(argv[1],unsigned(_wtoi(argv[2])),unsigned(_wtoi(argv[3])),unsigned(_wtoi(argv[4])),unsigned(_wtoi(argv[5])));}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());std::fflush(nullptr);ExitProcess(1);}}
