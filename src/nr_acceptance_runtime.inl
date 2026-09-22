// Included after F8. Lock order: this controller -> F8 control -> chain owner.
// The watchdog never calls a runtime pointer; only Present records callback time.
#include "nr_acceptance_policy.h"
namespace nr_acceptance {
using namespace nr_acceptance_policy;
constexpr unsigned kRows=32768,kSteps=6;
struct Row { unsigned group=0,config=0,route=0,temporal_state=0,reset_reasons=0,since_reset=0; float source_to_canonical=1; unsigned long long tick=0,source=0,instance=0,evaluations=0,submitted=0,temporal_submitted=0,carriers=0,attempts=0,stream=0,epoch=0; };
struct Group {
    Step step{};ChainConfig requested{};unsigned generation=0,percent=0;
    unsigned long long eval_start=0,submit_start=0,temporal_start=0,eval_end=0,submit_end=0,temporal_end=0;
    unsigned long long carrier_start=0,carrier_end=0,attempt_start=0,attempt_end=0;
    unsigned long long measured_instance=0;
    unsigned long long warmup_eval_start=0,warmup_submit_start=0,warmup_evaluations=0,warmup_submissions=0;
    unsigned warmup_temporal_state=0;
    unsigned long long usage_start=0,usage_end=0,usage_sampled_peak=0,budget=0;
    bool memory_known=false,adopted=false,temporal_adopted=false,measured=false,capture_requested=false,capture_complete=false,report_requested=false;
    unsigned long long capture_id=0,stage_tick=0;
    char reason[128]{},report_session[96]{};
    wchar_t report_path[1024]{},capture_path[1024]{};
    nr_timing::Snapshot cpu_before{},cpu_after{};
};
struct Session {
    unsigned long long id=0,last_present=0,last_memory=0,dropped_start=0;
    Program program=Program::Current;unsigned index=0,count=0,rows=0;bool overflow=false,restored=false,cancelled=false,deep=false;
    ChainConfig original{};nr_post_settings::Options post_original{};nr_processing::Session processing_original{};
    nr_processing::Start original_effective=nr_processing::Start::Before;
    Window window{};Group groups[kSteps]{};Row samples[kRows]{};
    LUID adapter{};bool adapter_known=false;
    reshade::api::effect_runtime* runtime=nullptr;
    wchar_t directory[1024]{};
};
SRWLOCK lock=SRWLOCK_INIT;
Session* session=nullptr;
std::atomic<bool> active{false};std::atomic<bool> packaging{false};
std::atomic<unsigned long long> next_id{0};
std::atomic<unsigned long long> dropped_samples{0};
wchar_t last_path[1024]{};char last_reason[128]{};bool last_restored=false;
bool Active(){return active.load(std::memory_order_acquire);}
nr_processing::Start ObservedRoute(){return IsRayReconstructionRouteObserved()?nr_processing::Start::After:nr_processing_runtime::Effective();}
void Reason(Group& g,const char* reason){if(!g.reason[0])std::snprintf(g.reason,sizeof(g.reason),"%s",reason);}
bool SameRequested(const ChainConfig& expected,ChainConfig actual){actual.perf_epoch=expected.perf_epoch;return SameConfig(expected,actual);}
void StartGroup(Session& s){
    auto& g=s.groups[s.index];g.step=Get(s.program,s.index,s.original_effective==nr_processing::Start::Before);g.requested=s.original;
    if(g.step.backend>=0){g.requested.temporal_backend=static_cast<unsigned>(g.step.backend);g.requested.temporal_scope=g.step.scope;}
    if(g.step.colour>=0)nr_colour_preferences::SetRange(g.requested,float(g.step.colour)/10.f);
    g.percent=s.post_original.percent;
    if(g.step.percent){
        if(s.original_effective==nr_processing::Start::Before){g.requested.work_mode=5;g.requested.custom_work_scale=float(g.step.percent)/100.f;}
        else g.percent=g.step.percent;
    }
    if(g.step.scope&&s.original.nr_passes<2)Reason(g,"later-scope-needs-two-layers");
    if(s.program!=Program::Current&&!s.original.enabled)Reason(g,"enable-NR-before-comparison");
    if(s.program!=Program::Current&&s.original_effective==nr_processing::Start::Present)Reason(g,"comparison-Present-unavailable-use-current-baseline");
    if(g.step.backend>0&&nr_processing_runtime::PresentSelected())Reason(g,"temporal-Present-unavailable");
    if(!g.reason[0]){
        EnterCriticalSection(&g_chain_cs);
        ApplyConfig(g.requested,"acceptance transient",false);
        const bool post=nr_post_settings::RequestForCapture({s.post_original.rr,g.percent});
        if(!post||!SameRequested(g.requested,g_config))Reason(g,"requested-settings-not-applied");
        g.generation=g_config_generation.load();
        LeaveCriticalSection(&g_chain_cs);
    }
    g.eval_start=g_nr_successful_evaluations.load();g.submit_start=g_nr_transfer_submitted_frames.load();g.temporal_start=nr_temporal_status::submitted.load();
    g.warmup_eval_start=g.eval_start;g.warmup_submit_start=g.submit_start;
    s.window.Start(GetTickCount64());
    if(g.reason[0])s.window.phase=Phase::Report;
}
DWORD WINAPI Export(void* opaque){
    std::unique_ptr<Session> s(static_cast<Session*>(opaque));
    wchar_t path[1024]{};swprintf_s(path,L"%ls\\acceptance.csv",s->directory);
    FILE* f=nullptr;_wfopen_s(&f,path,L"wb");bool ok=f!=nullptr;
    if(f){
        std::fprintf(f,"group,qpc,config,route,last_successful_source,instance,NR_evaluations,NR_submissions,temporal_submissions,temporal_state,carrier_calls,transfer_attempts,source_stream,source_epoch,primary_reset_reasons,primary_since_reset,primary_source_to_canonical\n");
        for(unsigned i=0;i<s->rows;++i){const auto& r=s->samples[i];std::fprintf(f,"%u,%llu,%u,%u,%llu,%llu,%llu,%llu,%llu,%u,%llu,%llu,%llu,%llu,%u,%u,%.9g\n",r.group,r.tick,r.config,r.route,r.source,r.instance,r.evaluations,r.submitted,r.temporal_submitted,r.temporal_state,r.carriers,r.attempts,r.stream,r.epoch,r.reset_reasons,r.since_reset,double(r.source_to_canonical));}
        ok=std::ferror(f)==0;ok=std::fclose(f)==0&&ok;
    }
    swprintf_s(path,L"%ls\\acceptance.txt",s->directory);f=nullptr;_wfopen_s(&f,path,L"wb");ok=f&&ok;
    if(f){
        std::fprintf(f,"format=unified-acceptance-v1\nbuild=%s\nsource=%s\nsession=%llu\nprogram=%u\nrestored=%u\ncancelled=%u\nrows=%u\nrowsOverflow=%u\nqpcFrequency=%llu\niniWrites=0\nmodelSwitches=0\nqualityVerdict=not-evaluated\nframeTiming=ReShade Present callback intervals, not GPU time or generated/displayed FPS; source column is latest successful NR source, not identity of each callback\nmemory=worker sampled process-local DXGI usage/budget, not exact peak or NGX allocation total\nmeasurement=20 seconds after at least 3 seconds and 64 successful post-reset evaluations; captures and F8 occur afterwards\n",
            kBuildIdentity,kSourceCommit,s->id,static_cast<unsigned>(s->program),s->restored,s->cancelled,s->rows,s->overflow,static_cast<unsigned long long>(NrTimingFrequency()));
        std::fprintf(f,"missedCallbackSamples=%llu\n",dropped_samples.load()-s->dropped_start);
        std::fprintf(f,"adoptionDenominator=observed carrier calls, not unique displayed frames; compare NR submissions, transfer attempts and source continuity; NR evaluate can include several layers\n");
        std::fprintf(f,"resetSchema=nr_reset_observation.Sample primary layer; bit64 is RR placeholder depth, not exposure readback; primary_source_to_canonical is the actual normalization multiplier, not nits; repeated rows of the same source are not new evaluations\n");
        for(unsigned i=0;i<s->count;++i){const auto& g=s->groups[i];char report[4096]{};WideToUtf8(g.report_path,report,sizeof(report));
            std::fprintf(f,"group.%u.warmup.nrEvaluations=%llu\ngroup.%u.warmup.nrSubmissions=%llu\ngroup.%u.warmup.temporalState=%s\n",
                i,g.warmup_evaluations,i,g.warmup_submissions,i,nr_temporal_status::Name(g.warmup_temporal_state));
            wchar_t local_report[1024]{};swprintf_s(local_report,L"%ls\\group-%u-f8.txt",s->directory,i);
            if(g.report_path[0]&&CopyFileW(g.report_path,local_report,TRUE))WideToUtf8(local_report,report,sizeof(report));
            char capture[4096]{};WideToUtf8(g.capture_path,capture,sizeof(capture));std::fprintf(f,"group.%u.captureDirectory=%s\n",i,capture);
            const auto carriers=g.carrier_end>=g.carrier_start?g.carrier_end-g.carrier_start:0;
            const auto attempts=g.attempt_end>=g.attempt_start?g.attempt_end-g.attempt_start:0;
            std::fprintf(f,"group.%u.carrierCalls=%llu\ngroup.%u.transferAttempts=%llu\n",i,carriers,i,attempts);
            if(carriers&&g.submit_end>=g.submit_start)std::fprintf(f,"group.%u.submittedPerCarrier=%.9g\n",i,double(g.submit_end-g.submit_start)/double(carriers));
            std::fprintf(f,"group.%u=%s\ngroup.%u.config=%u\ngroup.%u.postPercent=%u\ngroup.%u.measured=%u\ngroup.%u.reason=%s\ngroup.%u.nrEvaluations=%llu\ngroup.%u.nrSubmissions=%llu\ngroup.%u.temporalSubmissions=%llu\ngroup.%u.nrAdopted=%u\ngroup.%u.temporalAdopted=%u\ngroup.%u.captureRequest=%llu\ngroup.%u.captureComplete=%u\ngroup.%u.report=%s\ngroup.%u.memoryKnown=%u\ngroup.%u.usageStart=%llu\ngroup.%u.usageEnd=%llu\ngroup.%u.usageSampledPeak=%llu\ngroup.%u.budget=%llu\n",
                i,g.step.name?g.step.name:"not-started",i,g.generation,i,g.percent,i,g.measured,i,g.reason[0]?g.reason:"recorded-not-quality-verified",i,g.eval_end>=g.eval_start?g.eval_end-g.eval_start:0,i,g.submit_end>=g.submit_start?g.submit_end-g.submit_start:0,i,g.temporal_end>=g.temporal_start?g.temporal_end-g.temporal_start:0,i,g.adopted,i,g.temporal_adopted,i,g.capture_id,i,g.capture_complete,i,report,i,g.memory_known,i,g.usage_start,i,g.usage_end,i,g.usage_sampled_peak,i,g.budget);
            const bool same=g.cpu_before.epoch==g.cpu_after.epoch&&!g.cpu_before.counter_overflow&&!g.cpu_after.counter_overflow;
            std::fprintf(f,"group.%u.cpuEpochComparable=%u\n",i,same);
            if(same)for(unsigned j=0;j<nr_timing::kStageCount;++j){const auto& a=g.cpu_before.stages[j];const auto& b=g.cpu_after.stages[j];if(b.count>=a.count&&b.total_ticks>=a.total_ticks)std::fprintf(f,"group.%u.cpu.%s=count:%llu,totalTicks:%llu\n",i,nr_timing::Name(static_cast<nr_timing::Stage>(j)),static_cast<unsigned long long>(b.count-a.count),static_cast<unsigned long long>(b.total_ticks-a.total_ticks));}
        }
        ok=std::ferror(f)==0&&ok;ok=std::fclose(f)==0&&ok;
    }
    AcquireSRWLockExclusive(&lock);wcscpy_s(last_path,path);last_restored=s->restored;
    std::snprintf(last_reason,sizeof(last_reason),"%s",!ok?"export-failed":s->cancelled?"cancelled; inspect partial record":s->overflow?"sample-limit; partial":"recorded; inspect adoption and unsupported groups");
    packaging.store(false);ReleaseSRWLockExclusive(&lock);return 0;
}
void Finish(Session* s,bool cancelled){
    s->cancelled=cancelled;
    auto& g=s->groups[s->index];
    if(cancelled&&g.capture_requested)CancelPendingStageCapture(g.capture_id);
    if(g.report_requested&&nr_core_feedback::Busy())nr_core_feedback::Cancel();
    EnterCriticalSection(&g_chain_cs);
    const bool route_changed=nr_processing_runtime::Effective()!=s->processing_original.Current();
    ApplyConfig(s->original,"acceptance restore",false);
    nr_post_settings::RequestForCapture(s->post_original);
    nr_processing_runtime::session=s->processing_original;
    nr_processing_runtime::effective.store(static_cast<unsigned>(s->processing_original.Current()),std::memory_order_release);
    if(route_changed)g_history_generation.fetch_add(1,std::memory_order_relaxed);
    s->restored=SameRequested(s->original,g_config)&&nr_post_settings::Read().percent==s->post_original.percent;
    nr_f8::g_config_hold.store(false);LeaveCriticalSection(&g_chain_cs);
    session=nullptr;active.store(false);packaging.store(true);
    if(!QueueUserWorkItem(Export,s,WT_EXECUTEDEFAULT)){last_restored=s->restored;std::snprintf(last_reason,sizeof(last_reason),"export-worker-unavailable");packaging.store(false);delete s;}
}
void Tick(reshade::api::effect_runtime* runtime){
    auto* s=session;if(!s)return;const auto now=GetTickCount64();auto& g=s->groups[s->index];
    if(runtime){if(runtime!=s->runtime)return;s->last_present=now;}
    if(now-s->last_present>5000){Reason(g,"Present-stopped; restored");Finish(s,true);return;}
    if(s->window.phase==Phase::Warmup||s->window.phase==Phase::Measure){
        if(!runtime)return;
        Observation o{};Row r{};
        if(TryEnterCriticalSection(&g_chain_cs)){
            const auto& identity=g_applied_nr_identity;
            o.enabled=g_config.enabled;o.matches=g_config_generation.load()==g.generation&&SameRequested(g.requested,g_config)&&nr_post_settings::Read().percent==g.percent;
            const auto route=ObservedRoute();
            o.matches=o.matches&&route==s->original_effective;
            const auto& sample=nr_reset_observation::counters.layers[0].last;
            o.since_reset=sample.config_generation==g.generation?sample.since_reset:0;
            r.config=g_config_generation.load();r.route=static_cast<unsigned>(route);r.source=sample.source_frame;r.instance=identity.instance_id;
            r.stream=sample.stream;r.epoch=sample.epoch;r.reset_reasons=sample.reasons;r.since_reset=sample.since_reset;r.source_to_canonical=sample.source_to_canonical;
            if(s->window.phase==Phase::Measure&&o.enabled&&g.measured_instance!=r.instance)o.matches=false;
            LeaveCriticalSection(&g_chain_cs);
        }else {++dropped_samples;return;}
        r.group=s->index;r.tick=NrTimingCounter();r.evaluations=g_nr_successful_evaluations.load();r.submitted=g_nr_transfer_submitted_frames.load();r.temporal_submitted=nr_temporal_status::submitted.load();r.temporal_state=nr_temporal_status::state.load();
        r.carriers=g_carrier_evaluations.load();r.attempts=g_nr_transfer_attempted_frames.load();
        o.adopted=CountersAdvanced(g.submit_start,r.submitted);o.temporal=!o.enabled||g.requested.temporal_backend==0||CountersAdvanced(g.temporal_start,r.temporal_submitted);
        const auto previous=s->window.phase;s->window.Tick(now,o);
        if(previous==Phase::Warmup){
            g.warmup_evaluations=r.evaluations>=g.warmup_eval_start?r.evaluations-g.warmup_eval_start:0;
            g.warmup_submissions=r.submitted>=g.warmup_submit_start?r.submitted-g.warmup_submit_start:0;
            g.warmup_temporal_state=r.temporal_state;
            if(s->window.phase==Phase::Report&&o.matches&&g.requested.temporal_backend&&
               (r.temporal_state==10||r.temporal_state==11||r.temporal_state==18||r.temporal_state==19))
                Reason(g,nr_temporal_status::Name(r.temporal_state));
        }
        if(previous==Phase::Warmup&&s->window.phase==Phase::Measure){
            g.eval_start=r.evaluations;g.submit_start=r.submitted;g.temporal_start=r.temporal_submitted;SnapshotNrTiming(g.cpu_before);
            g.carrier_start=r.carriers;g.attempt_start=r.attempts;
            g.measured_instance=r.instance;
        }
        if(s->window.phase==Phase::Measure){if(s->rows<kRows)s->samples[s->rows++]=r;else s->overflow=true;}
        if(previous==Phase::Measure&&s->window.phase==Phase::Report){
            g.measured=true;g.eval_end=r.evaluations;g.submit_end=r.submitted;g.temporal_end=r.temporal_submitted;SnapshotNrTiming(g.cpu_after);
            g.carrier_end=r.carriers;g.attempt_end=r.attempts;
            g.adopted=CountersAdvanced(g.submit_start,g.submit_end);g.temporal_adopted=CountersAdvanced(g.temporal_start,g.temporal_end);
            if(g.requested.enabled&&!g.adopted)Reason(g,"NR-not-adopted-during-measurement");
            if(g.requested.temporal_backend&&!g.temporal_adopted)Reason(g,"temporal-not-adopted-during-measurement");
        }
        if(s->window.refusal)Reason(g,s->window.refusal);
    }
    if(s->window.phase==Phase::Report){
        if(!g.report_requested){
            if(nr_core_feedback::Busy())return;
            g.report_requested=nr_core_feedback::Request();g.stage_tick=now;
            if(g.report_requested){const auto pair=nr_core_feedback::SnapshotPair();std::snprintf(g.report_session,sizeof(g.report_session),"%s",pair.session);}
            else Reason(g,"report-request-failed");
        }
        if(nr_core_feedback::Busy()&&now-g.stage_tick<15000)return;
        const auto pair=nr_core_feedback::SnapshotPair();
        if(g.report_requested&&pair.ready&&pair.text_complete&&!std::strcmp(pair.session,g.report_session)&&pair.config==g.generation){
            AcquireSRWLockShared(&g_feedback_lock);wcscpy_s(g.report_path,nr_core_feedback::g_report_path);ReleaseSRWLockShared(&g_feedback_lock);
        }else {Reason(g,"paired-report-unavailable");if(nr_core_feedback::Busy())nr_core_feedback::Cancel();}
        s->window.phase=Phase::Capture;g.stage_tick=now;
        if(s->deep&&g.step.capture&&!g.reason[0]&&!nr_processing_runtime::PresentSelected()){
            g.capture_requested=RequestStageCaptureOwned();g.capture_id=g_stage_capture_request_id.load();
            if(!g.capture_requested)Reason(g,"stage-capture-unavailable");
        }
    }
    if(s->window.phase==Phase::Capture){
        if(g.capture_requested){
            const auto status=StageCaptureStatus();
            if(status!=0&&now-g.stage_tick<30000)return;
            g.capture_complete=status==0&&g_stage_capture_last_result.load()==1&&g_stage_capture_request_id.load()==g.capture_id;
            StageCaptureExportReceipt receipt{};
            if(ReadStageCaptureExportReceipt(g.capture_id,receipt)){
                wcscpy_s(g.capture_path,receipt.directory);
                g.capture_complete=g.capture_complete&&receipt.stages_complete;
            }else g.capture_complete=false;
            if(!g.capture_complete){Reason(g,"stage-capture-partial-or-timeout");CancelPendingStageCapture(g.capture_id);}
        }
        if(++s->index<s->count)StartGroup(*s);else {--s->index;Finish(s,false);}
    }
}
DWORD WINAPI Watchdog(void* opaque){
    const auto id=static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(opaque));
    for(;;){Sleep(250);AcquireSRWLockExclusive(&lock);
        if(!session||session->id!=id){ReleaseSRWLockExclusive(&lock);break;}
        AcquireSRWLockExclusive(&nr_f8::g_control);
        auto* s=session;
        if(GetTickCount64()-s->last_memory>=1000){
            s->last_memory=GetTickCount64();const auto memory=nr_process_memory::Read(s->adapter,s->adapter_known);
            if(s->window.phase==Phase::Measure&&SUCCEEDED(memory.local_result)){
                auto& g=s->groups[s->index];if(!g.memory_known)g.usage_start=memory.local.CurrentUsage;
                g.memory_known=true;g.usage_end=memory.local.CurrentUsage;g.budget=memory.local.Budget;g.usage_sampled_peak=std::max(g.usage_sampled_peak,static_cast<unsigned long long>(memory.local.CurrentUsage));
            }
        }
        Tick(nullptr);ReleaseSRWLockExclusive(&nr_f8::g_control);ReleaseSRWLockExclusive(&lock);
    }return 0;
}
bool Start(reshade::api::effect_runtime* runtime,Program program,bool deep){
    if(!runtime)return false;AcquireSRWLockExclusive(&lock);AcquireSRWLockExclusive(&nr_f8::g_control);
    bool accepted=!session&&!packaging.load()&&!nr_f8::IsActive()&&nr_f8::g_status.load()!=nr_f8::Status::Packaging&&!nr_core_feedback::Busy()&&StageCaptureStatus()==0;
    std::unique_ptr<Session> next(accepted?new(std::nothrow)Session:nullptr);HMODULE pinned=nullptr;
    accepted=next&&GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&Watchdog),&pinned);
    if(accepted)accepted=FeedbackCreateDirectory(next->directory);
    if(accepted){
        EnterCriticalSection(&g_chain_cs);bool expected=false;
        accepted=StageCaptureStatus()==0&&!g_config.region_preview&&!g_config.light_preview&&!g_config.face_preview&&!g_config.colour_neutral_probe&&nr_f8::g_config_hold.compare_exchange_strong(expected,true);
        if(accepted){next->original=g_config;next->processing_original=nr_processing_runtime::session;next->original_effective=ObservedRoute();next->post_original=nr_post_settings::Read();if(g_codec.device){next->adapter=ID3D12Device_GetAdapterLuid(g_codec.device);next->adapter_known=true;}}
        LeaveCriticalSection(&g_chain_cs);
    }
    if(accepted){
        next->id=++next_id;next->runtime=runtime;next->program=program;next->count=Count(program);next->deep=deep;next->last_present=GetTickCount64();next->dropped_start=dropped_samples.load();
        session=next.release();active.store(true);last_path[0]=0;last_reason[0]=0;last_restored=false;StartGroup(*session);
        if(!QueueUserWorkItem(Watchdog,reinterpret_cast<void*>(static_cast<std::uintptr_t>(session->id)),WT_EXECUTEDEFAULT)){Reason(session->groups[0],"watchdog-start-failed");Finish(session,true);accepted=false;}
    }
    if(!accepted)std::snprintf(last_reason,sizeof(last_reason),"busy, preview active, or output directory unavailable");
    ReleaseSRWLockExclusive(&nr_f8::g_control);ReleaseSRWLockExclusive(&lock);return accepted;
}
void Present(reshade::api::effect_runtime* runtime){if(!Active())return;if(!TryAcquireSRWLockExclusive(&lock)){++dropped_samples;return;}if(TryAcquireSRWLockExclusive(&nr_f8::g_control)){Tick(runtime);ReleaseSRWLockExclusive(&nr_f8::g_control);}else ++dropped_samples;ReleaseSRWLockExclusive(&lock);}
void Cancel(){AcquireSRWLockExclusive(&lock);AcquireSRWLockExclusive(&nr_f8::g_control);if(session){Reason(session->groups[session->index],"cancelled");Finish(session,true);}ReleaseSRWLockExclusive(&nr_f8::g_control);ReleaseSRWLockExclusive(&lock);}
void RuntimeDestroyed(reshade::api::effect_runtime* runtime){AcquireSRWLockExclusive(&lock);AcquireSRWLockExclusive(&nr_f8::g_control);if(session&&session->runtime==runtime){Reason(session->groups[session->index],"runtime-destroyed");Finish(session,true);}ReleaseSRWLockExclusive(&nr_f8::g_control);ReleaseSRWLockExclusive(&lock);}
void Draw(reshade::api::effect_runtime* runtime,bool pending){
    const bool zh=UseChineseUi();static int program=0;static bool deep=true;
    ImGui::Separator();ImGui::TextUnformatted(zh?"集中验收":"Guided acceptance");
    const bool running=Active();ImGui::BeginDisabled(running||packaging.load()||pending||nr_f8::IsActive()||nr_core_feedback::Busy());
    ImGui::Combo(zh?"本次方案":"This comparison",&program,zh?"当前设置·性能基准\0颜色与比例\0原生/AMD防闪\0":"Current settings / baseline\0Colour and scale\0Native / AMD temporal\0");
    ImGui::TextWrapped("%s",zh?"临时设置不写入INI，不切模型。每组预热后记录20秒；结束/取消恢复。RR/HDR/FG在游戏内手动切换，每次切好后重新开始。":"Temporary settings never write INI or switch models. Each group warms up then records 20 seconds. Finish/cancel restores settings. Change game RR/HDR/FG manually before starting a new comparison.");
    ImGui::TextWrapped("%s",program==1?(zh?"保持当前路线与模型；关闭防闪，比较100/99/低分，再以原比例比较新AI色彩范围0/0.7/1（临时启用对应颜色许可）。":"Keep route/model; temporal OFF; compare 100/99/low scale, then the new colour range 0/0.7/1 (temporary colour admission) at original scale."):program==2?(zh?"保持模型和比例；比较关闭、原生、AMD及双层以上的后续层范围。不可用组合明确记录。":"Keep model/scale; compare OFF, Native, AMD, then later-layer scope when available. Unsupported groups are explicit."):(zh?"保持全部设置，记录当前基准；用于同设置对比两个版本。":"Keep all settings; record a baseline for equal-settings version comparison."));
    if(program==1)ImGui::Checkbox(zh?"计时结束后采集模型阶段（会重置历史）":"Capture stages AFTER timing (resets history)",&deep);
    if(ImGui::Button(zh?"开始集中验收":"Start guided acceptance")){if(Start(runtime,static_cast<Program>(program),deep))runtime->open_overlay(false,reshade::api::input_source::mouse);}
    ImGui::EndDisabled();
    AcquireSRWLockShared(&lock);unsigned index=0,count=0,phase=0;const char* label="";if(session){index=session->index+1;count=session->count;phase=static_cast<unsigned>(session->window.phase);label=session->groups[session->index].step.name;}char path[4096]{},reason[128]{};WideToUtf8(last_path,path,sizeof(path));std::snprintf(reason,sizeof(reason),"%s",last_reason);const bool restored=last_restored;ReleaseSRWLockShared(&lock);
    const char* phase_name=phase==0?(zh?"预热":"Warmup"):phase==1?(zh?"计时":"Measuring"):phase==2?(zh?"文字报告":"Text report"):(zh?"画面采集":"Stage capture");
    if(running){ImGui::Text(zh?"第 %u/%u 组：%s；%s":"Group %u/%u: %s; %s",index,count,label,phase_name);if(ImGui::Button(zh?"停止并恢复设置":"Stop and restore settings"))Cancel();}
    if(reason[0])ImGui::TextWrapped(zh?"结果：%s；设置恢复=%s":"Result: %s; settings restored=%s",reason,restored?"yes":"no");
    if(path[0]){ImGui::TextWrapped("%s",path);if(ImGui::Button(zh?"复制验收记录路径":"Copy acceptance record path"))ImGui::SetClipboardText(path);}
    ImGui::TextWrapped("%s",zh?"记录成功不等于画质通过。CSV为回调间隔，不是补帧FPS；显存是进程采样值。无效果或旁路不能算性能提升。":"Recording is not a quality pass. CSV measures callback intervals, not generated FPS; memory is sampled process usage. Bypassed NR is not a performance gain.");
}
}
