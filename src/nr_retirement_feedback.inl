// F8 copies the complete published POD; its text projection is capped separately.
// Formatting never touches a live owner/fence. Omitted detail counts are explicit.
void FeedbackAppendRetirement(FeedbackSession& s,const char* prefix) {
    if(!s.retirement_scratch){FeedbackAppend(s,"%s.retirement.failed=cpu-snapshot-allocation; retirement evidence omitted\n",prefix);return;}
    SnapshotNrRetirementEvidence(*s.retirement_scratch);const auto& e=*s.retirement_scratch;const auto start=s.report_used;
    unsigned contexts=0,context_details=0,slots=0,bindings=0,recordings=0,private_flights=0,temporal_flights=0;
    const auto room=[&](std::size_t line_bound){return s.report_used-start+line_bound+512<=nr_feedback_limits::kRetirementTextBytes;};
    FeedbackAppend(s,"%s.retirement.sequence=%llu,tick:%llu,observations:%llu,suppressed:%llu\n"
        "%s.retirement.scope=frozen CPU-only original decisions; 1000ms stable refresh; never release permission; UINT64_MAX is unknown/removed, never completion; native lifetime/reset and context device generation unknown unless observed; globalCodecDevice is not context-device proof\n"
        "%s.retirement.counts=contexts:%u,slots:%u,bindings:%u,recordings:%u,privateFlights:%u,temporalFlights:%u,pending:%u/%u,releasing:%u,uncertain:%d\n"
        "%s.retirement.overflow=contexts:%u,slots:%u,bindings:%u,recordings:%u,private:%u,temporal:%u\n"
        "%s.retirement.maskLegend=1:create-quarantine,2:lifecycle-quarantine,4:uncertain,8:context-recording,16:pending-token,32:context-await-signal,64:context-fence,128:slot-recording,256:slot-await-signal,512:slot-fence,1024:binding-recording,2048:binding-await-signal,4096:binding-fence,8192:other-context-binding,16384:private-recording,32768:private-poison,65536:temporal-recording,131072:temporal-unsafe,262144:pending-full,524288:release-in-progress,1048576:post-ready,2097152:bindings\n"
        "%s.retirement.retireReasonLegend=0:unknown,1:feature-retired,2:aux-parent,3:handle-retired,4:feature-generation,5:parent-instance,6:extent-guide,7:color-contract,8:tuning,9:route-layer-compaction\n",
        prefix,e.sequence,e.tick,e.observations,e.suppressed,prefix,prefix,e.contexts,e.slots,e.bindings,e.recordings,e.private_flights,e.temporal_flights,e.pending_used,e.pending_capacity,e.releasing,e.tracking_uncertain?1:0,
        prefix,e.context_overflow,e.slot_overflow,e.binding_overflow,e.recording_overflow,e.private_overflow,e.temporal_overflow,prefix,prefix);
    // Every context's real first branch, complete masks and core identity fit
    // before optional detail. The full-width CPU gate covers all 32 entries.
    for(;contexts<e.contexts&&room(700);++contexts){const auto& c=e.context[contexts];
        FeedbackAppend(s,"%s.retirement.context.%u=id:%llu,parent:%llu,featureGen:%llu,config:%u,layer:%u,mode:%d,first:%s,blocked:0x%llX,evaluated:0x%llX,unknown:0x%llX,pendingDestroy:%d,refs:%u,pendingToken:%llu,await:%d,q:%llu,f:0x%llX,v:%llu,done:%llu,sampled:%d,postEvaluated:%d,postReady:%d,retireReason:%u\n",
            prefix,contexts,c.instance,c.parent,c.feature,c.configuration,c.layer,c.mode,nr_retirement::Name(c.first_block),c.blocked,c.evaluated,c.unknown,
            c.pending_destroy?1:0,c.lease.recording,c.pending_token,c.lease.awaiting_signal?1:0,c.lease.fence.queue,c.lease.fence.object,c.lease.fence.value,c.lease.fence.completed,c.lease.fence.sampled?1:0,c.post_evaluated?1:0,c.post_ready?1:0,c.retire_reason);
    }
    for(;recordings<e.recordings&&room(500);++recordings){const auto& r=e.recording[recordings];
        FeedbackAppend(s,"%s.retirement.recording.%u=token:%llu,list:0x%llX,expectedQ:%llu,submits:%llu,inFlight:%u,contextMask:0x%llX,direct:0x%llX,indirect:0x%llX,contextCount:%u,slotCount:%u,detached:%d,unprovenExecute:%d,nativeKnown:%d,nativeLifetime:%llu,nativeReset:%llu,device:0x%llX\n",
            prefix,recordings,r.token,r.address,r.expected_queue,r.submissions,r.in_flight,r.contexts,r.direct_contexts,r.indirect_contexts,r.context_count,r.slot_count,r.detached?1:0,r.unproven_execution?1:0,r.native_identity_known?1:0,r.native_lifetime,r.native_reset,r.device);
    }
    for(;bindings<e.bindings&&room(500);++bindings){const auto& v=e.binding[bindings];const auto& f=v.lease.fence;
        FeedbackAppend(s,"%s.retirement.binding.%u=id:%llu,featureGen:%llu,config:%u,i:%u,primary:%llu/%u/%d,secondary:%llu/%u/%d,refs:%u,await:%d,q:%llu,f:0x%llX,v:%llu,done:%llu,sampled:%d,tick:%llu\n",
            prefix,bindings,v.instance,v.feature,v.configuration,v.index,v.primary_instance,v.primary_slot,v.primary_known?1:0,v.secondary_instance,v.secondary_slot,v.secondary_known?1:0,
            v.lease.recording,v.lease.awaiting_signal?1:0,f.queue,f.object,f.value,f.completed,f.sampled?1:0,f.sample_tick);
    }
    for(;slots<e.slots&&room(350);++slots){const auto& v=e.slot[slots];const auto& f=v.lease.fence;
        FeedbackAppend(s,"%s.retirement.slot.%u=c:%u,i:%u,key:0x%llX,refs:%u,await:%d,q:%llu,f:0x%llX,v:%llu,done:%llu,sampled:%d,reused:%d,tick:%llu\n",
            prefix,slots,v.context,v.index,v.key,v.lease.recording,v.lease.awaiting_signal?1:0,f.queue,f.object,f.value,f.completed,f.sampled?1:0,f.reused?1:0,f.sample_tick);
    }
    const auto owners=[&](const nr_retirement::OwnerFlight* values,unsigned count,const char* kind,unsigned& written){
        for(;written<count&&room(450);++written){const auto& f=values[written];
            FeedbackAppend(s,"%s.retirement.%s.%u=id:%llu,i:%u,token:%llu,list:0x%llX,reset:%llu,device:0x%llX,q:%llu,f:0x%llX,v:%llu,done:%llu,sampled:%d,submitted:%d,poison:%d,unsafe:%d\n",
                prefix,kind,written,f.instance,f.index,f.token,f.list,f.reset,f.device,f.queue,f.fence.object,f.fence.value,f.fence.completed,f.fence.sampled?1:0,f.submitted?1:0,f.poisoned?1:0,f.unsafe?1:0);
        }
    };
    owners(e.private_flight,e.private_flights,"privateFlight",private_flights);owners(e.temporal_flight,e.temporal_flights,"temporalFlight",temporal_flights);
    for(;context_details<contexts&&room(450);++context_details){const auto& c=e.context[context_details];
        FeedbackAppend(s,"%s.retirement.context.%u.last=eval:%llu,frame:%llu,submitQ:%llu,submitV:%llu,completedQ:%llu,completedV:%llu,retiredTick:%llu,globalCodecDevice:0x%llX\n",
            prefix,context_details,c.last_eval,c.last_eval_frame,c.last_submit_queue,c.last_submit_value,c.last_completed_queue,c.last_completed_value,c.retired_tick,c.global_codec_device);
    }
    FeedbackAppend(s,"%s.retirement.textOmitted=contexts:%u,contextDetails:%u,slots:%u,bindings:%u,recordings:%u,private:%u,temporal:%u; full fixed POD retained, text capped at %u bytes\n",
        prefix,e.contexts-contexts,e.contexts-context_details,e.slots-slots,e.bindings-bindings,e.recordings-recordings,e.private_flights-private_flights,e.temporal_flights-temporal_flights,nr_feedback_limits::kRetirementTextBytes);
}
