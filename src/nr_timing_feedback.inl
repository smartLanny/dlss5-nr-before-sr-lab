// Included after FeedbackAppend. Snapshot storage stays off the worker stack.
#if NR_HAS_TIMING
void FeedbackAppendTiming(FeedbackSession &session, const char *prefix) {
    const auto snapshot=std::unique_ptr<nr_timing::Snapshot>(new(std::nothrow) nr_timing::Snapshot);
    if(!snapshot){FeedbackAppend(session,"%s.timing.available=0\n",prefix);return;}
    SnapshotNrTiming(*snapshot);
    const auto &s=*snapshot;
    const auto u=[](std::uint64_t value){return static_cast<unsigned long long>(value);};
    FeedbackAppend(session,"%s.timing.available=1\n%s.timing.schema=1\n"
        "%s.timing.epoch=%llu\n%s.timing.probeMode=%u\n%s.timing.probePhase=%llu\n"
        "%s.timing.waitMode=%u\n%s.timing.waitPhase=%llu\n%s.timing.qpcFrequency=%llu\n"
        "%s.timing.observed=%llu\n%s.timing.excludedOldEpoch=%llu\n"
        "%s.timing.counterOverflow=%d\n%s.timing.recentOverwritten=%llu\n"
        "%s.timing.note=CPU durations; nested stages overlap and must not be summed; neither GPU inference time nor frame rate nor input-to-display latency; compare unchanged settings within a timing epoch\n",
        prefix,prefix,prefix,u(s.epoch),prefix,s.probe.mode,prefix,u(s.probe.phase),
        prefix,s.wait.mode,prefix,u(s.wait.phase),prefix,u(s.qpc_frequency),
        prefix,u(s.observed),prefix,u(s.excluded_old_epoch),prefix,s.counter_overflow,
        prefix,u(s.recent_overwritten),prefix);
    const auto quantile=[&](const nr_timing::Stats &stats,unsigned percent)->std::string {
        if(!stats.measured || !s.qpc_frequency || s.counter_overflow)return "unknown";
        const auto target=(stats.measured/100)*percent+((stats.measured%100)*percent+99)/100;
        std::uint64_t sum=0;
        for(unsigned b=0;b<stats.histogram.size();++b){
            if(UINT64_MAX-sum<stats.histogram[b])return "unknown";
            sum+=stats.histogram[b];
            if(sum>=target)return b<nr_timing::kHistogramUpperUs.size()
                ?std::to_string(nr_timing::kHistogramUpperUs[b]):"above-100000";
        }
        return "unknown";
    };
    for(unsigned i=0;i<nr_timing::kStageCount;++i){
        const auto &stats=s.stages[i];const char *name=nr_timing::Name(static_cast<nr_timing::Stage>(i));
        FeedbackAppend(session,"%s.timing.%s=count:%llu,measured:%llu,totalTicks:%llu,maxTicks:%llu,p95UpperUs:%s,p99UpperUs:%s,histogram:",
            prefix,name,u(stats.count),u(stats.measured),u(stats.total_ticks),u(stats.maximum_ticks),
            quantile(stats,95).c_str(),quantile(stats,99).c_str());
        for(unsigned b=0;b<stats.histogram.size();++b)
            FeedbackAppend(session,"%s%llu",b?",":"",u(stats.histogram[b]));
        FeedbackAppend(session,"\n");
    }
    const unsigned count=std::min(s.recent_count,16u), first=s.recent_count-count;
    FeedbackAppend(session,"%s.timing.histogramUpperUs=100,250,500,1000,2000,4000,8000,16000,32000,50000,100000,unbounded\n"
        "%s.timing.percentileMeaning=histogram bucket upper bounds, not exact quantiles\n"
        "%s.timing.recentExported=%u\n%s.timing.recentAvailable=%u\n"
        "%s.timing.recentMeaning=latest bounded call observations; not a complete frame or submission trace; unknown identity fields are zero\n",
        prefix,prefix,prefix,count,prefix,s.recent_count,prefix);
    for(unsigned i=0;i<count;++i){
        const auto &r=s.recent[first+i];const auto &id=r.identity;const auto &d=r.detail;const auto &origin=r.readback_origin;
        FeedbackAppend(session,"%s.timing.recent.%u=stage:%s,sequence:%llu,epoch:%llu,"
            "probe:%u/%llu,wait:%u/%llu,frame:%llu,feature:%llu,config:%llu,mode:%u,role:%u,"
            "device:%llu,resource:%llu,resourceGeneration:%llu,flags:0x%X,resetReasons:0x%X,"
            "bypass:%u,nrResult:0x%X,nativeResult:0x%X,queue:%llu,fence:%llu/%llu/%llu,"
            "waitMethod:%u,waitFallback:%u,waitRegistration:0x%X,startedTicks:%llu,durationTicks:%llu,measured:%d,"
            "readbackOrigin:%llu/%llu/%llu/%llu/%llu/%llu/%u\n",
            prefix,i,nr_timing::Name(r.stage),u(r.sequence),u(id.epoch),
            id.probe.mode,u(id.probe.phase),id.wait.mode,u(id.wait.phase),u(id.frame),u(id.feature),u(id.config),id.mode,id.role,
            u(id.device),u(id.resource),u(id.resource_generation),d.flags,d.reset_reasons,
            d.bypass,static_cast<unsigned>(d.nr_result),static_cast<unsigned>(d.native_result),u(d.queue),
            u(d.fence_target),u(d.fence_before),u(d.fence_after),d.wait_method,d.wait_fallback,
            static_cast<unsigned>(d.wait_registration),u(r.started_ticks),u(r.duration_ticks),r.clock_known,
            u(origin.epoch),u(origin.probe.phase),u(origin.wait.phase),u(origin.frame),u(origin.feature),u(origin.config),origin.mode);
    }
    FeedbackAppend(session,"%s.timing.flagKey=1:carrier-known,2:resource-known,4:command-observed,8:NR-returned,16:native-returned,32:reset-bound,64:probe-dispatched,128:carrier-finished,256:readback-mapped,512:readback-origin-known,1024:exposure-reset-pending,2048:mixed-readback-origins\n"
        "%s.timing.resetReasonKey=1:first-create,2:game-reset,4:history-changed,8:color-contract,16:guide-changed,32:retry-pending,64:exposure-readback\n"
        "%s.timing.waitMethodKey=0:polling,1:event,2:polling-fallback,3:event-then-polling\n"
        "%s.timing.waitFallbackKey=0:none,1:event-create,2:event-reset,3:registration,4:earlier-registration,5:wait-failed\n"
        "%s.timing.readbackOriginKey=epoch/probePhase/waitPhase/frame/feature/config/mode; flags mark whether origin is known\n",
        prefix,prefix,prefix,prefix,prefix);
}
#endif
