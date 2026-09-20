// D7 exercises the production coordinator, stream writer and real ZIP with GPU/NGX seams.
void FaceShot(reshade::api::effect_runtime &r,bool expected=true) {
    const auto path=output_root+L"\\face_"+std::wstring(r.tag.begin(),r.tag.end())+L".png";
    Check(CopyFileW((output_root+L"\\..\\native-display.png").c_str(),path.c_str(),TRUE)!=FALSE,"face screenshot preserves full native fixture");
    char utf8[4096]={};WideToUtf8(path.c_str(),utf8,sizeof(utf8));Screenshot(&r,utf8);
    Check(g_session && g_session->groups[g_session->group].shot_received==expected,"face screenshot callback follows dimension contract");
}
void StartFace(reshade::api::effect_runtime &r,unsigned expected_width=0,unsigned expected_height=0) {
    Check(RequestFaceDiagnostic(.81f,.27f,expected_width,expected_height),"face request accepted");Present(&r);
    Check(g_session && g_session->group_count==9,"face production starts exactly nine groups");
}
void CompleteFaceGroup(reshade::api::effect_runtime &r,unsigned group,unsigned w=64,unsigned h=64) {
    Check(g_session && g_session->group==group && g_session->groups[group].roi_target==1 &&
        !g_session->groups[group].overview_target,"face owns one bounded ROI per group, no overview");
    test_clock+=3100;const auto shots=r.shots;Present(&r);
    Check(r.shots==shots,"elapsed time alone cannot request face screenshot");
    auto cold=Info(100+group,w,h);cold.identity.since_reset=63;FrameTicket ticket;
    Check(!ReserveFrame(cold,{},ticket),"face waits for at least 64 NR evaluations");
    DeliverFrame(100+group,w,h);
    Check(g_session->groups[group].frames[0].valid,"face GPU data delivered before screenshot");
    Present(&r);Check(r.shots==shots+1 && r.closes==r.shots,"face closes overlay before each display request");
    const auto generation=g_config_generation.load();Present(&r);
    Check(g_session->group==group && g_config_generation==generation,"face holds group configuration until callback");
    FaceShot(r);Present(&r);
}
void TestFaceDiagnostic() {
    using namespace nr_face_diagnostic;
    Check(AdmitDisplay(3840,2160)&&!AdmitDisplay(3840,2400)&&!AdmitDisplay(5120,2880)&&!AdmitDisplay(0,2160),"face admission uses existing 512 MiB archive and 64 MiB file caps");
    const auto roi=ResolveRoi(2228,1256,.81f,.27f);
    Check(roi.valid&&roi.x==1549&&roi.y==83&&roi.width==512&&roi.height==512,"off-center face ROI has exact source pixel bounds");
    const auto corner=ResolveRoi(100,80,1.f,0.f);
    Check(corner.valid&&!corner.x&&!corner.y&&corner.width==100&&corner.height==80,"small and corner ROI bounded without resampling");
    Check(!ValidCenter(NAN,.5f)&&!ValidCenter(.5f,INFINITY),"invalid normalized centers rejected");
    Reset();config.enabled=false;Check(!RequestFaceDiagnostic(),"face diagnostic cannot enable NR implicitly");
    config.enabled=true;Check(!RequestFaceDiagnostic(-.1f,.5f),"face invalid ROI rejected before request");
    Check(!RequestFaceDiagnostic(.5f,.5f,1920,0) && !RequestFaceDiagnostic(.5f,.5f,0,1080) &&
        g_status==Status::Idle&&!config_calls&&!g_config_hold,
        "partially supplied selection source extent rejected without mutation");
    Check(RequestFaceDiagnostic(),"pending face cancellation request");Cancel();
    Check(g_status==Status::Failed&&!config_calls&&SnapshotFaceDiagnosticProgress().restored,"pending face cancellation records unchanged settings");
    reshade::api::effect_runtime huge;huge.width=5120;huge.height=2880;
    Check(RequestFaceDiagnostic(),"display budget checked at Present");Present(&huge);
    Check(!g_session&&g_status==Status::Failed&&!config_calls&&!g_config_hold&&SnapshotFaceDiagnosticProgress().restored,"over-budget display rejects before transient mutation");
    Reset();config.enabled=true;Check(RequestFaceDiagnostic(),"pending face request");config.enabled=false;Present(&huge);
    Check(g_status==Status::Failed&&!config_calls&&!g_config_hold,"disabled during pending core cannot start transient groups");

    Reset();config.enabled=true;config.mode=2;rr=true;config.intensity=.73f;config.local_tone=1.44f;
    config.local_structure=.61f;config.skin_structure=1.77f;config.auto_mask=false;config.style=2;config.ui_correction=false;
    config.color_strength=.83f;config.light_control_mode=1;config.light_reflection=.09f;config.light_structure=1.97f;
    config.lighting_lock=.21f;config.work_mode=5;config.custom_work_scale=.75f;
    const auto original=config;disk_config=config;simulate_disk_poll=true;reshade::api::effect_runtime r;StartFace(r,2228,1256);
    for(unsigned group=0;group<9;++group) {
        ChainConfig expected=original;
        if(group>=2&&group<=7){expected.intensity=1.5f;expected.local_tone=expected.local_structure=1.f;expected.style=0;expected.ui_correction=true;}
        if(group==4||group==5)expected.local_tone=2.f;
        if(group==6||group==7)expected.local_structure=2.f;
        expected.auto_mask=(group%2)!=0||group==8;expected.skin_structure=0;
        expected.processing_start=nr_processing::FromLegacy(expected.mode);
        expected.processing_selection=config.processing_selection;
        Check(SameConfig(config,expected)&&SameConfig(g_session->groups[group].requested,expected),"nine-group table independently matches exact requested settings");
        Check(g_session->groups[group].expected_mode==1&&config.mode==2,"face follows live RR post route while preserving saved route");
        test_clock+=600;Check(SameConfig(SnapshotOverlayConfig().config,expected),"face held settings cannot be overwritten by overlay INI poll");
        CompleteFaceGroup(r,group,2228,1256);
    }
    Drain();Check(g_status==Status::Complete&&SameConfig(config,original)&&!g_config_hold&&!nr_post_settings::capture_calls,"nine-group face success restores every setting without post-route mutation");
    const auto progress=SnapshotFaceDiagnosticProgress();Check(progress.visible&&!progress.active&&progress.restored&&progress.group==8&&progress.roi_ready&&progress.screenshot_ready,"face progress retains completed restore outcome");
    Check(CopyFileW(g_last_zip,(output_root+L"\\face-diagnostic.zip").c_str(),TRUE)!=FALSE,"publish independent nine-group parser fixture");

    const unsigned first_extents[][2]={{1280,720},{1280,1080},{1920,720}};
    for(const auto &extent:first_extents) {
        Reset();config.enabled=true;config.mode=1;config.auto_mask=true;config.skin_structure=1.55f;
        const auto before=config;reshade::api::effect_runtime changed;StartFace(changed,1920,1080);
        test_clock+=3100;auto first=Info(190,extent[0],extent[1]);FrameTicket ticket;
        Check(!ReserveFrame(first,{},ticket) && g_session->face_abort && !g_session->face_geometry_set &&
            !g_session->groups[0].count && !g_session->payload_bytes && !changed.shots,
            "first face RAW rejects DRS or either source dimension changed since selection without pixels or screenshot");
        Present(&changed);Drain();
        const auto failed=SnapshotFaceDiagnosticProgress();
        Check(g_status==Status::Partial&&SameConfig(config,before)&&!g_config_hold&&!g_session&&
            failed.restored&&!failed.roi_ready&&!failed.screenshot_ready&&!changed.shots&&
            std::strstr(failed.reason,"source dimensions changed since region selection")!=nullptr,
            "first face source mismatch restores exact settings and retains explicit partial reason without display proof");
    }

    for(unsigned cancel_group=0;cancel_group<9;++cancel_group) {
        Reset();config.enabled=true;config.mode=1;config.intensity=.63f;config.skin_structure=1.55f;const auto before=config;
        reshade::api::effect_runtime c;StartFace(c);
        for(unsigned group=0;group<cancel_group;++group)CompleteFaceGroup(c,group);
        Cancel();Present(&c);Drain();
        Check(g_status==Status::Partial&&SameConfig(config,before)&&!g_config_hold&&SnapshotFaceDiagnosticProgress().restored,"cancellation restores exact settings at each of nine groups");
    }
    for(unsigned change=0;change<8;++change) {
        Reset();config.enabled=true;config.mode=1;const auto before=config;reshade::api::effect_runtime c;StartFace(c);
        CompleteFaceGroup(c,0);test_clock+=3100;auto n=Info(222,64,64);FrameTicket t;
        if(change==0)++n.source_width;else if(change==1)++n.model_width;else if(change==2)++n.source_base_x;
        else if(change==3)++n.destination_base_y;else if(change==4)++n.source_format;
        else if(change==5)++n.feature_generation;else if(change==6)n.codec_flags^=16;else n.has_pre_exposure=!n.has_pre_exposure;
        Check(!ReserveFrame(n,{},t)&&g_session->face_abort,"cross-group geometry or colour contract change rejects mixed face data");
        Present(&c);Drain();Check(g_status==Status::Partial&&SameConfig(config,before)&&!g_config_hold,"face geometry failure aborts and restores");
    }
    for(unsigned field=0;field<7;++field) {
        Reset();config.enabled=true;const auto before=config;reshade::api::effect_runtime c;StartFace(c);test_clock+=3100;
        auto n=Info(300,64,64);FrameTicket t;
        if(field==0)n.identity.tuning.intensity+=.1f;else if(field==1)n.identity.tuning.local_tone+=.1f;
        else if(field==2)n.identity.tuning.local_structure+=.1f;else if(field==3)n.identity.tuning.skin_structure=0;
        else if(field==4)n.identity.tuning.auto_mask=!n.identity.tuning.auto_mask;else if(field==5)++n.identity.tuning.style;
        else n.identity.tuning.ui_correction=!n.identity.tuning.ui_correction;
        Check(!ReserveFrame(n,{},t)&&g_session->face_abort,"each actual NR tuning field is checked against the group");
        Present(&c);Drain();Check(g_status==Status::Partial&&SameConfig(config,before),"actual tuning mismatch restores");
    }
    Reset();config.enabled=true;const auto before=config;reshade::api::effect_runtime c;StartFace(c);
    CompleteFaceGroup(c,0);
    test_clock+=3100;auto n=Info(400,64,64);n.identity.instance_id=999;FrameTicket t;
    Check(ReserveFrame(n,{},t),"new NR instance from intentional create-time tuning is allowed across groups");
    Present(&c);Check(c.shots==1,"reservation without delivered exact-fence GPU record cannot request screenshot");
    test_clock+=30001;AcquireSRWLockExclusive(&g_control);Tick(nullptr);ReleaseSRWLockExclusive(&g_control);Drain();
    Check(g_status==Status::Partial&&SameConfig(config,before)&&!g_config_hold,"face stopped-Present timeout restores without screenshot or fake proof");
    Reset();config.enabled=true;StartFace(c);test_clock+=3100;DeliverFrame(500,64,64,true);Present(&c);Drain();
    Check(g_status==Status::Partial&&!g_config_hold,"face readback failure aborts and restores without screenshot");
    Reset();config.enabled=true;reshade::api::effect_runtime d;StartFace(d);test_clock+=3100;DeliverFrame(600,64,64);Present(&d);d.width=1920;FaceShot(d,false);Present(&d);Drain();
    Check(g_status==Status::Partial&&!g_config_hold,"face display resize aborts and restores");
    Reset();config.enabled=true;reshade::api::effect_runtime gone;StartFace(gone);RuntimeDestroyed(&gone);Drain();
    Check(g_status==Status::Partial&&!g_config_hold&&!g_session,"runtime destruction immediately restores active face diagnostic");
}
