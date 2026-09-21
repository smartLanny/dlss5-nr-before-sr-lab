// Whole production entry points with real D3D12/AMD recording and completion.
// Only the opaque NGX model is substituted: it writes known nonzero FP16 edits.
// These constructed source frames are NOT native game SR/RR or NVIDIA inference.
#include <map>
namespace temporal_route_gpu {
using queue_test::Check;using queue_test::Hr;
struct Parameters:NgxParams {
    std::map<std::string,double> numbers;
    std::map<std::string,void*> pointers;
    inline static void* table[16]{};
    static Parameters& Of(const NgxParams* p){return *static_cast<Parameters*>(const_cast<NgxParams*>(p));}
    template<class T> static NgxResult Number(const NgxParams*p,const char*k,T*v){auto&n=Of(p).numbers;auto i=n.find(k);if(i==n.end())return 0xBAD00005u;*v=T(i->second);return kNgxSuccess;}
    template<class T> static void Put(NgxParams*p,const char*k,T v){Of(p).numbers[k]=double(v);}
    static NgxResult Pointer(const NgxParams*p,const char*k,void**v){auto&m=Of(p).pointers;auto i=m.find(k);if(i==m.end())return 0xBAD00005u;*v=i->second;return kNgxSuccess;}
    static void PutPointer(NgxParams*p,const char*k,void*v){Of(p).pointers[k]=v;}
    Parameters(){
        table[0]=table[1]=reinterpret_cast<void*>(&PutPointer);
        table[3]=reinterpret_cast<void*>(&Put<int>);table[4]=reinterpret_cast<void*>(&Put<unsigned>);table[6]=reinterpret_cast<void*>(&Put<float>);
        table[8]=table[9]=reinterpret_cast<void*>(&Pointer);table[11]=reinterpret_cast<void*>(&Number<int>);
        table[12]=reinterpret_cast<void*>(&Number<unsigned>);table[14]=reinterpret_cast<void*>(&Number<float>);vtable=table;
    }
};
std::vector<std::unique_ptr<post_output_test::ClearViews>> model_views;
ID3D12Device* device=nullptr;unsigned model_calls=0;
NgxResult Allocate(NgxParams**p){*p=new Parameters;return kNgxSuccess;}
NgxResult Destroy(NgxParams*p){delete static_cast<Parameters*>(p);return kNgxSuccess;}
NgxResult Create(void*,ID3D12GraphicsCommandList*,int feature,void*,void**out){Check(feature==18,"only model seam is synthetic");*out=new NgxHandle{900+model_calls};return kNgxSuccess;}
NgxResult Release(void*,void*p){delete static_cast<NgxHandle*>(p);return kNgxSuccess;}
NgxResult Evaluate(void*,ID3D12GraphicsCommandList*l,const void*,const void*raw,void*){
    ++model_calls;ID3D12Resource*r=nullptr;Check(GetResource(static_cast<const NgxParams*>(raw),"DLSSNR.Output",&r)&&r,"real production bound model output");
    model_views.push_back(std::make_unique<post_output_test::ClearViews>(device));
    const float edited[]={.55f,.5f,.45f,1};model_views.back()->Clear(device,l,r,0,edited);return kNgxSuccess;
}
HRESULT STDMETHODCALLTYPE Reset(ID3D12GraphicsCommandList*l,ID3D12CommandAllocator*a,ID3D12PipelineState*p){return ID3D12GraphicsCommandList_Reset(l,a,p);}
void Run(const char* scenario){
    const std::string choice=scenario;const bool pre=choice.rfind("pre",0)==0;
    const bool off=choice.find("off")!=std::string::npos,hdr=choice.find("hdr")!=std::string::npos;
    const bool native=choice.find("native")!=std::string::npos,later=choice.find("later")!=std::string::npos;
    const bool scaled=choice.find("scale")!=std::string::npos;
    const bool reject=hdr||native||(pre&&later);
    queue_test::Fixture f;device=f.device;g_original_command_list_reset[0]=Reset;
    g_config=ChainConfig{};g_config.enabled=true;g_config.mode=pre?2:1;
    g_config.processing_start=pre?nr_processing::Start::Before:nr_processing::Start::After;g_config.processing_selection=1;
    g_config.transfer_strength=g_config.post_transfer_strength=1;g_config.color_strength=.7f;g_config.warmup_after_create=0;
    g_config.suppress_reset_frame=false; // Explicit fixture setting; exercise the compositor on the cold frame.
    g_config.preserve_lighting=false;g_config.lighting_lock=0;g_config.temporal_backend=off?0:native?1:2;g_config.temporal_scope=later?1:0;
    if(later){nr_layers::Migrate(g_config.nr_layers,2,nr_layers::Primary(g_config));NormalizeConfig(g_config);}
    if(scaled){g_config.work_mode=5;g_config.custom_work_scale=.75f;}
    nr_processing_runtime::Reconcile();g_nr_api.initialized=true;g_nr_api.allocate=Allocate;g_nr_api.destroy=Destroy;
    g_nr_api.bridge_create=Create;g_nr_api.bridge_evaluate=Evaluate;g_nr_api.bridge_release=Release;
    Check(EnsureCodec(device),"actual production shaders");
    constexpr unsigned w=128,h=128;ID3D12Resource *color=nullptr,*output=nullptr,*depth=nullptr,*motion=nullptr;
    Check(CreateTexture(device,w,h,L"source B",&color)&&CreateTexture(device,w,h,L"native output",&output)&&
        CreateTexture(device,w,h,L"native depth",&depth,DXGI_FORMAT_R32_FLOAT)&&CreateTexture(device,w,h,L"native motion",&motion,DXGI_FORMAT_R16G16_FLOAT),"real typed producer surfaces");
    D3D12_RESOURCE_STATES cs=D3D12_RESOURCE_STATE_COMMON,os=cs,ds=cs,ms=cs;
    {queue_test::List init(f);post_output_test::ClearViews c(device),d(device);const float b[]={.25f,.375f,.5f,1},one[]={1,1,1,1},zero[]={0,0,0,0};
        Transition(init.native,color,cs,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);c.Clear(device,init.native,color,0,b);
        Transition(init.native,output,os,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);c.Clear(device,init.native,output,1,b);
        Transition(init.native,depth,ds,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);c.Clear(device,init.native,depth,2,one);
        Transition(init.native,motion,ms,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);d.Clear(device,init.native,motion,0,zero);
        Transition(init.native,color,cs,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);Transition(init.native,depth,ds,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(init.native,motion,ms,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);post_output_test::Submit(f,init);}
    const auto original=post_output_test::ReadTexture(f,color,cs);
    NgxHandle handle{501};FeatureRecord feature{};feature.handle=&handle;feature.feature_id=pre?1:13;feature.generation=551;
    feature.width=feature.out_width=w;feature.height=feature.out_height=h;feature.create_flags_known=true;feature.create_contract_known=true;feature.create_flags=hdr?1:0;feature.rr_depth_type=1;
    Parameters params;SetResource(&params,"Color",color);SetResource(&params,"Output",output);SetResource(&params,"Depth",depth);SetResource(&params,"MotionVectors",motion);
    SetUInt(&params,"DLSS.Render.Subrect.Dimensions.Width",w);SetUInt(&params,"DLSS.Render.Subrect.Dimensions.Height",h);
    SetFloat(&params,"MV.Scale.X",1);SetFloat(&params,"MV.Scale.Y",1);SetFloat(&params,"Jitter.Offset.X",0);SetFloat(&params,"Jitter.Offset.Y",0);
    SetFloat(&params,"PreExposure",1);SetFloat(&params,"ExposureScale",1);SetInt(&params,"Reset",0);
    queue_test::List list(f);list.Close();list.Prime(f.a);list.Prime(f.a);f.Drain(f.a);list.ResetWithNewAllocator(f);
    NrContext* context=nullptr;NrSlot* slot=nullptr;unsigned expected_recordings=0;
    const unsigned frames=off||reject?1:9;
    for(unsigned frame=1;frame<=frames;++frame){
        nr_source_runtime::StreamlineScope token(nr_source_frame::Source{41,51,frame});
        nr_source_runtime::EvaluateScope source(list.native,&handle,&params,feature);
        Check(bool(nr_source_runtime::CurrentSource()),"production source-frame scope");
        if(pre){auto sub=PrepareBeforeSr(list.native,&handle,&params,feature);slot=sub.timing_slot;
            if(!sub.active)std::printf("pre declined bypass=%u temporal=%s\n",unsigned(g_last_bypass_reason.load()),nr_temporal_status::Name(nr_temporal_status::state.load()));
            Check(sub.active&&!sub.fatal_recording,"actual PrepareBeforeSr reaches final candidate");
            ID3D12Resource* replacement=nullptr;GetResource(&params,"Color",&replacement);Check(replacement==slot->result,"native SR receives temporal candidate");RestoreSubstitution(sub);
        }else{auto sub=PrepareAfterSr(list.native,&handle,&params,feature);
            if(!sub.active)std::printf("post declined temporal=%s\n",nr_temporal_status::Name(nr_temporal_status::state.load()));
            Check(sub.active,"actual RR post preparation");slot=sub.slot;context=sub.context;
            // Construct the successful native RR output on the borrowed list.
            model_views.push_back(std::make_unique<post_output_test::ClearViews>(device));const float b[]={.25f,.375f,.5f,1};model_views.back()->Clear(device,list.native,output,0,b);
            const bool finished=FinishAfterSr(list.native,&params,feature,sub);
            if(!finished)std::printf("RR finish bypass=%u fatal=%x depth=%s temporal=%s modelCalls=%u\n",unsigned(g_last_bypass_reason.load()),sub.fatal_recording,
                RrDepthRejectName(g_rr_depth_last_reject.load()),nr_temporal_status::Name(nr_temporal_status::state.load()),model_calls);
            Check(finished&&!sub.fatal_recording,"actual FinishAfterSr RR consumer");RestorePostSrParameters(sub);
        }
        if(pre)for(auto&c:g_contexts)for(auto&s:c.slots)if(&s==slot)context=&c;
        Check(context!=nullptr,"actual production context owner");
        const auto recorded=nr_temporal_status::recorded.load();
        std::printf("frame=%u route=%s state=%s recorded=%llu work=%ux%u codec=%u sourceFmt=%u flagsKnown=%u temporal=%s\n",
            frame,scenario,nr_temporal_status::Name(nr_temporal_status::state.load()),recorded,context->width,context->height,
            unsigned(slot->codec_color_mode),unsigned(ID3D12Resource_GetDesc(slot->source).Format),feature.create_flags_known?1:0,
            context->temporal?context->temporal->status():"none");
        // Cold queue training predates its first actual NR retirement fence.
        // The first entry must decline, then its real submission creates the
        // queue/fence owner. Do not invent an adoption receipt to prime it.
        if(!off&&!reject&&frame==1)Check(nr_temporal_status::state.load()==2&&!context->temporal,
            "cold route waits for actual NR queue/fence establishment");
        if(!off&&!reject&&frame>1){
            if(later&&!slot->dual_selected)Check(frame==2&&nr_temporal_status::state.load()==4,
                "RR later scope waits for actual auxiliary Create submission before P1 exists");
            else ++expected_recordings;
        }
        Check(recorded==expected_recordings,"entry records only supported enabled source frames");
        if(off||reject)Check(!context->temporal&&nr_temporal_status::bytes.load()==0,"OFF/refusal allocates no temporal history");
        source.Finish(kNgxSuccess);list.Close();list.Submit(f.a);f.Drain(f.a);
        Check(nr_temporal_status::submitted.load()==recorded,"actual Execute Signal receipt adopts each temporal recording");
        Check(!g_lifecycle_tracking_uncertain.load(),"actual queue remains proven");
        if(frame==frames){auto state=slot->result_state;Check(post_output_test::ReadTexture(f,slot->result,state)!=original,"nonzero NR candidate survives actual consumer");}
        ID3D12CommandAllocator*a=nullptr;Hr(ID3D12Device_CreateCommandAllocator(device,D3D12_COMMAND_LIST_TYPE_DIRECT,IID_ID3D12CommandAllocator,(void**)&a),"completed next allocator");
        Hr(ProcessCommandListReset(0,list.native,a,nullptr),"actual completed native Reset");ReleaseCom(a);model_views.clear();
    }
    list.Close();g_config.temporal_backend=0;nr_temporal::Maintain(*context);
    if(!off&&!reject)Check(expected_recordings==(later?7u:8u),"bounded staircase converges to every eligible source frame");
    Check(!context->temporal&&nr_temporal_status::bytes.load()==0,"OFF retires completed temporal owner");
    std::printf("PASS production temporal route %s checks=%u modelStubCalls=%u recorded=%llu submitted=%llu; WARP, synthetic model/native frames, no game or NGX inference\n",
        scenario,queue_test::checks,model_calls,nr_temporal_status::recorded.load(),nr_temporal_status::submitted.load());
}
}
