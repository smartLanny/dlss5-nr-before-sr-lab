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
#include "nr_source_present_feedback.inl"
    {
        const auto ds=g_nr_descriptors.SnapshotCounters();
        append_compat("descriptor.r3Policy=full-mirror;heap-membership-index;range-local-invalidation;no-enabled-gate\n"
            "descriptor.heapRetireSlotVisits=%llu\ndescriptor.unknownSlotVisits=%llu\n"
            "descriptor.copyBeginBatches=%llu\ndescriptor.copyFinishBatches=%llu\n"
            "descriptor.profileEnabled=%d\n",
            static_cast<unsigned long long>(ds.heap_retire_slot_visits),
            static_cast<unsigned long long>(ds.unknown_slot_visits),
            static_cast<unsigned long long>(ds.copy_begin_batches),
            static_cast<unsigned long long>(ds.copy_finish_batches),ds.profile_enabled?1:0);
        if(ds.profile_enabled) {
            append_compat("descriptor.copyBeginCpuNs=%llu\ndescriptor.copyFinishCpuNs=%llu\n"
                "descriptor.heapRetireCpuNs=%llu\ndescriptor.unknownCpuNs=%llu\n"
                "descriptor.profileMeaning=CPU-wall-including-metadata-lock-wait;native-call-excluded;not-GPU-or-FPS\n",
                static_cast<unsigned long long>(ds.copy_begin_cpu_ns),
                static_cast<unsigned long long>(ds.copy_finish_cpu_ns),
                static_cast<unsigned long long>(ds.heap_retire_cpu_ns),
                static_cast<unsigned long long>(ds.unknown_cpu_ns));
        } else append_compat("descriptor.profileMeaning=unavailable;NR_DESCRIPTOR_PROFILE-build-required\n");
    }

    {
        const auto ds=g_nr_descriptors.SnapshotCounters();
        append_compat("descriptor.gpuMirror=%d\ndescriptor.gpuShadowSkippedCalls=%llu\n"
            "descriptor.gpuShadowSkippedDescriptors=%llu\n"
            "descriptor.copyMetadata=batch-lock;native-call-outside-lock;full-mirror-default\n",
            ds.gpu_mirror?1:0,static_cast<unsigned long long>(ds.gpu_shadow_skipped_calls),
            static_cast<unsigned long long>(ds.gpu_shadow_skipped_descriptors));
    }

    append_compat("guideReceipt.demandPolicy=advisory;unknown-retries;any-compatible-consumer;no-permanent-rejection\n"
        "guideReceipt.selectionPolicy=exact-admissible-first;scaled-fallback-only;ambiguity-within-tier\n");

    {
        if(TryAcquireSRWLockShared(&nr_source_runtime::source_lock)) {
            const auto skipped=nr_source_runtime::present_demand_skips;
            ReleaseSRWLockShared(&nr_source_runtime::source_lock);
            append_compat("guideReceipt.demandSkipped=%llu\n",static_cast<unsigned long long>(skipped));
        }
        append_compat("%s","guideReceipt.mappingPolicy=auto-whole-viewport-inference-not-lineage-proof\n"
            "guideReceipt.guides=real-native-depth-and-motion;not-dummy\n");
        unsigned requested=0,active=0;bool has_active=false,low_requested=false;
        if(nr_compat_present_runtime::TrySnapshotExperimentModes(requested,active,has_active,low_requested))
            append_compat("guideReceipt.modelMotionPolicyRequested=%s\nguideReceipt.modelMotionPolicyActive=%s\n"
                "descriptor.lowOverheadRequested=%d\ndescriptor.lowOverheadActivation=next-process-start\n",
                requested==1?"forced-layer-raster-compensation":"conservative-D18-layer-units",
                !has_active?"no-active-context":active==1?"forced-layer-raster-compensation":"conservative-D18-layer-units",
                low_requested?1:0);
        else append_compat("%s","guideReceipt.modelMotionPolicySnapshot=busy\n");
    }

    nr_compat_present::Observation cp{};
    if(nr_compat_present_runtime::TrySnapshot(cp)) {
        append_compat("compatPost.status=%s\ncompatPost.generation=%llu\ncompatPost.inputQuality=%s\n"
            "compatPost.history=%s\ncompatPost.source=%llu\ncompatPost.submittedSource=%llu\ncompatPost.completedSource=%llu\n"
            "compatPost.work=%ux%u\ncompatPost.passes=%u\ncompatPost.ownedBytes=%llu\ncompatPost.result=0x%08X\n",
            nr_compat_present::Name(cp.status),static_cast<unsigned long long>(cp.generation),
            cp.color_only?"unavailable-no-native-guide-receipt":"owned-current-native-guides",
            cp.color_only?"not-started":cp.reset_each_frame?"reset-every-frame":"source-history",
            static_cast<unsigned long long>(cp.source),static_cast<unsigned long long>(cp.submitted_source),static_cast<unsigned long long>(cp.completed_source),
            cp.work_width,cp.work_height,cp.passes,static_cast<unsigned long long>(cp.bytes),cp.result);
        for(unsigned layer=0;layer<nr_full::kMaxPasses;++layer)
            append_compat("compatPost.layer%u.attempted=%llu\ncompatPost.layer%u.evaluated=%llu\ncompatPost.layer%u.adopted=%llu\n",
                layer+1,static_cast<unsigned long long>(cp.attempted[layer]),layer+1,static_cast<unsigned long long>(cp.evaluated[layer]),layer+1,static_cast<unsigned long long>(cp.adopted[layer]));
        append_compat("compatPost.gpuTimingValid=%d\ncompatPost.gpuTimingSource=%llu\ncompatPost.gpuMs=%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n"
            "compatPost.gpuMsMeaning=capture,encode,NR1,NR2,NR3,NR4,NR5,compose,blit,total; own command list only\n",
            cp.timing_valid?1:0,static_cast<unsigned long long>(cp.timing_source),cp.capture_ms,cp.encode_ms,
            cp.nr_ms[0],cp.nr_ms[1],cp.nr_ms[2],cp.nr_ms[3],cp.nr_ms[4],cp.compose_ms,cp.blit_ms,cp.total_ms);
    } else append_compat("%s","compatPost.status=snapshot-busy-no-old-data-reused\n");
}
