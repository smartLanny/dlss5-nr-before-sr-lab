// UI decisions already made on the drawing thread. F8 reads this bounded value
// snapshot only; it must not advance or clear the live activity Window.
void FeedbackAppendUiActivity(FeedbackSession& session,const char* prefix) {
    const auto snapshot=std::unique_ptr<nr_ui_activity::TraceSnapshot>(
        new(std::nothrow) nr_ui_activity::TraceSnapshot);
    if(!snapshot){FeedbackAppend(session,"%s.uiActivity.available=0\n",prefix);return;}
    SnapshotNrUiActivity(*snapshot);
    const auto& s=*snapshot;
    const auto u=[](std::uint64_t value){return static_cast<unsigned long long>(value);};
    FeedbackAppend(session,"%s.uiActivity.schema=1\n%s.uiActivity.available=%d\n"
        "%s.uiActivity.snapshotTick=%llu\n%s.uiActivity.published=%llu\n"
        "%s.uiActivity.recentCount=%u\n%s.uiActivity.overwritten=%llu\n",
        prefix,prefix,s.published!=0,prefix,u(GetTickCount64()),prefix,u(s.published),
        prefix,s.recent_count,prefix,u(s.overwritten));
    const auto append=[&](const char* label,const nr_ui_activity::Publication& value) {
        if(!value.available)return;
        const auto& read=value.read;const auto& o=read.observation;
        const auto& b=o.before.counters;const auto& c=o.current;const auto& d=o.sample.delta;
        FeedbackAppend(session,"%s.uiActivity.%s=seq:%llu,thread:%llu,call:%llu,site:%u,atMs:%llu,"
            "readGap:%d/%llu,windowGap:%d/%llu,scopeBefore:%llu/%llu,readScope:%llu/%llu/%llu/%llu/%d,"
            "reset:%u,regressed:%u,observe:%d,kind:%u,initialized:%d,valid:%d/%d,sampleMs:%llu,"
            "counterBefore:%llu/%llu/%llu/%llu,counterNow:%llu/%llu/%llu/%llu,delta:%llu/%llu/%llu/%llu,"
            "reason:%u/%u,event:%d,display:%d/%d/%u/%u\n",
            prefix,label,u(value.sequence),u(read.thread_cookie),u(read.thread_call),static_cast<unsigned>(value.site),u(o.now),
            read.read_gap_known,u(read.read_gap_ms),o.gap_known,u(o.gap_ms),u(o.before.scope.config),u(o.before.scope.epoch),
            u(read.config_argument),u(read.epoch_entered),u(read.config_checked),u(read.epoch_checked),read.epoch_checked_known,
            o.reset_mask,o.regression_mask,o.observe_called,static_cast<unsigned>(o.kind),o.before.initialized,
            o.before.sample.valid,o.sample.valid,u(o.sample.elapsed_ms),
            u(b.carriers),u(b.nr),u(b.submitted),u(b.queue_waits),u(c.carriers),u(c.nr),u(c.submitted),u(c.queue_waits),
            u(d.carriers),u(d.nr),u(d.submitted),u(d.queue_waits),static_cast<unsigned>(o.live_reason),
            static_cast<unsigned>(o.sample.bypass_reason),value.transition,value.previous_known,value.previous_valid,
            static_cast<unsigned>(value.previous),static_cast<unsigned>(value.display));
        const char* origin=read.source==nr_ui_activity::ObservationSource::NativeWindow?"native-counter-window":
            read.source==nr_ui_activity::ObservationSource::PresentSnapshot?"Present-snapshot":
            read.source==nr_ui_activity::ObservationSource::FgRecentRecord?"FG-recent-record":"unknown";
        FeedbackAppend(session,"%s.uiActivity.%s.origin=%s\n",prefix,label,origin);
    };
    append("latest.top",s.latest[0]);append("latest.feedback",s.latest[1]);
    for(unsigned i=0;i<s.recent_count;++i){char label[24]{};std::snprintf(label,sizeof(label),"recent.%u",i);append(label,s.recent[i]);}
    FeedbackAppend(session,"%s.uiActivity.resetKey=1:first-window,2:clock-backwards,4:window-gap,8:config-scope,16:pipeline-epoch,32:counter-regressed,64:config-read-incoherent,128:epoch-read-incoherent; regression bits carrier/NR/submitted/queue=1/2/4/8\n"
        "%s.uiActivity.stateKey=0:unavailable,1:wrong-api,2:native,3:checking,4:intermittent,5:observed,6:queue-timeout,7:runtime,8:codec,9:RR,10:other-owner,11:conflict,12:waiting-SR,13:observer,14:command-state,15:bypassed,16:no-submission,17:compat-checking,18:FG-recorded,19:Present-submitted,20:compat-preparing,21:compat-bypassed\n"
        "%s.uiActivity.meaning=last real UI decisions, not a current screen or GPU proof; site 0=top 1=feedback; anonymous per-thread identity; gap tuple known/ms; readScope=configArgument/epochEntry/configCheck/epochCheck/epochCheckKnown; counterBefore=window baseline; valid=priorWindow/returnedSample; kind 1=reset 2=held 3=updated 4=incoherent 5=snapshot; origin names the actual observation; non-native origin has unavailable counter tuples and no Window Observe; display=previousKnown/previousValid/previous/current; F8 never samples the Window; missing latest means that site was not observed; recent history is bounded to 4 transitions\n",
        prefix,prefix,prefix);
}
