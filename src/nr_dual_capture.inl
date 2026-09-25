namespace nr_full_capture {void Collect();void Finish(ID3D12GraphicsCommandList*,NrSlot&);}
// A single observed warm frame, using the existing exact-submission recorder.
// This is deliberately not a fresh-history 16-frame replay sequence.
namespace nr_dual_capture {
std::atomic<unsigned> workers{0},status{0}; // 0 idle, 1 pending, 2 exported, 3 failed
unsigned long long captured_request=0;
struct Identity {
    unsigned long long request=0,parent=0,auxiliary=0,parent_evaluation=0;
    unsigned numerator=1,denominator=1,domain=0,generic=0;
    float gains[5]{1,1,1,1,1};
    unsigned full_passes=0,layer=0;
    bool present=false;
    std::atomic<unsigned>* export_status=nullptr;
    std::atomic<unsigned>* remaining=nullptr;
};
Identity identity{};
struct Job {nr_replay::ExportJob* frame=nullptr;HMODULE module=nullptr;Identity identity{};};
void ExportFinished(const Identity& id,bool ok) {
    if(!id.export_status)return;
    if(!ok)id.export_status->store(3);
    if(id.remaining&&id.remaining->fetch_sub(1)==1&&id.export_status->load()!=3)
        id.export_status->store(2);
}
DWORD WINAPI Export(void* opaque) {
    auto* job=static_cast<Job*>(opaque);auto* frame=job->frame;
    bool ok=frame->readback&&frame->header.completion==static_cast<unsigned>(nr_replay::Completion::Complete);
    wchar_t documents[768]{},parent[1024]{},directory[1024]{},path[1024]{};
    if(ok)ok=SUCCEEDED(SHGetFolderPathW(nullptr,CSIDL_PERSONAL,nullptr,SHGFP_TYPE_CURRENT,documents))&&
        swprintf_s(parent,L"%ls\\%ls",documents,job->identity.present?L"DLSS5-Present-Capture":L"DLSS5-Dual-Capture")>=0;
    if(ok&&!CreateDirectoryW(parent,nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)ok=false;
    if(ok)ok=swprintf_s(directory,L"%ls\\%lu-%llu-%llu",parent,GetCurrentProcessId(),job->identity.request,frame->header.sequence)>=0&&CreateDirectoryW(directory,nullptr);
    if(ok)ok=swprintf_s(path,L"%ls\\observed-frame.nrf",directory)>=0;
    HANDLE file=ok?CreateFileW(path,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr):INVALID_HANDLE_VALUE;
    ok=ok&&file!=INVALID_HANDLE_VALUE;
    void* mapped=nullptr;const D3D12_RANGE range{0,static_cast<SIZE_T>(frame->bytes)};
    if(ok)ok=SUCCEEDED(ID3D12Resource_Map(frame->readback,0,&range,&mapped))&&mapped;
    if(ok)ok=nr_replay::WriteBytes(file,&frame->header,sizeof(frame->header))&&nr_replay::WriteBytes(file,mapped,frame->bytes);
    if(mapped){const D3D12_RANGE empty{0,0};ID3D12Resource_Unmap(frame->readback,0,&empty);}
    if(file!=INVALID_HANDLE_VALUE){
        if(!ok){frame->header.completion=static_cast<unsigned>(nr_replay::Completion::Invalidated);LARGE_INTEGER zero{};
            SetFilePointerEx(file,zero,nullptr,FILE_BEGIN);nr_replay::WriteBytes(file,&frame->header,sizeof(frame->header));SetEndOfFile(file);}
        CloseHandle(file);
    }
    if(ok) {
        const auto& id=job->identity;char json[3072]{};
        const int length=std::snprintf(json,sizeof(json),
            "{\n\"schema\":\"%s\",\"complete\":true,\n"
            "\"full_reference_passes\":%u,\"captured_auxiliary_layer\":%u,\n"
            "\"scope\":\"one warm observed frame; all requested layers share one root recording; not a cold-history replay\",\n"
            "\"roles\":{\"Source\":\"stage reference input\",\"Proxy\":\"current layer model input\",\"Neural\":\"current layer model output\",\"Composite\":\"actual final composition\",\"Depth\":\"bound current-layer depth\",\"Motion\":\"bound current-layer motion\"},\n"
            "\"request\":%llu,\"parent_instance\":%llu,\"auxiliary_instance\":%llu,\"parent_evaluation\":%llu,\n"
            "\"scale\":[%u,%u],\"model_domain\":%u,\"phase\":\"unknown\",\n"
            "\"generic\":%u,\"generic_gains\":[%.9g,%.9g,%.9g,%.9g,%.9g],\n"
            "\"residual\":\"full reference directly selects the final layer; reduced NR2 uses matched residual; capture never reruns inference\",\n"
            "\"parameters\":\"captured-layer bound scalar snapshot in NRF header\",\"timing\":\"capture frame excluded from stable timing\"\n}\n",
            id.present?"present-nr-observed-frame-v1":"dual-nr-observed-frame-v1",
            id.full_passes,id.layer,id.request,id.parent,id.auxiliary,id.parent_evaluation,id.numerator,id.denominator,id.domain,id.generic,
            double(id.gains[0]),double(id.gains[1]),double(id.gains[2]),double(id.gains[3]),double(id.gains[4]));
        ok=length>0&&static_cast<std::size_t>(length)<sizeof(json)&&swprintf_s(path,L"%ls\\metadata.json",directory)>=0;
        file=ok?CreateFileW(path,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr):INVALID_HANDLE_VALUE;
        ok=ok&&file!=INVALID_HANDLE_VALUE;
        if(ok)ok=nr_replay::WriteBytes(file,json,static_cast<unsigned>(length));
        if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
    }
    status.store(ok?2:3);Log("[dual-capture] %s request=%llu; single observed frame, exact completion required.",ok?"exported":"failed",job->identity.request);
    ExportFinished(job->identity,ok);
    ReleaseCom(frame->readback);if(frame->bytes)nr_replay::resident_bytes.fetch_sub(frame->bytes);delete frame;
    auto module=job->module;delete job;workers.fetch_sub(1);FreeLibraryAndExitThread(module,ok?0:1);
}
void HandoffWithIdentity(nr_replay::ExportJob* frame,const Identity& captured) {
    auto* job=new(std::nothrow)Job{};
    if(job){job->frame=frame;job->identity=captured;
        if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&Export),&job->module)) {
            workers.fetch_add(1);auto thread=CreateThread(nullptr,0,Export,job,0,nullptr);
            if(thread){CloseHandle(thread);return;}workers.fetch_sub(1);FreeLibrary(job->module);
        }delete job;
    }
    status.store(3);ExportFinished(captured,false);ReleaseCom(frame->readback);if(frame->bytes)nr_replay::resident_bytes.fetch_sub(frame->bytes);delete frame;
}
void Handoff(nr_replay::ExportJob* frame){HandoffWithIdentity(frame,identity);}
void Collect() {
    if(g_lifecycle_tracking_uncertain.load())return;
    nr_full_capture::Collect();
    recorder.Collect([](void* key,std::uint64_t value){auto* queue=static_cast<LifecycleQueue*>(key);
        if(!queue||!queue->fence||!value)return false;
        const auto done=ID3D12Fence_GetCompletedValue(queue->fence);return done!=UINT64_MAX&&done>=value;
    },Handoff);
}
void Begin(ID3D12GraphicsCommandList* list,NrContext& primary,NrSlot& main,NrContext& aux,NrSlot& slot,const NrGuideBinding& guides) {
    main.dual_capture_index=nr_replay::Recorder::Invalid;
    const auto request=main.stage_capture.info.request_id;
    if(!main.stage_capture.armed||!request||captured_request==request||!recorder.Empty()||workers.load())return;
    captured_request=request;status.store(1);
    nr_replay::Header h{};h.sequence=(GetTickCount64()<<16)^GetCurrentProcessId();h.instance=aux.instance_id;
    h.mode=aux.mode;h.config=g_config_generation.load();h.source_width=primary.width;h.source_height=primary.height;
    h.work_width=aux.width;h.work_height=aux.height;h.color_mode=static_cast<unsigned>(aux.codec_color_mode);
    h.source_dirty=kSourceDirty?1:0;std::snprintf(h.source_commit,sizeof(h.source_commit),"%s",kSourceCommit);
    std::snprintf(h.build,sizeof(h.build),"%s",kBuildIdentity);nr_replay::SnapshotScalars(h,aux.params);
    auto* depth=guides.signature.linear_depth?slot.rr_depth:guides.depth;
    auto* motion=guides.signature.resample_motion?slot.motion_proxy:guides.motion;
    nr_replay::Input inputs[nr_replay::kPlanes]={nr_replay::Texture(main.neural,4),nr_replay::Texture(slot.proxy,4),
        nr_replay::Texture(slot.neural,4),nr_replay::Texture(depth,1),nr_replay::Texture(motion,2),{},nr_replay::Texture(main.result,4,main.result_format)};
    if(!recorder.Ensure(g_codec.device)){status.store(3);return;}
    const auto index=recorder.Arm(h,inputs,IsSameRrDepthDevice);
    if(index==nr_replay::Recorder::Invalid){status.store(3);return;}
    if(!recorder.Begin(list,index,primary.pending_recording_token)){recorder.Discard(index);status.store(3);return;}
    main.dual_capture_index=index;
    identity={request,primary.instance_id,aux.instance_id,primary.evaluations,1u,1u,main.dual_model_domain,g_config.generic_residual?1u:0u};
    std::copy_n(g_config.generic_gains,5,identity.gains);
}
void Finish(ID3D12GraphicsCommandList* list,NrSlot& main) {
    nr_full_capture::Finish(list,main);
    if(main.dual_capture_index==nr_replay::Recorder::Invalid||!main.dual_selected)return;
    auto& aux=*main.dual_selected->dual_owner;
    nr_replay::Header h{};h.evaluation=aux.evaluations;h.since_reset=aux.successful_evaluations_since_reset;h.frame=g_applied_nr_identity.frame_id;
    const auto state=main.result_state;Transition(list,main.result,main.result_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if(!recorder.Finish(list,main.dual_capture_index,h))status.store(3);
    Transition(list,main.result,main.result_state,state);
}
}

#include "nr_full_capture.inl"
