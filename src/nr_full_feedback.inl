// Included inside FeedbackAppendActivity; do not block a feedback lock while
// waiting for the Core lock held by another render thread.
{
    nr_dual_runtime::FullSnapshot full{};
    if(!nr_dual_runtime::TrySnapshotFull(full)) {
        FeedbackAppend(s,"%s.fullNR.snapshot=busy\n",prefix);
    } else {
        FeedbackAppend(s,"%s.fullNR.scopeMatches=%d\n%s.fullNR.enabled=%d\n"
            "%s.fullNR.config=%u\n%s.fullNR.lastMode=%u\n%s.fullNR.requestedPasses=%u\n"
            "%s.fullNR.preparedAuxiliary=%u\n%s.fullNR.lastAdoptedPasses=%u\n"
            "%s.fullNR.recordedAdoptions=%llu\n%s.fullNR.rootInstance=%llu\n",
            prefix,full.scope_matches?1:0,prefix,full.enabled?1:0,prefix,full.generation,prefix,full.mode,
            prefix,full.requested,prefix,full.prepared,prefix,full.adopted,
            prefix,full.recorded_adoptions,prefix,full.root);
        for(unsigned i=0;i<nr_full::kMaxPasses;++i) {
            const auto& l=full.layers[i];
            FeedbackAppend(s,"%s.fullNR.layer%u=instance:%llu,evaluations:%llu,timingCurrent:%d,"
                "samples:%u,lastMs:%.6f,medianMs:%.6f,p95Ms:%.6f\n",prefix,i+1,l.instance,l.evaluations,
                l.timing_current?1:0,l.samples,l.last,l.median,l.p95);
        }
        FeedbackAppend(s,"%s.fullNR.note=recorded adoption is not final display proof; timestamps require exact completed fences; capture/warmup/partial chains excluded\n",prefix);
    }
}
