// Insert at the start of FeedbackAppendActivity. No wait under a feedback lock.
{
    nr_compat_present::Observation cp{};
    if(nr_compat_present_runtime::TrySnapshot(cp)) {
        FeedbackAppend(s,"compatPost.status=%s\ncompatPost.generation=%llu\ncompatPost.inputQuality=color-only-experimental\n"
            "compatPost.history=reset-every-frame\ncompatPost.source=%llu\ncompatPost.submittedSource=%llu\ncompatPost.completedSource=%llu\n"
            "compatPost.work=%ux%u\ncompatPost.passes=%u\ncompatPost.ownedBytes=%llu\ncompatPost.result=0x%08X\n",
            nr_compat_present::Name(cp.status),static_cast<unsigned long long>(cp.generation),
            static_cast<unsigned long long>(cp.source),static_cast<unsigned long long>(cp.submitted_source),static_cast<unsigned long long>(cp.completed_source),
            cp.work_width,cp.work_height,cp.passes,static_cast<unsigned long long>(cp.bytes),cp.result);
        FeedbackAppend(s,"compatPost.gpuTimingValid=%d\ncompatPost.gpuTimingSource=%llu\ncompatPost.gpuMs=%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n"
            "compatPost.gpuMsMeaning=capture,encode,NR1,NR2,NR3,NR4,compose,blit,total; own command list only\n",
            cp.timing_valid?1:0,static_cast<unsigned long long>(cp.timing_source),cp.capture_ms,cp.encode_ms,
            cp.nr_ms[0],cp.nr_ms[1],cp.nr_ms[2],cp.nr_ms[3],cp.compose_ms,cp.blit_ms,cp.total_ms);
    } else FeedbackAppend(s,"compatPost.status=snapshot-busy-no-old-data-reused\n");
}
