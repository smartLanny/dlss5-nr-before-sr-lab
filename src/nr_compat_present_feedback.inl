// Insert at the start of FeedbackAppendActivity. No wait under a feedback lock.
{
    nr_fg_runtime::AppendFeedback(s,prefix);
    FeedbackAppend(s,"%s.compatCapture.presentStatus=%u\n%s.compatCapture.sharedPresentExportWorkers=%u\n"
        "%s.compatCapture.fgStatus=%u\n%s.compatCapture.fgExportWorkers=%u\n"
        "%s.compatCapture.nextRequestRoute=%s\n%s.compatCapture.statusKey=0:idle,1:pending,2:exported,3:failed; files require actual export completion; no native stage matrix or final-screen claim\n",
        prefix,nr_compat_present_runtime::CaptureStatus(false),prefix,nr_compat_present_runtime::CaptureWorkers(false),
        prefix,nr_compat_present_runtime::CaptureStatus(true),prefix,nr_compat_present_runtime::CaptureWorkers(true),
        prefix,nr_compat_present_runtime::CaptureUsesFg()?"FG":"Present",prefix);
    const auto append_compat=[&](const char* format,auto... values) {
        char text[4096]{};
        const int length=std::snprintf(text,sizeof(text),format,values...);
        if(length<0 || static_cast<std::size_t>(length)>=sizeof(text)) {
            FeedbackAppend(s,"%s.compatPost.snapshot=serialization-overflow\n",prefix);return;
        }
        const char* line=text;
        while(*line){const char* end=std::strchr(line,'\n');
            const int count=end?static_cast<int>(end-line):static_cast<int>(std::strlen(line));
            if(count)FeedbackAppend(s,"%s.%.*s\n",prefix,count,line);
            if(!end)break;line=end+1;
        }
    };
    nr_compat_present::Observation cp{};
    if(nr_compat_present_runtime::TrySnapshot(cp)) {
        append_compat("compatPost.status=%s\ncompatPost.generation=%llu\ncompatPost.inputQuality=%s\n"
            "compatPost.history=%s\ncompatPost.source=%llu\ncompatPost.submittedSource=%llu\ncompatPost.completedSource=%llu\n"
            "compatPost.work=%ux%u\ncompatPost.passes=%u\ncompatPost.ownedBytes=%llu\ncompatPost.result=0x%08X\n",
            nr_compat_present::Name(cp.status),static_cast<unsigned long long>(cp.generation),
            cp.color_only?"color-only":"owned-current-native-guides",cp.reset_each_frame?"reset-every-frame":"source-history",
            static_cast<unsigned long long>(cp.source),static_cast<unsigned long long>(cp.submitted_source),static_cast<unsigned long long>(cp.completed_source),
            cp.work_width,cp.work_height,cp.passes,static_cast<unsigned long long>(cp.bytes),cp.result);
        for(unsigned layer=0;layer<4;++layer)
            append_compat("compatPost.layer%u.attempted=%llu\ncompatPost.layer%u.evaluated=%llu\ncompatPost.layer%u.adopted=%llu\n",
                layer+1,static_cast<unsigned long long>(cp.attempted[layer]),layer+1,static_cast<unsigned long long>(cp.evaluated[layer]),layer+1,static_cast<unsigned long long>(cp.adopted[layer]));
        append_compat("compatPost.gpuTimingValid=%d\ncompatPost.gpuTimingSource=%llu\ncompatPost.gpuMs=%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n"
            "compatPost.gpuMsMeaning=capture,encode,NR1,NR2,NR3,NR4,compose,blit,total; own command list only\n",
            cp.timing_valid?1:0,static_cast<unsigned long long>(cp.timing_source),cp.capture_ms,cp.encode_ms,
            cp.nr_ms[0],cp.nr_ms[1],cp.nr_ms[2],cp.nr_ms[3],cp.compose_ms,cp.blit_ms,cp.total_ms);
    } else append_compat("%s","compatPost.status=snapshot-busy-no-old-data-reused\n");
}
