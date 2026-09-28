// Complete production controller and configuration functions. No ReShade/game
// is launched: the opaque runtime identity is never dereferenced by this test.
#include <fstream>
namespace acceptance_core_test {
using queue_test::Check;
void DrainExport(){
    const auto end=GetTickCount64()+5000;
    while(nr_acceptance::packaging.load()&&GetTickCount64()<end)Sleep(10);
    Check(!nr_acceptance::packaging.load(),"bounded export finishes");
    Check(nr_acceptance::last_restored,"actual configuration restored");
}
void Run(){
    InitializeCriticalSection(&g_chain_cs);
    wchar_t full[MAX_PATH]{};GetFullPathNameW(L"acceptance-user.ini",MAX_PATH,full,nullptr);wcscpy_s(g_ini_path,full);
    const std::string sentinel="[NRBeforeSR]\r\nEnabled=1\r\nOwner=unchanged\r\n";
    {std::ofstream out("acceptance-user.ini",std::ios::binary);out<<sentinel;}
    ChainConfig original=SnapshotConfig();original.enabled=true;original.nr_passes=2;
    original.processing_start=nr_processing::Start::After;original.mode=1;
    original.temporal_backend=0;original.region_preview=0;original.light_preview=0;
    original.face_preview=0;original.colour_neutral_probe=false;
    ApplyConfig(original,"controller fixture",false);original=SnapshotConfig();
    // Normal startup migrations precede acceptance. Freeze bytes only after
    // that real initialization; this controller must add no further writes.
    std::ifstream initial("acceptance-user.ini",std::ios::binary);
    const std::string before_acceptance((std::istreambuf_iterator<char>(initial)),{});initial.close();
    nr_processing_runtime::Reconcile();nr_post_settings::bits.store(75u|256u);
    auto* runtime=reinterpret_cast<reshade::api::effect_runtime*>(std::uintptr_t(0x1000));
    Check(nr_acceptance::Start(runtime,nr_acceptance_policy::Program::Colour,false),"actual start takes transient ownership");
    Check(nr_f8::g_config_hold.load()&&nr_post_settings::Read().percent==100,"actual first scale applied under hold");
    Check(!nr_acceptance::Start(runtime,nr_acceptance_policy::Program::Current,false),"second owner refused");
    Check(!nr_f8::RequestCore()&&!RequestStageCapture(),"normal captures cannot steal config owner");
    ChainConfig conflicting=original;conflicting.intensity+=0.1f;
    ApplyConfig(conflicting,"user during acceptance",true);
    Check(g_config.intensity==original.intensity,"external mutation held");
    AcquireSRWLockExclusive(&nr_acceptance::lock);AcquireSRWLockExclusive(&nr_f8::g_control);
    auto& s=*nr_acceptance::session;
    s.window.started=GetTickCount64()-3001;
    nr_reset_observation::counters.layers[0].last.config_generation=s.groups[0].generation;
    nr_reset_observation::counters.layers[0].last.since_reset=64;
    g_nr_successful_evaluations.fetch_add(64);
    nr_acceptance::Tick(runtime);
    Check(s.window.phase==nr_acceptance_policy::Phase::Warmup,"real controller rejects evaluations without submission");
    Check(s.groups[0].warmup_evaluations==64&&s.groups[0].warmup_submissions==0,"warmup activity separate from unstarted measurement");
    g_nr_transfer_submitted_frames.fetch_add(1);nr_acceptance::Tick(runtime);
    Check(s.window.phase==nr_acceptance_policy::Phase::Measure,"real adoption and warmup admit measurement");
    Check(s.groups[0].warmup_submissions==1,"warmup submission recorded even before measured interval");
    Check(!g_feedback_requested.load()&&StageCaptureStatus()==0,"no capture or report during measurement");
    // Exercise A->B->A through production request and normalization, not direct
    // config assignment. Only test time and upstream observation are synthetic.
    s.index=1;nr_acceptance::StartGroup(s);Check(nr_post_settings::Read().percent==99,"second scale applied");
    s.index=0;nr_acceptance::StartGroup(s);Check(nr_post_settings::Read().percent==100,"first scale reapplied");
    ReleaseSRWLockExclusive(&nr_f8::g_control);ReleaseSRWLockExclusive(&nr_acceptance::lock);
    nr_acceptance::Cancel();DrainExport();
    Check(!nr_f8::g_config_hold.load()&&!nr_acceptance::Active(),"cancel releases only transient owner");
    Check(nr_acceptance::SameRequested(original,SnapshotConfig())&&nr_post_settings::Read().percent==75,"original fields and separate post precision restored");
    Sleep(2);
    Check(nr_acceptance::Start(runtime,nr_acceptance_policy::Program::Current,false),"current baseline restarts");
    const auto history=g_history_generation.load();nr_acceptance::Cancel();DrainExport();
    Check(g_history_generation.load()==history,"unchanged baseline restore causes no gratuitous reset");
    // Exercise the real text publisher -> acceptance consumer seam. No GPU or
    // game evidence is fabricated: only report contents/config are test inputs.
    for(unsigned report_case=0;report_case<3;++report_case){
        Sleep(2);
        Check(nr_acceptance::Start(runtime,nr_acceptance_policy::Program::Current,false),"report pairing starts");
        AcquireSRWLockExclusive(&nr_acceptance::lock);AcquireSRWLockExclusive(&nr_f8::g_control);
        auto& run=*nr_acceptance::session;auto& group=run.groups[0];
        Check(nr_core_feedback::Request(),"actual text-only report requested");
        group.report_requested=true;group.stage_tick=GetTickCount64();
        auto pair=nr_core_feedback::SnapshotPair();
        std::snprintf(group.report_session,sizeof(group.report_session),"%s",pair.session);
        AcquireSRWLockExclusive(&g_feedback_lock);
        nr_core_feedback::g_pair.config=group.generation+(report_case==1?1:0);
        ReleaseSRWLockExclusive(&g_feedback_lock);
        g_feedback_requested=false; // simulated worker claim, no fake render callback
        nr_core_feedback::nr_feedback_report::Output report{};
        report.text=std::make_shared<const std::string>("fixture TXT");
        report.path=L"fixture-report.txt";report.complete=report_case!=2;
        nr_bundle::Result archive{};
        nr_core_feedback::Publish(archive,false,"fixture",nullptr,&report,false);
        pair=nr_core_feedback::SnapshotPair();
        Check(!pair.complete&&pair.text_complete==(report_case!=2),"text and image completion remain distinct");
        run.window.phase=nr_acceptance_policy::Phase::Report;
        nr_acceptance::Tick(runtime);
        ReleaseSRWLockExclusive(&nr_f8::g_control);ReleaseSRWLockExclusive(&nr_acceptance::lock);
        DrainExport();
        FILE* receipt=_wfopen(nr_acceptance::last_path,L"rb");char contents[8192]{};
        Check(receipt!=nullptr,"paired acceptance receipt exists");
        std::fread(contents,1,sizeof(contents)-1,receipt);std::fclose(receipt);
        Check((std::strstr(contents,"paired-report-unavailable")!=nullptr)==(report_case!=0),"complete text accepted; wrong config and partial text refused");
    }
    Sleep(2);
    Check(nr_acceptance::Start(runtime,nr_acceptance_policy::Program::Temporal,false),"temporal comparison starts without changing layer count");
    Check(g_config.nr_passes==original.nr_passes,"test does not increase NR workload");
    AcquireSRWLockExclusive(&nr_acceptance::lock);AcquireSRWLockExclusive(&nr_f8::g_control);
    nr_acceptance::session->last_present=GetTickCount64()-5001;nr_acceptance::Tick(nullptr);
    ReleaseSRWLockExclusive(&nr_f8::g_control);ReleaseSRWLockExclusive(&nr_acceptance::lock);DrainExport();
    Check(!nr_acceptance::Active()&&!nr_f8::g_config_hold.load(),"missing Present restores without calling runtime");
    Sleep(2);
    Check(nr_acceptance::Start(runtime,nr_acceptance_policy::Program::PrivateSr,false),"private SR comparison starts");
    Check(!g_config.private_residual_sr&&g_config.temporal_backend==0&&nr_post_settings::Read().percent==25,"low spatial baseline excludes external temporal");
    AcquireSRWLockExclusive(&nr_acceptance::lock);AcquireSRWLockExclusive(&nr_f8::g_control);
    auto& private_session=*nr_acceptance::session;
    private_session.index=1;nr_acceptance::StartGroup(private_session);
    Check(g_config.private_residual_sr&&g_config.temporal_backend==0&&nr_post_settings::Read().percent==25,"same low scale enables private SR only");
    private_session.index=2;nr_acceptance::StartGroup(private_session);
    Check(!g_config.private_residual_sr&&nr_post_settings::Read().percent==100,"high spatial baseline retains model and layer count");
    ReleaseSRWLockExclusive(&nr_f8::g_control);ReleaseSRWLockExclusive(&nr_acceptance::lock);
    nr_acceptance::Cancel();DrainExport();
    Check(nr_acceptance::SameRequested(original,SnapshotConfig())&&nr_post_settings::Read().percent==75,"private SR trial restores complete config and post precision");
    Sleep(2);
    Check(nr_acceptance::Start(runtime,nr_acceptance_policy::Program::Colour,false),"runtime destruction case starts");
    nr_acceptance::RuntimeDestroyed(runtime);DrainExport();
    g_last_carrier_feature.store(13);g_live_rr_handles.store(1);
    original.mode=2;original.processing_start=nr_processing::Start::Before;
    ApplyConfig(original,"RR observed fixture",false);nr_processing_runtime::Reconcile();
    const auto pre_scale=SnapshotConfig().custom_work_scale;
    Sleep(2);Check(nr_acceptance::Start(runtime,nr_acceptance_policy::Program::Colour,false),"observed RR starts despite saved pre preference");
    Check(nr_acceptance::session->original_effective==nr_processing::Start::After&&g_config.custom_work_scale==pre_scale&&nr_post_settings::Read().percent==100,"actual RR uses post precision without editing saved pre field");
    nr_acceptance::Cancel();DrainExport();g_last_carrier_feature.store(0);g_live_rr_handles.store(0);
    // A screenshot session must start from F6 OFF/Before without asking the
    // owner to enable NR first, and must restore every transient route field.
    ChainConfig off=SnapshotConfig();off.enabled=false;off.mode=0;
    off.processing_start=nr_processing::Start::Before;
    ApplyConfig(off,"screenshot fixture",false);off=SnapshotConfig();
    const auto original_route=nr_processing_runtime::session;
    const auto original_post=nr_post_settings::Read();
    Sleep(2);Check(nr_acceptance::Start(runtime,nr_acceptance_policy::Program::Screenshots,false),"NR-off Before screenshot session starts");
    Check(!g_config.enabled&&g_config.processing_start==nr_processing::Start::After&&nr_acceptance::session->count==9,"native baseline temporarily routes After with NR off");
    Check(!nr_acceptance::PerformanceActive(),"native no-NR baseline has no plugin GPU instrumentation");
    AcquireSRWLockExclusive(&nr_acceptance::lock);AcquireSRWLockExclusive(&nr_f8::g_control);
    auto& pictures=*nr_acceptance::session;
    pictures.index=1;nr_acceptance::StartGroup(pictures);
    Check(nr_acceptance::PerformanceActive(),"NR screenshot group enables session-only GPU timestamps");
    Check(g_config.enabled&&g_config.processing_start==nr_processing::Start::After&&nr_post_settings::Read().percent==100,"next group enables NR and full post precision");
    pictures.index=3;nr_acceptance::StartGroup(pictures);
    Check(g_config.private_residual_sr&&nr_post_settings::Read().percent==50&&g_config.nr_passes==off.nr_passes,"private-SR shot changes only route/precision/backend");
    pictures.index=8;nr_acceptance::StartGroup(pictures);
    Check(g_config.temporal_backend==2&&g_config.temporal_mode==1&&nr_post_settings::Read().percent==50,"AMDOF low-frequency shot uses same model/layers");
    auto& temporal_picture=pictures.groups[8];
    temporal_picture.screenshot_phase=nr_acceptance_policy::ScreenshotPhase::Capture;
    const auto saved_owner=g_applied_nr_identity.instance_id;
    g_applied_nr_identity.instance_id=321;
    nr_reset_observation::counters.layers[0].last.config_generation=temporal_picture.generation;
    nr_reset_observation::counters.layers[0].last.since_reset=64;
    g_nr_transfer_submitted_frames=temporal_picture.submit_start+64;
    nr_temporal_status::gpu_config=temporal_picture.generation;nr_temporal_status::gpu_instance=321;
    nr_temporal_status::gpu_frames=64;nr_temporal_status::gpu_adopted=100;
    pictures.window.started=GetTickCount64()-3001;
    nr_acceptance::TickScreenshotsAtExtent(runtime,1920,1080);
    Check(temporal_picture.adoption_probe&&!temporal_picture.shots[0].requested,"old adopted pixels only arm fresh-work probe");
    nr_temporal_status::gpu_frames.fetch_add(1);
    nr_acceptance::TickScreenshotsAtExtent(runtime,1920,1080);
    Check(!temporal_picture.shots[0].requested,"completed frames with no new adopted pixels cannot capture");
    nr_temporal_status::gpu_adopted.fetch_add(1);
    nr_acceptance::TickScreenshotsAtExtent(runtime,1920,1080);
    Check(temporal_picture.shots[0].requested,"fresh adopted work qualifies screenshot request");
    pictures.pending_shot[0]=0;g_applied_nr_identity.instance_id=saved_owner;
    pictures.index=2;nr_acceptance::StartGroup(pictures);
    auto& timed_picture=pictures.groups[2];
    nr_acceptance_gpu::read_index=nr_acceptance_gpu::write_index=0;
    nr_acceptance_gpu::Sample stale{};stale.config=timed_picture.generation-1;stale.ms=1;
    nr_acceptance_gpu::samples[nr_acceptance_gpu::write_index++]=stale;
    for(unsigned i=0;i<nr_acceptance_policy::kScreenshotWarmupSamples+
                    nr_acceptance_policy::kScreenshotGpuSamples;++i){
        nr_acceptance_gpu::Sample x{};x.config=timed_picture.generation;
        x.history=timed_picture.performance_history;x.since_reset=64;
        x.work_width=960;x.work_height=540;x.output_width=1920;x.output_height=1080;
        x.instance=500;x.frame=i+1;x.queue=700;x.fence=i+1;
        x.source={10,20,i+1};x.ms=2.0;
        nr_acceptance_gpu::samples[nr_acceptance_gpu::write_index++%nr_acceptance_gpu::kRing]=x;
    }
    Check(!nr_acceptance::TickScreenshotPerformance(pictures,timed_picture,runtime,GetTickCount64())&&
        timed_picture.performance_warmup==180&&timed_picture.performance_count==600&&
        timed_picture.screenshot_phase==nr_acceptance_policy::ScreenshotPhase::Capture,
        "one-click performance requires 180 owned warmup sources then 600 completed GPU samples");
    Check(!nr_acceptance::PerformanceActive(),"GPU instrumentation stops before image capture");
    pictures.index=0;nr_acceptance::StartGroup(pictures);
    auto& sg=pictures.groups[0];auto& sh=sg.shots[0];
    Check(!nr_acceptance::TickScreenshotPerformance(pictures,sg,runtime,GetTickCount64())&&
        std::strstr(sg.performance_reason,"not-applicable")!=nullptr,
        "NR-off baseline has no fabricated plugin GPU sample and reaches image phase");
    pictures.window.started=GetTickCount64()-3001;
    nr_acceptance::TickScreenshotsAtExtent(runtime,1920,1080);
    Check(sh.requested&&pictures.pending_shot[0],"actual screenshot branch schedules native baseline after warmup");
    char good[120]{};std::snprintf(good,sizeof(good),"DLSS5_SHOT_%llu_0_0.png",pictures.id);
    {std::ofstream image(good,std::ios::binary);image<<"test-image";}
    char absolute[MAX_PATH]{};GetFullPathNameA(good,MAX_PATH,absolute,nullptr);
    ReleaseSRWLockExclusive(&nr_f8::g_control);ReleaseSRWLockExclusive(&nr_acceptance::lock);
    nr_acceptance::ScreenshotReceipt(runtime,"C:\\wrong-token.png",1920,1080);
    Check(!sh.received&&!sg.reason[0],"wrong screenshot token ignored");
    nr_acceptance::ScreenshotReceipt(reinterpret_cast<reshade::api::effect_runtime*>(std::uintptr_t(0x2000)),absolute,1920,1080);
    Check(!sh.received,"wrong runtime callback ignored");
    nr_acceptance::ScreenshotReceipt(runtime,absolute,1280,720);
    Check(!sh.received&&sg.reason[0],"display resize rejects callback");
    sg.reason[0]=0;g_config_generation.fetch_add(1);
    nr_acceptance::ScreenshotReceipt(runtime,absolute,1920,1080);
    Check(!sh.received&&sg.reason[0],"configuration change rejects callback");
    g_config_generation.fetch_sub(1);
    sg.reason[0]=0;nr_acceptance::ScreenshotReceipt(runtime,absolute,1920,1080);
    Check(sh.received&&sg.shot_count==1,"exact callback and existing file accepted");
    // Starting an NR group with no real owner/submissions cannot request a shot.
    AcquireSRWLockExclusive(&nr_acceptance::lock);AcquireSRWLockExclusive(&nr_f8::g_control);
    pictures.index=1;nr_acceptance::StartGroup(pictures);pictures.groups[1].screenshot_phase=nr_acceptance_policy::ScreenshotPhase::Capture;
    pictures.window.started=GetTickCount64()-3001;
    nr_acceptance::TickScreenshotsAtExtent(runtime,1920,1080);
    Check(!pictures.groups[1].shots[0].requested,"zero NR adoption cannot schedule screenshot");
    pictures.groups[1].shots[0].requested=true;pictures.groups[1].shots[0].config=pictures.groups[1].generation;
    pictures.groups[1].shots[0].instance=g_applied_nr_identity.instance_id+1;
    pictures.groups[1].shots[0].width=1920;pictures.groups[1].shots[0].height=1080;
    char owner_tag[120]{};std::snprintf(owner_tag,sizeof(owner_tag),"DLSS5_SHOT_%llu_1_0.png",pictures.id);
    {std::ofstream image(owner_tag,std::ios::binary);image<<"test-image";}
    char owner_path[MAX_PATH]{};GetFullPathNameA(owner_tag,MAX_PATH,owner_path,nullptr);
    ReleaseSRWLockExclusive(&nr_f8::g_control);ReleaseSRWLockExclusive(&nr_acceptance::lock);
    nr_acceptance::ScreenshotReceipt(runtime,owner_path,1920,1080);
    Check(!pictures.groups[1].shots[0].received&&pictures.groups[1].reason[0],"wrong NR owner rejects callback");
    // A pending request without a callback is bounded; the watchdog path never
    // dereferences the opaque runtime pointer.
    pictures.groups[1].reason[0]=0;pictures.groups[1].shots[0].instance=g_applied_nr_identity.instance_id;
    pictures.groups[1].shots[0].tick=GetTickCount64()-5001;
    nr_acceptance::TickScreenshotsAtExtent(runtime,1920,1080);
    Check(std::strstr(pictures.groups[1].reason,"timeout")!=nullptr,"missing screenshot callback times out");
    nr_acceptance::Cancel();DrainExport();
    Check(!nr_acceptance::PerformanceActive(),"cancel disables diagnostic GPU instrumentation");
    Check(nr_acceptance::SameRequested(off,SnapshotConfig())&&nr_processing_runtime::session.Current()==original_route.Current()&&
        nr_post_settings::Read().percent==original_post.percent,"screenshot cancel restores NR-off Before and post precision");
    nr_acceptance::ScreenshotReceipt(runtime,absolute,1920,1080);
    Check(!nr_acceptance::Active(),"late screenshot cannot revive cancelled session");
    Sleep(2);Check(nr_acceptance::Start(runtime,nr_acceptance_policy::Program::Screenshots,false),"second screenshot session starts after cancel");
    AcquireSRWLockExclusive(&nr_acceptance::lock);AcquireSRWLockExclusive(&nr_f8::g_control);
    nr_acceptance::session->last_present=GetTickCount64()-5001;
    nr_acceptance::TickScreenshotsAtExtent(nullptr,0,0);
    ReleaseSRWLockExclusive(&nr_f8::g_control);ReleaseSRWLockExclusive(&nr_acceptance::lock);DrainExport();
    Check(!nr_acceptance::Active()&&nr_acceptance::SameRequested(off,SnapshotConfig()),"stopped Present restores screenshot session");
    std::ifstream in("acceptance-user.ini",std::ios::binary);std::string after((std::istreambuf_iterator<char>(in)),{});
    Check(after==before_acceptance,"user INI byte identical across all operations");
    Sleep(300); // let idle controller watchdogs observe their exact session id
    DeleteCriticalSection(&g_chain_cs);
    std::printf("actual acceptance controller: %u checks; no game/model or image-quality claim\n",queue_test::checks);
}
}
