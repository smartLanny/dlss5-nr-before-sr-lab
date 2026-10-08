namespace raw_nr_probe {
void SaveSceneFrame(RrOutput& output,SceneFrame& scene,unsigned frame){
 if(scene_bench::Functional()){output.Save(frame);scene.Save(frame);}
 else if(scene_bench::Capture()&&frame>=scene_bench::warm){output.Save(frame);char name[64]{};std::snprintf(name,sizeof(name),"frame-%04u.rgba16f",frame-scene_bench::warm);std::filesystem::rename("native-rr-output-rgba16f-"+std::to_string(frame)+".bin",name);scene_bench::capture_files.emplace_back(name);}
 if(frame>=scene_bench::warm)Check(scene_bench::measurements.Snapshot("observe",frame),"actual Sample-host process memory observe sample");
}
void RunOneRrGpu(const std::string& mode,const wchar_t* bridge,const wchar_t* pin){
 const bool baseline=mode=="baseline",identity=false,opaque_control=false,no_nr=baseline;
 Check(baseline||mode=="variant","only two registered one-RR histories");VerifyRuntime(pin,!baseline);
 one_rr_variant=!baseline;terminal_observer=PrintOneRrClosure;
 control="probe";std::vector<PhaseFrame> phases(scene_bench::Total());for(unsigned i=0;i<phases.size();++i){phases[i]=phase_J[i%32];phases[i].reset=i==0;}phase_frames=phases.data();phase_input_root.clear();
 std::printf("ONE_RR_COHORT name=%s R=1280x720 O=1920x1080 RRquality=2 RRpreset=0 flags=75 NRquality=5 intensity=1.5 strength=1 Jschedule=1 warm=%u observe=%u scene=analytic-diffuse-room\n",mode.c_str(),scene_bench::warm,scene_bench::observed);
 Check(std::filesystem::equivalent(std::filesystem::path(bridge),std::filesystem::current_path()/"nrchain_nvngx.dll"),"native and NR bridge is exact pinned adjacent file");
 ++device_entries;queue_test::Fixture f;Configure();Check(scene_bench::measurements.Initialize(f.device,f.a,f.adapter,!scene_bench::Functional()),"same existing queue/process adapter instrumentation");Check(scene_bench::measurements.Snapshot("before_model_create"),"process memory before actual models");GetCurrentDirectoryW(MAX_PATH,g_log_path);wcscat_s(g_log_path,L"\\raw-nr-core.log");
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
 auto owner=std::make_shared<Inputs>();owner->Make(f,ow,oh);SceneFrame scene;scene.Make(f,*owner);scene.Record(list.native,*owner,0);
 NgxParams* parent_params=nullptr;Check(allocate(&parent_params)==kNgxSuccess&&parent_params,"true native parent params");for(auto kv:{std::pair{"Width",rw},{"Height",rh},{"OutWidth",ow},{"OutHeight",oh},{"CreationNodeMask",1u},{"VisibilityNodeMask",1u}})SetUInt(parent_params,kv.first,kv.second);
 SetInt(parent_params,"PerfQualityValue",2);SetInt(parent_params,"RayReconstruction.Hint.Render.Preset.Quality",0);SetInt(parent_params,"DLSS.Feature.Create.Flags",75);SetInt(parent_params,"DLSS.Denoise.Mode",1);SetUInt(parent_params,"DLSS.Roughness.Mode",0);SetUInt(parent_params,"DLSS.Use.HW.Depth",1);SetInt(parent_params,"DLSS.Enable.Output.Subrects",0);
 for(auto kv:{std::pair{"Width",rw},{"Height",rh},{"OutWidth",ow},{"OutHeight",oh}}){unsigned v=0;Check(GetUInt(parent_params,kv.first,&v)&&v==kv.second,"actual parent R-to-O create extent getters");}
 for(auto kv:{std::pair{"PerfQualityValue",2},{"RayReconstruction.Hint.Render.Preset.Quality",0},{"DLSS.Feature.Create.Flags",75},{"DLSS.Denoise.Mode",1}}){int v=-1;Check(GetInt(parent_params,kv.first,&v)&&v==kv.second,"actual fixed native RR quality/preset/flags getters");std::printf("ONE_RR_CREATE_GET key=%s value=%d\n",kv.first,v);}
 Check(precreate.proven&&parent_create_attempts==0,"sole parent Create after exact original capability/optimal support");
 ScopedNgxLayer layer(NgxLayer::Core);g_original_core_create=create;NgxHandle* parent=nullptr;Flight parent_flight;
 EnterCriticalSection(&g_chain_cs);pipeline.Bind(list,heap);parent_flight.Pin(f,list,owner,0,"parent-create");active_rr_consumer=&parent_flight;GuideState(list.native,*owner);Check(envelope.EnvelopeApi().capture(&envelope,list.native),"actual parent Create state envelope");
 ++parent_create_attempts;const auto made=HookCoreCreate(list.native,13,parent_params,&parent);std::printf("PARENT_CREATE feature=13 result=%08x calls=1 quality=2 preset=0 RtoO=1\n",made);OpaqueResult(made,envelope,"parent-create");ObserveAfterOpaque(list.native,*owner,0,"parent-create");Check(parent,"real parent handle");LeaveCriticalSection(&g_chain_cs);parent_flight.Finish(f,list,"parent-create");active_rr_consumer=nullptr;
 SceneFactory factory;factory.Resolve(f,parent);factory.eval_params=factory.Allocate("one-RR-eval-params");
 // Native and signed NR loader/init domains remain distinct even on one device.
 NrRuntimeGate runtime_gate;runtime_gate.expected_evals=scene_bench::Total();if(!no_nr)runtime_gate.Initialize(f.device,factory);if(!EnsureCodec(f.device))Fail("codec");Capture capture;if(!baseline&&scene_bench::Functional())capture.Make(f);RrOutput rr_output;if(!scene_bench::Perf())rr_output.Make(f,*owner);Flight rr_consumer;
 NrContext* nr=nullptr;NrFlight flight;ControlSlotStates control_states;
 for(unsigned frame=0;frame<scene_bench::Total();++frame){const unsigned iteration=frame;bool continue_control=false;
  if(!scene_bench::Functional()){Check(scene_bench::measurements.Begin(list.native,frame),"same queue timestamp before actual scene producer");scene_bench::active_measurement=&scene_bench::measurements;}
  scene.Record(list.native,*owner,frame);factory.Bind(owner,frame);factory.Guard("raw-NR-input");
  Check(scene.Matches(list.native,*owner,frame),"both native histories use the actual same-frame producer recording");
  EnterCriticalSection(&g_chain_cs);if(baseline)pipeline.Bind(list,heap);GuideState(list.native,*owner,frame);const auto feature=FindFeature(parent);Check(feature.feature_id==13&&feature.generation==factory.parent_generation,"no forged Feature1 carrier");
  if(baseline){rr_consumer.Pin(f,list,owner,frame,"one-RR-baseline");active_rr_consumer=&rr_consumer;if(scene_bench::Functional())rr_output.Poison(list.native,*owner,frame);
   OneRrEvaluate(list,envelope,factory,owner,frame,rr_consumer,nullptr,nullptr);
   Check(scene_bench::measurements.End(list.native),"render timestamp ends after actual native RR before any image copy");if(scene_bench::Functional()||(scene_bench::Capture()&&frame>=scene_bench::warm))rr_output.Record(list.native,*owner,frame);
   LeaveCriticalSection(&g_chain_cs);rr_consumer.Finish(f,list,"one-RR-baseline");active_rr_consumer=nullptr;SaveSceneFrame(rr_output,scene,frame);continue;}

  const auto color=ReadColorContract(factory.eval_params);Check(color.has_pre_exposure&&color.has_exposure_scale&&!color.exposure_texture&&color.pre_exposure==.75f&&color.exposure_scale==2,"explicit fixed stored HDR exposure");NrGuideBinding guides{};
  Check(ResolveNrGuideBinding(factory.eval_params,feature,2,rw,rh,rw,rh,owner->colors[0].resource,0,0,CodecColorMode::Automatic,guides),"actual production raw guide resolver");
  Check(guides.jitter.known&&guides.jitter.x==phase_frames[frame].jx&&guides.jitter.y==phase_frames[frame].jy&&guides.has_motion_scale_x&&guides.has_motion_scale_y&&guides.valid_motion_scale_x&&guides.valid_motion_scale_y&&!guides.signature.resample_motion&&!guides.signature.linear_depth,"strict raw admission before codec or opaque call");
  nr=FindOrCreateContext(parent,feature.generation,2,rw,rh,CodecColorMode::Automatic,guides.signature,false);Check(nr&&nr->feature_generation==feature.generation&&nr->sr_handle==parent,"real parent generation owns independent F18 context");++nr->frames_seen;
  Check(PrepareContextUseLocked(list.native,*nr),"actual NR context recording admission");NrColorSemanticContract semantic{};semantic.flags_known=feature.create_flags_known;semantic.exposure=color;
  if(opaque_control)control_states.BeforeFactory(*nr);
  auto* slot=FindOrCreateSlot(*nr,f.device,owner->colors[0].resource,false,nullptr,CodecColorMode::Automatic,feature.create_flags,&semantic);Check(slot&&!nr->failed,"actual typed slot factory");slot->active_render_width=rw;slot->active_render_height=rh;
  if(opaque_control)control_states.Bind(*nr,*slot);
  // Region-OFF Seal alone is a no-op: explicitly construct the immutable
  // production codec plan and retain the two genuine raw guides.
  nr_binding::Plan bindings{};bindings.primary_owner=slot;WriteCodecDescriptorSet(f.device,*slot,nullptr,&bindings.heaps[nr_binding::Consumer]);bindings.retained[0]=guides.depth;bindings.retained[1]=guides.motion;nr_binding::Refusal refusal{};
  auto* packet=AcquireBindingPacket(f.device,list.native,*nr,bindings,&refusal);if(!packet)Fail(nr_binding::RefusalName(refusal),nr);slot->compose_binding=packet;
  Check(RecordNrUseLocked(list.native,*nr,*slot),"actual main slot recording pin");auto* diagnostic=capture.Pin(f,list.native,*nr,*slot,guides);if(!identity&&scene_bench::Functional())flight.clear.Prepare(f,*slot,*diagnostic);flight.Pin(list.native,*nr,*slot,*packet,*diagnostic,owner);
  auto admission=raw_nr_admission::Inputs(phase_frames[frame].sx,phase_frames[frame].sy,guides.jitter.x,guides.jitter.y);
  admission.has_mv_x=guides.has_motion_scale_x;admission.has_mv_y=guides.has_motion_scale_y;admission.has_j_x=guides.jitter.valid_x;admission.has_j_y=guides.jitter.valid_y;
  admission.mv_x=guides.signature.motion_scale_x;admission.mv_y=guides.signature.motion_scale_y;admission.hardware_depth=!guides.signature.linear_depth;
  // Replace cold planned dimensions with actual getters/GetDesc/real context.
  const auto actual_color_desc=ID3D12Resource_GetDesc(owner->colors[0].resource);
  Check(GetUInt(factory.eval_params,"DLSS.Render.Subrect.Dimensions.Width",&admission.raw_w)&&GetUInt(factory.eval_params,"DLSS.Render.Subrect.Dimensions.Height",&admission.raw_h),"actual raw render extent getter");
  admission.work_w=nr->width;admission.work_h=nr->height;admission.physical_w=static_cast<unsigned>(actual_color_desc.Width);admission.physical_h=actual_color_desc.Height;admission.samples=actual_color_desc.SampleDesc.Count;
  Check(GetUInt(factory.eval_params,"DLSS.Input.Color.Subrect.Base.X",&admission.base_x)&&GetUInt(factory.eval_params,"DLSS.Input.Color.Subrect.Base.Y",&admission.base_y),"actual source subrect getters");
  ID3D12Resource *actual_color=nullptr,*actual_depth=nullptr,*actual_motion=nullptr;
  admission.same_producer=GetResource(factory.eval_params,"Color",&actual_color)&&GetResource(factory.eval_params,"Depth",&actual_depth)&&GetResource(factory.eval_params,"MotionVectors",&actual_motion)&&actual_color==owner->colors[0].resource&&actual_depth==owner->depth.resource&&actual_motion==owner->motions[0].resource&&guides.depth==actual_depth&&guides.motion==actual_motion;
  // Actual seven-UAV producer was recorded for this frame, list lifetime,
  // Reset generation and exact bound resources before any opaque consumer.
  // The later existing exact-fence retirement permits readback validation.
  admission.same_frame=admission.same_producer&&scene.Matches(list.native,*owner,frame);
  Check(admission.same_frame,"same-frame scene producer witness before F18");
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
  Transition(list.native,slot->proxy,slot->proxy_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);DispatchCodec(list.native,*slot,g_codec.encode_v2,rw,rh,color,true,0,0,0,0,CodecColorMode::Automatic,rw,rh,rw,rh,1);auto roots=scene_bench::Functional()?Capture::Roots(list.native,frame,"encode-v2"):CodecConstants{};
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
  if(scene_bench::Functional())flight.clear.Record(f,list.native,*diagnostic,*slot);
  if(scene_bench::Functional())std::printf("NR_OUTPUT_SENTINEL frame=%u negative=-1234 wholePlane=1 encodedCaptureBeforeSaturate=1 nonvisibleCPUDescriptor=1\n",frame);
  runtime_gate.Before("Evaluate",frame,list.native,flight,factory,*nr);
  Check(envelope.EnvelopeApi().capture(&envelope,list.native),"before F18 Eval envelope and prior lease");NgxResult evaluated=0;{ScopedSrwExclusive lock(&g_nr_api_lock);ScopedPrivateNrCall private_call(PrivateNrOperation::Evaluate,nr->nr_handle);++nr_evals;evaluated=g_nr_api.bridge_evaluate(g_nr_api.signed_evaluate,list.native,nr->nr_handle,nr->params,nullptr);}
  std::printf("F18_EVAL frame=%u result=%08x calls=%u\n",frame,evaluated,nr_evals);Opaque(evaluated,envelope,*nr,"F18-Eval");ObserveAfterOpaque(list.native,*owner,frame,"F18-evaluate");ObserveSlot(list.native,*nr,*slot,"Evaluate");
  nr->guide_history.CommitOnEvaluate(guides.signature,true);++nr->evaluations;CompleteNrDiagnosticEvaluation(*nr,*slot);g_nr_successful_evaluations.fetch_add(1);
  UavBarrier(list.native,slot->neural);Transition(list.native,slot->neural,slot->neural_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  }
  // Save original function outputs even during the two production warm-up
  // frames. A warm-up edited result is explicit raw identity, never model proof.
  const bool combine=identity||(nr->successful_evaluations_since_reset>g_config.warmup_after_create&&!g_config.identity_use_original);
  if(frame>=scene_bench::warm)Check(combine&&!slot->suppress_transfer_this_frame,"observation consumes actual NR transfer after exactly two production warmups");
  if(combine){++probe_combines;Transition(list.native,slot->result,slot->result_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);DispatchCodec(list.native,*slot,g_codec.combine_v2,rw,rh,color,true,0,0,0,0,CodecColorMode::Automatic,rw,rh,rw,rh,slot->suppress_transfer_this_frame?0.f:1.f);roots=scene_bench::Functional()?Capture::Roots(list.native,frame,"combine-v2"):CodecConstants{};UavBarrier(list.native,slot->result);}
  else {Transition(list.native,slot->result,slot->result_state,D3D12_RESOURCE_STATE_COPY_DEST);Barrier(list.native,owner->colors[0].resource,static_cast<D3D12_RESOURCE_STATES>(owner->completed_states.at(owner->colors[0].resource)),D3D12_RESOURCE_STATE_COPY_SOURCE);
   ID3D12GraphicsCommandList_CopyResource(list.native,slot->result,owner->colors[0].resource);Barrier(list.native,owner->colors[0].resource,D3D12_RESOURCE_STATE_COPY_SOURCE,static_cast<D3D12_RESOURCE_STATES>(read_state));}
  Transition(list.native,slot->result,slot->result_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);if(scene_bench::Functional())capture.Record(list.native,*diagnostic,roots,*slot);
  std::printf("RAW_NR_FRAME frame=%u observe=%u combined=%u suppress=%u warmupCount=%u expected_c=s parentEval=one-per-frame sceneProducer=actual strength=1\n",frame,frame>=scene_bench::warm,combine,slot->suppress_transfer_this_frame,nr->successful_evaluations_since_reset);
  });if(refusal_at_entry)Fail(refusal_at_entry,nr);if(continue_control)continue;
  ObserveAfterOpaque(list.native,*owner,frame,"diagnostic-recorded");
  rr_consumer.Pin(f,list,owner,frame,"one-RR-variant",edited_slot->result,edited_slot);active_rr_consumer=&rr_consumer;
  Check(rr_consumer.nr_recording_token==flight.token,"NR producer and RR consumer use identical real recording");
  GuideState(list.native,*owner,frame); // actual-current raw guide states after opaque NR; never guessed
  if(scene_bench::Functional())rr_output.Poison(list.native,*owner,frame);OneRrEvaluate(list,envelope,factory,owner,frame,rr_consumer,edited_slot->result,nr);
  {nr_source_runtime::Lock lock;unsigned state=0;Check(nr_source_runtime::StateOf(nr_source_runtime::RecordingOf(list.native),nr_source_runtime::ResourceOf(edited_slot->result),state),"actual edited Color state after native RR");edited_slot->result_state=static_cast<D3D12_RESOURCE_STATES>(state);std::printf("ONE_RR_EDITED_STATE frame=%u state=%u actualObserver=1\n",frame,state);}
  Check(scene_bench::measurements.End(list.native),"render timestamp ends after actual native RR before any image copy");if(scene_bench::Functional()||(scene_bench::Capture()&&frame>=scene_bench::warm))rr_output.Record(list.native,*owner,frame);LeaveCriticalSection(&g_chain_cs);flight.Finish(f,list,frame,&rr_consumer);active_rr_consumer=nullptr;if(scene_bench::Functional())capture.Save(frame);SaveSceneFrame(rr_output,scene,frame);
 }
 Check(native_rr_evals==scene_bench::Total()&&parent_create_attempts==1&&nr_creates==(baseline?0u:1u)&&nr_evals==(baseline?0u:scene_bench::Total())&&!active_rr_consumer,"complete one-RR history and exact call counts");Check(scene.dispatches==scene_bench::Total()+1&&scene.saves==(scene_bench::Functional()?scene_bench::Total():0u),"single pool exactly bounded producer; no guide readbacks outside functional");if(!baseline)Check(probe_combines==scene_bench::Total()-2,"actual production combine after exactly two model warmup frames");Check(scene_bench::measurements.Snapshot("after_retire",scene_bench::Total()),"process memory all real flights retired before model release");if(!baseline)Check(nr&&CanDestroyContextLocked(*nr)&&CanDestroyBindingsLocked(*nr),"F18 dependencies retire only after real RR consumption");Check(SUCCEEDED(ID3D12Device_GetDeviceRemovedReason(f.device)),"device healthy");
 if(!baseline){flight.clear.Drop();++context_destroy_calls;DestroyContext(*nr);if(nr->lifecycle_dependencies_quarantined)Fail("F18-release-unproven",nr);}
 {ScopedSrwExclusive lock(&g_nr_api_lock);nr_private_sr_core::Scope tls;factory.Guard("parent-release");for(auto** p:{&factory.eval_params,&factory.capability})if(*p){Check(factory.exports.destroy(*p)==kNgxSuccess,"actual owner parameter destruction");*p=nullptr;}}
 ++parent_release_calls;Check(release(parent)==kNgxSuccess,"original parent Release after NR retirement and while native exports retained");nr_private_sr_core::ForgetParent(parent);ForgetFeature(parent);Check(destroy(parent_params)==kNgxSuccess,"parent params destroyed under retained original owner");
 precreate.Drop(destroy);FreeLibrary(factory.callback_owner);FreeLibrary(factory.capability_owner);nr_private_sr_core::ReleaseExports(factory.exports);
 Check(scene_bench::measurements.Snapshot("after_model_release",scene_bench::Total()),"process memory after real model handles released");
 DestroyBindings(context);if(!baseline&&scene_bench::Functional())capture.Drop();rr_output.Drop();scene.Drop();owner->Drop();ReleaseCom(heap);ReleaseCom(alternate_heap);if(!no_nr)runtime_gate.Drop();
 Check(scene_bench::measurements.Snapshot("after_pool_release",scene_bench::Total()),"process memory after single scene/output pool released; driver cache may remain");
 std::printf("PASS SCENE_HOST mode=%s parentCreate=1 parentEval=%u F18Create=%u F18Eval=%u SceneFeature=0 Feature1=0 R=1280x720 O=1920x1080 captureFiles=%zu qualityVerdict=unknown\n",mode.c_str(),native_rr_evals,nr_creates,nr_evals,scene_bench::capture_files.size());
}
void RunOneRr(const std::string& mode,const wchar_t* bridge,const wchar_t* pin){
 Check(mode=="baseline"||mode=="variant","only registered one-RR histories");
 for(unsigned i=0;i<scene_bench::Total();++i){const auto& p=phase_J[i%32];auto facts=raw_nr_admission::Inputs(p.sx,p.sy,p.jx,p.jy);const auto rejected=raw_nr_admission::Enter(facts,[]{});if(rejected){std::printf("COLD_REFUSAL frame=%u reason=%s device=0 opaque=0\n",i,rejected);probe_terminal::Exit(42,"one-RR-cold-refusal");}}
 RunOneRrGpu(mode,bridge,pin);
}

}
