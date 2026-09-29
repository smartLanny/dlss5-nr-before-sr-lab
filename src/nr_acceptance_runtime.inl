// Included after F8. Lock order: this controller -> F8 control -> chain owner.
// The watchdog never calls a runtime pointer; only Present records callback time.
#include "nr_acceptance_policy.h"
#include <shellapi.h>
namespace nr_acceptance {
using namespace nr_acceptance_policy;
constexpr unsigned kRows=32768,kSteps=9,kShots=3,kMemoryRows=2048;
struct Shot {
    bool requested=false,received=false,exported=false;
    unsigned config=0,width=0,height=0;
    unsigned long long instance=0,source=0,tick=0,private_adopted=0,gpu_frames=0,gpu_adopted=0;
    wchar_t path[1024]{};
    char exported_file[48]{};
};
struct Row { unsigned group=0,config=0,route=0,temporal_state=0,reset_reasons=0,since_reset=0; float source_to_canonical=1; unsigned long long tick=0,source=0,instance=0,evaluations=0,submitted=0,temporal_submitted=0,carriers=0,attempts=0,stream=0,epoch=0,gpu_config=0,gpu_instance=0,gpu_tested=0,gpu_adopted=0,present_completed=0,present_warmup=0; };
struct MemorySample {
    unsigned group=0,config=0,phase=0,percent=0,work_width=0,work_height=0,gpu_samples=0,nr_passes=0;
    unsigned observed_config_before=0,observed_config_after=0;
    unsigned long long tick=0,elapsed=0,usage=0,budget=0;
    unsigned long result=0;bool valid=false,config_matches=false;
};
struct PresentCounters {
    unsigned config=0;unsigned long long generation=0,completed_source=0,submitted=0,completed=0,evaluated=0,attempted=0,timing_source=0;
    bool reset_each_frame=false,timing_valid=false;double total_ms=0;
};
PresentCounters ReadPresentLocked(); // g_chain_cs held; defined after the Present runtime include.
struct Group {
    Step step{};ChainConfig requested{};unsigned generation=0,percent=0;
    unsigned long long eval_start=0,submit_start=0,temporal_start=0,eval_end=0,submit_end=0,temporal_end=0;
    unsigned long long gpu_frames_start=0,gpu_tested_start=0,gpu_adopted_start=0,gpu_rejected_start=0,gpu_warmup_start=0,gpu_depth_rejected_start=0;
    unsigned long long gpu_frames_end=0,gpu_tested_end=0,gpu_adopted_end=0,gpu_rejected_end=0,gpu_warmup_end=0,gpu_depth_rejected_end=0;
    unsigned long long gpu_config_start=0,gpu_instance_start=0,gpu_config_end=0,gpu_instance_end=0;
    unsigned long long private_composed_start=0,private_submitted_start=0,private_adopted_start=0;
    unsigned long long private_composed_end=0,private_submitted_end=0,private_adopted_end=0;
    unsigned long long carrier_start=0,carrier_end=0,attempt_start=0,attempt_end=0;
    unsigned long long measured_instance=0;
    unsigned present_config_start=0;unsigned long long present_generation_start=0,present_completed_start=0,present_measure_completed_start=0,present_measure_source_start=0,present_completed_end=0;
    bool present_reset_each_frame=false,present_timing_valid=false;double present_total_ms=0;
    unsigned long long warmup_eval_start=0,warmup_submit_start=0,warmup_evaluations=0,warmup_submissions=0;
    unsigned warmup_temporal_state=0;
    unsigned long long usage_start=0,usage_end=0,usage_sampled_peak=0,budget=0;
    unsigned memory_success=0,memory_failure=0,memory_measure_success=0;
    unsigned long long memory_group_start=0,memory_first_tick=0,memory_last_tick=0;
    unsigned dual_timing_config=0,dual_timing_samples=0;
    double dual_median_ms[7]{},dual_p95_ms[7]{};
    unsigned long long private_timing_config=0,private_timing_instance=0,private_timing_samples=0;
    double private_gpu_first_create_ms=0,private_gpu_first_inference_ms=0,private_gpu_median_ms=0,private_gpu_p95_ms=0;
    bool private_gpu_timing_available=false;
    bool memory_known=false,adopted=false,temporal_adopted=false,private_adopted=false,measured=false,capture_requested=false,capture_complete=false,report_requested=false,post_rr=false;
    unsigned long long native_sr_entries=0,native_rr_entries=0;
    unsigned long long native_sr_first_generation=0,native_sr_last_generation=0;
    unsigned long long native_rr_first_generation=0,native_rr_last_generation=0;
    nr_source_frame::Source native_sr_first_source{},native_sr_last_source{};
    nr_source_frame::Source native_rr_first_source{},native_rr_last_source{};
    unsigned long long capture_id=0,stage_tick=0;
    char reason[128]{},report_session[96]{};
    wchar_t report_path[1024]{},capture_path[1024]{};
    Shot shots[kShots]{};unsigned shot_count=0,shot_width=0,shot_height=0;
    bool adoption_probe=false;unsigned long long probe_private=0,probe_gpu=0;
    ScreenshotPhase screenshot_phase=ScreenshotPhase::Warmup;
    unsigned long long performance_started=0,performance_owner=0,performance_stream=0,
        performance_epoch=0,performance_source=0,performance_frame=0,performance_queue=0,
        performance_last_temporal_adopted=0,performance_history=0;
    unsigned performance_warmup=0,performance_count=0,performance_rejected=0;
    unsigned long long performance_diagnostics_start[static_cast<unsigned>(nr_acceptance_gpu::Diagnostic::Count)]{};
    unsigned long long performance_diagnostics_end[static_cast<unsigned>(nr_acceptance_gpu::Diagnostic::Count)]{};
    int recorded_reconstruction_mode=-1,recorded_restore_mode=-1,recorded_edge_recovery=-1,recorded_near_black_guard=-1;
    unsigned long long recorded_policy_frame=0;
    unsigned performance_work_width=0,performance_work_height=0,
        performance_output_width=0,performance_output_height=0;
    unsigned long long performance_temporal_adopted=0,performance_private_adopted=0;
    double performance_ms[kScreenshotGpuSamples]{};
    unsigned long long performance_sources[kScreenshotGpuSamples]{},performance_frames[kScreenshotGpuSamples]{},
        performance_fences[kScreenshotGpuSamples]{};
    char performance_reason[96]{};
    nr_timing::Snapshot cpu_before{},cpu_after{};
};
struct Session {
    unsigned long long id=0,last_present=0,last_memory=0,dropped_start=0;
    Program program=Program::Current;unsigned index=0,count=0,rows=0;bool overflow=false,restored=false,cancelled=false,deep=false;
    ChainConfig original{};nr_post_settings::Options post_original{};nr_processing::Session processing_original{};
    nr_processing::Start original_effective=nr_processing::Start::Before;
    Window window{};Group groups[kSteps]{};Row samples[kRows]{};
    MemorySample memory_samples[kMemoryRows]{};unsigned memory_rows=0;bool memory_overflow=false;
    LUID adapter{};bool adapter_known=false;
    reshade::api::effect_runtime* runtime=nullptr;
    wchar_t directory[1024]{};
    char pending_shot[96]{};
};
SRWLOCK lock=SRWLOCK_INIT;
Session* session=nullptr;
std::atomic<bool> active{false};std::atomic<bool> packaging{false};
std::atomic<bool> performance_active{false};
std::atomic<unsigned long long> next_id{0};
std::atomic<unsigned long long> dropped_samples{0};
wchar_t last_path[1024]{};char last_reason[128]{};bool last_restored=false;
wchar_t last_screenshot_page[1024]{};
bool Active(){return active.load(std::memory_order_acquire);}
bool PerformanceActive(){return performance_active.load(std::memory_order_acquire);}
nr_processing::Start ObservedRoute(){return IsRayReconstructionRouteObserved()?nr_processing::Start::After:nr_processing_runtime::Effective();}
void Reason(Group& g,const char* reason){if(!g.reason[0])std::snprintf(g.reason,sizeof(g.reason),"%s",reason);}
const char* NativeRouteName(const Group& g){
    if(g.native_sr_entries&&g.native_rr_entries)return "mixed";
    if(g.native_sr_entries)return "native-SR";
    if(g.native_rr_entries)return "native-RR";
    return "unknown";
}
// An outermost game Feature Evaluate reached a real native wrapper. This is
// route evidence, never an NR adoption or GPU timing sample. Calls can repeat
// the same source frame; generation is the observed native Feature lifetime.
void ObserveNativeEntry(int feature,unsigned long long feature_generation){
    if(!Active()||(feature!=1&&feature!=13))return;
    if(!TryAcquireSRWLockExclusive(&lock)){
        ++dropped_samples;return;
    }
    auto* s=session;
    if(s&&s->program==Program::Screenshots&&s->index<s->count){
        auto& g=s->groups[s->index];
        if(g.generation==g_config_generation.load(std::memory_order_acquire)){
            auto& count=feature==1?g.native_sr_entries:g.native_rr_entries;
            auto& first=feature==1?g.native_sr_first_generation:g.native_rr_first_generation;
            auto& last=feature==1?g.native_sr_last_generation:g.native_rr_last_generation;
            if(!count)first=feature_generation;
            ++count;last=feature_generation;
        }
    }
    ReleaseSRWLockExclusive(&lock);
}
void ObserveNativeSource(int feature,const nr_source_frame::Source& source){
    if(!Active()||!source||(feature!=1&&feature!=13))return;
    if(!TryAcquireSRWLockExclusive(&lock))return;
    auto* s=session;
    if(s&&s->program==Program::Screenshots&&s->index<s->count){
        auto& g=s->groups[s->index];
        if(g.generation==g_config_generation.load(std::memory_order_acquire)){
            auto& first=feature==1?g.native_sr_first_source:g.native_rr_first_source;
            auto& last=feature==1?g.native_sr_last_source:g.native_rr_last_source;
            if(!first)first=source;
            last=source;
        }
    }
    ReleaseSRWLockExclusive(&lock);
}
bool SameRequested(const ChainConfig& expected,ChainConfig actual){actual.perf_epoch=expected.perf_epoch;return SameConfig(expected,actual);}
void StartGroup(Session& s){
    performance_active.store(false,std::memory_order_release);
    auto& g=s.groups[s.index];g.step=Get(s.program,s.index,s.original_effective==nr_processing::Start::Before);g.requested=s.original;
    g.native_sr_entries=g.native_rr_entries=0;
    g.native_sr_first_generation=g.native_sr_last_generation=0;
    g.native_rr_first_generation=g.native_rr_last_generation=0;
    g.native_sr_first_source=g.native_sr_last_source={};
    g.native_rr_first_source=g.native_rr_last_source={};
    if(s.program==Program::Screenshots){
        g.requested.enabled=s.index!=0;
        g.requested.processing_start=nr_processing::Start::After;
        g.requested.processing_selection=nr_processing_runtime::next_selection.fetch_add(1,std::memory_order_relaxed);
        g.requested.mode=1;
    }
    if(g.step.backend>=0){g.requested.temporal_backend=static_cast<unsigned>(g.step.backend);g.requested.temporal_scope=g.step.scope;}
    if(g.step.private_sr>=0)g.requested.private_residual_sr=g.step.private_sr!=0;
    if(g.step.temporal_mode>=0)g.requested.temporal_mode=static_cast<unsigned>(g.step.temporal_mode);
    if(g.step.colour>=0)nr_colour_preferences::SetRange(g.requested,float(g.step.colour)/10.f);
    g.percent=s.post_original.percent;
    if(g.step.percent){
        if(s.program==Program::Screenshots)g.percent=g.step.percent;
        else
        if(s.original_effective==nr_processing::Start::Before){g.requested.work_mode=5;g.requested.custom_work_scale=float(g.step.percent)/100.f;}
        else g.percent=g.step.percent;
    }
    if(g.step.scope&&s.original.nr_passes<2)Reason(g,"later-scope-needs-two-layers");
    if(s.program!=Program::Current&&s.program!=Program::Screenshots&&!s.original.enabled)Reason(g,"enable-NR-before-comparison");
    if(s.program!=Program::Current&&s.program!=Program::Screenshots&&s.original_effective==nr_processing::Start::Present)Reason(g,"comparison-Present-unavailable-use-current-baseline");
    if(g.step.backend>0&&nr_processing_runtime::PresentSelected())Reason(g,"temporal-Present-unavailable");
    if(s.program==Program::PrivateSr&&s.original_effective==nr_processing::Start::Before)Reason(g,"private-SR-needs-proven-post-SR-or-RR");
    if(!g.reason[0]){
        EnterCriticalSection(&g_chain_cs);
        ApplyConfig(g.requested,"acceptance transient",false);
        const bool post=nr_post_settings::RequestForCapture({s.post_original.rr,g.percent});
        if(!post||!SameRequested(g.requested,g_config))Reason(g,"requested-settings-not-applied");
        g.post_rr=nr_post_settings::Read().rr;
        g.generation=g_config_generation.load();
        LeaveCriticalSection(&g_chain_cs);
    }
    if(s.original_effective==nr_processing::Start::Present&&s.program==Program::Current){
        EnterCriticalSection(&g_chain_cs);const auto p=ReadPresentLocked();LeaveCriticalSection(&g_chain_cs);
        g.eval_start=p.evaluated;g.submit_start=p.submitted;g.present_completed_start=p.completed;
        g.present_config_start=p.config;g.present_generation_start=p.generation;
    }else {g.eval_start=g_nr_successful_evaluations.load();g.submit_start=g_nr_transfer_submitted_frames.load();}
    g.temporal_start=nr_temporal_status::submitted.load();
    g.gpu_frames_start=nr_temporal_status::gpu_frames.load();g.gpu_tested_start=nr_temporal_status::gpu_tested.load();
    g.gpu_adopted_start=nr_temporal_status::gpu_adopted.load();g.gpu_rejected_start=nr_temporal_status::gpu_rejected.load();
    g.gpu_warmup_start=nr_temporal_status::gpu_warmup.load();g.gpu_depth_rejected_start=nr_temporal_status::gpu_depth_rejected.load();
    g.gpu_config_start=nr_temporal_status::gpu_config.load();g.gpu_instance_start=nr_temporal_status::gpu_instance.load();
    g.private_composed_start=nr_private_sr_status::composed.load();g.private_submitted_start=nr_private_sr_status::submitted.load();g.private_adopted_start=nr_private_sr_status::adopted.load();
    g.warmup_eval_start=g.eval_start;g.warmup_submit_start=g.submit_start;
    g.memory_group_start=GetTickCount64();s.window.Start(g.memory_group_start);
    if(s.program==Program::Screenshots){
        g.screenshot_phase=ScreenshotPhase::Warmup;
        g.performance_started=s.window.started;
        g.performance_history=g_history_generation.load();
        g.performance_last_temporal_adopted=nr_temporal_status::gpu_adopted.load();
        nr_acceptance_gpu::diagnostic_config.store(g.generation,std::memory_order_relaxed);
        for(unsigned d=0;d<static_cast<unsigned>(nr_acceptance_gpu::Diagnostic::Count);++d)
            g.performance_diagnostics_start[d]=nr_acceptance_gpu::diagnostics[d].load();
        performance_active.store(!g.reason[0]&&g.requested.enabled,std::memory_order_release);
    }
    if(g.reason[0])s.window.phase=Phase::Report;
}
bool ExportMemorySamples(const Session& s){
    wchar_t path[1100]{};swprintf_s(path,L"%ls\\memory-samples.csv",s.directory);
    FILE* f=nullptr;_wfopen_s(&f,path,L"wb");if(!f)return false;
    std::fputs("group,plannedConfig,observedConfigBefore,observedConfigAfter,configMatches,phase,processTickMs,groupElapsedMs,postPercent,workWidth,workHeight,validGpuSamples,nrPasses,scope,localQueryHr,localValid,usageBytes,budgetBytes\n",f);
    static constexpr const char* phases[]={"warmup","measure","report","capture"};
    for(unsigned i=0;i<s.memory_rows;++i){const auto& m=s.memory_samples[i];
        std::fprintf(f,"%u,%u,%u,%u,%u,%s,%llu,%llu,%u,%u,%u,%u,%u,process-DXGI-local,0x%08lx,%u,%llu,%llu\n",
            m.group,m.config,m.observed_config_before,m.observed_config_after,m.config_matches?1u:0u,
            m.phase<4?phases[m.phase]:"unknown",m.tick,m.elapsed,m.percent,
            m.work_width,m.work_height,m.gpu_samples,m.nr_passes,m.result,m.valid?1u:0u,m.usage,m.budget);
    }
    bool ok=std::ferror(f)==0;return std::fclose(f)==0&&ok;
}
const char* MemoryMeasureStatus(const Session& s,unsigned group){
    const auto& g=s.groups[group];
    if(g.memory_measure_success)return "sampled";
    if(s.program==Program::Screenshots&&group==0)return "native-no-NR: no plugin GPU measure phase; see capture-phase samples";
    return "no-successful-measure-sample; see memory-samples.csv";
}
#include "nr_acceptance_screenshot_export.inl"
DWORD WINAPI Export(void* opaque){
    std::unique_ptr<Session> s(static_cast<Session*>(opaque));
    if(s->program==Program::Screenshots)return ExportScreenshots(std::move(s));
    wchar_t path[1024]{};swprintf_s(path,L"%ls\\acceptance.csv",s->directory);
    FILE* f=nullptr;_wfopen_s(&f,path,L"wb");bool ok=f!=nullptr;
    if(f){
        std::fprintf(f,"group,qpc,config,route,last_successful_source,instance,NR_evaluations,NR_submissions,temporal_submissions,temporal_state,carrier_calls,transfer_attempts,source_stream,source_epoch,primary_reset_reasons,primary_since_reset,primary_source_to_canonical,present_completed,present_completed_since_owner_start\n");
        for(unsigned i=0;i<s->rows;++i){const auto& r=s->samples[i];std::fprintf(f,"%u,%llu,%u,%u,%llu,%llu,%llu,%llu,%llu,%u,%llu,%llu,%llu,%llu,%u,%u,%.9g,%llu,%llu\n",r.group,r.tick,r.config,r.route,r.source,r.instance,r.evaluations,r.submitted,r.temporal_submitted,r.temporal_state,r.carriers,r.attempts,r.stream,r.epoch,r.reset_reasons,r.since_reset,double(r.source_to_canonical),r.present_completed,r.present_warmup);}
        ok=std::ferror(f)==0;ok=std::fclose(f)==0&&ok;
    }
    swprintf_s(path,L"%ls\\acceptance.txt",s->directory);f=nullptr;_wfopen_s(&f,path,L"wb");ok=f&&ok;
    if(f){
        std::fprintf(f,"format=unified-acceptance-v3\nbuild=%s\nsource=%s\nsession=%llu\nprogram=%u\nrestored=%u\ncancelled=%u\nrows=%u\nrowsOverflow=%u\nqpcFrequency=%llu\niniWrites=0\nmodelSwitches=0\nqualityVerdict=not-evaluated\nframeTiming=ReShade Present callback intervals, not GPU time or generated/displayed FPS; source column is latest completed Present source for Present baseline, otherwise latest native NR source\nwholeFrameGpuTiming=unavailable; callback intervals must not be read as GPU cost\nmemory=worker sampled process DXGI local usage/budget; sampled peak is not an instantaneous peak or plugin/NGX allocation total; see memory-samples.csv\nmemorySampling=success/failure/coverage span warmup, measure, report and capture; measureSuccess only counts measure; CSV configMatches must be checked for config attribution; legacy usage fields retain prior sampling semantics\nmeasurement=20 seconds after at least 3 seconds; native needs 64 post-reset evaluations, Present baseline needs 64 completed NR flights from current owner; captures and F8 occur afterwards\n",
            kBuildIdentity,kSourceCommit,s->id,static_cast<unsigned>(s->program),s->restored,s->cancelled,s->rows,s->overflow,static_cast<unsigned long long>(NrTimingFrequency()));
        std::fprintf(f,"missedCallbackSamples=%llu\n",dropped_samples.load()-s->dropped_start);
        std::fprintf(f,"memorySampleRows=%u\nmemorySampleRowsOverflow=%u\n",s->memory_rows,s->memory_overflow?1u:0u);
        std::fprintf(f,"adoptionDenominator=native: observed carrier calls; Present baseline: exact completed owned flights, not native carrier calls; neither proves final displayed quality\n");
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
            if(s->program==Program::Current&&s->original_effective==nr_processing::Start::Present)
                std::fprintf(f,"group.%u.presentOwner=%llu\ngroup.%u.presentCompletedDuringMeasure=%llu\ngroup.%u.presentResetEachFrame=%u\ngroup.%u.presentGpuTotalMs=%s\ngroup.%u.presentGpuTotalLastMs=%.9g\n",
                    i,g.present_generation_start,i,g.present_completed_end>=g.present_measure_completed_start?g.present_completed_end-g.present_measure_completed_start:0,
                    i,g.present_reset_each_frame,i,g.present_timing_valid?"exact-source-last":"unavailable",i,g.present_timing_valid?g.present_total_ms:0.0);
            std::fprintf(f,"group.%u.privateSubmissions=%llu\n",i,g.private_submitted_end>=g.private_submitted_start?g.private_submitted_end-g.private_submitted_start:0);
            std::fprintf(f,"group.%u.temporalGpuScope=completed GPU counter readback for this config and owner; adoption is historical sampling, not final display proof\n",i);
            std::fprintf(f,"group.%u.temporalGpuConfig=%llu\ngroup.%u.temporalGpuInstance=%llu\n",i,g.gpu_config_end,i,g.gpu_instance_end);
            std::fprintf(f,"group.%u.temporalGpu.frames=%llu\ngroup.%u.temporalGpu.tested=%llu\ngroup.%u.temporalGpu.adopted=%llu\ngroup.%u.temporalGpu.rejected=%llu\ngroup.%u.temporalGpu.warmup=%llu\ngroup.%u.temporalGpu.depthRejected=%llu\n",
                i,g.gpu_frames_end>=g.gpu_frames_start?g.gpu_frames_end-g.gpu_frames_start:0,
                i,g.gpu_tested_end>=g.gpu_tested_start?g.gpu_tested_end-g.gpu_tested_start:0,
                i,g.gpu_adopted_end>=g.gpu_adopted_start?g.gpu_adopted_end-g.gpu_adopted_start:0,
                i,g.gpu_rejected_end>=g.gpu_rejected_start?g.gpu_rejected_end-g.gpu_rejected_start:0,
                i,g.gpu_warmup_end>=g.gpu_warmup_start?g.gpu_warmup_end-g.gpu_warmup_start:0,
                i,g.gpu_depth_rejected_end>=g.gpu_depth_rejected_start?g.gpu_depth_rejected_end-g.gpu_depth_rejected_start:0);
            std::fprintf(f,"group.%u.privateGpuTimingScope=actual queue timestamps for private residual Encode+Feature1+Compose only; excludes guides, native SR/RR, NR, and whole frame\n",i);
            std::fprintf(f,"group.%u.privateGpuTimingConfig=%llu\ngroup.%u.privateGpuTimingInstance=%llu\ngroup.%u.privateGpuTimingSamples=%llu\ngroup.%u.privateGpuTimingAvailable=%u\n",i,g.private_timing_config,i,g.private_timing_instance,i,g.private_timing_samples,i,g.private_gpu_timing_available);
            if(g.private_gpu_timing_available)std::fprintf(f,"group.%u.privateGpuMs=firstCreate:%.9g,firstInference:%.9g,median:%.9g,p95:%.9g\n",i,g.private_gpu_first_create_ms,g.private_gpu_first_inference_ms,g.private_gpu_median_ms,g.private_gpu_p95_ms);
            std::fprintf(f,"group.%u.dualGpuTimingScope=GPU timestamp pairs for the multi-layer dual path only; rolling window may include warmup; excludes private SR, temporal, unrelated game work and whole-frame GPU time\n",i);
            std::fprintf(f,"group.%u.dualGpuTimingConfig=%u\ngroup.%u.dualGpuTimingSamples=%u\n",i,g.dual_timing_config,i,g.dual_timing_samples);
            if(g.dual_timing_samples&&g.dual_timing_config==g.generation){
                static constexpr const char* names[]={"NR1","Down","NR2","Combine","NativeSR","Chain","LowResidual"};
                for(unsigned j=0;j<7;++j)std::fprintf(f,"group.%u.dualGpuMs.%s=median:%.9g,p95:%.9g\n",i,names[j],g.dual_median_ms[j],g.dual_p95_ms[j]);
            }
            if(carriers&&g.submit_end>=g.submit_start)std::fprintf(f,"group.%u.submittedPerCarrier=%.9g\n",i,double(g.submit_end-g.submit_start)/double(carriers));
            std::fprintf(f,"group.%u=%s\ngroup.%u.config=%u\ngroup.%u.postPercent=%u\ngroup.%u.measured=%u\ngroup.%u.reason=%s\ngroup.%u.nrEvaluations=%llu\ngroup.%u.nrSubmissions=%llu\ngroup.%u.temporalSubmissions=%llu\ngroup.%u.nrRouteAdopted=%u\ngroup.%u.temporalHistoryAdopted=%u\ngroup.%u.privateComposed=%llu\ngroup.%u.privateAdopted=%llu\ngroup.%u.privateAdoptedDuringMeasure=%u\ngroup.%u.wholeFrameGpuTime=unavailable\ngroup.%u.captureRequest=%llu\ngroup.%u.captureComplete=%u\ngroup.%u.report=%s\ngroup.%u.memoryKnown=%u\ngroup.%u.usageStart=%llu\ngroup.%u.usageEnd=%llu\ngroup.%u.usageSampledPeak=%llu\ngroup.%u.budget=%llu\n",
                i,g.step.name?g.step.name:"not-started",i,g.generation,i,g.percent,i,g.measured,i,g.reason[0]?g.reason:"recorded-not-quality-verified",i,g.eval_end>=g.eval_start?g.eval_end-g.eval_start:0,i,g.submit_end>=g.submit_start?g.submit_end-g.submit_start:0,i,g.temporal_end>=g.temporal_start?g.temporal_end-g.temporal_start:0,i,g.adopted,i,g.temporal_adopted,i,g.private_composed_end>=g.private_composed_start?g.private_composed_end-g.private_composed_start:0,i,g.private_adopted_end>=g.private_adopted_start?g.private_adopted_end-g.private_adopted_start:0,i,g.private_adopted,i,i,g.capture_id,i,g.capture_complete,i,report,i,g.memory_known,i,g.usage_start,i,g.usage_end,i,g.usage_sampled_peak,i,g.budget);
            std::fprintf(f,"group.%u.memoryScope=process-DXGI-local\ngroup.%u.memoryPeakScope=sampled-not-instantaneous\ngroup.%u.memorySuccess=%u\ngroup.%u.memoryFailure=%u\ngroup.%u.memoryMeasureSuccess=%u\ngroup.%u.memoryFirstTickMs=%llu\ngroup.%u.memoryLastTickMs=%llu\ngroup.%u.memoryCoverageMs=%llu\ngroup.%u.memoryCoverageSeconds=%.3f\ngroup.%u.memoryMeasureStatus=%s\n",
                i,i,i,g.memory_success,i,g.memory_failure,i,g.memory_measure_success,i,g.memory_first_tick,i,g.memory_last_tick,
                i,g.memory_success?g.memory_last_tick-g.memory_first_tick:0,
                i,g.memory_success?double(g.memory_last_tick-g.memory_first_tick)/1000.0:0.0,i,MemoryMeasureStatus(*s,i));
            const bool same=g.cpu_before.epoch==g.cpu_after.epoch&&!g.cpu_before.counter_overflow&&!g.cpu_after.counter_overflow;
            std::fprintf(f,"group.%u.cpuEpochComparable=%u\n",i,same);
            if(same)for(unsigned j=0;j<nr_timing::kStageCount;++j){const auto& a=g.cpu_before.stages[j];const auto& b=g.cpu_after.stages[j];if(b.count>=a.count&&b.total_ticks>=a.total_ticks)std::fprintf(f,"group.%u.cpu.%s=count:%llu,totalTicks:%llu\n",i,nr_timing::Name(static_cast<nr_timing::Stage>(j)),static_cast<unsigned long long>(b.count-a.count),static_cast<unsigned long long>(b.total_ticks-a.total_ticks));}
        }
        ok=std::ferror(f)==0&&ok;ok=std::fclose(f)==0&&ok;
    }
    ok=ExportMemorySamples(*s)&&ok;
    AcquireSRWLockExclusive(&lock);wcscpy_s(last_path,path);last_restored=s->restored;
    std::snprintf(last_reason,sizeof(last_reason),"%s",!ok?"export-failed":s->cancelled?"cancelled; inspect partial record":s->overflow?"sample-limit; partial":"recorded; inspect adoption and unsupported groups");
    packaging.store(false);ReleaseSRWLockExclusive(&lock);return 0;
}
void Finish(Session* s,bool cancelled){
    auto& g=s->groups[s->index];
    if(s->program==Program::Screenshots&&g.screenshot_phase!=ScreenshotPhase::Capture){
        // Cancellation and watchdog exit do not pass EndScreenshotPerformance.
        // Preserve the current group's entry/decline evidence before restore.
        performance_active.store(false,std::memory_order_release);
        for(unsigned d=0;d<static_cast<unsigned>(nr_acceptance_gpu::Diagnostic::Count);++d)
            g.performance_diagnostics_end[d]=nr_acceptance_gpu::diagnostics[d].load();
        const auto temporal=nr_temporal_status::gpu_adopted.load();
        const auto private_sr=nr_private_sr_status::adopted.load();
        g.performance_temporal_adopted=temporal>=g.gpu_adopted_start?temporal-g.gpu_adopted_start:0;
        g.performance_private_adopted=private_sr>=g.private_adopted_start?private_sr-g.private_adopted_start:0;
        if(!g.performance_reason[0])std::snprintf(g.performance_reason,sizeof(g.performance_reason),"%s",
            cancelled?"cancelled-partial":"ended-before-performance-complete");
    }else performance_active.store(false,std::memory_order_release);
    s->cancelled=cancelled;
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
void ScreenshotReceipt(reshade::api::effect_runtime* runtime,const char* path,unsigned width,unsigned height){
    if(!path)return;
    AcquireSRWLockExclusive(&lock);
    auto* s=session;
    if(s&&s->program==Program::Screenshots&&runtime==s->runtime&&s->index<s->count){
        auto& g=s->groups[s->index];
        if(g.shot_count<kShots){auto& shot=g.shots[g.shot_count];
            char tag[96]{};std::snprintf(tag,sizeof(tag),"DLSS5_SHOT_%llu_%u_%u",s->id,s->index,g.shot_count);
            const char* base=path;for(const char* p=path;*p;++p)if(*p=='/'||*p=='\\')base=p+1;
            const char* ext=std::strrchr(base,'.');const auto tag_len=std::strlen(tag);
            const bool exact=ext&&static_cast<std::size_t>(ext-base)>=tag_len&&
                std::memcmp(ext-tag_len,tag,tag_len)==0;
            if(shot.requested&&!shot.received&&!g.reason[0]&&exact){
                EnterCriticalSection(&g_chain_cs);
                const bool same=g_config_generation.load()==shot.config&&SameRequested(g.requested,g_config)&&
                    nr_post_settings::Read().percent==g.percent&&nr_post_settings::Read().rr==g.post_rr&&
                    ObservedRoute()==nr_processing::Start::After&&
                    (!g.requested.enabled||g_applied_nr_identity.instance_id==shot.instance);
                LeaveCriticalSection(&g_chain_cs);
                if(!same||width!=shot.width||height!=shot.height){Reason(g,"screenshot-owner-or-display-changed");}
                else {
                    wchar_t decoded[1024]{},canonical[1024]{};
                    if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,decoded,1024)){
                        const DWORD n=GetFullPathNameW(decoded,1024,canonical,nullptr);
                        if(n&&n<1024&&nr_bundle::LocalAbsolutePath(canonical)&&
                            GetFileAttributesW(canonical)!=INVALID_FILE_ATTRIBUTES){
                            wcscpy_s(shot.path,canonical);shot.received=true;++g.shot_count;
                        }else Reason(g,"screenshot-file-missing");
                    }else Reason(g,"screenshot-path-invalid");
                }
            }
        }
    }
    ReleaseSRWLockExclusive(&lock);
}
void Screenshot(reshade::api::effect_runtime* runtime,const char* path){
    if(!Active()||!runtime||!path)return;
    unsigned width=0,height=0;runtime->get_screenshot_width_and_height(&width,&height);
    ScreenshotReceipt(runtime,path,width,height);
}
void EndScreenshotPerformance(Session& s,Group& g,const char* reason,unsigned long long now){
    performance_active.store(false,std::memory_order_release);
    for(unsigned d=0;d<static_cast<unsigned>(nr_acceptance_gpu::Diagnostic::Count);++d)
        g.performance_diagnostics_end[d]=nr_acceptance_gpu::diagnostics[d].load();
    const auto temporal=nr_temporal_status::gpu_adopted.load();
    const auto private_sr=nr_private_sr_status::adopted.load();
    g.performance_temporal_adopted=temporal>=g.gpu_adopted_start?temporal-g.gpu_adopted_start:0;
    g.performance_private_adopted=private_sr>=g.private_adopted_start?private_sr-g.private_adopted_start:0;
    if(reason&&!g.performance_reason[0])std::snprintf(g.performance_reason,sizeof(g.performance_reason),"%s",reason);
    g.screenshot_phase=ScreenshotPhase::Capture;
    s.window.Start(now); // Screenshot admission has its own warmup and timeout.
    g.adoption_probe=false;
}
bool TickScreenshotPerformance(Session& s,Group& g,reshade::api::effect_runtime* runtime,unsigned long long now){
    if(g.screenshot_phase==ScreenshotPhase::Capture)return false;
    if(g.reason[0]){EndScreenshotPerformance(s,g,"group-admission-failed",now);return false;}
    if(!g.requested.enabled){EndScreenshotPerformance(s,g,"not-applicable-no-plugin-GPU-work",now);return false;}
    EnterCriticalSection(&g_chain_cs);
    const bool same=g_config_generation.load()==g.generation&&SameRequested(g.requested,g_config)&&
        nr_post_settings::Read().percent==g.percent&&nr_post_settings::Read().rr==g.post_rr&&
        ObservedRoute()==nr_processing::Start::After&&g_history_generation.load()==g.performance_history;
    if(!same){LeaveCriticalSection(&g_chain_cs);Reason(g,"configuration-route-or-reset-changed");
        EndScreenshotPerformance(s,g,"configuration-route-or-reset-changed",now);return false;}
    nr_acceptance_gpu::Sample sample{};
    while(nr_acceptance_gpu::Pop(sample)){
        if(sample.config!=g.generation||sample.history!=g.performance_history)continue;
        const bool valid=sample.instance&&sample.frame&&sample.queue&&sample.fence&&sample.source&&
            sample.work_width&&sample.work_height&&sample.output_width&&sample.output_height&&
            sample.since_reset>=64&&std::isfinite(sample.ms)&&sample.ms>0&&
            (!g.requested.private_residual_sr||sample.private_composed)&&
            (!g.requested.temporal_backend||sample.temporal_recorded);
        if(!valid){++g.performance_rejected;continue;}
        if(!g.performance_owner){g.performance_owner=sample.instance;g.performance_stream=sample.source.stream;
            g.performance_epoch=sample.source.epoch;g.performance_queue=sample.queue;
            g.performance_work_width=sample.work_width;g.performance_work_height=sample.work_height;
            g.performance_output_width=sample.output_width;g.performance_output_height=sample.output_height;}
        if(g.performance_owner!=sample.instance||g.performance_stream!=sample.source.stream||
           g.performance_epoch!=sample.source.epoch||g.performance_queue!=sample.queue||
           g.performance_work_width!=sample.work_width||g.performance_work_height!=sample.work_height||
           g.performance_output_width!=sample.output_width||g.performance_output_height!=sample.output_height||
           sample.source.sequence<=g.performance_source||sample.frame<=g.performance_frame){
            ++g.performance_rejected;continue;
        }
        g.performance_source=sample.source.sequence;g.performance_frame=sample.frame;
        if(g.screenshot_phase==ScreenshotPhase::Warmup){
            if(++g.performance_warmup==kScreenshotWarmupSamples){
                g.screenshot_phase=ScreenshotPhase::Measure;g.performance_started=now;
            }
        }else if(g.performance_count<kScreenshotGpuSamples){
            const unsigned index=g.performance_count++;
            g.performance_ms[index]=sample.ms;
            g.performance_sources[index]=sample.source.sequence;
            g.performance_frames[index]=sample.frame;
            g.performance_fences[index]=sample.fence;
            if(g.performance_count==kScreenshotGpuSamples)break;
        }
    }
    LeaveCriticalSection(&g_chain_cs);
    if(g.requested.enabled){
        const auto policy=SnapshotColorPolicy(); // Acquires g_chain_cs itself.
        if(policy.status==NrColorPolicyStatus::Current&&policy.config_generation==g.generation&&
           policy.mode==1&&policy.frame_id>=g.recorded_policy_frame){
            g.recorded_reconstruction_mode=policy.policy.recorded_reconstruction_mode;
            g.recorded_restore_mode=policy.policy.recorded_restore_mode;
            g.recorded_edge_recovery=policy.policy.recorded_edge_recovery;
            g.recorded_near_black_guard=policy.policy.recorded_near_black_guard?1:0;
            g.recorded_policy_frame=policy.frame_id;
        }
    }
    if(g.screenshot_phase==ScreenshotPhase::Measure&&g.performance_count==kScreenshotGpuSamples){
        if(g.requested.temporal_backend&&
           (nr_temporal_status::gpu_config.load()!=g.generation||
            nr_temporal_status::gpu_instance.load()!=g.performance_owner||
            nr_temporal_status::gpu_adopted.load()<=g.gpu_adopted_start))
            EndScreenshotPerformance(s,g,"temporal-GPU-history-not-adopted",now);
        else if(g.requested.private_residual_sr&&nr_private_sr_status::adopted.load()<=g.private_adopted_start)
            EndScreenshotPerformance(s,g,"private-SR-not-submitted",now);
        else EndScreenshotPerformance(s,g,"complete",now);
    }else if(now-g.performance_started >= (g.screenshot_phase==ScreenshotPhase::Warmup?
        kScreenshotWarmupTimeoutMs:kScreenshotMeasureTimeoutMs))
        EndScreenshotPerformance(s,g,g.screenshot_phase==ScreenshotPhase::Warmup?
            "warmup-insufficient-valid-GPU-sources":"measure-insufficient-valid-GPU-samples",now);
    return g.screenshot_phase!=ScreenshotPhase::Capture;
}
void TickScreenshotsAtExtent(reshade::api::effect_runtime* runtime,unsigned width,unsigned height){
    auto* s=session;if(!s)return;auto& g=s->groups[s->index];const auto now=GetTickCount64();
    if(runtime){if(runtime!=s->runtime)return;s->last_present=now;}
    if(now-s->last_present>5000){Reason(g,"Present-stopped; restored");Finish(s,true);return;}
    if(TickScreenshotPerformance(*s,g,runtime,now))return;
    if(g.reason[0]){if(++s->index<s->count)StartGroup(*s);else {--s->index;Finish(s,false);}return;}
    if(!runtime)return;
    EnterCriticalSection(&g_chain_cs);
    const auto sample=nr_reset_observation::counters.layers[0].last;
    const auto owner=g_applied_nr_identity.instance_id;
    const bool same=g_config_generation.load()==g.generation&&SameRequested(g.requested,g_config)&&
        nr_post_settings::Read().percent==g.percent&&nr_post_settings::Read().rr==g.post_rr&&
        ObservedRoute()==nr_processing::Start::After;
    LeaveCriticalSection(&g_chain_cs);
    if(!same){Reason(g,"configuration-or-route-changed");return;}
    if(!width||!height||width>16384||height>16384||static_cast<unsigned long long>(width)*height>67108864){Reason(g,"display-extent-unsupported");return;}
    if(!g.shot_width){g.shot_width=width;g.shot_height=height;}
    if(width!=g.shot_width||height!=g.shot_height){Reason(g,"display-extent-changed");return;}
    if(g.shot_count==kShots){if(++s->index<s->count)StartGroup(*s);else {--s->index;Finish(s,false);}return;}
    auto& shot=g.shots[g.shot_count];
    if(shot.requested){
        if(g.requested.enabled&&owner!=shot.instance)Reason(g,"owner-changed-while-screenshot-pending");
        else if(!shot.received&&now-shot.tick>5000)Reason(g,"screenshot-callback-timeout");return;
    }
    if(now-s->window.started>20000){Reason(g,"warmup-or-adoption-timeout");return;}
    if(now-s->window.started<3000)return;
    if(g.shot_count&&now-g.shots[g.shot_count-1].tick<250)return;
    if(g.requested.enabled){
        if(sample.config_generation!=g.generation||sample.since_reset<64||!owner||
            g_nr_transfer_submitted_frames.load()<g.submit_start+64)return;
        if(g.requested.private_residual_sr&&nr_private_sr_status::adopted.load()<g.private_adopted_start+64)return;
        if(g.requested.temporal_backend&&!(nr_temporal_status::gpu_config.load()==g.generation&&
            nr_temporal_status::gpu_instance.load()==owner&&nr_temporal_status::gpu_frames.load()>=64&&
            nr_temporal_status::gpu_adopted.load()>0))return;
    }
    // Old cumulative adoption must not qualify a currently bypassed backend.
    // Probe after warmup, then require new adopted work before the first image.
    if(!g.shot_count&&(g.requested.private_residual_sr||g.requested.temporal_backend)){
        if(!g.adoption_probe){g.adoption_probe=true;g.probe_private=nr_private_sr_status::adopted.load();
            g.probe_gpu=nr_temporal_status::gpu_adopted.load();return;}
        if(g.requested.private_residual_sr&&nr_private_sr_status::adopted.load()<=g.probe_private)return;
        if(g.requested.temporal_backend&&nr_temporal_status::gpu_adopted.load()<=g.probe_gpu)return;
    }
    if(g.shot_count&&owner!=g.shots[0].instance){Reason(g,"owner-changed-during-shots");return;}
    if(g.shot_count&&g.requested.enabled&&sample.source_frame<=g.shots[g.shot_count-1].source)return;
    if(g.shot_count&&g.requested.private_residual_sr&&
        nr_private_sr_status::adopted.load()<=g.shots[g.shot_count-1].private_adopted)return;
    if(g.shot_count&&g.requested.temporal_backend&&
        (nr_temporal_status::gpu_frames.load()<=g.shots[g.shot_count-1].gpu_frames||
         nr_temporal_status::gpu_adopted.load()<=g.shots[g.shot_count-1].gpu_adopted))return;
    shot.requested=true;shot.config=g.generation;shot.instance=owner;shot.source=g.requested.enabled?sample.source_frame:0;
    shot.private_adopted=nr_private_sr_status::adopted.load();shot.gpu_frames=nr_temporal_status::gpu_frames.load();
    shot.gpu_adopted=nr_temporal_status::gpu_adopted.load();
    shot.width=width;shot.height=height;shot.tick=now;
    std::snprintf(s->pending_shot,sizeof(s->pending_shot),"DLSS5_SHOT_%llu_%u_%u",s->id,s->index,g.shot_count);
}
void TickScreenshots(reshade::api::effect_runtime* runtime){
    unsigned width=0,height=0;if(runtime&&session&&runtime==session->runtime)
        runtime->get_screenshot_width_and_height(&width,&height);
    TickScreenshotsAtExtent(runtime,width,height);
}
void Tick(reshade::api::effect_runtime* runtime){
    auto* s=session;if(!s)return;const auto now=GetTickCount64();auto& g=s->groups[s->index];
    if(s->program==Program::Screenshots){TickScreenshots(runtime);return;}
    if(runtime){if(runtime!=s->runtime)return;s->last_present=now;}
    if(now-s->last_present>5000){Reason(g,"Present-stopped; restored");Finish(s,true);return;}
    if(s->window.phase==Phase::Warmup||s->window.phase==Phase::Measure){
        if(!runtime)return;
        Observation o{};Row r{};
        const bool present_baseline=s->program==Program::Current&&s->original_effective==nr_processing::Start::Present;
        if(TryEnterCriticalSection(&g_chain_cs)){
            const auto& identity=g_applied_nr_identity;
            o.enabled=g_config.enabled;o.matches=g_config_generation.load()==g.generation&&SameRequested(g.requested,g_config)&&nr_post_settings::Read().percent==g.percent;
            const auto route=ObservedRoute();
            o.matches=o.matches&&route==s->original_effective;
            r.config=g_config_generation.load();r.route=static_cast<unsigned>(route);
            if(present_baseline){
                const auto p=ReadPresentLocked();
                const bool owner=p.config==g.generation&&p.generation!=0;
                if(owner&&(g.present_config_start!=p.config||g.present_generation_start!=p.generation)){
                    g.present_config_start=p.config;g.present_generation_start=p.generation;
                    g.present_completed_start=p.completed;g.eval_start=p.evaluated;g.submit_start=p.submitted;
                    g.warmup_eval_start=p.evaluated;g.warmup_submit_start=p.submitted;
                    if(s->window.phase==Phase::Warmup)s->window.Start(now);else o.matches=false;
                }
                r.instance=p.generation;r.source=p.completed_source;r.evaluations=p.evaluated;r.submitted=p.submitted;
                r.attempts=p.attempted;r.present_completed=p.completed;
                r.present_warmup=owner&&p.completed>=g.present_completed_start?p.completed-g.present_completed_start:0;
                o.since_reset=static_cast<unsigned>(std::min<unsigned long long>(r.present_warmup,UINT_MAX));
                if(s->window.phase==Phase::Measure&&!owner)o.matches=false;
            }else {
                const auto& sample=nr_reset_observation::counters.layers[0].last;
                o.since_reset=sample.config_generation==g.generation?sample.since_reset:0;
                r.source=sample.source_frame;r.instance=identity.instance_id;
                r.stream=sample.stream;r.epoch=sample.epoch;r.reset_reasons=sample.reasons;r.since_reset=sample.since_reset;r.source_to_canonical=sample.source_to_canonical;
            }
            if(s->window.phase==Phase::Measure&&o.enabled&&g.measured_instance!=r.instance)o.matches=false;
            LeaveCriticalSection(&g_chain_cs);
        }else {++dropped_samples;return;}
        r.group=s->index;r.tick=NrTimingCounter();
        if(!present_baseline){r.evaluations=g_nr_successful_evaluations.load();r.submitted=g_nr_transfer_submitted_frames.load();}
        r.temporal_submitted=nr_temporal_status::submitted.load();r.temporal_state=nr_temporal_status::state.load();
        r.gpu_config=nr_temporal_status::gpu_config.load();r.gpu_instance=nr_temporal_status::gpu_instance.load();
        r.gpu_tested=nr_temporal_status::gpu_tested.load();r.gpu_adopted=nr_temporal_status::gpu_adopted.load();
        if(!present_baseline){r.carriers=g_carrier_evaluations.load();r.attempts=g_nr_transfer_attempted_frames.load();}
        const bool gpu_owner=r.instance&&r.gpu_config==g.generation&&r.gpu_instance==r.instance;
        if(s->window.phase==Phase::Measure&&g.requested.temporal_backend&&!gpu_owner)o.matches=false;
        if(gpu_owner&&(g.gpu_config_start!=r.gpu_config||g.gpu_instance_start!=r.gpu_instance)){
            g.gpu_config_start=r.gpu_config;g.gpu_instance_start=r.gpu_instance;
            g.gpu_frames_start=g.gpu_tested_start=g.gpu_adopted_start=0;
            g.gpu_rejected_start=g.gpu_warmup_start=g.gpu_depth_rejected_start=0;
            if(s->window.phase==Phase::Warmup)s->window.Start(now);
            else o.matches=false;
        }
        o.adopted=present_baseline?CountersAdvanced(g.present_completed_start,r.present_completed):CountersAdvanced(g.submit_start,r.submitted);
        o.temporal=g.requested.temporal_backend==0||s->program==Program::Current||
            (gpu_owner&&CountersAdvanced(g.gpu_tested_start,r.gpu_tested)&&CountersAdvanced(g.gpu_adopted_start,r.gpu_adopted));
        if(g.requested.private_residual_sr)
            o.temporal=o.temporal&&CountersAdvanced(g.private_adopted_start,nr_private_sr_status::adopted.load());
        const auto previous=s->window.phase;s->window.Tick(now,o);
        if(previous==Phase::Warmup){
            g.warmup_evaluations=r.evaluations>=g.warmup_eval_start?r.evaluations-g.warmup_eval_start:0;
            g.warmup_submissions=r.submitted>=g.warmup_submit_start?r.submitted-g.warmup_submit_start:0;
            g.warmup_temporal_state=r.temporal_state;
            if(s->window.phase==Phase::Report&&s->program!=Program::Current&&g.requested.temporal_backend&&!o.temporal)
                Reason(g,gpu_owner?"temporal-GPU-history-not-adopted":"temporal-GPU-owner-identity-unavailable");
            if(s->window.phase==Phase::Report&&g.requested.private_residual_sr&&
               !CountersAdvanced(g.private_adopted_start,nr_private_sr_status::adopted.load()))
                Reason(g,"private-SR-not-adopted-during-warmup");
            if(s->window.phase==Phase::Report&&o.matches&&g.requested.temporal_backend&&
               (r.temporal_state==10||r.temporal_state==11||r.temporal_state==18||r.temporal_state==19))
                Reason(g,nr_temporal_status::Name(r.temporal_state));
        }
        if(previous==Phase::Warmup&&s->window.phase==Phase::Measure){
            g.eval_start=r.evaluations;g.submit_start=r.submitted;g.temporal_start=r.temporal_submitted;SnapshotNrTiming(g.cpu_before);
            if(present_baseline){g.present_measure_completed_start=r.present_completed;g.present_measure_source_start=r.source;}
            g.gpu_frames_start=nr_temporal_status::gpu_frames.load();g.gpu_tested_start=r.gpu_tested;g.gpu_adopted_start=r.gpu_adopted;
            g.gpu_rejected_start=nr_temporal_status::gpu_rejected.load();g.gpu_warmup_start=nr_temporal_status::gpu_warmup.load();
            g.gpu_depth_rejected_start=nr_temporal_status::gpu_depth_rejected.load();
            g.private_composed_start=nr_private_sr_status::composed.load();g.private_submitted_start=nr_private_sr_status::submitted.load();g.private_adopted_start=nr_private_sr_status::adopted.load();
            g.carrier_start=r.carriers;g.attempt_start=r.attempts;
            g.measured_instance=r.instance;
        }
        if(s->window.phase==Phase::Measure){if(s->rows<kRows)s->samples[s->rows++]=r;else s->overflow=true;}
        if(previous==Phase::Measure&&s->window.phase==Phase::Report){
            g.measured=true;g.eval_end=r.evaluations;g.submit_end=r.submitted;g.temporal_end=r.temporal_submitted;SnapshotNrTiming(g.cpu_after);
            g.gpu_config_end=r.gpu_config;g.gpu_instance_end=r.gpu_instance;
            g.gpu_frames_end=nr_temporal_status::gpu_frames.load();g.gpu_tested_end=r.gpu_tested;g.gpu_adopted_end=r.gpu_adopted;
            g.gpu_rejected_end=nr_temporal_status::gpu_rejected.load();g.gpu_warmup_end=nr_temporal_status::gpu_warmup.load();
            g.gpu_depth_rejected_end=nr_temporal_status::gpu_depth_rejected.load();
            g.private_composed_end=nr_private_sr_status::composed.load();g.private_submitted_end=nr_private_sr_status::submitted.load();g.private_adopted_end=nr_private_sr_status::adopted.load();
            g.private_timing_config=nr_private_sr_status::timing_config.load();g.private_timing_instance=nr_private_sr_status::timing_instance.load();g.private_timing_samples=nr_private_sr_status::timing_samples.load();
            g.private_gpu_timing_available=nr_private_sr_status::gpu_timing_available.load()&&g.private_timing_config==g.generation&&g.private_timing_instance==g.measured_instance&&g.private_timing_samples>0;
            if(g.private_gpu_timing_available){g.private_gpu_first_create_ms=nr_private_sr_status::gpu_first_create_ms.load();g.private_gpu_first_inference_ms=nr_private_sr_status::gpu_first_inference_ms.load();g.private_gpu_median_ms=nr_private_sr_status::gpu_median_ms.load();g.private_gpu_p95_ms=nr_private_sr_status::gpu_p95_ms.load();}
            g.dual_timing_config=nr_dual_runtime::time_config.load();g.dual_timing_samples=nr_dual_runtime::time_samples.load();
            if(g.dual_timing_config==g.generation&&g.dual_timing_samples)
                for(unsigned j=0;j<7;++j){g.dual_median_ms[j]=nr_dual_runtime::time_median[j].load();g.dual_p95_ms[j]=nr_dual_runtime::time_p95[j].load();}
            g.carrier_end=r.carriers;g.attempt_end=r.attempts;
            if(present_baseline){
                g.present_completed_end=r.present_completed;
                EnterCriticalSection(&g_chain_cs);const auto p=ReadPresentLocked();LeaveCriticalSection(&g_chain_cs);
                g.present_reset_each_frame=p.reset_each_frame;
                g.present_timing_valid=p.timing_valid&&p.generation==g.measured_instance&&p.timing_source>=g.present_measure_source_start;
                if(g.present_timing_valid)g.present_total_ms=p.total_ms;
            }
            g.adopted=present_baseline?CountersAdvanced(g.present_measure_completed_start,g.present_completed_end):CountersAdvanced(g.submit_start,g.submit_end);
            g.temporal_adopted=g.gpu_config_end==g.generation&&g.gpu_instance_end==g.measured_instance&&
                CountersAdvanced(g.gpu_tested_start,g.gpu_tested_end)&&CountersAdvanced(g.gpu_adopted_start,g.gpu_adopted_end);
            g.private_adopted=CountersAdvanced(g.private_adopted_start,g.private_adopted_end);
            if(g.requested.enabled&&!g.adopted)Reason(g,"NR-not-adopted-during-measurement");
            if(s->program!=Program::Current&&g.requested.temporal_backend&&!g.temporal_adopted)Reason(g,"temporal-GPU-history-not-adopted-during-measurement");
            if(g.requested.private_residual_sr&&!g.private_adopted)Reason(g,"private-SR-not-adopted-during-measurement");
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
        const auto now=GetTickCount64();
        if(now-s->last_memory>=1000){
            s->last_memory=now;
            const unsigned observed_before=g_config_generation.load();
            const auto memory=nr_process_memory::Read(s->adapter,s->adapter_known);
            const unsigned observed_after=g_config_generation.load();
            auto& g=s->groups[s->index];
            const unsigned phase=s->program==Program::Screenshots?
                (g.screenshot_phase==ScreenshotPhase::Capture?3u:static_cast<unsigned>(g.screenshot_phase)):
                static_cast<unsigned>(s->window.phase);
            if(phase<4){
                const bool valid=SUCCEEDED(memory.local_result);
                if(valid){if(!g.memory_success)g.memory_first_tick=now;++g.memory_success;g.memory_last_tick=now;}
                else ++g.memory_failure;
                const bool measuring=s->program==Program::Screenshots?
                    g.screenshot_phase==ScreenshotPhase::Measure:s->window.phase==Phase::Measure;
                if(valid&&measuring){
                    ++g.memory_measure_success;
                    if(!g.memory_known)g.usage_start=memory.local.CurrentUsage;
                    g.memory_known=true;g.usage_end=memory.local.CurrentUsage;g.budget=memory.local.Budget;
                    g.usage_sampled_peak=std::max(g.usage_sampled_peak,static_cast<unsigned long long>(memory.local.CurrentUsage));
                }
                if(s->memory_rows<kMemoryRows){auto& m=s->memory_samples[s->memory_rows++];
                    m.group=s->index;m.config=g.generation;m.phase=phase;m.tick=now;
                    m.observed_config_before=observed_before;m.observed_config_after=observed_after;
                    m.config_matches=observed_before==g.generation&&observed_after==g.generation;
                    m.elapsed=now>=g.memory_group_start?now-g.memory_group_start:0;
                    m.percent=g.percent;m.work_width=g.performance_work_width;m.work_height=g.performance_work_height;
                    m.gpu_samples=g.performance_count;m.nr_passes=g.requested.nr_passes;
                    m.result=static_cast<unsigned long>(memory.local_result);m.valid=valid;
                    if(valid){m.usage=memory.local.CurrentUsage;m.budget=memory.local.Budget;}
                }else s->memory_overflow=true;
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
        session=next.release();active.store(true);last_path[0]=0;last_reason[0]=0;last_restored=false;
        if(program==Program::Screenshots)last_screenshot_page[0]=0;
        StartGroup(*session);
        if(!QueueUserWorkItem(Watchdog,reinterpret_cast<void*>(static_cast<std::uintptr_t>(session->id)),WT_EXECUTEDEFAULT)){Reason(session->groups[0],"watchdog-start-failed");Finish(session,true);accepted=false;}
    }
    if(!accepted)std::snprintf(last_reason,sizeof(last_reason),"busy, preview active, or output directory unavailable");
    ReleaseSRWLockExclusive(&nr_f8::g_control);ReleaseSRWLockExclusive(&lock);return accepted;
}
void Present(reshade::api::effect_runtime* runtime){
    if(!Active())return;if(!TryAcquireSRWLockExclusive(&lock)){++dropped_samples;return;}
    char tag[96]{};
    if(TryAcquireSRWLockExclusive(&nr_f8::g_control)){
        Tick(runtime);
        if(session&&session->runtime==runtime&&session->program==Program::Screenshots){
            std::snprintf(tag,sizeof(tag),"%s",session->pending_shot);session->pending_shot[0]=0;
        }
        ReleaseSRWLockExclusive(&nr_f8::g_control);
    }else ++dropped_samples;
    ReleaseSRWLockExclusive(&lock);
    if(tag[0]&&runtime){runtime->open_overlay(false,reshade::api::input_source::mouse);runtime->save_screenshot(tag);}
}
void Cancel(){AcquireSRWLockExclusive(&lock);AcquireSRWLockExclusive(&nr_f8::g_control);if(session){Reason(session->groups[session->index],"cancelled");Finish(session,true);}ReleaseSRWLockExclusive(&nr_f8::g_control);ReleaseSRWLockExclusive(&lock);}
void RuntimeDestroyed(reshade::api::effect_runtime* runtime){AcquireSRWLockExclusive(&lock);AcquireSRWLockExclusive(&nr_f8::g_control);if(session&&session->runtime==runtime){Reason(session->groups[session->index],"runtime-destroyed");Finish(session,true);}ReleaseSRWLockExclusive(&nr_f8::g_control);ReleaseSRWLockExclusive(&lock);}
void Draw(reshade::api::effect_runtime* runtime,bool pending){
    const bool zh=UseChineseUi();static int program=0;static bool deep=true;
    ImGui::Separator();ImGui::TextUnformatted(zh?"集中验收":"Guided acceptance");
    const bool running=Active();ImGui::BeginDisabled(running||packaging.load()||pending||nr_f8::IsActive()||nr_core_feedback::Busy());
    ImGui::Combo(zh?"本次方案":"This comparison",&program,zh?"当前设置·性能基准\0颜色与比例\0原生/AMD防闪\0私有 SR 与空间恢复\0普通累积与低频重建\0":"Current settings / baseline\0Colour and scale\0Native / AMD temporal\0Private SR / spatial recovery\0Ordinary / low-frequency temporal\0");
    ImGui::TextWrapped("%s",zh?"临时设置不写入INI，不切模型。每组预热后记录20秒；结束/取消恢复。RR/HDR/FG在游戏内手动切换，每次切好后重新开始。":"Temporary settings never write INI or switch models. Each group warms up then records 20 seconds. Finish/cancel restores settings. Change game RR/HDR/FG manually before starting a new comparison.");
    ImGui::TextWrapped("%s",program==1?(zh?"保持当前路线与模型；关闭防闪，比较100/99/低分，再以原比例比较 AI 色彩范围 0/0.5/1。":"Keep route/model; temporal OFF; compare 100/99/low scale, then AI colour range 0/0.5/1 at the original scale."):program==2?(zh?"保持模型和比例；比较关闭、原生、AMD及双层以上后续层。只有实际 GPU 历史采用可验证才计入通过。":"Keep model/scale; compare OFF, Native, AMD and later-layer scope. GPU history use must be observable before a group can pass."):program==3?(zh?"同一模型和层数：低比例空间恢复、低比例私有 SR、高比例空间恢复。独立关闭外部累积，私有 SR 须实际采用。":"Same model and layer count: low-scale spatial recovery, low-scale private SR, high-scale spatial recovery. External temporal is disabled for this comparison; private SR must be adopted."):program==4?(zh?"同一模型与 AMDOF 运动源：普通累积对照低频重建。独立关闭私有 SR；历史采用计数不可用时跳过。":"Same model and AMDOF motion source: ordinary accumulation versus low-frequency reconstruction. Private SR is disabled; skip if GPU history-use counts are unavailable."):(zh?"保持全部设置，记录当前基准；用于同设置对比两个版本。":"Keep all settings; record a baseline for equal-settings version comparison."));
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
void DrawScreenshotUi(reshade::api::effect_runtime* runtime,bool pending){
    const bool zh=UseChineseUi();
    ImGui::Separator();ImGui::TextUnformatted(zh?"一键画面对比":"One-click picture comparison");
    ImGui::TextWrapped("%s",zh?"固定视角，9组各自先测GPU再截3张：预热180个有效源帧（最多20秒），采样600个有效帧（最多60秒）；不更换模型和层数，不写入设置。截图来自不同显示时刻。":"Hold a fixed view. Each of nine groups measures GPU work before three screenshots: 180 valid source frames (20s limit), then 600 valid samples (60s limit). Model, layers and saved settings stay intact.");
    const bool running=Active();
    ImGui::BeginDisabled(running||packaging.load()||pending||nr_f8::IsActive()||nr_core_feedback::Busy());
    if(ImGui::Button(zh?"开始自动截图对比":"Start screenshot comparison")){
        if(Start(runtime,Program::Screenshots,false))runtime->open_overlay(false,reshade::api::input_source::mouse);
    }
    ImGui::EndDisabled();
    AcquireSRWLockShared(&lock);unsigned index=0,count=0,shots=0,warmup=0,gpu_samples=0;const char* label="";
    ScreenshotPhase shot_phase=ScreenshotPhase::Capture;
    if(session&&session->program==Program::Screenshots){index=session->index+1;count=session->count;
        const auto& g=session->groups[session->index];shots=g.shot_count;label=g.step.name;
        shot_phase=g.screenshot_phase;warmup=g.performance_warmup;gpu_samples=g.performance_count;}
    wchar_t page[1024]{};wcscpy_s(page,last_screenshot_page);char reason[128]{};std::snprintf(reason,sizeof(reason),"%s",last_reason);
    const bool restored=last_restored;ReleaseSRWLockShared(&lock);
    if(count){
        if(shot_phase==ScreenshotPhase::Warmup)ImGui::Text(zh?"第%u/%u组 %s，GPU预热%u/180":"Group %u/%u %s, GPU warmup %u/180",index,count,label,warmup);
        else if(shot_phase==ScreenshotPhase::Measure)ImGui::Text(zh?"第%u/%u组 %s，GPU样本%u/600":"Group %u/%u %s, GPU samples %u/600",index,count,label,gpu_samples);
        else ImGui::Text(zh?"第%u/%u组 %s，截图%u/3":"Group %u/%u %s, screenshots %u/3",index,count,label,shots);
        if(ImGui::Button(zh?"停止并恢复":"Stop and restore"))Cancel();}
    if(reason[0])ImGui::TextWrapped("%s; restored=%s",reason,restored?"yes":"no");
    if(page[0]){char utf8[4096]{};WideToUtf8(page,utf8,sizeof(utf8));ImGui::TextWrapped("%s",utf8);
        if(ImGui::Button(zh?"打开对比页":"Open comparison page"))ShellExecuteW(nullptr,L"open",page,nullptr,nullptr,SW_SHOWNORMAL);
        ImGui::SameLine();if(ImGui::Button(zh?"打开结果目录":"Open result folder")){
            wchar_t directory[1024]{};wcscpy_s(directory,page);if(auto* slash=wcsrchr(directory,L'\\')){*slash=0;ShellExecuteW(nullptr,L"open",directory,nullptr,nullptr,SW_SHOWNORMAL);}
        }
    }
}
}
