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
    Check(nr_acceptance::Start(runtime,nr_acceptance_policy::Program::Colour,false),"runtime destruction case starts");
    nr_acceptance::RuntimeDestroyed(runtime);DrainExport();
    g_last_carrier_feature.store(13);g_live_rr_handles.store(1);
    original.mode=2;original.processing_start=nr_processing::Start::Before;
    ApplyConfig(original,"RR observed fixture",false);nr_processing_runtime::Reconcile();
    const auto pre_scale=SnapshotConfig().custom_work_scale;
    Sleep(2);Check(nr_acceptance::Start(runtime,nr_acceptance_policy::Program::Colour,false),"observed RR starts despite saved pre preference");
    Check(nr_acceptance::session->original_effective==nr_processing::Start::After&&g_config.custom_work_scale==pre_scale&&nr_post_settings::Read().percent==100,"actual RR uses post precision without editing saved pre field");
    nr_acceptance::Cancel();DrainExport();g_last_carrier_feature.store(0);g_live_rr_handles.store(0);
    std::ifstream in("acceptance-user.ini",std::ios::binary);std::string after((std::istreambuf_iterator<char>(in)),{});
    Check(after==before_acceptance,"user INI byte identical across all operations");
    Sleep(300); // let idle controller watchdogs observe their exact session id
    DeleteCriticalSection(&g_chain_cs);
    std::printf("actual acceptance controller: %u checks; no game/model or image-quality claim\n",queue_test::checks);
}
}
