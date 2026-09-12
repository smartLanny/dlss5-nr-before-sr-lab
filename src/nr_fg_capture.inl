// Include after nr_fg_consumer.inl. The caller holds the Core chain lock during
// Start/Finish/Submit/Invalidate. These recordings borrow the real application
// list; only its actual submission AND retirement can authorize export.
namespace nr_fg_capture {
std::atomic<unsigned long long> requested{0};
std::atomic<unsigned> status{0},workers{0},remaining{0}; // idle, pending, complete, failed
unsigned long long consumed=0;
struct Identity {
    unsigned long long request=0,root=0,instance=0,source_stream=0,source_epoch=0,source_sequence=0;
    unsigned count=0,layer=0,color_format=0,color_encoding=0;
    bool hudless=false,full=false;
};
struct Layer {nr_replay::Recorder recorder;Identity identity{};unsigned index=nr_replay::Recorder::Invalid;};
Layer layers[4]{};
struct Attempt {
    unsigned long long request=0,token=0,root=0;
    nr_source_frame::Source source{};
    unsigned constants[64]{},constant_count=0,flags=0,pass=0;
};
thread_local Attempt attempt{};
bool Empty(){for(auto& layer:layers)if(!layer.recorder.Empty())return false;return true;}
bool RequestCapture(){
    if(!TryEnterCriticalSection(&g_chain_cs))return false;
    struct Unlock{~Unlock(){LeaveCriticalSection(&g_chain_cs);}} unlock;
    if(!Empty())return false;
    if(status.load(std::memory_order_acquire)==1||workers.load(std::memory_order_acquire))return false;
    const auto old=requested.load(std::memory_order_relaxed);if(old>=(UINT64_MAX>>3))return false;
    requested.store(old+1,std::memory_order_release);status.store(1,std::memory_order_release);return true;
}
void Start(ID3D12GraphicsCommandList* list,const nr_source_frame::Source& source,NrContext& primary){
    attempt={};const auto request=requested.load(std::memory_order_acquire);
    if(!request||request==consumed||!source||!list||!Empty()||workers.load()||
       !primary.nr_handle||primary.retired||primary.failed||
       primary.successful_evaluations_since_reset<=g_config.warmup_after_create)return;
    unsigned long long token=0;
    if(!ReadLifecycleToken(list,token)||!token||token!=primary.pending_recording_token){status.store(3);consumed=request;return;}
    attempt.request=request;attempt.token=token;attempt.root=primary.instance_id;attempt.source=source;
}
void Composition(const void* constants,unsigned count,unsigned flags,unsigned pass){
    if(!attempt.request||!constants||!count||count>64||pass>2)return;
    std::memcpy(attempt.constants,constants,count*4u);attempt.constant_count=count;attempt.flags=flags;attempt.pass=pass;
}
void Submit(std::uint64_t token,void* queue,std::uint64_t fence){for(auto& layer:layers)layer.recorder.Submit(token,queue,fence);}
void Invalidate(std::uint64_t token,bool unsubmitted){for(auto& layer:layers)layer.recorder.Invalidate(token,unsubmitted);}
void Failed(){status.store(3,std::memory_order_release);if(attempt.request)consumed=attempt.request;attempt={};}
void Waiting(){attempt={};status.store(1,std::memory_order_release);}

#ifndef NR_FG_CAPTURE_TEST_HANDOFF
struct Job {nr_replay::ExportJob* frame=nullptr;Identity identity{};HMODULE module=nullptr;};
bool WriteMetadata(const wchar_t* directory,const Identity& id,const nr_replay::Header& header){
    wchar_t path[1024]{};if(swprintf_s(path,L"%ls\\layer-%u.json",directory,id.layer)<0)return false;
    char json[4096]{};const int length=std::snprintf(json,sizeof(json),
        "{\n\"schema\":\"pre-fg-nr-observed-layer-v1\",\"complete\":true,\n"
        "\"scope\":\"one warm observed source before the original FG consumer; not final screen output, native SR output, or cold-history replay\",\n"
        "\"request\":%llu,\"source\":{\"stream\":%llu,\"epoch\":%llu,\"sequence\":%llu},\n"
        "\"root_instance\":%llu,\"layer_instance\":%llu,\"layer\":%u,\"requested_layers\":%u,\"full_reference\":%s,\n"
        "\"input_plane\":\"%s\",\"native_color_format\":%u,\"native_color_encoding\":%u,\n"
        "\"source_extent\":[%u,%u],\"work_extent\":[%u,%u],\"evaluation\":%u,\n"
        "\"roles\":{\"Source\":\"original canonical input of this pre-FG source\",\"Proxy\":\"this layer actual model input\",\"Neural\":\"this layer actual model output\",\"Composite\":\"final canonical scene composition before FG and before HUD reconstruction\",\"Depth\":\"actual bound depth\",\"Motion\":\"actual bound motion\"},\n"
        "\"parameters\":\"each layer actual bound parameter snapshot in its NRF header\",\"composition\":\"actual final codec constants in NRF header; capture performs no inference\",\n"
        "\"completion\":\"actual application list submission, exact queue fence, and recording retirement\",\"timing\":\"capture source excluded from stable timing\"\n}\n",
        id.request,id.source_stream,id.source_epoch,id.source_sequence,id.root,id.instance,id.layer,id.count,id.full?"true":"false",
        id.hudless?"HUDless scene":"application backbuffer",id.color_format,id.color_encoding,
        header.source_width,header.source_height,header.work_width,header.work_height,header.evaluation);
    if(length<=0||static_cast<std::size_t>(length)>=sizeof(json))return false;
    HANDLE file=CreateFileW(path,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;const bool ok=nr_replay::WriteBytes(file,json,static_cast<unsigned>(length));CloseHandle(file);return ok;
}
bool WriteCompletion(const wchar_t* directory,const Identity& id,bool complete){
    if(!directory[0])return false;wchar_t path[1024]{};if(swprintf_s(path,L"%ls\\capture.json",directory)<0)return false;
    char json[768]{};const auto length=std::snprintf(json,sizeof(json),
        "{\"schema\":\"pre-fg-nr-observed-capture-v1\",\"complete\":%s,\"request\":%llu,\"layers\":%u,"
        "\"source\":{\"stream\":%llu,\"epoch\":%llu,\"sequence\":%llu},\"scope\":\"pre-FG canonical scene; not final screen output\"}\n",
        complete?"true":"false",id.request,id.count,id.source_stream,id.source_epoch,id.source_sequence);
    if(length<=0||static_cast<std::size_t>(length)>=sizeof(json))return false;
    HANDLE file=CreateFileW(path,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;const bool ok=nr_replay::WriteBytes(file,json,static_cast<unsigned>(length));CloseHandle(file);return ok;
}
DWORD WINAPI Export(void* pointer){
    auto* job=static_cast<Job*>(pointer);auto* frame=job->frame;const auto id=job->identity;
    bool ok=frame&&frame->readback&&frame->header.completion==static_cast<unsigned>(nr_replay::Completion::Complete);
    wchar_t documents[768]{},parent[1024]{},directory[1024]{},path[1024]{};
    if(ok)ok=SUCCEEDED(SHGetFolderPathW(nullptr,CSIDL_PERSONAL,nullptr,SHGFP_TYPE_CURRENT,documents))&&
        swprintf_s(parent,L"%ls\\DLSS5-FG-Capture",documents)>=0;
    if(ok&&!CreateDirectoryW(parent,nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)ok=false;
    if(ok)ok=swprintf_s(directory,L"%ls\\%lu-%llu",parent,GetCurrentProcessId(),id.request)>=0;
    if(ok&&!CreateDirectoryW(directory,nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)ok=false;
    if(ok)ok=swprintf_s(path,L"%ls\\layer-%u.nrf",directory,id.layer)>=0;
    HANDLE file=ok?CreateFileW(path,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr):INVALID_HANDLE_VALUE;
    ok=ok&&file!=INVALID_HANDLE_VALUE;void* mapped=nullptr;const D3D12_RANGE range{0,static_cast<SIZE_T>(frame->bytes)};
    if(ok)ok=SUCCEEDED(ID3D12Resource_Map(frame->readback,0,&range,&mapped))&&mapped;
    if(ok)ok=nr_replay::WriteBytes(file,&frame->header,sizeof(frame->header))&&nr_replay::WriteBytes(file,mapped,frame->bytes);
    if(mapped){D3D12_RANGE none{};ID3D12Resource_Unmap(frame->readback,0,&none);}
    if(file!=INVALID_HANDLE_VALUE){if(!ok){frame->header.completion=static_cast<unsigned>(nr_replay::Completion::Invalidated);
        LARGE_INTEGER zero{};SetFilePointerEx(file,zero,nullptr,FILE_BEGIN);nr_replay::WriteBytes(file,&frame->header,sizeof(frame->header));SetEndOfFile(file);}CloseHandle(file);}
    if(ok)ok=WriteMetadata(directory,id,frame->header);
    if(!ok)status.store(3,std::memory_order_release);
    if(remaining.fetch_sub(1)==1){const bool complete=status.load()!=3;
        const bool manifest=WriteCompletion(directory,id,complete);status.store(complete&&manifest?2u:3u,std::memory_order_release);}
    Log("[fg-capture] %s request=%llu layer=%u/%u source=%llu:%llu:%llu; pre-FG, exact completion.",
        ok?"exported":"failed",id.request,id.layer,id.count,id.source_stream,id.source_epoch,id.source_sequence);
    ReleaseCom(frame->readback);if(frame->bytes)nr_replay::resident_bytes.fetch_sub(frame->bytes);delete frame;
    auto module=job->module;delete job;workers.fetch_sub(1);FreeLibraryAndExitThread(module,ok?0:1);
}
void Handoff(nr_replay::ExportJob* frame,const Identity& identity){
    auto* job=new(std::nothrow)Job{};
    if(job){job->frame=frame;job->identity=identity;
        if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&Export),&job->module)){
            workers.fetch_add(1);auto thread=CreateThread(nullptr,0,Export,job,0,nullptr);
            if(thread){CloseHandle(thread);return;}workers.fetch_sub(1);FreeLibrary(job->module);
        }delete job;
    }
    status.store(3);remaining.fetch_sub(1);ReleaseCom(frame->readback);
    if(frame->bytes)nr_replay::resident_bytes.fetch_sub(frame->bytes);delete frame;
}
#else
void Handoff(nr_replay::ExportJob*,const Identity&);
#endif
void Collect(){
    if(!TryEnterCriticalSection(&g_chain_cs))return;
    struct Unlock{~Unlock(){LeaveCriticalSection(&g_chain_cs);}} unlock;
    if(g_lifecycle_tracking_uncertain.load())return;
    for(auto& layer:layers)layer.recorder.Collect(
        [](void* pointer,std::uint64_t value){auto* queue=static_cast<LifecycleQueue*>(pointer);
            if(!queue||!queue->fence||!value)return false;const auto done=ID3D12Fence_GetCompletedValue(queue->fence);
            return done!=UINT64_MAX&&done>=value;
        },[&](nr_replay::ExportJob* frame){Handoff(frame,layer.identity);});
}

void Finish(ID3D12GraphicsCommandList* list,const nr_source_runtime::NativeInputs& input,
    nr_compat_present_runtime::Context& image,NrContext& primary,NrSlot& main,
    const NrGuideBinding& guides,const nr_dual_runtime::Prepared& prepared,bool changed){
    if(!attempt.request)return;
    unsigned long long token=0;
    if(!list||!(attempt.source==input.source)||attempt.root!=primary.instance_id||
       !ReadLifecycleToken(list,token)||token!=attempt.token||primary.pending_recording_token!=token){Failed();return;}
    if(!changed){
        if(primary.failed||primary.retired||primary.lifecycle_dependencies_quarantined||primary.create_dependencies_quarantined)Failed();
        else Waiting();
        return;
    }
    const unsigned count=g_config.nr_full_reference?g_config.nr_full_passes:(g_config.nr_passes==2?2u:1u);
    if(count<1||count>4||!image.device||!image.image.source||!image.image.result||!attempt.constant_count){Failed();return;}
    NrContext* contexts[]={&primary,nullptr,nullptr,nullptr};NrSlot* slots[]={&main,nullptr,nullptr,nullptr};
    const NrGuideBinding* bound_guides[]={&guides,nullptr,nullptr,nullptr};
    for(unsigned i=1;i<count;++i){const auto& stage=prepared.full?prepared.layers[i-1]:static_cast<const nr_dual_runtime::LayerPrepared&>(prepared);
        if(!stage.context||!stage.slot||!stage.evaluated||!stage.adoptable||
           (prepared.full&&(prepared.requested!=count||prepared.prepared!=count-1))){Waiting();return;}
        contexts[i]=stage.context;slots[i]=stage.slot;bound_guides[i]=&stage.guides;
    }
    if(count>1&&main.dual_selected!=slots[count-1]){Waiting();return;}
    for(unsigned i=0;i<count;++i){auto& context=*contexts[i];auto& slot=*slots[i];auto& layer=layers[i];const auto& g=*bound_guides[i];
        if(!context.params||!context.nr_handle||context.failed||context.retired||context.pending_recording_token!=token){Failed();break;}
        if(context.successful_evaluations_since_reset<=g_config.warmup_after_create){Waiting();break;}
        ID3D12Resource *depth=nullptr,*motion=nullptr;
        if(!GetResource(context.params,"DLSSNR.Depth",&depth)||!GetResource(context.params,"DLSSNR.MVec",&motion)||
           depth!=(g.signature.linear_depth?slot.rr_depth:g.depth)||
           motion!=(g.signature.resample_motion?slot.motion_proxy:g.motion)){Failed();break;}
        nr_replay::Header header{};header.sequence=(attempt.request<<3)|(i+1);header.instance=context.instance_id;
        header.mode=context.mode;header.config=g_config_generation.load();header.frame=input.source.sequence;
        header.source_width=image.source.width;header.source_height=image.source.height;
        header.work_width=context.width;header.work_height=context.height;header.color_mode=static_cast<unsigned>(context.codec_color_mode);
        header.pre_exposure=1;header.exposure_scale=1;header.proxy_scale=g_config.proxy_scale;header.source_dirty=kSourceDirty?1u:0u;
        std::snprintf(header.source_commit,sizeof(header.source_commit),"%s",kSourceCommit);
        std::snprintf(header.build,sizeof(header.build),"%s",kBuildIdentity);nr_replay::SnapshotScalars(header,context.params);
        nr_replay::Input inputs[nr_replay::kPlanes]={nr_replay::Texture(image.image.source,4),nr_replay::Texture(slot.proxy,4),
            nr_replay::Texture(slot.neural,4),nr_replay::Texture(depth,1),nr_replay::Texture(motion,2),{},nr_replay::Texture(image.image.result,4)};
        if(!layer.recorder.Ensure(image.device)){if(!layer.recorder.Empty()||!layer.recorder.ResetDevice()||!layer.recorder.Ensure(image.device)){Failed();break;}}
        layer.index=layer.recorder.Arm(header,inputs,IsSameRrDepthDevice);
        if(layer.index==nr_replay::Recorder::Invalid){Failed();break;}
        layer.identity={attempt.request,primary.instance_id,context.instance_id,input.source.stream,input.source.epoch,input.source.sequence,
            count,i+1,static_cast<unsigned>(ID3D12Resource_GetDesc(input.color).Format),input.encoding,input.hudless!=nullptr,g_config.nr_full_reference};
    }
    if(!attempt.request){for(auto& layer:layers)if(layer.index!=nr_replay::Recorder::Invalid){layer.recorder.Discard(layer.index);layer.index=nr_replay::Recorder::Invalid;}return;}
    // Everything is armed before recording the first capture dispatch. These
    // are actual Core state variables, restored before the FG consumer resumes.
    const auto source_state=image.image.source_state,result_state=image.image.result_state;
    Transition(list,image.image.source,image.image.source_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(list,image.image.result,image.image.result_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    bool complete=true;remaining.store(count);
    for(unsigned i=0;i<count;++i){auto& layer=layers[i];auto& slot=*slots[i];auto& context=*contexts[i];const auto& g=*bound_guides[i];
        const auto proxy_state=slot.proxy_state,neural_state=slot.neural_state;
        // Auxiliary RecordSecond already promises readable input/output on
        // successful adoption; slot.proxy_state does not own its aliased P1.
        if(i==0)Transition(list,slot.proxy,slot.proxy_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(list,slot.neural,slot.neural_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        const auto depth_state=slot.rr_depth_state,motion_state=slot.motion_proxy_state;
        if(g.signature.linear_depth)Transition(list,slot.rr_depth,slot.rr_depth_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if(g.signature.resample_motion)Transition(list,slot.motion_proxy,slot.motion_proxy_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        const bool began=layer.recorder.Begin(list,layer.index,token);
        if(began){layer.recorder.Composition(layer.index,attempt.constants,attempt.constant_count,attempt.flags,attempt.pass);
            nr_replay::Header metadata{};metadata.evaluation=context.evaluations;metadata.since_reset=context.successful_evaluations_since_reset;metadata.frame=input.source.sequence;
            complete&=layer.recorder.Finish(list,layer.index,metadata);
        }else {layer.recorder.Discard(layer.index);complete=false;remaining.fetch_sub(1);}
        if(i==0)Transition(list,slot.proxy,slot.proxy_state,proxy_state);
        Transition(list,slot.neural,slot.neural_state,neural_state);
        if(g.signature.linear_depth)Transition(list,slot.rr_depth,slot.rr_depth_state,depth_state);
        if(g.signature.resample_motion)Transition(list,slot.motion_proxy,slot.motion_proxy_state,motion_state);
        slot.dual_time_stable=false;layer.index=nr_replay::Recorder::Invalid;
    }
    Transition(list,image.image.source,image.image.source_state,source_state);
    Transition(list,image.image.result,image.image.result_state,result_state);main.result_state=image.image.result_state;
    main.dual_time_stable=false;consumed=attempt.request;attempt={};status.store(complete?1u:3u,std::memory_order_release);
}
} // namespace nr_fg_capture
