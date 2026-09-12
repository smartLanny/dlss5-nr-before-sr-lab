// Last actual CanPresent decision, separate from cumulative NR counters.
// The source snapshot is nonblocking and contains lifetime cookies, not pointers.
{
    nr_processing::Snapshot route{};
    if(nr_processing_runtime::TrySnapshot(route))
        append_compat("processing.requested=%s\nprocessing.retained=%s\nprocessing.attempt=%s\nprocessing.fallbackReason=%s\n",
            nr_processing::Name(route.requested),nr_processing::Name(route.retained),nr_processing::Name(route.current),route.reason);
    else append_compat("%s","processing.snapshot=busy\n");
    nr_source_frame::PresentObservation decision{};
    if(nr_source_runtime::TryPresentObservation(decision)) {
        const auto number=[](auto value){return static_cast<unsigned long long>(value);};
        append_compat("compatSource.observation=%llu\ncompatSource.decision=%s\n"
            "compatSource.accepted=%d\ncompatSource.rejectMask=%u\ncompatSource.presentQueue=%llu\n"
            "compatSource.nativeProducer=%d\ncompatSource.promotionEligible=%d\ncompatSource.promotionBlocked=%d\n"
            "compatSource.claimStarted=%d\ncompatSource.claimReason=%llu\n"
            "compatSource.receiptDecision=%s\ncompatSource.receiptSource=%llu,%llu,%llu\n"
            "compatSource.receiptAge=%u\ncompatSource.receiptDeviceAge=%u\n"
            "compatSource.receiptProducerQueue=%llu\ncompatSource.receiptProducerFence=%llu\ncompatSource.receiptCrossQueueGpuWait=%d\n"
            "compatSource.receiptOutput=%ux%u\ncompatSource.receiptDepthSubrect=%u,%u,%u,%u\n"
            "compatSource.receiptMotionSubrect=%u,%u,%u,%u\ncompatSource.receiptMotionScale=%.9g,%.9g\n"
            "compatSource.receiptReset=%d\ncompatSource.receiptJitterPhase=source:%s,output:%s\n",
            number(decision.ordinal),nr_source_frame::PresentDecisionName(decision.decision),
            decision.accepted?1:0,decision.reject_mask,number(decision.queue_identity),
            decision.native_producer_on_device?1:0,decision.promotion_eligible?1:0,
            decision.promotion_blocked_by_native_producer?1:0,decision.claim_started?1:0,number(decision.claim_reason),
            nr_late_present::Name(static_cast<nr_late_present::Decision>(decision.receipt_decision)),
            number(decision.receipt_stream),number(decision.receipt_epoch),number(decision.receipt_sequence),
            decision.receipt_age,decision.receipt_device_age,number(decision.receipt_queue),number(decision.receipt_fence),
            decision.receipt_cross_queue_wait?1:0,decision.receipt_output_width,decision.receipt_output_height,
            decision.receipt_depth_x,decision.receipt_depth_y,decision.receipt_depth_width,decision.receipt_depth_height,
            decision.receipt_motion_x,decision.receipt_motion_y,decision.receipt_motion_width,decision.receipt_motion_height,
            double(decision.receipt_motion_scale_x),double(decision.receipt_motion_scale_y),decision.receipt_game_reset?1:0,
            decision.receipt_source_jitter_known?"known-native-input":"unknown",
            decision.receipt_output_jitter_known?"known":"unknown-not-relabeled");
        const auto label=[&](const char* phase,const nr_source_frame::Label& value) {
            append_compat("compatSource.%s.role=%u\ncompatSource.%s.source=%llu,%llu,%llu\n"
                "compatSource.%s.content=%llu\ncompatSource.%s.queue=%llu\ncompatSource.%s.submission=%llu\n"
                "compatSource.%s.submitted=%d\ncompatSource.%s.exactWrite=%d\ncompatSource.%s.tainted=%d\n"
                "compatSource.%s.processed=%d\ncompatSource.%s.fgConsumed=%d\n",
                phase,static_cast<unsigned>(value.role),phase,number(value.source.stream),number(value.source.epoch),number(value.source.sequence),
                phase,number(value.content),phase,number(value.queue),phase,number(value.submission),
                phase,value.submitted?1:0,phase,value.exact_write?1:0,phase,value.tainted?1:0,
                phase,value.processed?1:0,phase,value.fg_consumed?1:0);
        };
        label("lookup",decision.lookup);label("final",decision.label);
        append_compat("%s","compatSource.roleMeaning=0:unknown,1:application,2:generated,3:retired\n"
            "compatSource.rejectMaskMeaning=1:no-application,2:FG-consumed,4:unsubmitted,8:wrong-queue,16:native-producer,32:inline-processed,64:receipt\n"
            "compatSource.lineageScope=diagnostic except explicit generated/FG/inline-consumed labels\n"
            "compatSource.scope=last-CanPresent-decision; ordinal and producer fence are not source-frame identity\n");
    } else append_compat("%s","compatSource.decision=unobserved-or-snapshot-busy\n");
}
