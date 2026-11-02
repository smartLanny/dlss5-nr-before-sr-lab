// Control runs under g_control, metadata under g_lock. Never acquire g_chain_cs
// while holding g_lock. GPU callbacks only publish small immutable records.
#include "nr_face_diagnostic.h"
namespace nr_f8 {
constexpr unsigned kFrames = 16, kSlots = 17, kGroups = nr_face_diagnostic::kGroups;
constexpr unsigned kCurrentRoiFrames = 3, kMatrixRoiFrames = 2, kRoiSide = 512;
enum class Status { Idle, Requested, Running, Packaging, Complete, Partial, Failed };
enum class Comparison { ShareMatrix, Current, Strength, Protection, Jitter, FaceDiagnostic };
const char *GroupLabel(Comparison kind,unsigned index);
struct Options {
    Comparison comparison = Comparison::Current;
    bool custom_roi = false; unsigned x = 0, y = 0; bool full_screen = false;
    float center_x = .5f, center_y = .5f; // FaceDiagnostic only, normalized source coordinates.
    // The UI binds its displayed ROI to this source extent. Both zero retain
    // the controller-only API; a partially supplied extent is never admitted.
    unsigned expected_source_width = 0, expected_source_height = 0;
};
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
    unsigned generation = 0, count = 0, delivered = 0, roi_target = 0;
    unsigned requested_post_percent = 0, actual_post_percent = 0;
    int expected_mode = 2;
    unsigned long long started = 0, shot_present = 0;
    bool applied = false, sealed = false, partial = false;
    bool overview_target = false, overview_reserved = false;
    bool rr_at_start = false;
    bool shot_requested = false, shot_received = false;
    char reason[160] = {};
    wchar_t screenshot[1024] = {};
    Frame frames[kSlots] = {};
};
struct Session {
    unsigned long long id = 0, started = 0;
    Options options = {};
    ChainConfig original = {};
    nr_processing::Session processing_original{};
    bool processing_saved=false,processing_restored=false;
    nr_post_settings::Options post_original = {};
    Group groups[kGroups] = {};
    unsigned group = 0, group_count = 3;
    bool restored = false, post_restored = false, partial = false;
    bool face_geometry_set = false, face_abort = false;
    NrStageCaptureInfo face_geometry = {};
    unsigned display_width = 0, display_height = 0;
    UINT64 payload_bytes = 0;
    wchar_t directory[1024] = {};
    FeedbackSession activity = {};
    nr_core_feedback::PairSnapshot core_pair = {};
    wchar_t core_report[1024] = {}, core_zip[1024] = {};
    reshade::api::effect_runtime *runtime = nullptr;
};
#include "nr_f8_stream.inl"
SRWLOCK g_lock = SRWLOCK_INIT, g_control = SRWLOCK_INIT;
std::atomic<Status> g_status{Status::Idle};
std::atomic<bool> g_config_hold{false}, g_cancel{false};
// Hotkeys can arrive while the settings overlay is closed. Keep this request
// until DrawSettings consumes it; capture progress itself remains independent
// of whether any ImGui page is visible.
std::atomic<bool> g_settings_feedback_page_requested{false};
std::atomic<unsigned long long> g_present{0};
std::atomic<unsigned> g_hotkey{VK_F8};
Session *g_session = nullptr;
Options g_options = {};
unsigned long long g_next_session = 0;
wchar_t g_last_zip[1024] = {};

struct FaceDiagnosticProgress {
    bool visible = false, active = false, roi_ready = false, screenshot_ready = false, restored = false;
    Status status = Status::Idle;
    unsigned group = 0, total = 0;
    char reason[160] = {};
};
FaceDiagnosticProgress g_face_progress = {}; // g_lock; keeps the last restore/package outcome.
const char *GroupLabel(Comparison kind, unsigned index) {
    if (kind == Comparison::FaceDiagnostic)
        return index < nr_face_diagnostic::kGroups ? nr_face_diagnostic::kLabels[index] : "invalid-face-group";
    if (kind == Comparison::Current) return "current";
    if (kind == Comparison::ShareMatrix) {
        const char *labels[] = {"original", "pre-50", "pre-75", "pre-100", "post-50", "post-75", "post-100"};
        return index < 7 ? labels[index] : "invalid-matrix-group";
    }
    return kind == Comparison::Strength ? "strength" : kind == Comparison::Protection ? "protection" : "jitter";
}
FaceDiagnosticProgress SnapshotFaceDiagnosticProgress() {
    AcquireSRWLockShared(&g_lock);
    auto result = g_face_progress;
    if (result.visible) result.status = g_status.load();
    if (g_session && g_session->options.comparison == Comparison::FaceDiagnostic) {
        const auto &s = *g_session; const auto &g = s.groups[s.group];
        result.visible = true; result.group = s.group; result.total = s.group_count;
        result.roi_ready = g.frames[0].valid; result.screenshot_ready = g.shot_received;
        result.restored = s.restored;
        std::snprintf(result.reason, sizeof(result.reason), "%s", g.reason);
    }
    result.active = result.visible && (result.status == Status::Requested || result.status == Status::Running);
    ReleaseSRWLockShared(&g_lock);
    return result;
}
void UnchangedFaceFailure(const char *reason) {
    AcquireSRWLockExclusive(&g_lock);
    if(g_options.comparison==Comparison::FaceDiagnostic && g_face_progress.visible) {
        g_face_progress.restored=true;
        std::snprintf(g_face_progress.reason,sizeof(g_face_progress.reason),"%s",reason);
    }
    ReleaseSRWLockExclusive(&g_lock);
}

bool IsActive() { const auto s=g_status.load(); return s==Status::Requested || s==Status::Running; }
void RequestSettingsFeedbackPage() {
    g_settings_feedback_page_requested.store(true, std::memory_order_release);
}
bool TakeSettingsFeedbackPageRequest() {
    return g_settings_feedback_page_requested.exchange(
        false, std::memory_order_acq_rel);
}
void OpenWindow() { RequestSettingsFeedbackPage(); }
void Cancel() {
    // Serialize against Present/Watchdog so cancellation cannot land between
    // Present's Requested check and image-session publication.
    AcquireSRWLockExclusive(&g_control);
    g_cancel.store(true, std::memory_order_release);
    if (g_status.load(std::memory_order_acquire)==Status::Requested) {
        nr_core_feedback::Cancel();
        UnchangedFaceFailure("cancelled before changing settings");
        g_status.store(Status::Failed, std::memory_order_release);
    }
    ReleaseSRWLockExclusive(&g_control);
}
bool RequestCore() {
    if(nr_acceptance::Active())return false;
    AcquireSRWLockExclusive(&g_control);
    const bool accepted = !nr_acceptance::Active() && !IsActive() && g_status.load()!=Status::Packaging &&
        StageCaptureStatus()!=1 && StageCaptureStatus()!=2 && nr_core_feedback::Request();
    if (accepted) {
        // A fresh core-only request must not display a previous image
        // result beside its new core path as though they were paired.
        AcquireSRWLockExclusive(&g_lock); g_last_zip[0]=0; ReleaseSRWLockExclusive(&g_lock);
        g_status.store(Status::Idle);
    }
    ReleaseSRWLockExclusive(&g_control);
    return accepted;
}
bool RequestWithOptions(const Options &options) {
    if(nr_acceptance::Active())return false;
    // Present has its own current-source capture. Never run the native-stage
    // comparison controller against a Present image or relabel its stages.
    if(nr_processing_runtime::PresentSelected()) {
        const bool capture=nr_compat_present_runtime::RequestCapture();
        const bool report=RequestCore();
        return capture||report;
    }
    AcquireSRWLockExclusive(&g_control);
    const bool face = options.comparison == Comparison::FaceDiagnostic;
    const bool valid_face = !face || (!options.full_screen &&
        nr_face_diagnostic::ValidCenter(options.center_x, options.center_y) &&
        ((options.expected_source_width == 0) == (options.expected_source_height == 0)) &&
        SnapshotConfig().enabled);
    bool accepted = valid_face && !g_config_hold.load() && !IsActive() && g_status.load()!=Status::Packaging && StageCaptureStatus()!=1 && StageCaptureStatus()!=2 && nr_core_feedback::Request(true);
    if (accepted) {
        g_options=options; g_cancel.store(false);
        AcquireSRWLockExclusive(&g_lock); g_last_zip[0]=0;
        g_face_progress={};g_face_progress.visible=face;g_face_progress.total=face?nr_face_diagnostic::kGroups:0;
        ReleaseSRWLockExclusive(&g_lock);
        g_status.store(Status::Requested);
    }
    ReleaseSRWLockExclusive(&g_control);
    return accepted;
}
bool RequestFaceDiagnostic(float center_x = .5f, float center_y = .5f,
                           unsigned expected_source_width = 0, unsigned expected_source_height = 0) {
    Options options; options.comparison = Comparison::FaceDiagnostic;
    options.center_x = center_x; options.center_y = center_y;
    options.expected_source_width = expected_source_width;
    options.expected_source_height = expected_source_height;
    return RequestWithOptions(options);
}
bool RequestFullscreen(Comparison kind = Comparison::Current) {
    return RequestWithOptions({kind, false, 0, 0, true});
}
ChainConfig GroupConfig(const ChainConfig &base, Comparison kind, unsigned index) {
    if(kind==Comparison::FaceDiagnostic) {
        auto c=nr_face_diagnostic::GroupConfig(base,index);
        c.processing_start=nr_processing::FromLegacy(c.mode);
        return c;
    }
    ChainConfig c=base;
    if (kind==Comparison::ShareMatrix) {
        c.enabled=index!=0;
        if(index) {
            c.mode=index<=3?2:1;
            if(index<=3) {
                c.work_mode=5;
                c.custom_work_scale=index==1?.5f:index==2?.75f:1.f;
            }
        }
    }
    else if (kind!=Comparison::Current) {
        c.enabled=true;
        if (kind==Comparison::Strength) c.transfer_strength=c.post_transfer_strength=index==0?1.f:index==1?1.2f:1.5f;
        if (kind==Comparison::Protection) { c.mode=2; c.detail_stability=index==0?0.f:1.f; }
        if (kind==Comparison::Jitter) c.jitter_mode=index==0?NrJitterMode::Legacy:NrJitterMode::GameInputPixel;
    }
    if (kind!=Comparison::Current) {
        c.processing_start=nr_processing::FromLegacy(c.mode);
        NormalizeConfig(c);
    }
    return c;
}
unsigned GroupPostPercent(Comparison kind,unsigned index,unsigned fallback) {
    if(kind==Comparison::ShareMatrix && index>=4)return index==4?50u:index==5?75u:100u;
    return fallback;
}
bool UsesPostOptions(Comparison kind) { return kind==Comparison::ShareMatrix; }
bool GroupUsesPostOptions(Comparison kind,unsigned index) {
    return kind==Comparison::ShareMatrix && index>=4;
}
void Break(Group &g, const char *reason) {
    g.partial=true; g.sealed=true;
    if (!g.reason[0]) std::snprintf(g.reason,sizeof(g.reason),"%s",reason);
}
bool SameFaceGeometry(const NrStageCaptureInfo &a,const NrStageCaptureInfo &b) {
    // Model tuning deliberately recreates Feature 18; its instance may change between groups.
    return a.feature_generation==b.feature_generation &&
        a.mode==b.mode && (a.codec_flags&~(8u|512u))==(b.codec_flags&~(8u|512u)) &&
        a.source_width==b.source_width && a.source_height==b.source_height &&
        a.model_width==b.model_width && a.model_height==b.model_height &&
        a.source_format==b.source_format && a.result_format==b.result_format &&
        a.source_base_x==b.source_base_x && a.source_base_y==b.source_base_y &&
        a.destination_base_x==b.destination_base_x && a.destination_base_y==b.destination_base_y &&
        a.has_pre_exposure==b.has_pre_exposure && a.has_exposure_scale==b.has_exposure_scale &&
        a.proxy_curve==b.proxy_curve && a.proxy_scale==b.proxy_scale && a.work_linear_scale==b.work_linear_scale;
}

// Called only for an actual successful composition, under the chain lock.
// Frames are reserved at record time; readiness is driven by exact-fence pixels.
bool ReserveFrame(NrStageCaptureInfo &info, const NrCaptureParams &params, FrameTicket &ticket) {
    if (g_status.load()!=Status::Running || !TryAcquireSRWLockExclusive(&g_lock)) return false;
    Session *s=g_session;
    bool accepted=false;
    if (s && !s->options.full_screen && s->group<s->group_count) {
        Group &g=s->groups[s->group];
        if (g.applied && g.requested.enabled && !g.sealed &&
            GetTickCount64()-g.started>=3000 && info.identity.warmed_up &&
            info.identity.since_reset>=64 && info.identity.config_generation==g.generation &&
            g.generation==g_config_generation.load() && info.mode==g.expected_mode) {
            if(s->options.comparison==Comparison::FaceDiagnostic) {
                if(params.reset || !nr_face_diagnostic::MatchesApplied(g.requested,info.identity.tuning))
                    Break(g,"face diagnostic reset or actual model tuning does not match this group");
                else if(!s->face_geometry_set && s->options.expected_source_width != 0 &&
                    (info.source_width != s->options.expected_source_width ||
                     info.source_height != s->options.expected_source_height))
                    Break(g,"face diagnostic source dimensions changed since region selection");
                else if(s->face_geometry_set && !SameFaceGeometry(s->face_geometry,info))
                    Break(g,"face diagnostic source/model geometry, route or colour contract changed");
                else if(!info.source_width || !info.source_height || !info.model_width || !info.model_height)
                    Break(g,"face diagnostic source/model dimensions unavailable");
                if(g.partial)s->face_abort=true;
            }
            if (g.count>0 && g.count<g.roi_target) {
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
                const bool overview=g.count==g.roi_target;
                if(overview && !g.overview_target) {
                    g.sealed=true;ReleaseSRWLockExclusive(&g_lock);return false;
                }
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
                    info.width=info.region_width=std::min(kRoiSide,info.source_width);
                    info.height=info.region_height=std::min(kRoiSide,info.source_height);
                    info.region_x=s->options.custom_roi?std::min(s->options.x,info.source_width-info.width):(info.source_width-info.width)/2;
                    info.region_y=s->options.custom_roi?std::min(s->options.y,info.source_height-info.height):(info.source_height-info.height)/2;
                    if(s->options.comparison==Comparison::FaceDiagnostic) {
                        const auto roi=nr_face_diagnostic::ResolveRoi(info.source_width,info.source_height,s->options.center_x,s->options.center_y);
                        info.region_x=roi.x;info.region_y=roi.y;info.width=info.region_width=roi.width;info.height=info.region_height=roi.height;
                        if(!s->face_geometry_set){s->face_geometry=info;s->face_geometry_set=true;}
                    }
                }
                const UINT64 bytes=static_cast<UINT64>(info.width)*info.height*80u+16u;
                if (bytes>nr_f8_budget::kPayloadLimit-s->payload_bytes) {
                    Break(g,"session payload budget exhausted");
                    ReleaseSRWLockExclusive(&g_lock); return false;
                }
                s->payload_bytes+=bytes;
                const unsigned slot=overview?kFrames:g.count++;
                ticket={s->id,s->group,slot};
                Frame &f=g.frames[slot]; f.info=info; f.params=params; f.reserved=true;
                if(overview){g.overview_reserved=true;g.sealed=true;}
                else if(g.count==g.roi_target && !g.overview_target)g.sealed=true;
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
    const char *group_label = nullptr;
    bool face = false; float center_x = .5f, center_y = .5f;
    NrStageCaptureInfo expected = {};
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
            if(ok){ok=WriteRecord(file,job->ticket,job->info,job->params,job->requested,job->actual,mapped,job->bytes-16,true,job->group_label,job->face,job->center_x,job->center_y,&job->expected);CloseHandle(file);}
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
            job->group_label=GroupLabel(g_session->options.comparison,ticket.group);
            job->face=g_session->options.comparison==Comparison::FaceDiagnostic;
            job->center_x=g_session->options.center_x;job->center_y=g_session->options.center_y;job->expected=g_session->face_geometry;
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
            if(g_session->options.comparison==Comparison::FaceDiagnostic) {
                unsigned w=0,h=0;runtime->get_screenshot_width_and_height(&w,&h);
                if(w!=g_session->display_width || h!=g_session->display_height) {
                    Break(g,"face diagnostic display dimensions changed before screenshot callback");g_session->face_abort=true;
                    ReleaseSRWLockExclusive(&g_lock);return;
                }
            }
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
    if (g.count!=g.roi_target || (g.overview_target && !g.overview_reserved)) return false;
    for (unsigned i=0;i<g.count;++i) if (!g.frames[i].valid) return false;
    if (g.overview_target && !g.frames[kFrames].valid) return false;
    return true;
}
bool FaceScreenshotFits(const wchar_t *path,unsigned width,unsigned height) {
    HANDLE h=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)return false;
    unsigned char header[24]={};DWORD got=0;LARGE_INTEGER size={};
    bool ok=GetFileSizeEx(h,&size) && size.QuadPart>=24 && size.QuadPart<=static_cast<LONGLONG>(nr_bundle::kCopyLimit) &&
        ReadFile(h,header,sizeof(header),&got,nullptr) && got==sizeof(header);
    CloseHandle(h);
    const unsigned char signature[]={137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82};
    const auto be=[&](unsigned at){return (unsigned(header[at])<<24)|(unsigned(header[at+1])<<16)|(unsigned(header[at+2])<<8)|header[at+3];};
    return ok && std::memcmp(header,signature,sizeof(signature))==0 && be(16)==width && be(20)==height;
}
void WriteTimeline(FILE *out,const Session &s) {
    std::fprintf(out,"group,index,overview,valid,instance,frame,config,sinceReset,mode,sourceW,sourceH,modelW,modelH,roiX,roiY,sampleW,sampleH,stride,sourceBaseX,sourceBaseY,outputBaseX,outputBaseY,sourceFormat,outputFormat,reset,resetKnown,gameJitterValid,gameJitterX,gameJitterY,nrJitterForwarded,nrJitterX,nrJitterY,gameMvValid,gameMvX,gameMvY,nrMvX,nrMvY,depthW,depthH,mvW,mvH,preExposure,hasPreExposure,exposureScale,hasExposureScale,gpuRawExposure,gpuResolvedExposure,gpuSourceToCanonical,gpuPreExposure,transfer,color,maxLumaRatio,proxyScale,proxyCurve,frameConfidence,detailStability,lightingLock,motionRequested,motionAvailable,modelIntensity,skinStructure,groupName,localTone,localStructure,style,autoMask,uiCorrection");
#if defined(NR_HAS_NETWORK_MODEL_SELECTION)
    std::fprintf(out,",networkRequest,runtimeGeneration,runtimeModule");
#endif
    std::fprintf(out,"\n");
    for(unsigned g=0;g<s.group_count;++g) for(unsigned row=0;row<s.groups[g].count+(s.groups[g].overview_reserved?1u:0u);++row) {
        const unsigned i=row<s.groups[g].count?row:kFrames;
        const auto &f=s.groups[g].frames[i];const auto &n=f.info;const auto &p=f.params;
        std::fprintf(out,"%u,%u,%d,%d,%llu,%llu,%u,%u,%d,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%d,%d,%d,%.9g,%.9g,%d,%.9g,%.9g,%d,%.9g,%.9g,%.9g,%.9g,%u,%u,%u,%u,%.9g,%d,%.9g,%d,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%u,%.9g,%.9g,%.9g,%d,0,%.9g,%.9g,%s,%.9g,%.9g,%u,%d,%d",
            g,i,i==kFrames,f.valid,n.identity.instance_id,n.identity.frame_id,n.identity.config_generation,n.identity.since_reset,n.mode,
            n.source_width,n.source_height,n.model_width,n.model_height,n.region_x,n.region_y,n.width,n.height,n.stride,
            n.source_base_x,n.source_base_y,n.destination_base_x,n.destination_base_y,n.source_format,n.result_format,
            p.reset,p.reset_known,p.game_jitter_valid,p.game_jitter_x,p.game_jitter_y,p.nr_jitter_forwarded,p.nr_jitter_x,p.nr_jitter_y,
            p.game_mv_valid,p.game_mv_x,p.game_mv_y,p.nr_mv_x,p.nr_mv_y,p.depth_width,p.depth_height,p.mv_width,p.mv_height,
            n.pre_exposure,n.has_pre_exposure,n.exposure_scale,n.has_exposure_scale,n.gpu_raw_exposure,n.gpu_resolved_exposure,n.gpu_source_to_canonical,n.gpu_pre_exposure,
            n.transfer_strength,n.color_strength,n.max_luma_ratio,n.proxy_scale,n.proxy_curve,n.frame_confidence,n.detail_stability,n.lighting_lock,n.motion_requested,
            n.identity.tuning.intensity,n.identity.tuning.skin_structure,GroupLabel(s.options.comparison,g),
            n.identity.tuning.local_tone,n.identity.tuning.local_structure,n.identity.tuning.style,n.identity.tuning.auto_mask,n.identity.tuning.ui_correction);
#if defined(NR_HAS_NETWORK_MODEL_SELECTION)
        std::fprintf(out,",%d,%llu,%llu",n.identity.tuning.network_preset,
            n.identity.tuning.runtime_generation,n.identity.tuning.runtime_module_address);
#endif
        std::fprintf(out,"\n");
    }
}
DWORD WINAPI PackageWorker(void *opaque) {
    const auto &module_identity = EnsureRuntimeIdentityOnWorker();
#if defined(NR_HAS_NETWORK_MODEL_SELECTION)
    EnsureNrNetworkIdentityOnWorker();
#endif
    auto *s=static_cast<Session *>(opaque);
    FeedbackAppend(s->activity, "capture.kind=image-supplement\ncore.session=%s\ncore.config=%u\ncore.complete=%d\n",
        s->core_pair.session, s->core_pair.config, s->core_pair.complete ? 1 : 0);
    FeedbackAppend(s->activity, "artifact.sourceCommit=%s\nartifact.sourceDirty=%d\n"
        "artifact.addonSha256=%s\nartifact.identityStatus=%s\n"
        "artifact.hashMeasurement=loaded-path-file-once-worker\n",
        kSourceCommit, kSourceDirty ? 1 : 0, module_identity.sha256, module_identity.status);nr_bundle::Result result={};result.status=nr_bundle::Status::Failed;
    bool complete=s->restored && s->post_restored && s->processing_restored && !s->partial;
    const bool face=s->options.comparison==Comparison::FaceDiagnostic;
    if(face)for(unsigned i=0;i<s->group_count;++i) {
        auto &g=s->groups[i];
        if(g.shot_received && !FaceScreenshotFits(g.screenshot,s->display_width,s->display_height)) {
            Break(g,"face diagnostic screenshot is not the expected full-size PNG or exceeds 64 MiB");g.shot_received=false;
        }
    }
    for(unsigned i=0;i<s->group_count;++i)complete=Complete(s->groups[i]) && complete;
    FeedbackAppend(s->activity,"format=DLSS5-F8-v4\nsession=%llu\ncomparison=%u\ngroups=%u\ncapture.complete=%d\nconfig.restored=%d\npostConfig.restored=%d\nconfig.writesToIni=0\ncapture.order=sequential; keep the scene and camera fixed\ncapture.sameFrameClaim=0\nnativeSrOutput=not-obtained\nmvDepthPixels=not-obtained; resource read contract unavailable\nmotionProtection=unavailable\npayloadBytes=%llu\ngpuBudgetBytes=%llu\ncpuBudgetBytes=%llu\n",
        s->id,static_cast<unsigned>(s->options.comparison),s->group_count,complete,s->restored,s->post_restored,s->payload_bytes,nr_f8_budget::kGpuLimit,nr_f8_budget::kCpuLimit);
    FeedbackAppend(s->activity,"processingSession.restored=%d\n",s->processing_restored);
    FeedbackAppend(s->activity,"capture.fullScreen=%d\ncapture.displayFile=original-runtime-screenshot; no crop or resize\ncapture.gpuPlanes=%s\n",
        s->options.full_screen, s->options.full_screen?"not-requested":"bounded-ROI-and-optional-overview");
    if(face)FeedbackAppend(s->activity,"faceDiagnostic.version=1\nfaceDiagnostic.centerX=%.9g\nfaceDiagnostic.centerY=%.9g\nfaceDiagnostic.roiTarget=1\nfaceDiagnostic.stability=minimum-3000ms-and-64-NR-evaluations-after-reset; exact-fence-ROI-before-display-request\nfaceDiagnostic.displayWidth=%u\nfaceDiagnostic.displayHeight=%u\nfaceDiagnostic.selectionSourceWidth=%u\nfaceDiagnostic.selectionSourceHeight=%u\nfaceDiagnostic.moduleInventory=paired-core-package\nfaceDiagnostic.isMaskProof=0\n",
        s->options.center_x,s->options.center_y,s->display_width,s->display_height,
        s->options.expected_source_width,s->options.expected_source_height);
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
        if(face) {
            nr_bundle::AddFile(*job,s->core_report,"core/feedback.txt",nr_bundle::FileKind::Copy,true);
            nr_bundle::AddFile(*job,s->core_zip,"core/paired-archive",nr_bundle::FileKind::HashOnly,true);
            char core_report[4096]={},core_zip[4096]={};WideToUtf8(s->core_report,core_report,sizeof(core_report));WideToUtf8(s->core_zip,core_zip,sizeof(core_zip));
            FeedbackAppend(s->activity,"core.reportPath=%s\ncore.archivePath=%s\ncore.archiveIdentity=core/paired-archive manifest SHA256\n",core_report,core_zip);
        }
        if(!face)FeedbackAddModules(*job,s->activity); // The paired core package owns D7 module inventory; retain slots for nine image groups.
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
            for(unsigned i=0;i<s->group_count;++i){const auto &g=s->groups[i];unsigned valid=0;for(unsigned f=0;f<g.count;++f)valid+=g.frames[f].valid;
                std::fprintf(file,"Group %u: requestedMode=%d expectedCaptureMode=%d rrAtStart=%d enabled=%d requestedStrength=%.4g actualStrength=%.4g requestedPostPercent=%u actualPostPercent=%u ROI=%u/%u roiSide=%u overview=%d screenshotPresent=%llu complete=%d reason=%s\n",i,g.requested.mode,g.expected_mode,g.rr_at_start,g.requested.enabled,ActiveTransferStrength(g.requested),ActiveTransferStrength(g.actual),g.requested_post_percent,g.actual_post_percent,valid,g.roi_target,kRoiSide,g.overview_reserved&&g.frames[kFrames].valid,g.shot_present,Complete(g),g.reason);}
            if(!s->options.full_screen)std::fprintf(file,"\n分块格式 F8CHK004：头中的roiTarget记录本组目标（当前采集3帧，分享矩阵2帧）；ROI索引从0连续，16仅供当前采集的独立概览。原图/代理/NR/合成/保护五平面是同一次Combine dispatch的512原像素ROI；曝光尾部写入每条JSON。代理/NR按合成时工作网格映射采样。缺失帧保留元数据而不是拼接。\n截图是之后的独立Present，各组也是顺序采集；它们不是同一帧最终SR证据。采集期间需保持场景与镜头。原生最终SR纹理、MV/Depth像素未取得，不能验证时域重投影。\n保护数据RGBA=权重、原因位、原始log残差、空间保护后log残差；原因1低采样/2细碎不一致/4近黑/8极端比值/16关闭/32输入无效/64零强度/128历史抑制/256无残差/512后置细节/边缘空间权重不可用（lightingLock按独立亮度保护生效）。这是空间保护，不是人脸/HUD识别，也不证明运动保护。\n每帧JSON含完整请求/实际参数、codecFlags、曝光有效性、jitter、MV比例与实际模型tuning；请在相同颜色域分析，不独立归一化或用均值替代画质。部分包请检查manifest缺失原因。\n");
            if(s->options.full_screen)std::fprintf(file,"\n全屏原图：每组保存 ReShade 提供的完整截图文件，不裁切、不缩小。配置快照见各组 settings.json；actual 是已应用设置，不代表模型已执行。截图按顺序取得，各组不是同一帧。没有采集内部 GPU 平面，不能据此证明人脸遮罩或 NR 已执行。\nFull-screen display screenshots retain their original bytes; no GPU planes or same-frame comparison.\n");
            const bool ok=std::ferror(file)==0;std::fclose(file);
            if(ok){nr_bundle::AddFile(*job,path,"capture-analysis.txt",nr_bundle::FileKind::Copy,true);
                nr_bundle::AddFile(*job,path,"分析说明.txt",nr_bundle::FileKind::Copy,true);}
            else nr_bundle::AddMissing(*job,"capture-analysis.txt","explanation write failed",true);
        } else nr_bundle::AddMissing(*job,"capture-analysis.txt","explanation open failed",true);
        for(unsigned i=0;i<s->group_count;++i) {
            const auto &g=s->groups[i];char name[128]={};
            for(unsigned chunk=0;chunk<5;++chunk) {
                if(chunk<4 && chunk*4>=g.count)continue;
                if(chunk==4 && !g.overview_reserved)continue;
                swprintf_s(path,L"%ls\\group-%u-chunk-%u.f8c",s->directory,i,chunk);
                if(chunk==4)std::snprintf(name,sizeof(name),"groups/%u/overview.f8c",i);
                else std::snprintf(name,sizeof(name),"groups/%u/chunk-%03u.f8c",i,chunk);
                if(WriteChunk(path,*s,i,chunk))nr_bundle::AddFile(*job,path,name,nr_bundle::FileKind::Copy,true);
                else nr_bundle::AddMissing(*job,name,"chunk stream failed or exceeded 64 MiB",true);
            }
            JsonBuffer settings;
            settings.Append("{\"schema\":1,\"group\":%u,\"configGeneration\":%u,\"expectedMode\":%d,\"rrAtStart\":%d,\"requestedPostPercent\":%u,\"actualPostPercent\":%u,\"snapshotMeaning\":\"applied-settings-at-group-start; not execution proof\"",i,g.generation,g.expected_mode,g.rr_at_start,g.requested_post_percent,g.actual_post_percent);
            settings.Append(",\"groupName\":\"%s\",\"complete\":%d,\"reason\":\"%s\"",GroupLabel(s->options.comparison,i),Complete(g),g.reason);
            if(face) {
                const auto &e=s->face_geometry;
                settings.Append(",\"faceDiagnostic\":{\"centerX\":%.9g,\"centerY\":%.9g,\"roiX\":%u,\"roiY\":%u,\"roiWidth\":%u,\"roiHeight\":%u,\"expectedSourceWidth\":%u,\"expectedSourceHeight\":%u,\"expectedModelWidth\":%u,\"expectedModelHeight\":%u,\"displayWidth\":%u,\"displayHeight\":%u,\"roiTarget\":1,\"screenshotAfterValidGpuRecord\":true}",
                    s->options.center_x,s->options.center_y,e.region_x,e.region_y,e.width,e.height,e.source_width,e.source_height,e.model_width,e.model_height,s->display_width,s->display_height);
            }
            ConfigJson(settings,"requested",g.requested);ConfigJson(settings,"actual",g.actual);settings.Append("}\n");
            swprintf_s(path,L"%ls\\group-%u-settings.json",s->directory,i);
            std::snprintf(name,sizeof(name),"groups/%u/settings.json",i);
            file=nullptr;_wfopen_s(&file,path,L"wb");
            bool settings_ok=false;
            if(file){settings_ok=settings.ok && std::fwrite(settings.data,1,settings.used,file)==settings.used;settings_ok=std::fclose(file)==0 && settings_ok;}
            if(settings_ok)nr_bundle::AddFile(*job,path,name,nr_bundle::FileKind::Copy,true);
            else nr_bundle::AddMissing(*job,name,"group settings write failed",true);
            const wchar_t *extension=wcsrchr(g.screenshot,L'.');char ext[16]={};
            if(extension)WideToUtf8(extension,ext,sizeof(ext));
            std::snprintf(name,sizeof(name),"groups/%u/display%s",i,ext[0]?ext:".missing");
            if(g.shot_received)nr_bundle::AddFile(*job,g.screenshot,name,nr_bundle::FileKind::Copy,true);
            else nr_bundle::AddMissing(*job,name,"matching screenshot callback was not received",true);
            if(!Complete(g)){std::snprintf(name,sizeof(name),"groups/%u/incomplete",i);nr_bundle::AddMissing(*job,name,g.reason[0]?g.reason:"missing exact-fence ROI, overview or screenshot",true);}
        }
        const bool registrations_ok=job->rejected_inputs==0;
        result=nr_bundle::Run(*job);complete=complete && registrations_ok;nr_bundle::DestroyJob(job);
    }
    AcquireSRWLockExclusive(&g_lock);
    if(result.zip_path[0])wcscpy_s(g_last_zip,result.zip_path);
    if(face && !(complete && result.required_complete)) {
        const char *reason="face diagnostic package is partial; inspect manifest and paired core report";
        for(unsigned i=0;i<s->group_count;++i)if(s->groups[i].reason[0]){reason=s->groups[i].reason;break;}
        std::snprintf(g_face_progress.reason,sizeof(g_face_progress.reason),"%s",reason);
    }
    g_status.store(result.status==nr_bundle::Status::Failed?Status::Failed:complete && result.required_complete?Status::Complete:Status::Partial);
    ReleaseSRWLockExclusive(&g_lock);
    // Every path was generated in this unique session directory, never discovered recursively.
    for(auto &g:s->groups)for(auto &f:g.frames)if(f.spool[0])DeleteFileW(f.spool);
    delete s;return 0;
}

void StartGroup(Session &s) {
    const unsigned index=s.group;
    ChainConfig requested=GroupConfig(s.original,s.options.comparison,index);
    if(s.options.comparison!=Comparison::Current)
        requested.processing_selection=nr_processing_runtime::next_selection.fetch_add(1,std::memory_order_relaxed);
    const bool face=s.options.comparison==Comparison::FaceDiagnostic;
    const bool rr_pre_skip=s.options.comparison!=Comparison::Current && !face && requested.enabled && requested.mode==2 &&
        IsRayReconstructionRouteObserved();
    const unsigned requested_post=GroupPostPercent(s.options.comparison,index,s.post_original.percent);
    const bool group_uses_post=GroupUsesPostOptions(s.options.comparison,index);
    const bool post_applied=rr_pre_skip || !requested.enabled || !group_uses_post ||
        nr_post_settings::RequestForCapture({true,requested_post});
    if(s.options.comparison!=Comparison::Current && !rr_pre_skip)ApplyConfig(requested,"F8 transient comparison",false);
    ChainConfig actual=SnapshotConfig();
    const auto actual_post=nr_post_settings::Read();
    actual.perf_epoch=requested.perf_epoch; // internal monotonic statistics epoch is not a user setting
    AcquireSRWLockExclusive(&g_lock);
    Group &g=s.groups[index];g.requested=requested;g.actual=actual;g.generation=g_config_generation.load();
    g.requested_post_percent=requested_post;g.actual_post_percent=actual_post.percent;
    g.rr_at_start=IsRayReconstructionRouteObserved();
    g.expected_mode=(s.options.comparison==Comparison::Current || face) && g.rr_at_start?1:requested.mode;
    g.roi_target=s.options.full_screen?0:face?1:s.options.comparison==Comparison::Current?kCurrentRoiFrames:kMatrixRoiFrames;
    g.overview_target=!s.options.full_screen && s.options.comparison==Comparison::Current;
    g.sealed=s.options.full_screen; // Full display images do not request raw GPU planes.
    g.applied=rr_pre_skip || (post_applied && SameConfig(requested,actual) &&
        (!requested.enabled || !group_uses_post || actual_post.percent==requested_post));g.started=GetTickCount64();
    if(rr_pre_skip)Break(g,"NR -> SR comparison skipped: Ray Reconstruction is active; only post-RR ordering may be sampled");
    else if(!g.applied)Break(g,"configuration was not applied");
    ReleaseSRWLockExclusive(&g_lock);
}
void Finish(Session *s) {
    AcquireSRWLockExclusive(&g_lock);g_session=nullptr;g_status.store(Status::Packaging);ReleaseSRWLockExclusive(&g_lock);
    // CRITICAL_SECTION is recursive: keep the existing ApplyConfig transaction
    // and restore its companion route value atomically before a carrier can run.
    // g_lock is already released; never reverse the controller's lock order.
    EnterCriticalSection(&g_chain_cs);
    if(s->options.comparison!=Comparison::Current)ApplyConfig(s->original,"F8 restore original",false);
    s->processing_restored=s->options.comparison==Comparison::Current;
    if(s->options.comparison!=Comparison::Current && s->processing_saved) {
        nr_processing_runtime::session=s->processing_original;
        nr_processing_runtime::effective.store(static_cast<unsigned>(s->processing_original.Current()),std::memory_order_release);
        g_history_generation.fetch_add(1,std::memory_order_relaxed);
        s->processing_restored=true;
    }
    ChainConfig restored=g_config;restored.perf_epoch=s->original.perf_epoch;
    LeaveCriticalSection(&g_chain_cs);
    if(UsesPostOptions(s->options.comparison))nr_post_settings::RequestForCapture(s->post_original);
    s->restored=SameConfig(s->original,restored);
    s->post_restored=!UsesPostOptions(s->options.comparison) || nr_post_settings::Read().percent==s->post_original.percent;
    if(s->options.comparison==Comparison::FaceDiagnostic) {
        AcquireSRWLockExclusive(&g_lock);auto &p=g_face_progress;
        p.visible=true;p.active=false;p.group=s->group;p.total=s->group_count;p.restored=s->restored && s->post_restored;
        p.roi_ready=s->groups[s->group].frames[0].valid;p.screenshot_ready=s->groups[s->group].shot_received;
        std::snprintf(p.reason,sizeof(p.reason),"%s",s->groups[s->group].reason);
        ReleaseSRWLockExclusive(&g_lock);
    }
    g_config_hold.store(false);
    FeedbackAppendActivity(s->activity,"end");
    if(!QueueUserWorkItem(PackageWorker,s,WT_EXECUTEDEFAULT)) {
        for(auto &g:s->groups)for(auto &f:g.frames)if(f.spool[0])DeleteFileW(f.spool);delete s;g_status.store(Status::Failed);
    }
}
// Caller holds g_control. A watchdog calls this with no runtime so stopped
// Presents cannot strand transient settings. Screenshots only run on Present.
void Tick(reshade::api::effect_runtime *runtime) {
    bool advance=false,finish=false,shot=false,face_shot=false;char tag[80]={};
    AcquireSRWLockExclusive(&g_lock);Session *s=g_session;
    if(s && s->group<s->group_count) {
        Group &g=s->groups[s->group];const auto age=GetTickCount64()-g.started;
        const bool face=s->options.comparison==Comparison::FaceDiagnostic;
        if(g_cancel.load() || age>=30000 || (face && (s->face_abort || g.partial))) {
            Break(g,g_cancel.load()?"cancelled":age>=30000?"group timeout; missing pixels retained as partial":"Ray Reconstruction owns the rendering path");
            s->partial=true;finish=true;
        } else {
            const bool rr_now=IsRayReconstructionRouteObserved();
            if((s->options.comparison==Comparison::Current || face) && rr_now!=g.rr_at_start)
                Break(g,"RR route changed during current-state capture; mixed-route pixels were not combined");
            else if(g.requested.enabled && ((s->options.comparison!=Comparison::Current && !face && g.requested.mode==2 && rr_now) || OtherNrOwnerActive()))
                Break(g,s->options.comparison!=Comparison::Current && !face && g.requested.mode==2 && rr_now
                    ?"NR -> SR comparison skipped: Ray Reconstruction is active; only post-RR ordering may be sampled"
                    :"another NR addon owns this path; only screenshots/logs available");
            if(face && g.partial){s->partial=true;finish=true;}
            if(!finish && runtime && runtime==s->runtime && !g.shot_requested && age>=3000 && (!face || g.frames[0].valid)) {
                g.shot_requested=true;g.shot_present=g_present.load();shot=true;
                face_shot=face;
                std::snprintf(tag,sizeof(tag),"DLSS5_F8_%llu_%u",s->id,s->group);
            }
            const unsigned expected=g.count+(g.overview_reserved?1u:0u);
            bool ready=g.shot_received && ((g.partial&&g.sealed) ||
                (g.applied && (!g.requested.enabled || (g.sealed && g.delivered==expected))));
            if(ready) {s->partial|=!Complete(g);if(s->group+1<s->group_count){++s->group;advance=true;}else finish=true;}
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    if(shot) {
        if(face_shot)runtime->open_overlay(false,reshade::api::input_source::mouse);
        runtime->save_screenshot(tag); // failure is recorded by the bounded callback timeout
    }
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
        if(g_cancel.load()){nr_core_feedback::Cancel();UnchangedFaceFailure("cancelled before changing settings");g_status.store(Status::Failed);ReleaseSRWLockExclusive(&g_control);return;}
        if(nr_core_feedback::Busy()){ReleaseSRWLockExclusive(&g_control);return;}
        HMODULE pinned=nullptr;
        auto *s=new(std::nothrow) Session{};
        if(!s || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&Watchdog),&pinned)){
            delete s;UnchangedFaceFailure("capture session allocation or module pin failed before changing settings");g_status.store(Status::Failed);ReleaseSRWLockExclusive(&g_control);return;
        }
        if(!FeedbackCreateDirectory(s->directory)){delete s;UnchangedFaceFailure("capture directory creation failed before changing settings");g_status.store(Status::Failed);ReleaseSRWLockExclusive(&g_control);return;}
        s->id=++g_next_session;s->options=g_options;s->runtime=runtime;s->started=GetTickCount64();
        s->core_pair=nr_core_feedback::SnapshotPair();
#if defined(NR_HAS_CORE_TEXT_REPORT)
        AcquireSRWLockShared(&g_feedback_lock);
        wcscpy_s(s->core_report,nr_core_feedback::g_report_path);wcscpy_s(s->core_zip,g_feedback_last_zip);
        ReleaseSRWLockShared(&g_feedback_lock);
#endif
        s->partial=!s->core_pair.ready || !s->core_pair.complete;
        s->group_count=s->options.comparison==Comparison::Current?1:s->options.comparison==Comparison::ShareMatrix?7:
            s->options.comparison==Comparison::FaceDiagnostic?nr_face_diagnostic::kGroups:
            (s->options.comparison==Comparison::Protection || s->options.comparison==Comparison::Jitter)?2:3;
        const bool face=s->options.comparison==Comparison::FaceDiagnostic;
        if(face) {
            if(runtime)runtime->get_screenshot_width_and_height(&s->display_width,&s->display_height);
            const char *reason=!SnapshotConfig().enabled?"NR was disabled before face diagnostic started":
                !nr_face_diagnostic::AdmitDisplay(s->display_width,s->display_height)?"full display plus nine face ROIs exceed existing archive budget or display dimensions unavailable":nullptr;
            if(reason) {
                UnchangedFaceFailure(reason);
                delete s;g_status.store(Status::Failed);ReleaseSRWLockExclusive(&g_control);return;
            }
        }
        const UINT64 roi_bytes=static_cast<UINT64>(kRoiSide)*kRoiSide*80u+16u;
        const UINT64 overview_reserve=s->options.comparison==Comparison::Current?40ull*1024*1024+16u:0u;
        if(roi_bytes*s->group_count*(face?1:s->options.comparison==Comparison::Current?kCurrentRoiFrames:kMatrixRoiFrames)+overview_reserve>
            nr_f8_budget::kPayloadLimit){delete s;g_status.store(Status::Failed);ReleaseSRWLockExclusive(&g_control);return;}
        EnterCriticalSection(&g_chain_cs);
        bool expected_hold=false;
        const bool acquired_hold=StageCaptureStatus()==0 && g_config_hold.compare_exchange_strong(expected_hold,true);
        if(acquired_hold) {
            s->original=g_config;
            s->processing_original=nr_processing_runtime::session;
            s->processing_saved=true;
        }
        LeaveCriticalSection(&g_chain_cs);
        if(!acquired_hold) {
            delete s;UnchangedFaceFailure("another diagnostic owns the configuration; no settings changed");
            g_status.store(Status::Failed);ReleaseSRWLockExclusive(&g_control);return;
        }
        s->post_original=nr_post_settings::Read();
        FeedbackAppend(s->activity,"build=%s\n",kBuildIdentity);FeedbackAppendActivity(s->activity,"start");
        AcquireSRWLockExclusive(&g_lock);g_session=s;g_status.store(Status::Running);ReleaseSRWLockExclusive(&g_lock);
        StartGroup(*s);
        if(!QueueUserWorkItem(Watchdog,reinterpret_cast<void *>(static_cast<std::uintptr_t>(s->id)),WT_EXECUTEDEFAULT)){s->partial=true;Finish(s);}
    }
    Tick(runtime);ReleaseSRWLockExclusive(&g_control);
}
void RuntimeDestroyed(reshade::api::effect_runtime *runtime) {
    AcquireSRWLockExclusive(&g_control);
    if(g_status.load()==Status::Requested){nr_core_feedback::Cancel();UnchangedFaceFailure("runtime destroyed before changing settings");g_status.store(Status::Failed);}
    AcquireSRWLockExclusive(&g_lock);auto *s=g_session;
    const bool current=s && s->runtime==runtime;
    if(current){Break(s->groups[s->group],"capture runtime destroyed");s->partial=true;s->runtime=nullptr;}
    ReleaseSRWLockExclusive(&g_lock);
    if(current)Finish(s);
    ReleaseSRWLockExclusive(&g_control);
}
void F8Tooltip(const char *text) {
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::BeginTooltip();ImGui::PushTextWrapPos(ImGui::GetFontSize()*32.f);
        ImGui::TextUnformatted(text);ImGui::PopTextWrapPos();ImGui::EndTooltip();
    }
}

void DrawCoreTextStatus(bool chinese) {
    if (g_feedback_status.load(std::memory_order_acquire) < 3) return;
    struct View { wchar_t path[1024]{}; char utf8[4096]{}; std::shared_ptr<const std::string> text; DWORD error=0; bool complete=false; };
    auto view = std::unique_ptr<View>(new (std::nothrow) View);
    if (!view) return;
    AcquireSRWLockShared(&g_feedback_lock);
    view->text=nr_core_feedback::g_full_report; view->error=nr_core_feedback::g_report_error;
    view->complete=nr_core_feedback::g_report_complete;
    nr_bundle::CopyPath(view->path,nr_core_feedback::g_report_path);
    ReleaseSRWLockShared(&g_feedback_lock);
    if (view->text) {
        const auto clipboard=nr_core_feedback::GetReportClipboardStatus();
        ImGui::TextWrapped("%s",clipboard==nr_core_feedback::ReportClipboardStatus::Copied
            ?(chinese?"TXT 已保存，文件路径已自动复制。直接发送这个文件即可。":"TXT saved and its file path was copied automatically. Send that file directly.")
            :clipboard==nr_core_feedback::ReportClipboardStatus::Failed
                ?(chinese?"TXT 已保存，但自动复制路径失败；文件路径如下。":"TXT was saved, but automatic path copy failed. The file path is shown below.")
                :(chinese?"TXT 已保存，正在复制文件路径。":"TXT saved; copying its file path."));
        if (!view->complete) ImGui::TextWrapped("%s", chinese ? "TXT 部分证据不可用，缺失原因已写入文件；不代表游戏正常。" : "TXT is partial; missing evidence is explicit, not proof of game health.");
        WideToUtf8(view->path,view->utf8,sizeof(view->utf8));
        ImGui::TextWrapped("%s",view->utf8);
    } else if (view->error) {
        ImGui::TextWrapped(chinese ? "TXT 保存失败（Win32 %lu）；没有复用上一次的报告。" : "TXT failed (Win32 %lu); no previous report was reused.",view->error);
    }
}

void DrawCoreStatus() {
    const bool zh=UseChineseUi();
    char path[4096]={},reason[160]={};
    AcquireSRWLockShared(&g_feedback_lock);
    const auto status=g_feedback_status.load();
    WideToUtf8(g_feedback_last_zip,path,sizeof(path));
    nr_bundle::CopyText(reason,sizeof(reason),nr_core_feedback::g_reason);
    ReleaseSRWLockShared(&g_feedback_lock);
    if(status==0)return;
    if(status==1 || status==2) {
        ImGui::TextWrapped("%s",zh?"核心反馈：正在记录状态和日志，不等待 NR 图像。":"Core feedback: recording state and compact logs; no NR images required。");
        if(ImGui::Button(zh?"取消核心采集":"Cancel core capture"))nr_core_feedback::Cancel();
    } else {
        ImGui::TextWrapped("%s",status==3?(zh?"核心反馈已生成。":"Core feedback saved."):
            status==4?(zh?"核心反馈部分完成，缺失原因已记录。":"Core feedback partially complete; missing reasons are included."):
            (zh?"核心反馈保存失败，没有复用旧包路径。":"Core feedback failed; no old package path reused."));
        if(reason[0])ImGui::TextWrapped("%s",reason);
        if(path[0]){ImGui::TextWrapped("%s",path);if(ImGui::Button(zh?"复制核心包路径":"Copy core path"))ImGui::SetClipboardText(path);}
    }
    DrawCoreTextStatus(UseChineseUi());
}
void DrawCompatibilityCapture(bool zh,bool report_busy) {
    const unsigned present=nr_compat_present_runtime::CaptureStatus(false),fg=nr_compat_present_runtime::CaptureStatus(true);
    const unsigned present_workers=nr_compat_present_runtime::CaptureWorkers(false),fg_workers=nr_compat_present_runtime::CaptureWorkers(true);
    ImGui::TextWrapped("%s",zh?"F7：捕获当前兼容路径的各层数据，不切换画面设置；原生七组矩阵不适用于此路径。":"F7 captures each layer of the current compatibility path without changing picture settings. The native seven-group matrix does not apply here.");
    ImGui::TextWrapped("%s",nr_compat_present_runtime::CaptureUsesFg()
        ?(zh?"下次请求：帧生成前的当前来源。":"Next request: the current source before frame generation.")
        :(zh?"下次请求：当前 Present 兼容处理。":"Next request: current Present compatibility processing."));
    const bool pending=present==1||fg==1||present_workers||fg_workers;
    ImGui::BeginDisabled(report_busy||pending);
    static bool declined=false;
    if(ImGui::Button(zh?"采集当前兼容路径各层（F7）":"Capture current compatibility layers (F7)"))declined=!RequestFullscreen();
    ImGui::EndDisabled();
    if(declined)ImGui::TextWrapped("%s",zh?"本次请求未被接受，请等待当前工作完成后再试。":"This request was not accepted; retry after the current work finishes.");
    const auto state=[&](bool is_fg,unsigned value,unsigned workers) {
        if(value==0)return;
        const char* route=is_fg?"FG":"Present";
        if(value==3)ImGui::TextWrapped(zh?"%s 各层捕获失败；请按 F8 保存报告。":"%s layer capture failed; save an F8 report.",route);
        else if(value==2&&!workers) {
            ImGui::TextWrapped(zh?"%s 各层数据已导出。":"%s layer data exported.",route);
            ImGui::TextWrapped("%s",is_fg?"Documents/DLSS5-FG-Capture":"Documents/DLSS5-Present-Capture");
        } else if(workers)ImGui::TextWrapped(zh?"%s 正在导出各层数据。":"%s layer data is being exported.",route);
        else if(value==1)ImGui::TextWrapped(zh?"%s 请求待完成：等待可捕获的当前画面和已提交工作完成。":"%s request pending: waiting for a capturable current frame and completion of submitted work.",route);
        else ImGui::TextWrapped(zh?"%s 捕获状态暂不可用；请按 F8 保存报告。":"%s capture status is unavailable; save an F8 report.",route);
    };
    state(false,present,present_workers);state(true,fg,fg_workers);
    ImGui::TextWrapped("%s",zh?"这些文件是当前处理层的数据，不是游戏原生阶段截图，也不代表最终屏幕输出。":"These files describe current processing layers, not native game-stage screenshots or final screen output.");
}

void DrawControls() {
    const auto state=g_status.load();
    const bool use_chinese_ui=UseChineseUi();
    ImGui::TextWrapped("%s", use_chinese_ui
        ? "不生效或旁路：按 F8，发送一个完整 TXT 即可。颜色、闪烁等画面异常：可另用 F7 图像补充。"
        : "Bypass or no effect: press F8 and send its single TXT. For visual issues, optionally add an F7 image supplement.");
    DrawCoreStatus();
    const bool busy=IsActive() || state==Status::Packaging || nr_core_feedback::Busy();
    ImGui::BeginDisabled(busy);
    if(ImGui::Button(use_chinese_ui?"生成反馈 TXT（F8，推荐）":"Save feedback TXT (F8, recommended)"))RequestCore();
    F8Tooltip(use_chinese_ui?"用于不生效、旁路和兼容性。成功写完一个带时间、游戏、版本名的 TXT 后自动复制文件路径；不复制正文、不生成 ZIP、不采图片、不改设置。TXT 上限 192 KiB。":"For bypass and compatibility issues. After one named TXT is written successfully, its file path is copied automatically. No report text, ZIP, images or setting changes. Bounded report ceiling: 192 KiB.");
    ImGui::EndDisabled();
    const bool compatibility=nr_processing_runtime::PresentSelected();
    if(compatibility)DrawCompatibilityCapture(use_chinese_ui,busy);
    if(!busy) {

        if(!compatibility) {
        if(ImGui::Button(use_chinese_ui?"采集全屏原图（F7）":"Capture full-screen original (F7)"))RequestFullscreen();
        F8Tooltip(use_chinese_ui?"保存当前完整游戏画面，不裁切、不缩小。":"Save the entire game view without cropping or resizing.");
        if(ImGui::Button(use_chinese_ui?"七组画面对比：全屏原图":"Seven-group full-screen comparison"))RequestFullscreen(Comparison::ShareMatrix);
        F8Tooltip(use_chinese_ui?"依次保存关闭 NR、NR→SR 50/75/100、SR→NR 50/75/100。保持镜头不动，完成或取消后恢复设置。开启 RR 时跳过 NR→SR。":"Capture NR off, NR-before-SR 50/75/100 and SR-before-NR 50/75/100 in order. Keep the camera still. Original settings are restored. RR skips NR-before-SR groups.");
        }

    } else if(!compatibility) {
        AcquireSRWLockShared(&g_lock);
        if(g_session){const auto &s=*g_session;const auto &g=s.groups[s.group];ImGui::Text(use_chinese_ui?"第 %u/%u 组，截图%s":"Group %u/%u, screenshot %s",s.group+1,s.group_count,g.shot_received?(use_chinese_ui?"已保存":"saved"):(use_chinese_ui?"等待中":"pending"));}
        ReleaseSRWLockShared(&g_lock);
        if(IsActive() && ImGui::Button(use_chinese_ui?"取消并恢复设置":"Cancel and restore"))Cancel();
    }
    if(!compatibility && (state==Status::Complete || state==Status::Partial || state==Status::Failed)) {
        ImGui::TextWrapped("%s",state==Status::Complete?(use_chinese_ui?"图像补充包已生成，请与新核心包一起提交。":"Image supplement saved; submit its paired core package too."):state==Status::Partial?(use_chinese_ui?"图像补充包部分完成，缺失原因已记录。":"Image supplement partially complete; missing-data reasons included."):(use_chinese_ui?"图像采集失败或已取消，请检查核心包及磁盘空间。":"Image capture failed or was cancelled; inspect the core package and disk space."));
        char path[4096]={};AcquireSRWLockShared(&g_lock);WideToUtf8(g_last_zip,path,sizeof(path));ReleaseSRWLockShared(&g_lock);
        if(path[0]){ImGui::TextWrapped("%s",path);if(ImGui::Button(use_chinese_ui?"复制图像包路径":"Copy image path"))ImGui::SetClipboardText(path);}
    }
}
} // namespace nr_f8
