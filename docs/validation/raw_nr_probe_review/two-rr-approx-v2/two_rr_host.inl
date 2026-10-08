namespace raw_nr_probe {
void RunTwoRrApproxGpu(const std::string& mode,const wchar_t* bridge,const wchar_t* pin){
 const bool baseline=false,identity=false,opaque_control=false,no_nr=false;
 Check(mode=="two-rr-approx","only registered approximate two-RR history");VerifyRuntime(pin,!baseline);
 one_rr_variant=!baseline;terminal_observer=PrintApproxClosure;
 control="probe";phase_frames=phase_J;phase_input_root=std::string(PHASE_INPUT_ROOT)+"/J";
 std::printf("TWO_RR_APPROX_COHORT name=%s R=1280x720 O=1920x1080 RRquality=2 RRpreset=0 flags=75 NRquality=5 intensity=1.5 strength=1 Jschedule=1 warm=16 observe=16\n",mode.c_str());
 Check(std::filesystem::equivalent(std::filesystem::path(bridge),std::filesystem::current_path()/"nrchain_nvngx.dll"),"native and NR bridge is exact pinned adjacent file");
 ++device_entries;queue_test::Fixture f;Configure();GetCurrentDirectoryW(MAX_PATH,g_log_path);wcscat_s(g_log_path,L"\\raw-nr-core.log");
 auto library=LoadLibraryW(bridge);Check(library,"pinned native bootstrap bridge");auto bootstrap=reinterpret_cast<unsigned(*)(ID3D12Device*)>(GetProcAddress(library,"NRChain_Bootstrap"));Check(bootstrap,"actual bootstrap export");
 const auto initialized=bootstrap(f.device);std::printf("BOOTSTRAP result=%08x count=1\n",initialized);if(initialized!=kNgxSuccess)Fail("native-init",nullptr,initialized);
 auto* ngx=GetModuleHandleW(L"_nvngx.dll");Check(ngx,"actual original Core module");auto allocate=reinterpret_cast<nr_private_sr_core::Allocate>(GetProcAddress(ngx,"NVSDK_NGX_D3D12_AllocateParameters"));
 auto create=reinterpret_cast<nr_private_sr_core::Create>(GetProcAddress(ngx,"NVSDK_NGX_D3D12_CreateFeature"));auto release=reinterpret_cast<nr_private_sr_core::Release>(GetProcAddress(ngx,"NVSDK_NGX_D3D12_ReleaseFeature"));auto destroy=reinterpret_cast<nr_private_sr_core::Destroy>(GetProcAddress(ngx,"NVSDK_NGX_D3D12_DestroyParameters"));Check(allocate&&create&&release&&destroy,"actual original exports");
 PreCreateCapability precreate;precreate.Query(ngx,f.device);
 const auto hooks=MH_Initialize();Check(hooks==MH_OK||hooks==MH_ERROR_ALREADY_INITIALIZED,"real native hooks");Check(NrDescriptorConfigureStartup(false),"descriptor observer startup");descriptor_warp_test::InstallCommandObservation(f);TrackGraphicsQueueLocked(f.a);queue_test::List list(f);
 const auto coverage=EnsurePostBoundaryNativeHooks(list.native);Check((coverage&(nr_post_boundary::Legacy|nr_post_boundary::Operations))==(nr_post_boundary::Legacy|nr_post_boundary::Operations),"actual source operation observation");nr_source_runtime::SeedCoreRecordingDevice(list.native);list.Close();list.ResetWithNewAllocator(f);
 Check(EnsureCodec(f.device),"actual production codec initialization after native observation installation");
 descriptor_warp_test::Pipeline pipeline(f),alternate(f);auto* heap=descriptor_warp_test::Heap(f,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,true);auto* alternate_heap=descriptor_warp_test::Heap(f,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,true);
 Adapter envelope(f);envelope.list=list.native;envelope.alternate_pipeline=alternate.state;envelope.alternate_heap=alternate_heap;
 auto owner=std::make_shared<Inputs>();owner->Make(f,ow,oh);owner->UploadGuides(list.native);SubmitReset(f,list);const auto first=ReadPhase(0,"radiance-rgba16f.bin",size_t(rw)*rh*8);owner->colors[0].FillAndRecord(list.native,first);
 NgxParams* parent_params=nullptr;Check(allocate(&parent_params)==kNgxSuccess&&parent_params,"true native parent params");for(auto kv:{std::pair{"Width",rw},{"Height",rh},{"OutWidth",ow},{"OutHeight",oh},{"CreationNodeMask",1u},{"VisibilityNodeMask",1u}})SetUInt(parent_params,kv.first,kv.second);
 SetInt(parent_params,"PerfQualityValue",2);SetInt(parent_params,"RayReconstruction.Hint.Render.Preset.Quality",0);SetInt(parent_params,"DLSS.Feature.Create.Flags",75);SetInt(parent_params,"DLSS.Denoise.Mode",1);SetUInt(parent_params,"DLSS.Roughness.Mode",0);SetUInt(parent_params,"DLSS.Use.HW.Depth",1);SetInt(parent_params,"DLSS.Enable.Output.Subrects",0);
 for(auto kv:{std::pair{"Width",rw},{"Height",rh},{"OutWidth",ow},{"OutHeight",oh}}){unsigned v=0;Check(GetUInt(parent_params,kv.first,&v)&&v==kv.second,"actual parent R-to-O create extent getters");}
 for(auto kv:{std::pair{"PerfQualityValue",2},{"RayReconstruction.Hint.Render.Preset.Quality",0},{"DLSS.Feature.Create.Flags",75},{"DLSS.Denoise.Mode",1}}){int v=-1;Check(GetInt(parent_params,kv.first,&v)&&v==kv.second,"actual fixed native RR quality/preset/flags getters");std::printf("ONE_RR_CREATE_GET key=%s value=%d\n",kv.first,v);}
 Check(precreate.proven&&parent_create_attempts==0,"sole parent Create after exact original capability/optimal support");
 ScopedNgxLayer layer(NgxLayer::Core);g_original_core_create=create;NgxHandle* parent=nullptr;Flight parent_flight;
 EnterCriticalSection(&g_chain_cs);pipeline.Bind(list,heap);parent_flight.Pin(f,list,owner,0,"parent-create");active_rr_consumer=&parent_flight;GuideState(list.native,*owner);Check(envelope.EnvelopeApi().capture(&envelope,list.native),"actual parent Create state envelope");
 ++parent_create_attempts;const auto made=HookCoreCreate(list.native,13,parent_params,&parent);std::printf("PARENT_CREATE feature=13 result=%08x calls=1 quality=2 preset=0 RtoO=1\n",made);OpaqueResult(made,envelope,"parent-create");ObserveAfterOpaque(list.native,*owner,0,"parent-create");Check(parent,"real parent handle");LeaveCriticalSection(&g_chain_cs);parent_flight.Finish(f,list,"parent-create");active_rr_consumer=nullptr;
 SceneFactory factory;factory.Resolve(f,parent);factory.eval_params=factory.Allocate("one-RR-eval-params");
 ApproxResources approx;approx.Make(f,list.native);
 ApproxChild child;child.Resolve(f,parent);child.Query();child.Prepare();Flight child_consumer;
 EnterCriticalSection(&g_chain_cs);child_consumer.Pin(f,list,owner,0,"approx-child-create",approx.clean.resource,&approx,approx.composite.resource,approx.readbacks[2]);active_child_consumer=&child_consumer;
 GuideState(list.native,*owner);ApproxResources::Move(list.native,approx.clean.resource,approx.clean_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
 Check(envelope.EnvelopeApi().capture(&envelope,list.native),"private child Create current envelope and actual lease");OpaqueResult(child.Create(list.native),envelope,"approx-child-create");approx.ObserveClean(list.native);ObserveAfterOpaque(list.native,*owner,0,"approx-child-create");LeaveCriticalSection(&g_chain_cs);child_consumer.Finish(f,list,"approx-child-create");active_child_consumer=nullptr;

 // Native and signed NR loader/init domains remain distinct even on one device.
 NrRuntimeGate runtime_gate;if(!no_nr)runtime_gate.Initialize(f.device,factory);if(!EnsureCodec(f.device))Fail("codec");Capture capture;if(!baseline)capture.Make(f);RrOutput rr_output;rr_output.Make(f,*owner);Flight rr_consumer;
 NrContext* nr=nullptr;NrFlight flight;ControlSlotStates control_states;
 for(unsigned iteration=0;iteration<(opaque_control?2u:identity?1u:32u);++iteration){const unsigned frame=opaque_control?0u:iteration;bool continue_control=false;auto bytes=ReadPhase(frame,"radiance-rgba16f.bin",size_t(rw)*rh*8);owner->SelectFrame(frame);
  if(frame==0)Barrier(list.native,owner->colors[frame].resource,static_cast<D3D12_RESOURCE_STATES>(owner->completed_states.at(owner->colors[frame].resource)),D3D12_RESOURCE_STATE_COPY_DEST);
  owner->colors[frame].FillAndRecord(list.native,bytes);factory.Bind(owner,frame);factory.Guard("raw-NR-input");
  EnterCriticalSection(&g_chain_cs);if(baseline)pipeline.Bind(list,heap);GuideState(list.native,*owner,frame);const auto feature=FindFeature(parent);Check(feature.feature_id==13&&feature.generation==factory.parent_generation,"no forged Feature1 carrier");
  if(baseline){rr_consumer.Pin(f,list,owner,frame,"one-RR-baseline");active_rr_consumer=&rr_consumer;rr_output.Poison(list.native,*owner,frame);
   OneRrEvaluate(list,envelope,factory,owner,frame,rr_consumer,nullptr,nullptr);rr_output.Record(list.native,*owner,frame);
   LeaveCriticalSection(&g_chain_cs);rr_consumer.Finish(f,list,"one-RR-baseline");active_rr_consumer=nullptr;rr_output.Save(frame);continue;}

  child.BindClean(owner,frame,approx.clean.resource);child_consumer.Pin(f,list,owner,frame,"approx-child-eval",approx.clean.resource,&approx,approx.composite.resource,approx.readbacks[2]);active_child_consumer=&child_consumer;
  approx.PoisonClean(list.native);Check(envelope.EnvelopeApi().capture(&envelope,list.native),"private child Eval current envelope and actual lease");OpaqueResult(child.Evaluate(list.native,frame),envelope,"approx-child-evaluate");approx.ObserveClean(list.native);ObserveAfterOpaque(list.native,*owner,frame,"approx-child-evaluate");
  ApproxResources::Move(list.native,approx.clean.resource,approx.clean_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  const auto color=ReadColorContract(factory.eval_params);Check(color.has_pre_exposure&&color.has_exposure_scale&&!color.exposure_texture&&color.pre_exposure==.75f&&color.exposure_scale==2,"explicit fixed stored HDR exposure");NrGuideBinding guides{};
  Check(ResolveNrGuideBinding(factory.eval_params,feature,2,rw,rh,rw,rh,owner->colors[frame].resource,0,0,CodecColorMode::Automatic,guides),"actual production raw guide resolver");
  Check(guides.jitter.known&&guides.jitter.x==phase_frames[frame].jx&&guides.jitter.y==phase_frames[frame].jy&&guides.has_motion_scale_x&&guides.has_motion_scale_y&&guides.valid_motion_scale_x&&guides.valid_motion_scale_y&&!guides.signature.resample_motion&&!guides.signature.linear_depth,"strict raw admission before codec or opaque call");
  // Deliberately approximate: clean color phase is zero; guides still belong to raw jittered producer.
  guides.jitter.x=guides.jitter.y=guides.jitter.source_x=guides.jitter.source_y=0;guides.jitter_offset_x=guides.jitter_offset_y=0;
  Check(guides.jitter.known&&guides.depth==owner->depth.resource&&guides.motion==owner->motions[frame].resource&&child.child_evaluates==frame+1,"approximate clean NR contract and real child producer count");
  std::printf("APPROX_NR_GUIDES frame=%u colorPhase=0 jitter=0 rawDepth=1 rawMV=1 rawVisibilityApproximate=1 exactCleanGuideClaim=0 layers=1 N=R=1\n",frame);
  nr=FindOrCreateContext(parent,feature.generation,2,rw,rh,CodecColorMode::Automatic,guides.signature,false);Check(nr&&nr->feature_generation==feature.generation&&nr->sr_handle==parent,"real parent generation owns independent F18 context");++nr->frames_seen;
  Check(PrepareContextUseLocked(list.native,*nr),"actual NR context recording admission");NrColorSemanticContract semantic{};semantic.flags_known=feature.create_flags_known;semantic.exposure=color;
  if(opaque_control)control_states.BeforeFactory(*nr);
  auto* slot=FindOrCreateSlot(*nr,f.device,approx.clean.resource,false,nullptr,CodecColorMode::Automatic,feature.create_flags,&semantic);Check(slot&&!nr->failed,"actual typed slot factory");slot->active_render_width=rw;slot->active_render_height=rh;
  if(opaque_control)control_states.Bind(*nr,*slot);
  // Region-OFF Seal alone is a no-op: explicitly construct the immutable
  // production codec plan and retain the two genuine raw guides.
  nr_binding::Plan bindings{};bindings.primary_owner=slot;WriteCodecDescriptorSet(f.device,*slot,nullptr,&bindings.heaps[nr_binding::Consumer]);bindings.retained[0]=guides.depth;bindings.retained[1]=guides.motion;nr_binding::Refusal refusal{};
  auto* packet=AcquireBindingPacket(f.device,list.native,*nr,bindings,&refusal);if(!packet)Fail(nr_binding::RefusalName(refusal),nr);slot->compose_binding=packet;
  Check(RecordNrUseLocked(list.native,*nr,*slot),"actual main slot recording pin");auto* diagnostic=capture.Pin(f,list.native,*nr,*slot,guides);if(!identity)flight.clear.Prepare(f,*slot,*diagnostic);flight.Pin(list.native,*nr,*slot,*packet,*diagnostic,owner);
  // Exact raw lineage admission protects the raw/final RR contract only.
  auto admission=raw_nr_admission::Inputs(phase_frames[frame].sx,phase_frames[frame].sy,phase_frames[frame].jx,phase_frames[frame].jy);
  admission.has_mv_x=guides.has_motion_scale_x;admission.has_mv_y=guides.has_motion_scale_y;admission.has_j_x=guides.jitter.valid_x;admission.has_j_y=guides.jitter.valid_y;
  admission.mv_x=guides.signature.motion_scale_x;admission.mv_y=guides.signature.motion_scale_y;admission.hardware_depth=!guides.signature.linear_depth;
  // Replace cold planned dimensions with actual getters/GetDesc/real context.
  const auto actual_color_desc=ID3D12Resource_GetDesc(owner->colors[frame].resource);
  Check(GetUInt(factory.eval_params,"DLSS.Render.Subrect.Dimensions.Width",&admission.raw_w)&&GetUInt(factory.eval_params,"DLSS.Render.Subrect.Dimensions.Height",&admission.raw_h),"actual raw render extent getter");
  admission.work_w=nr->width;admission.work_h=nr->height;admission.physical_w=static_cast<unsigned>(actual_color_desc.Width);admission.physical_h=actual_color_desc.Height;admission.samples=actual_color_desc.SampleDesc.Count;
  Check(GetUInt(factory.eval_params,"DLSS.Input.Color.Subrect.Base.X",&admission.base_x)&&GetUInt(factory.eval_params,"DLSS.Input.Color.Subrect.Base.Y",&admission.base_y),"actual source subrect getters");
  ID3D12Resource *actual_color=nullptr,*actual_depth=nullptr,*actual_motion=nullptr;
  admission.same_producer=GetResource(factory.eval_params,"Color",&actual_color)&&GetResource(factory.eval_params,"Depth",&actual_depth)&&GetResource(factory.eval_params,"MotionVectors",&actual_motion)&&actual_color==owner->colors[frame].resource&&actual_depth==owner->depth.resource&&actual_motion==owner->motions[frame].resource&&guides.depth==actual_depth&&guides.motion==actual_motion;
  const auto actual_frame_depth=ReadPhase(frame,"depth-r32f.bin",size_t(rw)*rh*4),initial_uploaded_depth=ReadPhase(0,"depth-r32f.bin",size_t(rw)*rh*4);
  const auto actual_frame_motion=ReadPhase(frame,"motion-rg32f.bin",size_t(rw)*rh*8);
  admission.same_frame=admission.same_producer&&actual_frame_depth==initial_uploaded_depth&&Digest(actual_frame_motion.data(),actual_frame_motion.size())==owner->motion_hashes[frame];
  // Same-frame is this fixture's immutable upload lineage: current Color was
  // uploaded from ReadPhase(frame) immediately above, MV hash matches that
  // frame's uploaded owner, and actual current depth equals the shared static
  // plane depth. It is not inferred for arbitrary game resources.
  unsigned actual_hardware_depth=0;admission.hardware_depth=admission.hardware_depth&&GetUInt(parent_params,"DLSS.Use.HW.Depth",&actual_hardware_depth)&&actual_hardware_depth==1;
  Check(actual_color_desc.Format==DXGI_FORMAT_R16G16B16A16_FLOAT&&actual_color_desc.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&actual_color_desc.DepthOrArraySize==1&&actual_color_desc.MipLevels==1,"actual raw color resource format/shape");
  admission.pre_exposure=color.pre_exposure;admission.exposure_scale=color.exposure_scale;admission.stage=raw_nr_admission::Stage::Live;
  admission.feature_id=feature.feature_id;admission.expected_generation=factory.parent_generation;admission.actual_generation=feature.generation;
  admission.expected_domain=factory.exports.domain_epoch;admission.actual_domain=nr_private_sr_core::ParentInDomain(parent,feature.generation,factory.exports.domain_epoch,reinterpret_cast<void*>(factory.exports.create))?factory.exports.domain_epoch:0;
  admission.domain_alive=nr_private_sr_core::DomainAlive(factory.exports);admission.device_same=IsSameRrDepthDevice(factory.exports.domain_device,f.device);
  const auto admitted_native=SnapshotPostBoundaryList(list.native);admission.native_known=admitted_native.known;admission.expected_native=flight.native_token;admission.actual_native=admitted_native.token;
  admission.expected_reset=flight.reset;admission.actual_reset=admitted_native.reset;admission.expected_recording=flight.token;ReadLifecycleToken(list.native,admission.actual_recording);
  auto* admitted_record=FindLifecycleCommandListLocked(admission.actual_recording);admission.recording_fresh=admitted_record&&!admitted_record->detached&&admitted_record->submission_count==0;
  admission.lifecycle_certain=!g_lifecycle_tracking_uncertain.load();
  NrSlot* edited_slot=slot;const auto refusal_at_entry=raw_nr_admission::Enter(admission,[&]{
#include "control_branch.inl"
  Transition(list.native,slot->proxy,slot->proxy_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);DispatchCodec(list.native,*slot,g_codec.encode_v2,rw,rh,color,true,0,0,0,0,CodecColorMode::Automatic,rw,rh,rw,rh,1);auto roots=Capture::Roots(list.native,frame,"encode-v2");
  UavBarrier(list.native,slot->proxy);Transition(list.native,slot->proxy,slot->proxy_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);Transition(list.native,slot->neural,slot->neural_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  if(identity){
   // Explicit no-model control: a real GPU copy gives NeuralColor the exact
   // encoded proxy bytes. Actual production shader decode/combine still run.
   Transition(list.native,slot->proxy,slot->proxy_state,D3D12_RESOURCE_STATE_COPY_SOURCE);
   Transition(list.native,slot->neural,slot->neural_state,D3D12_RESOURCE_STATE_COPY_DEST);
   ID3D12GraphicsCommandList_CopyResource(list.native,slot->neural,slot->proxy);
   Transition(list.native,slot->proxy,slot->proxy_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
   Transition(list.native,slot->neural,slot->neural_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
   std::printf("CODEC_IDENTITY frame=0 F18Init=0 F18Create=0 F18Eval=0 neuralBytes=proxyBytes originalDecodeAndCombine=1\n");
  }else{
  if(!nr->params){ScopedSrwExclusive lock(&g_nr_api_lock);Check(g_nr_api.allocate(&nr->params)==kNgxSuccess&&nr->params,"actual F18 parameter allocation");}
  const bool first_nr=!nr->nr_handle;Check(BindNrParameters(*nr,*slot,guides,feature,rw,rh,color,first_nr),"actual production F18 binder");VerifyBinder(*nr,*slot,guides,frame);
  // Every opaque Create and Eval has its own captured current command state.
  if(first_nr){runtime_gate.Before("Create",frame,list.native,flight,factory,*nr);Check(envelope.EnvelopeApi().capture(&envelope,list.native),"before F18 Create envelope and prior lease");void* handle=nullptr;++nr_creates;
   const auto created=CreateSignedNrFeature(list.native,*nr,feature,&handle);std::printf("F18_CREATE frame=%u result=%08x handle=%p calls=%u\n",frame,created,handle,nr_creates);
   if(created!=kNgxSuccess||!handle)OpaqueFailure(envelope,*nr,"F18-Create-partial-or-error",created);nr->nr_handle=static_cast<NgxHandle*>(handle);Opaque(created,envelope,*nr,"F18-Create");ObserveSlot(list.native,*nr,*slot,"Create");}
  Transition(list.native,slot->neural,slot->neural_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  flight.clear.Record(f,list.native,*diagnostic,*slot);
  std::printf("NR_OUTPUT_SENTINEL frame=%u negative=-1234 wholePlane=1 encodedCaptureBeforeSaturate=1 nonvisibleCPUDescriptor=1\n",frame);
  runtime_gate.Before("Evaluate",frame,list.native,flight,factory,*nr);
  Check(envelope.EnvelopeApi().capture(&envelope,list.native),"before F18 Eval envelope and prior lease");NgxResult evaluated=0;{ScopedSrwExclusive lock(&g_nr_api_lock);ScopedPrivateNrCall private_call(PrivateNrOperation::Evaluate,nr->nr_handle);++nr_evals;evaluated=g_nr_api.bridge_evaluate(g_nr_api.signed_evaluate,list.native,nr->nr_handle,nr->params,nullptr);}
  std::printf("F18_EVAL frame=%u result=%08x calls=%u\n",frame,evaluated,nr_evals);Opaque(evaluated,envelope,*nr,"F18-Eval");ObserveAfterOpaque(list.native,*owner,frame,"F18-evaluate");ObserveSlot(list.native,*nr,*slot,"Evaluate");
  nr->guide_history.CommitOnEvaluate(guides.signature,true);++nr->evaluations;CompleteNrDiagnosticEvaluation(*nr,*slot);g_nr_successful_evaluations.fetch_add(1);
  UavBarrier(list.native,slot->neural);Transition(list.native,slot->neural,slot->neural_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  }
  // Save original function outputs even during the two production warm-up
  // frames. A warm-up edited result is explicit raw identity, never model proof.
  const bool combine=identity||(nr->successful_evaluations_since_reset>g_config.warmup_after_create&&!g_config.identity_use_original);
  if(combine){++probe_combines;Transition(list.native,slot->result,slot->result_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);DispatchCodec(list.native,*slot,g_codec.combine_v2,rw,rh,color,true,0,0,0,0,CodecColorMode::Automatic,rw,rh,rw,rh,slot->suppress_transfer_this_frame?0.f:1.f);roots=Capture::Roots(list.native,frame,"combine-v2");UavBarrier(list.native,slot->result);}
  else {Transition(list.native,slot->result,slot->result_state,D3D12_RESOURCE_STATE_COPY_DEST);ApproxResources::Move(list.native,approx.clean.resource,approx.clean_state,D3D12_RESOURCE_STATE_COPY_SOURCE);
   ID3D12GraphicsCommandList_CopyResource(list.native,slot->result,approx.clean.resource);ApproxResources::Move(list.native,approx.clean.resource,approx.clean_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);}
  Transition(list.native,slot->result,slot->result_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);capture.Record(list.native,*diagnostic,roots,*slot);
  std::printf("APPROX_CLEAN_NR_FRAME frame=%u observe=%u combined=%u suppress=%u warmupCount=%u expected_colorPhase=0 rawGuides=approximate parentEval=one-per-frame childEval=one-per-frame strength=1\n",frame,frame>=16,combine,slot->suppress_transfer_this_frame,nr->successful_evaluations_since_reset);
  });if(refusal_at_entry)Fail(refusal_at_entry,nr);if(continue_control)continue;
  ObserveAfterOpaque(list.native,*owner,frame,"diagnostic-recorded");
  auto* composition=approx.PinComposition(f,list.native,*nr,*edited_slot,owner,frame);active_composition_packet=composition;approx.Compose(list.native,*composition,*owner,frame,*edited_slot);
  rr_consumer.Pin(f,list,owner,frame,"two-RR-approx-final",approx.composite.resource,&approx);active_rr_consumer=&rr_consumer;
  Check(rr_consumer.nr_recording_token==flight.token,"NR producer and RR consumer use identical real recording");
  GuideState(list.native,*owner,frame); // actual-current raw guide states after opaque NR; never guessed
  rr_output.Poison(list.native,*owner,frame);OneRrEvaluate(list,envelope,factory,owner,frame,rr_consumer,approx.composite.resource,nr);
  {nr_source_runtime::Lock lock;unsigned state=0;Check(nr_source_runtime::StateOf(nr_source_runtime::RecordingOf(list.native),nr_source_runtime::ResourceOf(approx.composite.resource),state),"actual composite Color state after native RR");approx.composite_state=static_cast<D3D12_RESOURCE_STATES>(state);std::printf("ONE_RR_EDITED_STATE frame=%u state=%u actualObserver=1\n",frame,state);}
  rr_output.Record(list.native,*owner,frame);LeaveCriticalSection(&g_chain_cs);flight.Finish(f,list,frame,&rr_consumer,&child_consumer,composition);active_rr_consumer=nullptr;active_child_consumer=nullptr;active_composition_packet=nullptr;capture.Save(frame);approx.Save(frame);rr_output.Save(frame);
 }
 Check(child.child_creates==1&&child.child_evaluates==32&&!active_child_consumer&&!active_composition_packet,"separate child history complete and retired");
 Check(native_rr_evals==32&&parent_create_attempts==1&&nr_creates==(baseline?0u:1u)&&nr_evals==(baseline?0u:32u)&&!active_rr_consumer,"complete one-RR history and exact call counts");if(!baseline)Check(nr&&CanDestroyContextLocked(*nr)&&CanDestroyBindingsLocked(*nr),"F18 dependencies retire only after real RR consumption");Check(SUCCEEDED(ID3D12Device_GetDeviceRemovedReason(f.device)),"device healthy");
 if(!baseline){flight.clear.Drop();++context_destroy_calls;DestroyContext(*nr);if(nr->lifecycle_dependencies_quarantined)Fail("F18-release-unproven",nr);}
 child.ReleaseChild();approx.Drop();
 {ScopedSrwExclusive lock(&g_nr_api_lock);nr_private_sr_core::Scope tls;factory.Guard("parent-release");for(auto** p:{&factory.eval_params,&factory.capability})if(*p){Check(factory.exports.destroy(*p)==kNgxSuccess,"actual owner parameter destruction");*p=nullptr;}}
 ++parent_release_calls;Check(release(parent)==kNgxSuccess,"original parent Release after NR retirement and while native exports retained");nr_private_sr_core::ForgetParent(parent);ForgetFeature(parent);Check(destroy(parent_params)==kNgxSuccess,"parent params destroyed under retained original owner");
 precreate.Drop(destroy);FreeLibrary(factory.callback_owner);FreeLibrary(factory.capability_owner);nr_private_sr_core::ReleaseExports(factory.exports);
 DestroyBindings(context);if(!baseline)capture.Drop();rr_output.Drop();owner->Drop();ReleaseCom(heap);ReleaseCom(alternate_heap);if(!no_nr)runtime_gate.Drop();std::printf("PASS TWO_RR_APPROX_HOST mode=%s parentCreate=1 parentEval=32 F18Create=%u F18Eval=%u SceneCreate=1 SceneEval=32 rawGuides=approximate Feature1=0 R=1280x720 O=1920x1080 RGBfiniteFullWrite=32 qualityVerdict=unknown\n",mode.c_str(),nr_creates,nr_evals);
}
}
