// Recorder control is serialized by g_chain_cs; workers own detached readbacks.
// This explicit diagnostic is separate from F7/F8 and never changes the INI.
namespace nr_replay {
enum class Status : unsigned { Idle, Requested, Recording, Draining, Complete, Partial, Failed };
std::atomic<Status> status{Status::Idle};
std::atomic<unsigned> workers{0}, written_mask{0}, bad_mask{0};
std::atomic<unsigned> recorded_frames{0};
std::atomic<unsigned> reason{0};
wchar_t directory[1024]{};
std::uint64_t sequence_id=0, selected_instance=0, sequence_bytes=0;
ULONGLONG start_tick=0;
unsigned selected_config=0;
bool owns_hold=false;

bool Active(){const auto s=status.load();return s==Status::Requested || s==Status::Recording;}
void Stop(unsigned why) {
    reason.store(why);status.store(recorded_frames.load()?Status::Draining:Status::Failed);
    if(owns_hold){owns_hold=false;nr_f8::g_config_hold.store(false,std::memory_order_release);}
}
void PollStatus() {
    if(Active() && GetTickCount64()-start_tick>30000)Stop(1);
    if(status.load()==Status::Draining && !workers.load() && recorder.Empty()) {
        constexpr unsigned all=(1u<<kFrames)-1;
        status.store(written_mask.load()==all && !bad_mask.load() && !reason.load()
            ?Status::Complete:Status::Partial);
    }
}
bool WriteBytes(HANDLE file,const void *data,std::uint64_t bytes) {
    const auto *cursor=static_cast<const unsigned char *>(data);
    while(bytes) {
        const DWORD chunk=static_cast<DWORD>(std::min<std::uint64_t>(bytes,1u<<20));
        DWORD written=0;if(!WriteFile(file,cursor,chunk,&written,nullptr)||written!=chunk)return false;
        cursor+=chunk;bytes-=chunk;
    }
    return true;
}
struct WorkerJob { ExportJob *frame=nullptr;HMODULE module=nullptr;wchar_t path[1024]{}; };
bool HashModule(HMODULE module,char (&digest)[65],unsigned long long &bytes) {
    wchar_t path[1024]{};const DWORD length=module?GetModuleFileNameW(module,path,1024):0;
    if(!length || length>=1024)return false;
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    LARGE_INTEGER size{};bool ok=GetFileSizeEx(file,&size) && size.QuadPart>0 && size.QuadPart<=(512ll<<20);
    auto buffer=std::unique_ptr<unsigned char[]>(new(std::nothrow)unsigned char[65536]);
    ok=ok&&bool(buffer);nr_bundle::Sha256 hash;std::uint64_t total=0;
    while(ok && total<static_cast<std::uint64_t>(size.QuadPart)) {
        DWORD read=0;const auto wanted=static_cast<DWORD>(std::min<std::uint64_t>(65536,size.QuadPart-total));
        ok=ReadFile(file,buffer.get(),wanted,&read,nullptr)&&read==wanted;
        if(ok){hash.Update(buffer.get(),read);total+=read;}
    }
    CloseHandle(file);if(ok){hash.Finish(digest);bytes=total;}return ok;
}
bool WriteRuntimeIdentity() {
    char nr[65]{},bridge[65]{},core[65]{};unsigned long long n=0,b=0,c=0;
    HMODULE core_module=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(g_nr_api.allocate),&core_module);
    if(!HashModule(g_nr_api.signed_module,nr,n)||!HashModule(g_nr_api.bridge_module,bridge,b)||
       !HashModule(core_module,core,c))return false;
    char text[1024]{};const int length=std::snprintf(text,sizeof(text),
        "{\n\"schema\":\"nr-replay-runtime-v1\",\n\"nr_sha256\":\"%s\",\"nr_bytes\":%llu,\n"
        "\"bridge_sha256\":\"%s\",\"bridge_bytes\":%llu,\n\"core_sha256\":\"%s\",\"core_bytes\":%llu,\n"
        "\"identity_scope\":\"loaded-module-path files hashed on export worker; not a mapped-memory hash\"\n}\n",nr,n,bridge,b,core,c);
    wchar_t path[1024]{};if(length<=0 || static_cast<std::size_t>(length)>=sizeof(text) ||
        swprintf_s(path,L"%ls\\runtime.json",directory)<0)return false;
    HANDLE file=CreateFileW(path,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    const bool ok=WriteBytes(file,text,static_cast<unsigned>(length));CloseHandle(file);return ok;
}
DWORD WINAPI ExportFrame(void *opaque) {
    auto *job=static_cast<WorkerJob *>(opaque);auto *frame=job->frame;
    bool ok=frame->header.completion==static_cast<unsigned>(Completion::Complete) && frame->readback;
    HANDLE file=CreateFileW(job->path,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    ok=ok && file!=INVALID_HANDLE_VALUE;
    void *mapped=nullptr;const D3D12_RANGE read{0,static_cast<SIZE_T>(frame->bytes)};
    if(ok)ok=SUCCEEDED(ID3D12Resource_Map(frame->readback,0,&read,&mapped)) && mapped;
    if(ok)ok=WriteBytes(file,&frame->header,sizeof(frame->header)) && WriteBytes(file,mapped,frame->bytes);
    if(ok && frame->header.index==0)ok=WriteRuntimeIdentity();
    if(mapped){const D3D12_RANGE none{0,0};ID3D12Resource_Unmap(frame->readback,0,&none);}
    if(file!=INVALID_HANDLE_VALUE) {
        // Failed files remain explicitly incomplete. They are never promoted by
        // their presence or byte count, and the reader also validates lengths.
        if(!ok) {
            frame->header.completion=static_cast<unsigned>(Completion::Invalidated);
            LARGE_INTEGER zero{};SetFilePointerEx(file,zero,nullptr,FILE_BEGIN);
            WriteBytes(file,&frame->header,sizeof(frame->header));SetEndOfFile(file);
        }
        CloseHandle(file);
    }
    const unsigned bit=1u<<frame->header.index;
    if(ok)written_mask.fetch_or(bit);else bad_mask.fetch_or(bit);
    ReleaseCom(frame->readback);
    if(frame->bytes)resident_bytes.fetch_sub(frame->bytes);
    delete frame;
    const auto module=job->module;delete job;workers.fetch_sub(1);
    FreeLibraryAndExitThread(module,ok?0:1);
}
void Handoff(ExportJob *frame) {
    auto *job=new(std::nothrow)WorkerJob{};
    if(job) {
        job->frame=frame;
        const bool path_ok=swprintf_s(job->path,L"%ls\\frame-%02u.nrf",directory,frame->header.index)>=0;
        const bool module_ok=GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&ExportFrame),&job->module)!=FALSE;
        if(path_ok && module_ok) {
            workers.fetch_add(1);
            HANDLE thread=CreateThread(nullptr,0,ExportFrame,job,0,nullptr);
            if(thread){CloseHandle(thread);return;}
            workers.fetch_sub(1);
        }
        if(job->module)FreeLibrary(job->module);delete job;
    }
    bad_mask.fetch_or(1u<<frame->header.index);ReleaseCom(frame->readback);
    if(frame->bytes)resident_bytes.fetch_sub(frame->bytes);delete frame;
}
void Collect() {
    if(g_lifecycle_tracking_uncertain.load() && Active())Stop(13);
    PollStatus();
    if(g_lifecycle_tracking_uncertain.load())return;
    recorder.Collect([](void *key,std::uint64_t value) {
        auto *queue=static_cast<LifecycleQueue *>(key);
        if(!queue || !queue->fence || !value)return false;
        const auto done=ID3D12Fence_GetCompletedValue(queue->fence);
        return done!=UINT64_MAX && done>=value;
    },Handoff);
    PollStatus();
}
bool Request() {
    EnterCriticalSection(&g_chain_cs);
    bool accepted=false;
    if(!Active() && status.load()!=Status::Draining && recorder.Empty() && !workers.load() &&
        StageCaptureStatus()==0 && !nr_f8::IsActive() && g_config.enabled && !g_config.identity_use_original) {
        bool expected=false;
        if(nr_f8::g_config_hold.compare_exchange_strong(expected,true)) {
            owns_hold=true;
            wchar_t documents[768]{},parent[1024]{};
            accepted=SUCCEEDED(SHGetFolderPathW(nullptr,CSIDL_PERSONAL,nullptr,SHGFP_TYPE_CURRENT,documents)) &&
                swprintf_s(parent,L"%ls\\DLSS5-Replay",documents)>=0;
            if(accepted && !CreateDirectoryW(parent,nullptr) && GetLastError()!=ERROR_ALREADY_EXISTS)accepted=false;
            sequence_id=(GetTickCount64()<<16)^GetCurrentProcessId();
            if(accepted)accepted=swprintf_s(directory,L"%ls\\sequence-%llu",parent,sequence_id)>=0 &&
                CreateDirectoryW(directory,nullptr)!=FALSE;
            if(accepted) {
                recorder.ResetDevice();
                selected_instance=0;sequence_bytes=0;selected_config=g_config_generation.load();
                recorded_frames.store(0);written_mask.store(0);bad_mask.store(0);reason.store(0);
                start_tick=GetTickCount64();status.store(Status::Requested);
                for(auto &context:g_contexts)if(context.sr_handle && !context.retired)context.retired=true;
                Log("[replay] explicit full-input sequence requested: 16 consecutive evaluations from fresh history; "
                    "bounded memory/files; no extra NR and no INI writes.");
            } else Stop(2);
        }
    }
    LeaveCriticalSection(&g_chain_cs);return accepted;
}
void Cancel() {
    EnterCriticalSection(&g_chain_cs);
    if(Active())Stop(3);
    LeaveCriticalSection(&g_chain_cs);
}
unsigned MaskPresence(const NgxParams *params) {
    ID3D12Resource *typed=nullptr;void *untyped=nullptr;
    const auto tf=ParamSlot<GetD3D12Fn>(params,9);const auto uf=ParamSlot<GetVoidFn>(params,8);
    const auto tr=tf?tf(params,"DLSSNR.ControlMask",&typed):0;
    const auto ur=uf?uf(params,"DLSSNR.ControlMask",&untyped):0;
    if((tr==kNgxSuccess && typed)||(ur==kNgxSuccess && untyped))return 2;
    return tr==kNgxSuccess && ur==kNgxSuccess?1:0;
}
void SnapshotScalars(Header &header,const NgxParams *params) {
    static constexpr const char *uint_keys[]={
        "Width","Height","OutWidth","OutHeight","DLSSNR.Width","DLSSNR.Height",
        "DLSSNR.InputWidth","DLSSNR.InputHeight","DLSSNR.OutputWidth","DLSSNR.OutputHeight",
        "DLSSNR.Output.Width","DLSSNR.Output.Height",
        "DLSSNR.ColorSubrectBaseX","DLSSNR.ColorSubrectBaseY","DLSSNR.ColorSubrectWidth","DLSSNR.ColorSubrectHeight",
        "DLSSNR.DepthSubrectBaseX","DLSSNR.DepthSubrectBaseY","DLSSNR.DepthSubrectWidth","DLSSNR.DepthSubrectHeight",
        "DLSSNR.MVecSubrectBaseX","DLSSNR.MVecSubrectBaseY","DLSSNR.MVecSubrectWidth","DLSSNR.MVecSubrectHeight",
        "DLSSNR.OutputSubrectBaseX","DLSSNR.OutputSubrectBaseY","DLSSNR.OutputSubrectWidth","DLSSNR.OutputSubrectHeight",
        "DLSSNR.Style","DLSSNR.UICorrection","DLSSNR.Jitter.SourceWidth","DLSSNR.Jitter.SourceHeight",
        "DLSSNR.Jitter.WorkWidth","DLSSNR.Jitter.WorkHeight"};
    static constexpr const char *int_keys[]={"DLSSNR.Enabled","DLSSNR.Reset","DLSSNR.UseAutoMask",
        "DLSSNR.DepthInverted","DLSS.Feature.Create.Flags","PerfQualityValue","DLSSNR.Upscaling",
        "DLSSNR.Jitter.Valid","DLSS.Indicator.Invert.X.Axis","DLSS.Indicator.Invert.Y.Axis"};
    static constexpr const char *float_keys[]={"DLSSNR.Intensity","DLSSNR.LocalToneStrength",
        "DLSSNR.LocalStructureStrength","DLSSNR.SkinStructureStrength","DLSSNR.ScalingRatio","DLSSNR.Scale",
        "DLSSNR.MVecScaleX","DLSSNR.MVecScaleY",
        "Jitter.Offset.X","Jitter.Offset.Y","DLSSNR.Jitter.Offset.X","DLSSNR.Jitter.Offset.Y",
        "DLSS.Pre.Exposure","DLSS.Exposure.Scale","DLSSNR.Jitter.Scale.X","DLSSNR.Jitter.Scale.Y"};
    static_assert(std::size(uint_keys)+std::size(int_keys)+std::size(float_keys)<=kParameters);
    auto add=[&](const char *name,ScalarType type) {
        auto &scalar=header.parameters[header.parameter_count++];
        std::snprintf(scalar.name,sizeof(scalar.name),"%s",name);scalar.type=static_cast<unsigned>(type);
        if(type==ScalarType::UInt32){unsigned value=0;scalar.present=GetUInt(params,name,&value);scalar.value=value;}
        else if(type==ScalarType::Int32){int value=0;scalar.present=GetInt(params,name,&value);std::memcpy(&scalar.value,&value,4);}
        else {float value=0;scalar.present=GetFloat(params,name,&value);std::memcpy(&scalar.value,&value,4);}
    };
    for(auto key:uint_keys)add(key,ScalarType::UInt32);
    for(auto key:int_keys)add(key,ScalarType::Int32);
    for(auto key:float_keys)add(key,ScalarType::Float32);
}
Input Texture(ID3D12Resource *resource,unsigned channels,DXGI_FORMAT requested=DXGI_FORMAT_UNKNOWN) {
    Input input{};input.resource=resource;input.channels=channels;
    if(!resource)return input;
    const auto format=ID3D12Resource_GetDesc(resource).Format;
    input.view=ReadFormat(requested!=DXGI_FORMAT_UNKNOWN?requested:format);
    if((channels==4 && input.view==DXGI_FORMAT_R16G16B16A16_FLOAT) ||
       (channels==2 && input.view==DXGI_FORMAT_R16G16_FLOAT))input.encoding=Encoding::Float16;
    return input;
}
unsigned Begin(ID3D12GraphicsCommandList *list,NrContext &context,NrSlot &slot,
               const NrGuideBinding &guides,const ColorContract &color,
               unsigned source_width,unsigned source_height,unsigned source_x,unsigned source_y,
               unsigned output_x,unsigned output_y) {
    slot.replay_capture_index=Recorder::Invalid;
    if(!Active())return Recorder::Invalid;
    if(selected_config!=g_config_generation.load() || context.retired){Stop(4);return Recorder::Invalid;}
    if(!selected_instance) {
        if(context.evaluations || !context.diagnostic_reset_requested)return Recorder::Invalid;
        selected_instance=context.instance_id;status.store(Status::Recording);
    }
    if(context.instance_id!=selected_instance){Stop(5);return Recorder::Invalid;}
    const auto index=recorded_frames.load();
    if(context.evaluations!=index || (index && context.diagnostic_reset_requested)){Stop(6);return Recorder::Invalid;}
    Header header{};header.sequence=sequence_id;header.instance=context.instance_id;header.index=index;
    header.mode=context.mode;header.config=selected_config;header.reset=context.diagnostic_reset_requested?1:0;
    header.source_x=source_x;header.source_y=source_y;
    header.source_width=source_width;header.source_height=source_height;
    header.work_width=context.width;header.work_height=context.height;header.color_mode=static_cast<unsigned>(context.codec_color_mode);
    header.pre_exposure=color.pre_exposure;header.exposure_scale=color.exposure_scale;header.proxy_scale=g_config.proxy_scale;
    header.source_dirty=kSourceDirty?1u:0u;
    std::snprintf(header.source_commit,sizeof(header.source_commit),"%s",kSourceCommit);
    std::snprintf(header.build,sizeof(header.build),"%s",kBuildIdentity);
    SnapshotScalars(header,context.params);header.control_mask_before=MaskPresence(context.params);
    int auto_mask=0;if(GetInt(context.params,"DLSSNR.UseAutoMask",&auto_mask))header.auto_mask_before=auto_mask?2:1;
    ID3D12Resource *depth=guides.signature.linear_depth?slot.rr_depth:guides.depth;
    ID3D12Resource *motion=guides.signature.resample_motion?slot.motion_proxy:guides.motion;
    // These are complete resources, retaining their recorded subrect parameters.
    const bool native=context.successful_evaluations_since_reset+1<=g_config.warmup_after_create ||
        (context.mode==1 && slot.suppress_transfer_this_frame);
    Input inputs[kPlanes]={Texture(slot.source,4,slot.source_format),Texture(slot.proxy,4),Texture(slot.neural,4),
        Texture(depth,1),Texture(motion,2),Texture(slot.exposure_source,1),
        Texture(native?slot.source:slot.result,4,native?slot.source_format:slot.result_format)};
    inputs[0].width=source_width;inputs[0].height=source_height;inputs[0].x=source_x;inputs[0].y=source_y;
    inputs[6].width=source_width;inputs[6].height=source_height;
    inputs[6].x=native?source_x:output_x;inputs[6].y=native?source_y:output_y;
    if(!recorder.Ensure(g_codec.device)){Stop(7);return Recorder::Invalid;}
    const auto armed=recorder.Arm(header,inputs,IsSameRrDepthDevice);
    if(armed==Recorder::Invalid){Stop(8);return armed;}
    if(recorder.Metadata(armed).payload_bytes>kSequenceLimit-sequence_bytes) {
        recorder.Discard(armed);Stop(9);return Recorder::Invalid;
    }
    if(!recorder.Begin(list,armed,context.pending_recording_token)) {
        recorder.Discard(armed);Stop(10);return Recorder::Invalid;
    }
    sequence_bytes+=recorder.Metadata(armed).payload_bytes;recorded_frames.fetch_add(1);
    slot.replay_capture_index=armed;
    return armed;
}
void Finish(ID3D12GraphicsCommandList *list,NrContext &context,NrSlot &slot,unsigned index,bool native) {
    if(index==Recorder::Invalid)return;
    Header metadata{};metadata.evaluation=context.evaluations;
    metadata.since_reset=context.successful_evaluations_since_reset;
    metadata.frame=g_applied_nr_identity.frame_id;metadata.control_mask_after=MaskPresence(context.params);
    int auto_mask=0;if(GetInt(context.params,"DLSSNR.UseAutoMask",&auto_mask))metadata.auto_mask_after=auto_mask?2:1;
    // The owned neural/result states are exact. Game-owned guides were only
    // read through typed SRVs and have had no new transitions introduced.
    const auto result_state=slot.result_state;
    if(!native)Transition(list,slot.result,slot.result_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    const bool complete=recorder.Finish(list,index,metadata);
    if(!native)Transition(list,slot.result,slot.result_state,result_state);
    if(!complete)Stop(11);
    if(recorded_frames.load()==kFrames){context.retired=true;Stop(0);}
}
struct Attempt {
    unsigned index=Recorder::Invalid;
    bool complete=false;
    ~Attempt(){if(index!=Recorder::Invalid && !complete)Stop(12);}
    void Finish(ID3D12GraphicsCommandList *list,NrContext &context,NrSlot &slot,bool native) {
        nr_replay::Finish(list,context,slot,index,native);complete=true;
    }
};
void DrawUi() {
    const bool zh=UseChineseUi();
    ImGui::SeparatorText(zh?"完整模型重放采集":"Full model replay capture");
    ImGui::TextWrapped("%s",zh?
        "记录从新历史开始的16次连续模型输入、导引与结果。仅诊断使用，可能短时影响帧率；文件上限8 GiB。不会改写设置。":
        "Records 16 consecutive model inputs, guides and results from fresh history. Diagnostic only; may temporarily affect frame time. Files capped at 8 GiB; settings are not saved.");
    const auto state=status.load();
    if(Active()) {if(ImGui::Button(zh?"停止重放采集":"Stop replay capture"))Cancel();}
    else {
        const bool busy=state==Status::Draining || workers.load() || nr_f8::g_config_hold.load();
        if(busy)ImGui::BeginDisabled();
        if(ImGui::Button(zh?"采集完整输入序列":"Capture full input sequence"))Request();
        if(busy)ImGui::EndDisabled();
    }
    static constexpr const char *en[]={"Idle","Waiting for fresh history","Recording","Waiting for GPU / export",
        "Sequence exported; verify before replay","Partial; not a complete replay","Could not capture"};
    static constexpr const char *cn[]={"未开始","等待新历史","采集中","等待GPU完成／导出",
        "序列已导出；重放前需校验","不完整；不能作为完整重放","未能采集"};
    ImGui::Text(zh?"%s | 已录制 %u/16 | 原因 %u":"%s | Recorded %u/16 | Reason %u",
        zh?cn[static_cast<unsigned>(state)]:en[static_cast<unsigned>(state)],recorded_frames.load(),reason.load());
    ImGui::TextDisabled("%s",zh?"保存到文档 / DLSS5-Replay；不会自动上传":"Saved in Documents / DLSS5-Replay; no automatic upload");
}
} // namespace nr_replay
