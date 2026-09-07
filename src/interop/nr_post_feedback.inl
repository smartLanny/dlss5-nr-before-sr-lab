// Included inside the existing F8/F7 FeedbackAppendActivity function.
// Keep one named TXT and the established bounded FeedbackAppend writer.
{
    const auto post = nr_post_settings::Read();
    const auto now = GetTickCount64();
    const auto attempted = nr_post_settings::last_rr_tick.load(std::memory_order_acquire);
    const bool recent = attempted != 0 && now >= attempted && now - attempted <= 2000;
    FeedbackAppend(s,
        "%s.post.request.rr=%d\n%s.post.request.percent=%u\n"
        "%s.post.request.revision=%llu\n%s.post.settingsReady=%d\n"
        "%s.post.saveFailed=%d\n%s.post.requestRejected=%d\n%s.post.rejectedPercent=%u\n",
        prefix, post.rr ? 1 : 0, prefix, post.percent,
        prefix, nr_post_settings::revision.load(std::memory_order_acquire),
        prefix, nr_post_settings::ready.load(std::memory_order_acquire) ? 1 : 0,
        prefix, nr_post_settings::persistence_failed.load(std::memory_order_acquire) ? 1 : 0,
        prefix, nr_post_settings::request_rejected.load(std::memory_order_acquire) ? 1 : 0,
        prefix, nr_post_settings::rejected_percent.load(std::memory_order_acquire));
    FeedbackAppend(s,
        "%s.post.rr.attempts=%llu\n%s.post.rr.nrRecordings=%llu\n"
        "%s.post.rr.nativeFailures=%llu\n%s.post.rr.lastStage=%d\n"
        "%s.post.rr.recentAttempt=%d\n%s.post.rr.attemptAgeMs=%lld\n"
        "%s.post.note=sampled atomic observations; request/recording is not GPU completion, presentation or game verification\n",
        prefix, nr_post_settings::rr_attempts.load(std::memory_order_acquire),
        prefix, nr_post_settings::rr_recorded.load(std::memory_order_acquire),
        prefix, nr_post_settings::rr_native_failures.load(std::memory_order_acquire),
        prefix, nr_post_settings::last_rr_stage.load(std::memory_order_acquire),
        prefix, recent ? 1 : 0,
        prefix, attempted != 0 && now >= attempted ? static_cast<long long>(now - attempted) : -1LL,
        prefix);
}
