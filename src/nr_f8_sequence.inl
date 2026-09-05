// Control runs under g_control, metadata under g_lock. Never acquire g_chain_cs
// while holding g_lock. GPU callbacks only publish small immutable records.
namespace nr_f8 {
constexpr unsigned kFrames = 16, kSlots = 17, kGroups = 3;
enum class Status { Idle, Requested, Running, Packaging, Complete, Partial, Failed };
enum class Comparison { Standard, Current, Strength, Protection, Jitter };
struct Options { Comparison comparison = Comparison::Current; bool custom_roi = false; unsigned x = 0, y = 0; };
struct Frame {
    NrStageCaptureInfo info = {};
    NrCaptureParams params = {};
    wchar_t spool[1024] = {};
    UINT64 bytes = 0;
    bool reserved = false, delivered = false, valid = false;
};
struct Group {
    ChainConfig requested = {};
    ChainConfig actual = {};
    unsigned generation = 0, count = 0, delivered = 0;
    unsigned long long started = 0, shot_present = 0;
    bool applied = false, sealed = false, partial = false;
    bool shot_requested = false, shot_received = false;
    char reason[160] = {};
    wchar_t screenshot[1024] = {};
    Frame frames[kSlots] = {};
};
struct Session {
    unsigned long long id = 0, started = 0;
    Options options = {};
    ChainConfig original = {};
    Group groups[kGroups] = {};
    unsigned group = 0, group_count = 3;
    bool restored = false, partial = false;
    UINT64 payload_bytes = 0;
    wchar_t directory[1024] = {};
    FeedbackSession activity = {};
    nr_core_feedback::PairSnapshot core_pair = {};
    reshade::api::effect_runtime *runtime = nullptr;
};
#include "nr_f8_stream.inl"
SRWLOCK g_lock = SRWLOCK_INIT, g_control = SRWLOCK_INIT;
std::atomic<Status> g_status{Status::Idle};
std::atomic<bool> g_config_hold{false}, g_cancel{false}, g_window_open{false};
std::atomic<unsigned long long> g_present{0};
std::atomic<unsigned> g_hotkey{VK_F8};
Session *g_session = nullptr;
Options g_options = {};
unsigned long long g_next_session = 0;
wchar_t g_last_zip[1024] = {};

bool IsActive() { const auto s=g_status.load(); return s==Status::Requested || s==Status::Running; }
void OpenWindow() { g_window_open.store(true); }
void Cancel() {
    g_cancel.store(true);
    if (g_status.load()==Status::Requested) nr_core_feedback::Cancel();
}
bool RequestCore() {
    AcquireSRWLockExclusive(&g_control);
    const bool accepted = !IsActive() && g_status.load()!=Status::Packaging &&
        StageCaptureStatus()!=1 && StageCaptureStatus()!=2 && nr_core_feedback::Request();
    ReleaseSRWLockExclusive(&g_control);
    return accepted;
}
bool RequestWithOptions(const Options &options) {
    AcquireSRWLockExclusive(&g_control);
    bool accepted = !IsActive() && g_status.load()!=Status::Packaging && StageCaptureStatus()!=1 && StageCaptureStatus()!=2 && nr_core_feedback::Request();
    if (accepted) {
        g_options=options; g_cancel.store(false);
        AcquireSRWLockExclusive(&g_lock); g_last_zip[0]=0; ReleaseSRWLockExclusive(&g_lock);
        g_status.store(Status::Requested);
    }
    ReleaseSRWLockExclusive(&g_control);
    return accepted;
}
ChainConfig GroupConfig(const ChainConfig &base, Comparison kind, unsigned index) {
    ChainConfig c=base;
    if (kind==Comparison::Standard) { c.enabled=index!=0; c.mode=index==2?1:2; }
    else if (kind!=Comparison::Current) {
        c.enabled=true;
        if (kind==Comparison::Strength) c.transfer_strength=c.post_transfer_strength=index==0?1.f:index==1?1.2f:1.5f;
        if (kind==Comparison::Protection) { c.mode=2; c.detail_stability=index==0?0.f:1.f; }
        if (kind==Comparison::Jitter) c.jitter_mode=index==0?NrJitterMode::Legacy:NrJitterMode::GameInputPixel;
    }
    if (kind!=Comparison::Current) NormalizeConfig(c);
    return c;
}
void Break(Group &g, const char *reason) {
    g.partial=true; g.sealed=true;
    if (!g.reason[0]) std::snprintf(g.reason,sizeof(g.reason),"%s",reason);
}

// Called only for an actual successful composition, under the chain lock.
// Frames are reserved at record time; readiness is driven by exact-fence pixels.
bool ReserveFrame(NrStageCaptureInfo &info, const NrCaptureParams &params, FrameTicket &ticket) {
    if (g_status.load()!=Status::Running || !TryAcquireSRWLockExclusive(&g_lock)) return false;
    Session *s=g_session;
    bool accepted=false;
    if (s && s->group<s->group_count) {
        Group &g=s->groups[s->group];
        if (g.applied && g.requested.enabled && !g.sealed && g.count<kSlots &&
            GetTickCount64()-g.started>=3000 && info.identity.warmed_up &&
            info.identity.since_reset>=64 && info.identity.config_generation==g.generation &&
            g.generation==g_config_generation.load() && info.mode==g.requested.mode) {
            if (g.count>0 && g.count<kFrames) {
                const auto &last=g.frames[g.count-1].info;
                if (last.identity.instance_id!=info.identity.instance_id ||
                    last.feature_generation!=info.feature_generation ||
                    (last.codec_flags&~8u)!=(info.codec_flags&~8u) ||
                    last.source_format!=info.source_format || last.result_format!=info.result_format ||
                    last.identity.frame_id+1!=info.identity.frame_id ||
                    last.source_width!=info.source_width || last.source_height!=info.source_height ||
                    last.model_width!=info.model_width || last.model_height!=info.model_height ||
                    params.reset) Break(g,"reset, instance/size change or gap in successful composition frames");
            }
            if (!g.sealed && info.source_width && info.source_height) {
                const bool overview=g.count==kFrames;
                if (overview) {
                    UINT64 bytes=0;
                    if (!ComputeStageCaptureGrid(info.source_width,info.source_height,
                        info.stride,info.width,info.height,bytes)) {
                        Break(g,"unsupported overview dimensions");
                        ReleaseSRWLockExclusive(&g_lock); return false;
                    }
                    info.region_x=info.region_y=info.region_width=info.region_height=0;
                } else {
                    info.stride=1;
                    info.width=info.region_width=std::min(256u,info.source_width);
                    info.height=info.region_height=std::min(256u,info.source_height);
                    info.region_x=s->options.custom_roi?std::min(s->options.x,info.source_width-info.width):(info.source_width-info.width)/2;
                    info.region_y=s->options.custom_roi?std::min(s->options.y,info.source_height-info.height):(info.source_height-info.height)/2;
                }
                const UINT64 bytes=static_cast<UINT64>(info.width)*info.height*80u+16u;
                if (bytes>nr_f8_budget::kPayloadLimit-s->payload_bytes) {
                    Break(g,"session payload budget exhausted");
                    ReleaseSRWLockExclusive(&g_lock); return false;
                }
                s->payload_bytes+=bytes;
                ticket={s->id,s->group,g.count};
                Frame &f=g.frames[g.count++]; f.info=info; f.params=params; f.reserved=true;
                if (g.count==kSlots) g.sealed=true;
                accepted=true;
            }
        }
    }
    ReleaseSRWLockExclusive(&g_lock); return accepted;
}
struct ReadbackJob {
    FrameTicket ticket; NrStageCaptureInfo info; NrCaptureParams params;
    ChainConfig requested, actual; ID3D12Resource *readback; UINT64 bytes; bool ok;
    wchar_t spool[1024] = {};
};
DWORD WINAPI ReadbackWorker(void *opaque) {
    auto *job=static_cast<ReadbackJob *>(opaque);
    bool ok=job->ok && job->readback && job->bytes>=16 && job->bytes<=40ull*1024*1024;
    void *mapped=nullptr;
    if(ok) {
        const D3D12_RANGE range={0,static_cast<SIZE_T>(job->bytes)};
        ok=SUCCEEDED(ID3D12Resource_Map(job->readback,0,&range,&mapped)) && mapped;
        if(ok) {
            const auto *e=reinterpret_cast<const float *>(static_cast<const unsigned char *>(mapped)+job->bytes-16);
            job->info.gpu_raw_exposure=e[0];job->info.gpu_resolved_exposure=e[1];
            job->info.gpu_source_to_canonical=e[2];job->info.gpu_pre_exposure=e[3];
            HANDLE file=CreateFileW(job->spool,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            ok=file!=INVALID_HANDLE_VALUE;
            if(ok){ok=WriteRecord(file,job->ticket,job->info,job->params,job->requested,job->actual,mapped,job->bytes-16,true);CloseHandle(file);}
            const D3D12_RANGE written={0,0};ID3D12Resource_Unmap(job->readback,0,&written);
        }
    }
    if(job->readback){ReleaseCom(job->readback);nr_f8_budget::cpu_bytes.fetch_sub(job->bytes+nr_f8_budget::kWorkerReserve);}
    bool retained=false;
    AcquireSRWLockExclusive(&g_lock);
    if(g_session && g_session->id==job->ticket.session && job->ticket.group<g_session->group_count && job->ticket.index<kSlots) {
        Group &g=g_session->groups[job->ticket.group];Frame &f=g.frames[job->ticket.index];
        if(f.reserved && !f.delivered && f.info.identity.frame_id==job->info.identity.frame_id &&
           f.info.identity.instance_id==job->info.identity.instance_id && f.info.identity.config_generation==job->info.identity.config_generation) {
            f.delivered=true;f.valid=ok;f.info=job->info;++g.delivered;
            if(ok){wcscpy_s(f.spool,job->spool);f.bytes=job->bytes-16;retained=true;}
            else Break(g,"GPU proof, budget, readback or stream write failed");
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    if(!retained && job->spool[0])DeleteFileW(job->spool);
    delete job;return 0;
}
void RawReady(FrameTicket ticket,NrStageCaptureInfo info,ID3D12Resource *readback,UINT64 bytes,bool ok) {
    auto *job=new(std::nothrow) ReadbackJob{};bool current=false;
    if(job) {
        job->ticket=ticket;job->info=info;job->readback=readback;job->bytes=bytes;job->ok=ok;
        AcquireSRWLockExclusive(&g_lock);
        if(g_session && g_session->id==ticket.session && ticket.group<g_session->group_count && ticket.index<kSlots) {
            const auto &g=g_session->groups[ticket.group];job->requested=g.requested;job->actual=g.actual;
            job->params=g.frames[ticket.index].params;
            swprintf_s(job->spool,L"%ls\\frame-%u-%u.tmp",g_session->directory,ticket.group,ticket.index);current=true;
        }
        ReleaseSRWLockExclusive(&g_lock);
        if(current && QueueUserWorkItem(ReadbackWorker,job,WT_EXECUTEDEFAULT))return;
    }
    if(readback){ReleaseCom(readback);nr_f8_budget::cpu_bytes.fetch_sub(bytes+nr_f8_budget::kWorkerReserve);}
    delete job; // Timeout retains explicit missing records; render thread never maps or writes.
}
void Screenshot(reshade::api::effect_runtime *runtime,const char *path) {
    if (!path) return;
    AcquireSRWLockExclusive(&g_lock);
    if (g_session && g_session->runtime==runtime && g_session->group<g_session->group_count) {
        Group &g=g_session->groups[g_session->group];
        char tag[80]={}; std::snprintf(tag,sizeof(tag),"DLSS5_F8_%llu_%u",g_session->id,g_session->group);
        const char *basename=path;
        for(const char *p=path;*p;++p)if(*p=='/' || *p=='\\')basename=p+1;
        const char *extension=std::strrchr(basename,'.');
        const std::size_t tag_length=std::strlen(tag);
        const std::size_t stem_length=extension?static_cast<std::size_t>(extension-basename):0;
        const bool exact_postfix=extension && extension[1] && stem_length>=tag_length &&
            std::memcmp(basename+stem_length-tag_length,tag,tag_length)==0;
        if(g.shot_requested && !g.shot_received && exact_postfix) {
            wchar_t decoded[1024]={},canonical[1024]={};
            if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,decoded,1024)) {
                const DWORD n=GetFullPathNameW(decoded,1024,canonical,nullptr);
                if(n && n<1024 && nr_bundle::LocalAbsolutePath(canonical)) {
                    wcscpy_s(g.screenshot,canonical);g.shot_received=true;
                }
            }
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
}

bool Complete(const Group &g) {
    if (!g.applied || g.partial || !g.shot_received) return false;
    if (!g.requested.enabled) return true;
    if (g.count!=kSlots) return false;
    for (const auto &f:g.frames) if (!f.valid) return false;
    return true;
}
void WriteTimeline(FILE *out,const Session &s) {
    std::fprintf(out,"group,index,overview,valid,instance,frame,config,sinceReset,mode,sourceW,sourceH,modelW,modelH,roiX,roiY,sampleW,sampleH,stride,sourceBaseX,sourceBaseY,outputBaseX,outputBaseY,sourceFormat,outputFormat,reset,resetKnown,gameJitterValid,gameJitterX,gameJitterY,nrJitterForwarded,nrJitterX,nrJitterY,gameMvValid,gameMvX,gameMvY,nrMvX,nrMvY,depthW,depthH,mvW,mvH,preExposure,hasPreExposure,exposureScale,hasExposureScale,gpuRawExposure,gpuResolvedExposure,gpuSourceToCanonical,gpuPreExposure,transfer,color,maxLumaRatio,proxyScale,proxyCurve,frameConfidence,detailStability,lightingLock,motionRequested,motionAvailable,modelIntensity,skinStructure\n");
    for(unsigned g=0;g<s.group_count;++g) for(unsigned i=0;i<s.groups[g].count;++i) {
        const auto &f=s.groups[g].frames[i];const auto &n=f.info;const auto &p=f.params;
        std::fprintf(out,"%u,%u,%d,%d,%llu,%llu,%u,%u,%d,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%d,%d,%d,%.9g,%.9g,%d,%.9g,%.9g,%d,%.9g,%.9g,%.9g,%.9g,%u,%u,%u,%u,%.9g,%d,%.9g,%d,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%u,%.9g,%.9g,%.9g,%d,0,%.9g,%.9g\n",
            g,i,i==kFrames,f.valid,n.identity.instance_id,n.identity.frame_id,n.identity.config_generation,n.identity.since_reset,n.mode,
            n.source_width,n.source_height,n.model_width,n.model_height,n.region_x,n.region_y,n.width,n.height,n.stride,
            n.source_base_x,n.source_base_y,n.destination_base_x,n.destination_base_y,n.source_format,n.result_format,
            p.reset,p.reset_known,p.game_jitter_valid,p.game_jitter_x,p.game_jitter_y,p.nr_jitter_forwarded,p.nr_jitter_x,p.nr_jitter_y,
            p.game_mv_valid,p.game_mv_x,p.game_mv_y,p.nr_mv_x,p.nr_mv_y,p.depth_width,p.depth_height,p.mv_width,p.mv_height,
            n.pre_exposure,n.has_pre_exposure,n.exposure_scale,n.has_exposure_scale,n.gpu_raw_exposure,n.gpu_resolved_exposure,n.gpu_source_to_canonical,n.gpu_pre_exposure,
            n.transfer_strength,n.color_strength,n.max_luma_ratio,n.proxy_scale,n.proxy_curve,n.frame_confidence,n.detail_stability,n.lighting_lock,n.motion_requested,
            n.identity.tuning.intensity,n.identity.tuning.skin_structure);
    }
}
DWORD WINAPI PackageWorker(void *opaque) {
    const auto &module_identity = EnsureRuntimeIdentityOnWorker();
    auto *s=static_cast<Session *>(opaque);
    FeedbackAppend(s->activity, "capture.kind=image-supplement\ncore.session=%s\ncore.config=%u\ncore.complete=%d\n",
        s->core_pair.session, s->core_pair.config, s->core_pair.complete ? 1 : 0);
    FeedbackAppend(s->activity, "artifact.sourceCommit=%s\nartifact.sourceDirty=%d\n"
        "artifact.addonSha256=%s\nartifact.identityStatus=%s\n"
        "artifact.hashMeasurement=loaded-path-file-once-worker\n",
        kSourceCommit, kSourceDirty ? 1 : 0, module_identity.sha256, module_identity.status);nr_bundle::Result result={};result.status=nr_bundle::Status::Failed;
    bool complete=s->restored && !s->partial;
    for(unsigned i=0;i<s->group_count;++i)complete=Complete(s->groups[i]) && complete;
    FeedbackAppend(s->activity,"format=DLSS5-F8-v4\nsession=%llu\ncomparison=%u\ngroups=%u\ncapture.complete=%d\nconfig.restored=%d\nconfig.writesToIni=0\nnativeSrOutput=not-obtained\nmvDepthPixels=not-obtained; resource read contract unavailable\nmotionProtection=unavailable\npayloadBytes=%llu\ngpuBudgetBytes=%llu\ncpuBudgetBytes=%llu\n",
        s->id,static_cast<unsigned>(s->options.comparison),s->group_count,complete,s->restored,s->payload_bytes,nr_f8_budget::kGpuLimit,nr_f8_budget::kCpuLimit);
    auto *job=nr_bundle::CreateJob(s->directory,s->activity.report);
    if(job) {
        nr_bundle::AddFile(*job,g_log_path,"logs/nr-current.log",nr_bundle::FileKind::LogTail,true);
        FeedbackAddLogBeside(*job,g_log_path,L"nr-before-sr.previous.log","logs/nr-previous.log",false);
        FeedbackAddLogBeside(*job,g_log_path,L"ReShade.log","logs/reshade-addon-dir.log",false);
        wchar_t exe[1024]={},reshade_path[1024]={};GetModuleFileNameW(nullptr,exe,1024);
        FeedbackAddLogBeside(*job,exe,L"ReShade.log","logs/reshade-game-dir.log",false);
        if(GetModuleFileNameW(reshade::internal::get_reshade_module_handle(),reshade_path,1024))
            FeedbackAddLogBeside(*job,reshade_path,L"ReShade.log","logs/reshade-module-dir.log",true);
        nr_bundle::AddFile(*job,g_ini_path,"settings/nr_before_sr.ini",nr_bundle::FileKind::Copy,false);
        nr_bundle::AddFile(*job,g_reshade_ini_path,"settings/ReShade.ini",nr_bundle::FileKind::Copy,false);
        FeedbackAddModules(*job,s->activity);
        nr_bundle::CopyText(job->metadata,sizeof(job->metadata),s->activity.report);
        wchar_t path[1024]={};swprintf_s(path,L"%ls\\timeline.csv",s->directory);FILE *file=nullptr;
        _wfopen_s(&file,path,L"wb");
        if(file){WriteTimeline(file,*s);const bool ok=std::ferror(file)==0;std::fclose(file);
            if(ok)nr_bundle::AddFile(*job,path,"timeline.csv",nr_bundle::FileKind::Copy,true);
            else nr_bundle::AddMissing(*job,"timeline.csv","timeline write failed",true);}
        else nr_bundle::AddMissing(*job,"timeline.csv","timeline open failed",true);
        swprintf_s(path,L"%ls\\capture-analysis.txt",s->directory);_wfopen_s(&file,path,L"wb");
        if(file) {
            std::fprintf(file,"DLSS5 %s 本地采集 / local capture\n完整采集=%d；原设置恢复=%d；不自动上传。\n默认仅采当前状态，不启用NR也不切换模式。只有用户选择对比时临时切换。\n\n",kBuildIdentity,complete,s->restored);
            for(unsigned i=0;i<s->group_count;++i){const auto &g=s->groups[i];unsigned valid=0;for(unsigned f=0;f<std::min(g.count,kFrames);++f)valid+=g.frames[f].valid;
                std::fprintf(file,"Group %u: mode=%d enabled=%d requestedStrength=%.4g actualStrength=%.4g ROI=%u/16 overview=%d screenshotPresent=%llu complete=%d reason=%s\n",i,g.requested.mode,g.requested.enabled,ActiveTransferStrength(g.requested),ActiveTransferStrength(g.actual),valid,g.frames[16].valid,g.shot_present,Complete(g),g.reason);}
            std::fprintf(file,"\n分块格式 F8CHK004：每块最多4帧，overview单独保存。原图/代理/NR/合成/保护五平面是同一次Combine dispatch的原像素ROI；曝光尾部写入每条JSON。代理/NR按合成时工作网格映射采样。索引0-15要求NR帧连续、instance/config/尺寸相同；16为独立概览。缺失帧保留元数据而不是拼接。\n截图是独立Present，不是同帧最终SR证据。原生最终SR纹理、MV/Depth像素未取得，不能验证时域重投影。\n保护数据RGBA=权重、原因位、原始log残差、空间保护后log残差；原因1低采样/2细碎不一致/4近黑/8极端比值/16关闭/32输入无效/64零强度/128历史抑制/256无残差/512后置不可用。这是空间保护，不是人脸/HUD识别，也不证明运动保护。\n每帧JSON含完整请求/实际参数、codecFlags、曝光有效性、jitter、MV比例与实际模型tuning；请在相同颜色域分析，不独立归一化或用均值替代画质。部分包请检查manifest缺失原因。\n");
            const bool ok=std::ferror(file)==0;std::fclose(file);
            if(ok){nr_bundle::AddFile(*job,path,"capture-analysis.txt",nr_bundle::FileKind::Copy,true);
                nr_bundle::AddFile(*job,path,"分析说明.txt",nr_bundle::FileKind::Copy,true);}
            else nr_bundle::AddMissing(*job,"capture-analysis.txt","explanation write failed",true);
        } else nr_bundle::AddMissing(*job,"capture-analysis.txt","explanation open failed",true);
        for(unsigned i=0;i<s->group_count;++i) {
            const auto &g=s->groups[i];char name[128]={};
            for(unsigned chunk=0;chunk<5;++chunk) {
                if(chunk*4>=g.count)continue;
                swprintf_s(path,L"%ls\\group-%u-chunk-%u.f8c",s->directory,i,chunk);
                if(chunk==4)std::snprintf(name,sizeof(name),"groups/%u/overview.f8c",i);
                else std::snprintf(name,sizeof(name),"groups/%u/chunk-%03u.f8c",i,chunk);
                if(WriteChunk(path,*s,i,chunk))nr_bundle::AddFile(*job,path,name,nr_bundle::FileKind::Copy,true);
                else nr_bundle::AddMissing(*job,name,"chunk stream failed or exceeded 64 MiB",true);
            }
            const wchar_t *extension=wcsrchr(g.screenshot,L'.');char ext[16]={};
            if(extension)WideToUtf8(extension,ext,sizeof(ext));
            std::snprintf(name,sizeof(name),"groups/%u/display%s",i,ext[0]?ext:".missing");
            if(g.shot_received)nr_bundle::AddFile(*job,g.screenshot,name,nr_bundle::FileKind::Copy,true);
            else nr_bundle::AddMissing(*job,name,"matching screenshot callback was not received",true);
            if(!Complete(g)){std::snprintf(name,sizeof(name),"groups/%u/incomplete",i);nr_bundle::AddMissing(*job,name,g.reason[0]?g.reason:"missing exact-fence ROI, overview or screenshot",true);}
        }
        result=nr_bundle::Run(*job);nr_bundle::DestroyJob(job);
    }
    AcquireSRWLockExclusive(&g_lock);
    if(result.zip_path[0])wcscpy_s(g_last_zip,result.zip_path);
    g_status.store(result.status==nr_bundle::Status::Failed?Status::Failed:complete && result.required_complete?Status::Complete:Status::Partial);
    ReleaseSRWLockExclusive(&g_lock);
    // Every path was generated in this unique session directory, never discovered recursively.
    for(auto &g:s->groups)for(auto &f:g.frames)if(f.spool[0])DeleteFileW(f.spool);
    delete s;return 0;
}

void StartGroup(Session &s) {
    const unsigned index=s.group;
    const ChainConfig requested=GroupConfig(s.original,s.options.comparison,index);
    if(s.options.comparison!=Comparison::Current)ApplyConfig(requested,"F8 transient comparison",false);
    ChainConfig actual=SnapshotConfig();
    actual.perf_epoch=requested.perf_epoch; // internal monotonic statistics epoch is not a user setting
    AcquireSRWLockExclusive(&g_lock);
    Group &g=s.groups[index];g.requested=requested;g.actual=actual;g.generation=g_config_generation.load();
    g.applied=SameConfig(requested,actual);g.started=GetTickCount64();
    if(!g.applied)Break(g,"configuration was not applied");
    ReleaseSRWLockExclusive(&g_lock);
}
void Finish(Session *s) {
    AcquireSRWLockExclusive(&g_lock);g_session=nullptr;g_status.store(Status::Packaging);ReleaseSRWLockExclusive(&g_lock);
    if(s->options.comparison!=Comparison::Current)ApplyConfig(s->original,"F8 restore original",false);
    ChainConfig restored=SnapshotConfig();restored.perf_epoch=s->original.perf_epoch;
    s->restored=SameConfig(s->original,restored);
    g_config_hold.store(false);
    FeedbackAppendActivity(s->activity,"end");
    if(!QueueUserWorkItem(PackageWorker,s,WT_EXECUTEDEFAULT)) {
        for(auto &g:s->groups)for(auto &f:g.frames)if(f.spool[0])DeleteFileW(f.spool);delete s;g_status.store(Status::Failed);
    }
}
// Caller holds g_control. A watchdog calls this with no runtime so stopped
// Presents cannot strand transient settings. Screenshots only run on Present.
void Tick(reshade::api::effect_runtime *runtime) {
    bool advance=false,finish=false,shot=false;char tag[80]={};
    AcquireSRWLockExclusive(&g_lock);Session *s=g_session;
    if(s && s->group<s->group_count) {
        Group &g=s->groups[s->group];const auto age=GetTickCount64()-g.started;
        if(g_cancel.load() || age>=30000) {
            Break(g,g_cancel.load()?"cancelled":age>=30000?"group timeout; missing pixels retained as partial":"Ray Reconstruction owns the rendering path");
            s->partial=true;finish=true;
        } else {
            if(g.requested.enabled && (ShouldGuardFeature1ForRayReconstruction() || OtherNrOwnerActive()))
                Break(g,"RR or another NR addon owns this path; only screenshots/logs available");
            if(runtime && runtime==s->runtime && !g.shot_requested && age>=3000) {
                g.shot_requested=true;g.shot_present=g_present.load();shot=true;
                std::snprintf(tag,sizeof(tag),"DLSS5_F8_%llu_%u",s->id,s->group);
            }
            bool ready=g.applied && g.shot_received && (!g.requested.enabled || (g.sealed && g.delivered==g.count));
            if(ready) {s->partial|=!Complete(g);if(s->group+1<s->group_count){++s->group;advance=true;}else finish=true;}
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    if(shot)runtime->save_screenshot(tag); // failure is recorded by the bounded callback timeout
    if(finish && s)Finish(s);else if(advance && s)StartGroup(*s);
}
DWORD WINAPI Watchdog(void *opaque) {
    const auto session_id=static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(opaque));
    for(;;) {
        Sleep(250);AcquireSRWLockExclusive(&g_control);
        AcquireSRWLockShared(&g_lock);bool current=g_session && g_session->id==session_id;ReleaseSRWLockShared(&g_lock);
        if(current)Tick(nullptr);ReleaseSRWLockExclusive(&g_control);if(!current)break;
    }
    return 0;
}
void Present(reshade::api::effect_runtime *runtime) {
    ++g_present;
    if(!TryAcquireSRWLockExclusive(&g_control))return;
    if(g_status.load()==Status::Requested) {
        if(g_cancel.load()){nr_core_feedback::Cancel();g_status.store(Status::Failed);ReleaseSRWLockExclusive(&g_control);return;}
        if(nr_core_feedback::Busy()){ReleaseSRWLockExclusive(&g_control);return;}
        HMODULE pinned=nullptr;
        auto *s=new(std::nothrow) Session{};
        if(!s || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&Watchdog),&pinned)){
            delete s;g_status.store(Status::Failed);ReleaseSRWLockExclusive(&g_control);return;
        }
        if(!FeedbackCreateDirectory(s->directory)){delete s;g_status.store(Status::Failed);ReleaseSRWLockExclusive(&g_control);return;}
        s->id=++g_next_session;s->options=g_options;s->runtime=runtime;s->started=GetTickCount64();
        s->core_pair=nr_core_feedback::SnapshotPair();
        s->partial=!s->core_pair.ready || !s->core_pair.complete;
        s->group_count=s->options.comparison==Comparison::Current?1:(s->options.comparison==Comparison::Protection || s->options.comparison==Comparison::Jitter)?2:3;
        g_config_hold.store(true);s->original=SnapshotConfig();
        FeedbackAppend(s->activity,"build=%s\n",kBuildIdentity);FeedbackAppendActivity(s->activity,"start");
        AcquireSRWLockExclusive(&g_lock);g_session=s;g_status.store(Status::Running);ReleaseSRWLockExclusive(&g_lock);
        StartGroup(*s);
        if(!QueueUserWorkItem(Watchdog,reinterpret_cast<void *>(static_cast<std::uintptr_t>(s->id)),WT_EXECUTEDEFAULT)){s->partial=true;Finish(s);}
    }
    Tick(runtime);ReleaseSRWLockExclusive(&g_control);
}
void F8Tooltip(const char *text) {
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::BeginTooltip();ImGui::PushTextWrapPos(ImGui::GetFontSize()*32.f);
        ImGui::TextUnformatted(text);ImGui::PopTextWrapPos();ImGui::EndTooltip();
    }
}
void DrawCoreStatus() {
    const bool zh=UseChineseUi();
    const auto status=g_feedback_status.load();
    if(status==0)return;
    char path[4096]={},reason[160]={};
    AcquireSRWLockShared(&g_feedback_lock);
    WideToUtf8(g_feedback_last_zip,path,sizeof(path));
    nr_bundle::CopyText(reason,sizeof(reason),nr_core_feedback::g_reason);
    ReleaseSRWLockShared(&g_feedback_lock);
    if(status==1 || status==2) {
        ImGui::TextWrapped("%s",zh?"核心反馈：正在记录状态和生成日志小包，不等待 NR 图像。":"Core feedback: recording state and compact logs; no NR images required.");
        if(ImGui::Button(zh?"取消核心采集":"Cancel core capture"))nr_core_feedback::Cancel();
    } else {
        ImGui::TextWrapped("%s",status==3?(zh?"核心反馈包已生成。":"Core feedback package saved."):
            status==4?(zh?"核心反馈包部分完成，请保留其中的缺失原因。":"Core feedback partially complete; missing reasons are included."):
            (zh?"核心反馈保存失败，没有复用旧包路径。":"Core feedback failed; no old package path reused."));
        if(reason[0])ImGui::TextWrapped("%s",reason);
        if(path[0]){ImGui::TextWrapped("%s",path);if(ImGui::Button(zh?"复制核心包路径":"Copy core path"))ImGui::SetClipboardText(path);}
    }
}
void DrawControls() {
    const auto state=g_status.load();
    const bool use_chinese_ui=UseChineseUi();
    DrawCoreStatus();
    const bool busy=IsActive() || state==Status::Packaging || nr_core_feedback::Busy();
    ImGui::BeginDisabled(busy);
    if(ImGui::Button(use_chinese_ui?"采集核心反馈包（F8，推荐）":"Collect core feedback (F8, recommended)"))RequestCore();
    F8Tooltip(use_chinese_ui?"用于不生效、旁路和兼容性。只保存有界日志与状态，不采图片、不改设置，ZIP 上限 8 MiB。":"For bypass and compatibility issues. Bounded logs and state only; no images or setting changes. ZIP ceiling: 8 MiB.");
    ImGui::EndDisabled();
    if(!busy) {
        static int comparison=1,x=0,y=0,key=VK_F8;static bool custom=false;
        if(ImGui::Button(use_chinese_ui?"采集图像补充包（F7，仅画面异常）":"Capture image supplement (F7, visual issues only)"))RequestWithOptions({Comparison::Current,false,0,0});
        F8Tooltip(use_chinese_ui?"先生成新的配对核心包，再采当前图像；用于颜色、亮度、闪烁和细节问题。请一起提交界面显示的新核心包。默认不改设置，图像部分可能等待 GPU 或标记缺失。":"Creates a fresh paired core package before images. Use for color, lighting, flicker and detail issues; submit the new core path too. Default capture changes no settings; missing GPU images are explicit.");
        if(ImGui::TreeNode(use_chinese_ui?"对比采集与区域设置":"Comparison and region options")) {
        ImGui::Combo(use_chinese_ui?"采集内容":"Comparison",&comparison,use_chinese_ui?"标准三组对比\0只采当前问题\0强度 1.0 / 1.2 / 1.5\0细节保护关闭 / 开启\0NR jitter 未传递 / 传递\0":"Standard three groups\0Current issue only\0Strength 1.0 / 1.2 / 1.5\0Detail protection off / on\0NR jitter off / on\0");
        F8Tooltip(use_chinese_ui?"对比采集会临时切换所选参数；固定镜头，不改变游戏 DLSS/HDR。每组等待3秒与64次成功 NR，结束后恢复。":"Comparison capture temporarily changes the selected parameter. Keep the camera fixed; game DLSS/HDR stays unchanged. Waits 3 seconds and 64 successful NR evaluations, then restores settings.");
        ImGui::Checkbox(use_chinese_ui?"指定问题位置（原图像素）":"Custom source-pixel region",&custom);
        if(custom){ImGui::InputInt("X",&x);ImGui::InputInt("Y",&y);}else ImGui::TextDisabled("%s",use_chinese_ui?"默认中央 256×256 原像素区域":"Default: central 256 x 256 native pixels");
        if(ImGui::Button(use_chinese_ui?"开始采集":"Start capture"))RequestWithOptions({static_cast<Comparison>(comparison),custom,static_cast<unsigned>(std::max(x,0)),static_cast<unsigned>(std::max(y,0))});
        ImGui::TreePop();
        }
        if(ImGui::TreeNode(use_chinese_ui?"快捷键设置":"Shortcut")) {
            key=static_cast<int>(g_hotkey.load());
            if(ImGui::SliderInt(use_chinese_ui?"功能键":"Function key",&key,VK_F1,VK_F12,"%d"))g_hotkey.store(nr_feedback_text::CoreHotkey(static_cast<unsigned>(key)));
            key=static_cast<int>(g_hotkey.load());
            ImGui::Text("F%d",key-VK_F1+1);
            ImGui::TextDisabled("%s",use_chinese_ui?"F6 总开关、F7 图像采集已保留；冲突设置回到 F8。":"F6 master and F7 images are reserved; conflicting settings fall back to F8.");
            if(ImGui::Button(use_chinese_ui?"保存快捷键":"Save shortcut")){wchar_t value[16]={};swprintf_s(value,L"%u",g_hotkey.load());WritePrivateProfileStringW(L"NRBeforeSR",L"FeedbackVirtualKey",value,g_ini_path);}
            ImGui::TreePop();
        }
    } else {
        AcquireSRWLockShared(&g_lock);
        if(g_session){const auto &s=*g_session;const auto &g=s.groups[s.group];ImGui::Text(use_chinese_ui?"第 %u/%u 组，已记录 %u/16 帧，后台取得 %u 帧":"Group %u/%u, recorded %u/16 frames, delivered %u",s.group+1,s.group_count,std::min(g.count,kFrames),g.delivered);}
        ReleaseSRWLockShared(&g_lock);
        if(IsActive() && ImGui::Button(use_chinese_ui?"取消并恢复设置":"Cancel and restore"))Cancel();
    }
    if(state==Status::Complete || state==Status::Partial || state==Status::Failed) {
        ImGui::TextWrapped("%s",state==Status::Complete?(use_chinese_ui?"图像补充包已生成，请与新核心包一起提交。":"Image supplement saved; submit its paired core package too."):state==Status::Partial?(use_chinese_ui?"图像补充包部分完成，缺失原因已记录。":"Image supplement partially complete; missing-data reasons included."):(use_chinese_ui?"图像采集失败或已取消，请检查核心包及磁盘空间。":"Image capture failed or was cancelled; inspect the core package and disk space."));
        char path[4096]={};AcquireSRWLockShared(&g_lock);WideToUtf8(g_last_zip,path,sizeof(path));ReleaseSRWLockShared(&g_lock);
        if(path[0]){ImGui::TextWrapped("%s",path);if(ImGui::Button(use_chinese_ui?"复制图像包路径":"Copy image path"))ImGui::SetClipboardText(path);}
    }
}
void DrawOverlay(reshade::api::effect_runtime *) {
    if(!g_window_open.load() && !IsActive() && g_status.load()!=Status::Packaging && !nr_core_feedback::Busy())return;
    const bool use_chinese_ui=UseChineseUi();
    bool open=true;if(ImGui::Begin(use_chinese_ui?"DLSS5 反馈采集###dlss5-feedback-window":"DLSS5 feedback capture###dlss5-feedback-window",&open,ImGuiWindowFlags_AlwaysAutoResize))DrawControls();ImGui::End();
    if(!open){g_window_open.store(false);if(IsActive())Cancel();}
}
} // namespace nr_f8
