// Private-SR-only full-plane capture. The caller owns Flight and the exact
// recording/fence proof. The worker never takes a rendering/lifecycle lock.
#include "nr_private_sr_capture_policy.h"

namespace nr_private_sr_capture {
using Id=std::uint64_t;
using Source=nr_private_sr::Source;
using Recording=nr_private_sr::Recording;
using View=nr_edit_dx12::View;
enum PlaneRole : unsigned { RawDepth,RawMotion,Base,Candidate,Carrier,Tail,Private,Final,Depth,Motion };
constexpr const char* kNames[kPlanes]={"native-depth","native-motion","native-base","nr-candidate",
    "carrier","tail","private-reconstruction","final","normalized-depth","normalized-motion"};
struct Plane {
    ID3D12Resource *input=nullptr,*readback=nullptr;
    D3D12_RESOURCE_DESC description{};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    Id row_bytes=0,buffer_bytes=0,allocation_bytes=0,resource=0;
    unsigned rows=0,view_format=0,state=0;
    nr_post_residual_dx12::Rect rect{};
    bool copied=false;
};
struct Job {
    std::array<Plane,kPlanes> planes{};
    nr_private_sr_guides::Input guides{};
    nr_private_sr_guides::Constants constants{};
    nr_private_sr::BindEvidence bind{};
    Recording recording{};
    Source source{},reference{},session_first{},previous_capture{};
    Id view=0,configuration=0,encoding=0,native_generation=0;
    Id resident=0,resident_cap=0,payload=0,queue=0,fence=0,fence_value=0,completed=0;
    unsigned attempt=0,session=0,index=0,requested=0,color_mode=0;
    unsigned result=0,status=0;
    float pre_exposure=1,exposure_scale=1;
    unsigned exposure_known=0,exposure_format=0,exposure_width=0,exposure_height=0;
    bool exposure_texture=false,native_reset_known=false,native_reset=true,source_gap=false;
    bool composed=false,released=false,submitted=false,disk_reserved=false,per_channel_support=true;
    const char* failure="none";
    const char* session_reason="first-valid-source";
};
struct Run {
    bool enabled=false,finished=false;
    unsigned session=0,count=0,requested=0;
    Id resident_cap=0,encoding=0,native_generation=0;
    Source first{},previous{};
    nr_private_sr_guides::Input contract{};
    nr_post_residual_dx12::Rect base_rect{},candidate_rect{};
};
struct Worker {
    // 0 cold, 1 starting, 2 ready, 3 failed, 4 stopped. Initialization is
    // serialized by the product owner; only the worker publishes ready/stop.
    std::atomic<unsigned> state{0},users{0},active_runs{0},attempts{0},sessions{0},leases{0};
    std::atomic<Id> resident{0},resident_cap{0},disk{0};
    SRWLOCK lock=SRWLOCK_INIT;
    std::array<Job*,kAttempts> jobs{};
    unsigned read=0,write=0;
    HANDLE wake=nullptr;
    HMODULE module=nullptr;
    wchar_t directory[MAX_PATH]{};
    wchar_t folder[MAX_PATH]{};
};
Worker worker;
// The Core includes this file inside its private namespace. Reuse its already
// global <string>/<cstdio>; importing a new STL header here would nest std.
struct Text {
    std::string value;
    Text& operator<<(const char* text){value+=text;return *this;}
    Text& operator<<(char* text){value+=text;return *this;}
    Text& operator<<(char text){value+=text;return *this;}
    template<class Number> Text& operator<<(Number number){value+=std::to_string(number);return *this;}
    const std::string& str()const{return value;}
};
Sequence SequenceOf(Source s){return {s.stream,s.epoch,s.sequence};}
bool Reserve(std::atomic<Id>& account,Id bytes,Id cap) {
    auto old=account.load(std::memory_order_relaxed);
    do {if(!Fits(old,bytes,cap))return false;}
    while(!account.compare_exchange_weak(old,old+bytes,std::memory_order_relaxed));
    return true;
}
Id PhysicalCap() {
    MEMORYSTATUSEX memory{};memory.dwLength=sizeof(memory);
    if(!GlobalMemoryStatusEx(&memory)||memory.ullAvailPhys<=kPhysicalReserve)return 0;
    return std::min<Id>(kResidentCap,memory.ullAvailPhys-kPhysicalReserve);
}
void ReleaseJob(Job*& job) {
    if(!job)return;
    for(auto& p:job->planes){ReleaseCom(p.input);ReleaseCom(p.readback);}
    worker.resident.fetch_sub(job->resident,std::memory_order_relaxed);
    worker.leases.fetch_sub(1,std::memory_order_relaxed);
    delete job;job=nullptr;
}
bool WriteAll(HANDLE file,const void* data,Id bytes) {
    auto* cursor=static_cast<const unsigned char*>(data);
    while(bytes){DWORD written=0;const auto chunk=DWORD(std::min<Id>(bytes,1ull<<20));
        if(!WriteFile(file,cursor,chunk,&written,nullptr)||written!=chunk)return false;
        bytes-=chunk;cursor+=chunk;
    }
    return true;
}
void OutputPath(wchar_t (&path)[MAX_PATH],unsigned attempt,const wchar_t* leaf) {
    _snwprintf_s(path,_TRUNCATE,L"%s\\attempt-%02u-%s",worker.folder,attempt,leaf);
}
bool WriteText(const wchar_t* path,const std::string& text,bool replace=false) {
    HANDLE file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,
        replace?CREATE_ALWAYS:CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    const bool ok=WriteAll(file,text.data(),text.size());CloseHandle(file);return ok;
}
void JsonSource(Text& out,Source s) {
    out<<'['<<s.stream<<','<<s.epoch<<','<<s.sequence<<']';
}
void JsonFloat(Text& out,float f) {
    if(std::isfinite(f)){char number[64]{};std::snprintf(number,sizeof(number),"%.9g",double(f));out<<number;}
    else out<<"null";
}
void JsonRect(Text& out,nr_post_residual_dx12::Rect r) {
    out<<'['<<r.x<<','<<r.y<<','<<r.width<<','<<r.height<<']';
}
bool SavePlane(const Job& job,unsigned role) {
    const auto& p=job.planes[role];
    if(!p.copied||!p.readback||!p.rows||!p.row_bytes)return false;
    wchar_t leaf[64]{},path[MAX_PATH]{};
    _snwprintf_s(leaf,_TRUNCATE,L"%S.bin",kNames[role]);OutputPath(path,job.attempt,leaf);
    HANDLE file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    void* mapped=nullptr;const D3D12_RANGE range{0,SIZE_T(p.buffer_bytes)};
    const auto hr=ID3D12Resource_Map(p.readback,0,&range,&mapped);
    bool ok=SUCCEEDED(hr)&&mapped;
    if(ok){
        const auto* bytes=static_cast<const unsigned char*>(mapped)+p.footprint.Offset;
        for(unsigned row=0;ok&&row<p.rows;++row)
            ok=WriteAll(file,bytes+Id(row)*p.footprint.Footprint.RowPitch,p.row_bytes);
        const D3D12_RANGE none{0,0};ID3D12Resource_Unmap(p.readback,0,&none);
    }
    CloseHandle(file);return ok;
}
std::string Metadata(const Job& j,bool pixels_valid,unsigned saved) {
    char attempt[4]{};std::snprintf(attempt,sizeof(attempt),"%02u",j.attempt);
    Text out;out<<"{\n\"schema\":\"dlss5-private-sr-planes-v1\",\"attempt\":"<<j.attempt
        <<",\"session\":"<<j.session<<",\"sessionIndex\":"<<j.index<<",\"requestedFrames\":"<<j.requested
        <<",\"sessionReason\":\""<<j.session_reason<<"\",\"sessionFirstSource\":";JsonSource(out,j.session_first);
    out<<",\"previousCapturedSource\":";JsonSource(out,j.previous_capture);
    out<<",\"source\":";JsonSource(out,j.source);out<<",\"motionReference\":";JsonSource(out,j.reference);
    out<<",\"predecessor\":";JsonSource(out,j.bind.predecessor);
    out<<",\"pixelsValid\":"<<(pixels_valid?"true":"false")<<",\"savedPlaneMask\":"<<saved
        <<",\"failure\":\""<<j.failure<<"\",\"recording\":{\"device\":"<<j.recording.device
        <<",\"queue\":"<<j.recording.queue<<",\"list\":"<<j.recording.list<<",\"lifetime\":"<<j.recording.lifetime
        <<",\"reset\":"<<j.recording.reset<<"},\"completion\":{\"submitted\":"<<(j.submitted?"true":"false")
        <<",\"recordingReleased\":"<<(j.released?"true":"false")<<",\"queue\":"<<j.queue<<",\"fence\":"<<j.fence
        <<",\"target\":"<<j.fence_value<<",\"observed\":"<<j.completed<<"},\"identity\":{\"view\":"<<j.view
        <<",\"configuration\":"<<j.configuration<<",\"encodingGeneration\":"<<j.encoding
        <<",\"nativeFeatureGeneration\":"<<j.native_generation<<",\"nativeFeature\":"<<j.guides.native_feature
        <<",\"nativeFlags\":"<<j.guides.native_flags<<",\"colorMode\":"<<j.color_mode<<"},\"reset\":{\"nativeKnown\":"
        <<(j.native_reset_known?"true":"false")<<",\"native\":"<<(j.native_reset?"true":"false")
        <<",\"sourceGap\":"<<(j.source_gap?"true":"false")<<",\"normalizedGuideRequest\":"<<(j.guides.reset?"true":"false")
        <<",\"actualBind\":"<<(j.bind.bound?"true":"false")<<",\"actualBindReset\":"<<(j.bind.reset_causes?"true":"false")
        <<",\"actualBindResetCauses\":"<<j.bind.reset_causes<<",\"historyCauses\":"<<j.bind.history_causes
        <<",\"privateResult\":"<<j.result<<",\"backendStatus\":"<<j.status<<"},\"guides\":{\"render\":["
        <<j.guides.render_width<<','<<j.guides.render_height<<"],\"work\":["<<j.guides.work_width<<','<<j.guides.work_height
        <<"],\"output\":["<<j.guides.output_width<<','<<j.guides.output_height<<"],\"nativeJitterRenderPx\":[";
    JsonFloat(out,j.guides.original_jitter_x);out<<',';JsonFloat(out,j.guides.original_jitter_y);
    out<<"],\"nativeMVScaleRenderPx\":[";JsonFloat(out,j.guides.original_motion_scale_x);out<<',';JsonFloat(out,j.guides.original_motion_scale_y);
    out<<"],\"normalizerMVScaleWorkPx\":[";JsonFloat(out,j.constants.motion_scale[0]);out<<',';JsonFloat(out,j.constants.motion_scale[1]);
    out<<"],\"depthExtent\":["<<j.constants.depth[0]<<','<<j.constants.depth[1]<<"],\"motionExtent\":["<<j.constants.motion[0]<<','<<j.constants.motion[1]
        <<"],\"depthOrigin\":["<<j.constants.depth_origin[0]<<','<<j.constants.depth_origin[1]<<"],\"motionOrigin\":["<<j.constants.motion_origin[0]<<','<<j.constants.motion_origin[1]
        <<"],\"linearDepth\":"<<j.constants.linear_depth<<",\"invertedDepth\":"<<j.constants.inverted<<",\"projectionAB\":[";
    JsonFloat(out,j.constants.projection_a);out<<',';JsonFloat(out,j.constants.projection_b);
    out<<"],\"phase\":\"native-render-raster-to-post-reconstruction-N-grid\",\"rasterConvention\":"<<unsigned(j.guides.raster_convention)
        <<",\"actualBindJitterWorkPx\":[0,0],\"actualBindMVScaleWorkPx\":[1,1]},\"exposure\":{\"nativeScalarKnownMask\":"<<j.exposure_known<<",\"nativePreExposure\":";
    JsonFloat(out,j.pre_exposure);out<<",\"nativeExposureScale\":";JsonFloat(out,j.exposure_scale);
    out<<",\"nativeTexturePresent\":"<<(j.exposure_texture?"true":"false")<<",\"nativeTextureFormat\":"<<j.exposure_format
        <<",\"nativeTextureExtent\":["<<j.exposure_width<<','<<j.exposure_height<<"],\"nativeTexturePixels\":\"not-captured\","
        "\"actualPrivateBindPreExposure\":1,\"actualPrivateBindExposureScale\":1,\"actualPrivateBindExposureTexture\":null},"
        "\"derivations\":{\"E_N\":\"offline-area-downsample-of-same-source-Z-minus-B\",\"support\":\"raw-tail-alpha-1-or-2\","
        "\"ZEqualsB\":\"offline-from-raw-B-and-Z\",\"spatialReference\":\"not-captured\"},\"supportPolicy\":\""
        <<(j.per_channel_support?"per-channel-stored-carrier-neutral":"legacy-any-channel-neighborhood")
        <<"\",\"timingExcluded\":true,\"performanceSample\":\"capture_period_not_performance_sample\",\"gameAcceptance\":\"unjudged\",\"fullPrivateNGXInputs\":"<<(pixels_valid?"true":"false")
        <<",\"nativeNGXReplayComplete\":false,\"budgets\":{\"residentBytes\":"<<j.resident<<",\"sessionResidentCap\":"<<j.resident_cap
        <<",\"processResidentCap\":"<<kResidentCap<<",\"processDiskCap\":"<<kDiskCap<<",\"frameCap\":"<<kFrameCap<<"},\"planes\":[\n";
    for(unsigned n=0;n<kPlanes;++n){const auto& p=j.planes[n];if(n)out<<",\n";
        out<<"{\"role\":\""<<kNames[n]<<"\",\"file\":\"attempt-"<<attempt<<'-'<<kNames[n]
            <<".bin\",\"copied\":"<<(p.copied?"true":"false")<<",\"saved\":"<<((saved&(1u<<n))?"true":"false")
            <<",\"resource\":"<<p.resource<<",\"resourceFormat\":"<<unsigned(p.description.Format)<<",\"viewFormat\":"<<p.view_format
            <<",\"copyFormat\":"<<unsigned(p.footprint.Footprint.Format)<<",\"extent\":["<<p.description.Width<<','<<p.description.Height
            <<"],\"subrect\":";JsonRect(out,p.rect);
        out<<",\"mip\":0,\"plane\":0,\"rows\":"<<p.rows<<",\"rowBytes\":"<<p.row_bytes<<",\"gpuRowPitch\":"<<p.footprint.Footprint.RowPitch
            <<",\"fileBytes\":"<<p.row_bytes*p.rows<<",\"fileLayout\":\"tight-native-rows-little-endian\",\"stateRestored\":"<<p.state<<'}';
    }
    out<<"\n]}\n";return out.str();
}
void SaveSummary(const Job& job,const Series& series) {
    wchar_t path[MAX_PATH]{};_snwprintf_s(path,_TRUNCATE,L"%s\\session-%02u.json",worker.folder,job.session);
    Text out;out<<"{\"schema\":\"dlss5-private-sr-session-v1\",\"session\":"<<job.session
        <<",\"requestedFrames\":"<<series.requested<<",\"seenFrames\":"<<series.seen<<",\"sourceFirst\":";JsonSource(out,job.session_first);
    out<<",\"frameMask\":"<<series.present<<",\"anyFailedFrame\":"<<(series.failed?"true":"false")
        <<",\"continuousPixelsComplete\":"<<(series.Complete()?"true":"false")
        <<",\"status\":\""<<(series.Complete()?"complete":"insufficient-or-failed")
        <<"\",\"timingExcluded\":true,\"gameAcceptance\":\"unjudged\"}\n";
    (void)WriteText(path,out.str(),true);
}
void SaveJob(Job& job,std::array<Series,kSessions>& series) {
    const bool proof=CompletionProof(job.submitted,job.released,job.queue,job.recording.queue,job.fence_value,job.completed);
    unsigned saved=0;bool valid=proof&&job.composed&&job.bind.bound&&std::strcmp(job.failure,"none")==0;
    ULARGE_INTEGER free{};
    if(valid&&(!GetDiskFreeSpaceExW(worker.folder,&free,nullptr,nullptr)||
        free.QuadPart<kDiskReserve||job.payload>free.QuadPart-kDiskReserve)){job.failure="disk-headroom";valid=false;}
    if(valid)for(unsigned n=0;n<kPlanes;++n){
        if(SavePlane(job,n))saved|=1u<<n;else{job.failure="plane-map-or-write-failed";valid=false;break;}
    }
    valid=valid&&saved==((1u<<kPlanes)-1);
    wchar_t path[MAX_PATH]{};OutputPath(path,job.attempt,L"frame.json");
    const auto metadata=Metadata(job,valid,saved);
    if(metadata.size()>16384||!WriteText(path,metadata)){valid=false;job.failure="frame-metadata-write-failed";}
    if(job.session&&job.session<=kSessions){auto& s=series[job.session-1];
        if(!s.requested){s.requested=job.requested;s.first=SequenceOf(job.session_first);}
        s.Observe(job.index,SequenceOf(job.source),valid);SaveSummary(job,s);
    }
    Log("[private-sr-capture] attempt=%u session=%u frame=%u/%u pixels=%u reason=%s; %ls; quality unjudged",
        job.attempt,job.session,job.index,job.requested,valid?1u:0u,job.failure,path);
}
DWORD WINAPI WorkerMain(void*) {
    const auto module=worker.module;
    std::array<Series,kSessions> series{};
    bool ready=false;
    try {
        const auto attributes=GetFileAttributesW(worker.directory);
        ULARGE_INTEGER free{};
        if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_DIRECTORY)&&
            GetDiskFreeSpaceExW(worker.directory,&free,nullptr,nullptr)&&free.QuadPart>kDiskReserve){
            _snwprintf_s(worker.folder,_TRUNCATE,L"%s\\private-sr-%lu-%llu",worker.directory,GetCurrentProcessId(),GetTickCount64());
            if(CreateDirectoryW(worker.folder,nullptr)){
                wchar_t path[MAX_PATH]{};_snwprintf_s(path,_TRUNCATE,L"%s\\capture.json",worker.folder);
                ready=WriteText(path,"{\"schema\":\"dlss5-private-sr-capture-v1\",\"defaultEnabled\":false,"
                    "\"maxAttempts\":64,\"maxSessions\":8,\"residentCapBytes\":8589934592,\"diskCapBytes\":8589934592,"
                    "\"physicalReserveBytes\":4294967296,\"diskReserveBytes\":1073741824,\"timingExcluded\":true,"
                    "\"producer\":\"product-Capture-Finish-exact-Flight\",\"gameAcceptance\":\"unjudged\"}\n");
            }
        }
    }catch(...){ready=false;}
    worker.resident_cap.store(ready?PhysicalCap():0,std::memory_order_relaxed);
    if(!worker.resident_cap.load())ready=false;
    worker.state.store(ready?2u:3u,std::memory_order_release);
    if(!ready)Log("[private-sr-capture] output directory or initial physical/disk budget unavailable; rendering unchanged");
    for(;;){
        Job* job=nullptr;
        AcquireSRWLockExclusive(&worker.lock);
        if(worker.read!=worker.write){job=worker.jobs[worker.read%kAttempts];worker.jobs[worker.read%kAttempts]=nullptr;++worker.read;}
        ReleaseSRWLockExclusive(&worker.lock);
        if(job){try{SaveJob(*job,series);}catch(...){Log("[private-sr-capture] output failed: bounded worker exception; no quality proof");}
            ReleaseJob(job);continue;}
        bool stop=false;
        AcquireSRWLockExclusive(&worker.lock);
        if(worker.read==worker.write&&!worker.users.load(std::memory_order_acquire)&&!worker.leases.load(std::memory_order_acquire)){
            worker.state.store(4,std::memory_order_release);stop=true;
        }
        ReleaseSRWLockExclusive(&worker.lock);
        if(stop)break;
        WaitForSingleObject(worker.wake,250); // CPU worker only; never a GPU fence wait.
    }
    // Ready -> stopped is published while holding the same short lock used by
    // Enable/Close. A new owner cannot acquire a reference to an exiting thread.
    CloseHandle(worker.wake);
    if(module)FreeLibraryAndExitThread(module,0);return 0;
}
bool Enable(Run& run,unsigned requested) {
    if(requested!=16&&requested!=32)return false;
    const auto state=worker.state.load(std::memory_order_acquire);
    if(state==3||state==4)return false;
    if(!state){
        wchar_t setting[MAX_PATH]{};
        const auto length=GetEnvironmentVariableW(L"DLSS5_PRIVATE_SR_EVIDENCE_DIR",setting,MAX_PATH);
        // No fallback into game, product, current directory or a drive root.
        if(!length||length>=MAX_PATH-64||!((setting[0]>=L'A'&&setting[0]<=L'Z')||(setting[0]>=L'a'&&setting[0]<=L'z'))||
           setting[1]!=L':'||(setting[2]!=L'\\'&&setting[2]!=L'/')||length<=3){
            Log("[private-sr-capture] DLSS5_PRIVATE_SR_EVIDENCE_DIR must name an existing absolute directory; metadata only");return false;
        }
        const auto canonical=GetFullPathNameW(setting,MAX_PATH,worker.directory,nullptr);
        if(!canonical||canonical>=MAX_PATH-64)return false;
        worker.wake=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!worker.wake)return false;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&WorkerMain),&worker.module)){CloseHandle(worker.wake);return false;}
        worker.users.fetch_add(1);worker.state.store(1,std::memory_order_release);
        HANDLE thread=CreateThread(nullptr,0,WorkerMain,nullptr,0,nullptr);
        if(!thread){worker.users.fetch_sub(1);worker.state.store(3);CloseHandle(worker.wake);FreeLibrary(worker.module);return false;}
        CloseHandle(thread);
    }else{
        AcquireSRWLockExclusive(&worker.lock);
        if(worker.state.load(std::memory_order_acquire)>=3){ReleaseSRWLockExclusive(&worker.lock);return false;}
        worker.users.fetch_add(1);ReleaseSRWLockExclusive(&worker.lock);
    }
    worker.active_runs.fetch_add(1);run.enabled=true;run.requested=requested;return true;
}
void Close(Run& run) {
    if(!run.enabled)return;run.enabled=false;
    if(!run.finished)worker.active_runs.fetch_sub(1,std::memory_order_relaxed);
    AcquireSRWLockExclusive(&worker.lock);
    worker.users.fetch_sub(1,std::memory_order_release);
    if(worker.state.load(std::memory_order_acquire)<4)SetEvent(worker.wake);
    ReleaseSRWLockExclusive(&worker.lock);
}
bool Active() {
    const auto state=worker.state.load(std::memory_order_acquire);
    return state==1||(state==2&&worker.active_runs.load(std::memory_order_relaxed)!=0)||worker.leases.load(std::memory_order_relaxed)!=0;
}
bool AddPlane(Job& job,unsigned role,ID3D12Device* device,D3D12_RESOURCE_DESC desc,
              unsigned format,nr_post_residual_dx12::Rect rect) {
    auto& p=job.planes[role];if(p.readback)return false;
    if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||desc.Width>16384||!desc.Width||
       desc.Height>16384||!desc.Height||desc.DepthOrArraySize!=1||desc.MipLevels!=1||desc.SampleDesc.Count!=1||
       !rect.width||!rect.height||rect.x>desc.Width||rect.width>desc.Width-rect.x||
       rect.y>desc.Height||rect.height>desc.Height-rect.y){job.failure="unsupported-full-plane-extent";return false;}
    D3D12_FEATURE_DATA_FORMAT_INFO info{};info.Format=desc.Format;
    if(FAILED(ID3D12Device_CheckFeatureSupport(device,D3D12_FEATURE_FORMAT_INFO,&info,sizeof(info)))||info.PlaneCount!=1){
        job.failure="unsupported-multiplane-format";return false;
    }
    ID3D12Device_GetCopyableFootprints(device,&desc,0,1,0,&p.footprint,&p.rows,&p.row_bytes,&p.buffer_bytes);
    if(!p.buffer_bytes||p.buffer_bytes==UINT64_MAX||!p.rows||!p.row_bytes){job.failure="invalid-copy-footprint";return false;}
    D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=p.buffer_bytes;
    buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    const auto allocation=ID3D12Device_GetResourceAllocationInfo(device,0,1,&buffer);
    if(!allocation.SizeInBytes||allocation.SizeInBytes==UINT64_MAX||!Fits(job.resident,allocation.SizeInBytes,kFrameCap)||
       !Reserve(worker.resident,allocation.SizeInBytes,std::min(job.resident_cap,worker.resident_cap.load()))){
        job.failure="readback-resident-budget";return false;
    }
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;heap.CreationNodeMask=heap.VisibleNodeMask=1;
    if(FAILED(ID3D12Device_CreateCommittedResource(device,&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,IID_ID3D12Resource,reinterpret_cast<void**>(&p.readback)))){
        worker.resident.fetch_sub(allocation.SizeInBytes);job.failure="readback-allocation-failed";return false;
    }
    p.description=desc;p.view_format=format;p.rect=rect;p.allocation_bytes=allocation.SizeInBytes;
    job.resident+=allocation.SizeInBytes;job.payload+=p.row_bytes*p.rows;return true;
}
bool AddResource(Job& job,unsigned role,ID3D12Device* device,View view,nr_post_residual_dx12::Rect rect) {
    if(!view.resource||!view.state_known||view.mip){job.failure="missing-resource-state";return false;}
    ID3D12Device* owner=nullptr;
    const auto hr=ID3D12Resource_GetDevice(view.resource,IID_ID3D12Device,reinterpret_cast<void**>(&owner));
    const bool same=SUCCEEDED(hr)&&owner&&IsSameRrDepthDevice(owner,device);ReleaseCom(owner);
    if(!same){job.failure="resource-device-mismatch";return false;}
    if(!AddPlane(job,role,device,ID3D12Resource_GetDesc(view.resource),view.format,rect))return false;
    auto& p=job.planes[role];p.input=view.resource;ID3D12Resource_AddRef(p.input);p.resource=reinterpret_cast<Id>(p.input);p.state=view.state;
    return true;
}
bool BindPlanned(Job& job,unsigned role,View view) {
    auto& p=job.planes[role];
    if(!view.resource||!view.state_known||view.mip||!p.readback){job.failure="recorded-plane-missing";return false;}
    const auto d=ID3D12Resource_GetDesc(view.resource);
    if(d.Dimension!=p.description.Dimension||d.Width!=p.description.Width||d.Height!=p.description.Height||
       d.Format!=p.description.Format||d.DepthOrArraySize!=1||d.MipLevels!=1||d.SampleDesc.Count!=1||view.format!=p.view_format){
        job.failure="recorded-plane-contract-mismatch";return false;
    }
    p.input=view.resource;ID3D12Resource_AddRef(p.input);p.resource=reinterpret_cast<Id>(p.input);p.state=view.state;return true;
}
void CopyPlane(ID3D12GraphicsCommandList* list,Job& job,unsigned role) {
    auto& p=job.planes[role];
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={p.input,0,static_cast<D3D12_RESOURCE_STATES>(p.state),D3D12_RESOURCE_STATE_COPY_SOURCE};
    if(p.state!=D3D12_RESOURCE_STATE_COPY_SOURCE)ID3D12GraphicsCommandList_ResourceBarrier(list,1,&barrier);
    D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=p.input;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.pResource=p.readback;to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=p.footprint;
    // Whole mip/plane, including physical border and the proven subrect. No ROI
    // is substituted for a legal Feature1 replay input or its full N-grid guides.
    ID3D12GraphicsCommandList_CopyTextureRegion(list,&to,0,0,0,&from,nullptr);
    if(p.state!=D3D12_RESOURCE_STATE_COPY_SOURCE){std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);
        ID3D12GraphicsCommandList_ResourceBarrier(list,1,&barrier);}
    p.copied=true;
}
Job* Begin(Run& run,ID3D12Device* device,ID3D12GraphicsCommandList* list,
           const nr_private_sr_guides::Input& guides,const NgxParams* params,unsigned color_mode,
           bool native_reset_known,bool native_reset,bool source_gap,Id native_generation) {
    if(!run.enabled||run.finished||worker.state.load(std::memory_order_acquire)!=2)return nullptr;
    auto attempt=worker.attempts.load(std::memory_order_relaxed);
    do{if(attempt>=kAttempts){run.finished=true;worker.active_runs.fetch_sub(1);return nullptr;}}
    while(!worker.attempts.compare_exchange_weak(attempt,attempt+1,std::memory_order_relaxed));
    auto* job=new(std::nothrow)Job;if(!job)return nullptr;worker.leases.fetch_add(1);
    job->attempt=attempt+1;job->requested=run.requested;job->guides=guides;job->recording=guides.recording;
    job->source=guides.source;job->reference=guides.motion_reference;job->color_mode=color_mode;job->native_generation=native_generation;
    job->native_reset_known=native_reset_known;job->native_reset=native_reset;job->source_gap=source_gap;
    job->constants=nr_private_sr_guides::PrepareScalars(guides).constants;
    if(!Adjacent(SequenceOf(run.previous),SequenceOf(job->source)))run.resident_cap=PhysicalCap();
    job->resident_cap=std::min(run.resident_cap,worker.resident_cap.load());
    if(GetFloat(params,"DLSS.Pre.Exposure",&job->pre_exposure))job->exposure_known|=1;
    if(GetFloat(params,"DLSS.Exposure.Scale",&job->exposure_scale))job->exposure_known|=2;
    ID3D12Resource* exposure=nullptr;GetResource(params,"ExposureTexture",&exposure);
    if(exposure){const auto d=ID3D12Resource_GetDesc(exposure);job->exposure_texture=true;
        job->exposure_format=unsigned(d.Format);job->exposure_width=unsigned(std::min<Id>(d.Width,UINT_MAX));job->exposure_height=d.Height;}
    const auto& s=guides.signature;
    if(AddResource(*job,RawDepth,device,guides.depth,{s.depth_x,s.depth_y,s.depth_width,s.depth_height})&&
       AddResource(*job,RawMotion,device,guides.motion,{s.motion_x,s.motion_y,s.motion_source_width,s.motion_source_height})){
        CopyPlane(list,*job,RawDepth);CopyPlane(list,*job,RawMotion);
    }
    return job;
}
bool BeforeRecord(Job& job,ID3D12Device* device,ID3D12GraphicsCommandList* list,
                  const nr_post_private_sr_dx12::Input& input) {
    job.view=input.view;job.configuration=input.configuration;job.encoding=input.encoding_generation;
    job.per_channel_support=input.per_channel_support;
    if(std::strcmp(job.failure,"none")!=0)return false;
    if(!AddResource(job,Base,device,input.pair.base,input.pair.base_rect)||
       !AddResource(job,Candidate,device,input.pair.candidate,input.pair.candidate_rect)||
       !AddResource(job,Depth,device,input.depth,{0,0,input.pair.work_width,input.pair.work_height})||
       !AddResource(job,Motion,device,input.motion,{0,0,input.pair.work_width,input.pair.work_height}))return false;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Width=input.pair.work_width;desc.Height=input.pair.work_height;desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    if(!AddPlane(job,Carrier,device,desc,10,{0,0,unsigned(desc.Width),desc.Height}))return false;
    desc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
    if(!AddPlane(job,Tail,device,desc,2,{0,0,unsigned(desc.Width),desc.Height}))return false;
    desc.Width=job.guides.output_width;desc.Height=job.guides.output_height;desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    if(!AddPlane(job,Private,device,desc,10,{0,0,unsigned(desc.Width),desc.Height})||
       !AddResource(job,Final,device,input.target,input.target_rect))return false;
    // The candidate is the complete, controlled multi-layer Z before Compose.
    CopyPlane(list,job,Base);CopyPlane(list,job,Candidate);return true;
}
bool SameContract(const Run& run,const Job& job) {
    const auto& a=run.contract;const auto& b=job.guides;
    const auto rect=[](nr_post_residual_dx12::Rect a,nr_post_residual_dx12::Rect b){
        return a.x==b.x&&a.y==b.y&&a.width==b.width&&a.height==b.height;};
    return run.encoding==job.encoding&&run.native_generation==job.native_generation&&
        a.native_feature==b.native_feature&&a.native_flags==b.native_flags&&
        a.render_width==b.render_width&&a.render_height==b.render_height&&
        a.work_width==b.work_width&&a.work_height==b.work_height&&a.output_width==b.output_width&&a.output_height==b.output_height&&
        a.original_motion_scale_x==b.original_motion_scale_x&&a.original_motion_scale_y==b.original_motion_scale_y&&
        SameNrGuideMappingContract(a.signature,b.signature)&&
        rect(run.base_rect,job.planes[Base].rect)&&rect(run.candidate_rect,job.planes[Candidate].rect);
}
void AfterRecord(Run& run,Job& job,ID3D12GraphicsCommandList* list,
                 const nr_post_private_sr_dx12::Result& result,const nr_post_private_sr_dx12::RecordedPlanes& planes) {
    job.bind=result.bind;job.result=result.sr_result;job.status=unsigned(result.status);job.composed=result.composed;
    if(!result.composed){job.failure="private-not-composed";return;}
    if(std::strcmp(job.failure,"none")!=0)return;
    const View carrier{planes.carrier,10,0,D3D12_RESOURCE_STATE_COMMON,true};
    const View tail{planes.tail,2,0,D3D12_RESOURCE_STATE_COMMON,true};
    const View reconstructed{planes.reconstructed,10,0,D3D12_RESOURCE_STATE_COMMON,true};
    if(!planes||!BindPlanned(job,Carrier,carrier)||!BindPlanned(job,Tail,tail)||!BindPlanned(job,Private,reconstructed))return;
    if(!Reserve(worker.disk,job.payload,kDiskCap-(2ull<<20))){job.failure="process-disk-budget";return;}
    job.disk_reserved=true;
    job.previous_capture=run.previous;
    const bool adjacent=Adjacent(SequenceOf(run.previous),SequenceOf(job.source));
    const bool same=SameContract(run,job);
    if(!run.session||!adjacent||!same){
        const auto session=worker.sessions.fetch_add(1);
        if(session>=kSessions){job.failure="session-budget";run.finished=true;worker.active_runs.fetch_sub(1);return;}
        job.session_reason=!run.session?"first-valid-source":!adjacent?"source-or-capture-gap":"configuration-or-guide-contract-change";
        run.session=session+1;run.count=0;run.first=job.source;run.contract=job.guides;
        run.encoding=job.encoding;run.native_generation=job.native_generation;
        run.base_rect=job.planes[Base].rect;run.candidate_rect=job.planes[Candidate].rect;
    }else job.session_reason="adjacent-same-contract";
    job.session=run.session;job.session_first=run.first;job.index=++run.count;run.previous=job.source;
    for(unsigned n:{Carrier,Tail,Private,Final,Depth,Motion})CopyPlane(list,job,n);
    run.finished=run.count>=run.requested;
    if(run.finished)worker.active_runs.fetch_sub(1);
}
void Handoff(Job*& job,Recording recording,ID3D12Fence* fence,Id value,Id completed,bool submitted,bool released) {
    if(!job)return;
    // Called only by the existing product Collect proof. Keep the check local
    // as well; an uncompleted or replayable recording must retain every lease.
    if(!(recording==job->recording)||!CompletionProof(submitted,released,recording.queue,job->recording.queue,value,completed))return;
    job->queue=recording.queue;job->fence=reinterpret_cast<Id>(fence);job->fence_value=value;
    job->completed=completed;job->submitted=submitted;job->released=released;
    for(auto& p:job->planes)ReleaseCom(p.input); // GPU source uses are proven finished.
    if(!job->composed&&std::strcmp(job->failure,"none")==0)job->failure="finish-not-recorded";
    AcquireSRWLockExclusive(&worker.lock);
    if(worker.write-worker.read>=kAttempts){ReleaseSRWLockExclusive(&worker.lock);
        Log("[private-sr-capture] bounded worker queue unavailable; capture skipped");ReleaseJob(job);return;}
    worker.jobs[worker.write%kAttempts]=job;++worker.write;job=nullptr;
    SetEvent(worker.wake);ReleaseSRWLockExclusive(&worker.lock);
}
}
