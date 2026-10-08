// Private real default entry control: HookCoreEvaluate is the only entry.
// NR-off is genuine master-off passthrough; NR-on uses unmodified RR-post.
namespace raw_nr_probe {
SceneFactory* post_factory=nullptr;Flight* post_consumer=nullptr;unsigned post_frame=0;
NgxResult CountOriginalRr(ID3D12GraphicsCommandList* list,const NgxHandle* handle,const NgxParams* params,void* callback){
 Check(post_factory&&post_consumer&&post_consumer->packet&&handle==post_factory->parent&&params==post_factory->eval_params&&callback==nullptr,"default native thunk receives exact untouched parent/params");
 post_factory->Guard("default-original-native-evaluate");scene_renderer_state::Guard();
 unsigned long long nr_token=0;const auto native=SnapshotPostBoundaryList(list);
 Check(native_rr_evals==post_frame&&ReadLifecycleToken(list,nr_token)&&nr_token==post_consumer->nr_recording_token&&native.token==post_consumer->native_lifetime_token&&native.reset==post_consumer->native_reset,"exactly one native call for current real recording");
 NgxResult result=0;{ScopedSrwExclusive lock(&g_nr_api_lock);nr_private_sr_core::Scope tls;++native_rr_evals;result=post_factory->exports.evaluate(list,handle,params,callback);}
 std::printf("DEFAULT_NATIVE_EVAL frame=%u result=%08x calls=%u nativeToken=%llu reset=%llu nrToken=%llu\n",post_frame,result,native_rr_evals,native.token,native.reset,nr_token);
 return result;
}
void ConfigureDefault(bool enabled){
 g_config=ChainConfig{};g_config.enabled=enabled;g_config.mode=1;
 g_config.processing_start=nr_processing::Start::After;g_config.processing_selection=1;
 g_config.nr_passes=1;g_config.private_residual_sr=false;g_config.spatial_experiment_mode=0;g_config.temporal_backend=0;
 g_config_generation.store(17);g_enabled_fast.store(enabled,std::memory_order_release);nr_processing_runtime::Reconcile();
 Check(nr_processing_runtime::work_enabled.load()==enabled&&nr_processing_runtime::Effective()==nr_processing::Start::After,"ordinary configuration reconciliation; true master-off stays source-disabled");
 Check(nr_post_settings::Read().percent==100&&g_config.warmup_after_create==2&&!g_config.identity_use_original,"fixed default post precision100 and original two model warmups");
 std::printf("DEFAULT_CONFIG master=%u mode=1 start=After sourceWork=%u privateSR=0 layers=1 Npost=1920x1080 postPercent=100 flagsUnchanged=1\n",enabled,nr_processing_runtime::work_enabled.load());
}
NrContext* ActualPostContext(const NgxHandle* parent,unsigned long long generation){
 NrContext* found=nullptr;for(auto& candidate:g_contexts)if(candidate.sr_handle==parent&&candidate.feature_generation==generation){Check(!found,"one true production post context");found=&candidate;}
 return found;
}
void RunDefaultPost(bool enabled){
 VerifyRuntime(L"runtime.sha256",enabled);one_rr_variant=enabled;terminal_observer=PrintOneRrClosure;control="probe";
 std::array<PhaseFrame,3> phases{};for(unsigned i=0;i<3;++i){phases[i]=phase_J[i];phases[i].reset=i==0;}phase_frames=phases.data();phase_input_root.clear();
 ++device_entries;queue_test::Fixture f;ConfigureDefault(enabled);GetCurrentDirectoryW(MAX_PATH,g_log_path);wcscat_s(g_log_path,L"\\default-post-core.log");
 auto library=LoadLibraryW(L"nrchain_nvngx.dll");Check(library,"exact adjacent bootstrap bridge");auto bootstrap=reinterpret_cast<unsigned(*)(ID3D12Device*)>(GetProcAddress(library,"NRChain_Bootstrap"));Check(bootstrap,"real bootstrap export");
 const auto initialized=bootstrap(f.device);std::printf("BOOTSTRAP result=%08x count=1\n",initialized);if(initialized!=kNgxSuccess)Fail("default-native-init",nullptr,initialized);
 auto* ngx=GetModuleHandleW(L"_nvngx.dll");Check(ngx,"actual original Core module");
 auto allocate=reinterpret_cast<nr_private_sr_core::Allocate>(GetProcAddress(ngx,"NVSDK_NGX_D3D12_AllocateParameters"));auto create=reinterpret_cast<nr_private_sr_core::Create>(GetProcAddress(ngx,"NVSDK_NGX_D3D12_CreateFeature"));
 auto release=reinterpret_cast<nr_private_sr_core::Release>(GetProcAddress(ngx,"NVSDK_NGX_D3D12_ReleaseFeature"));auto destroy=reinterpret_cast<nr_private_sr_core::Destroy>(GetProcAddress(ngx,"NVSDK_NGX_D3D12_DestroyParameters"));Check(allocate&&create&&release&&destroy,"actual original native exports");
 PreCreateCapability precreate;precreate.Query(ngx,f.device);
 const auto hooks=MH_Initialize();Check(hooks==MH_OK||hooks==MH_ERROR_ALREADY_INITIALIZED,"existing native observation hooks");Check(NrDescriptorConfigureStartup(false),"descriptor startup");descriptor_warp_test::InstallCommandObservation(f);TrackGraphicsQueueLocked(f.a);queue_test::List list(f);
 const auto coverage=EnsurePostBoundaryNativeHooks(list.native);Check((coverage&(nr_post_boundary::Legacy|nr_post_boundary::Operations))==(nr_post_boundary::Legacy|nr_post_boundary::Operations)&&!(coverage&(nr_post_boundary::HookMissing|nr_post_boundary::InterfaceUnknown)),"real complete native legacy and enhanced capability coverage");
 nr_source_runtime::SeedCoreRecordingDevice(list.native);list.Close();list.ResetWithNewAllocator(f);
 Check(EnsureCodec(f.device),"original production codec setup; functional setup cost not performance");
 scene_renderer_state::Install(list.native);
 descriptor_warp_test::Pipeline pipeline(f),alternate(f);auto* heap=descriptor_warp_test::Heap(f,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,true);auto* alternate_heap=descriptor_warp_test::Heap(f,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,true);
 Adapter envelope(f);envelope.list=list.native;envelope.alternate_pipeline=alternate.state;envelope.alternate_heap=alternate_heap;
 auto owner=std::make_shared<Inputs>();owner->Make(f,ow,oh);scene_renderer_state::Add(owner->colors[0].resource);
 for(auto* resource:{owner->depth.resource,owner->motions[0].resource,owner->normal.resource,owner->diffuse.resource,owner->specular.resource,owner->roughness.resource,owner->clean.resource})scene_renderer_state::Add(resource);
 SceneFrame scene;scene.Make(f,*owner);scene.Record(list.native,*owner,0);
 NgxParams* parent_params=nullptr;Check(allocate(&parent_params)==kNgxSuccess&&parent_params,"actual native parent params");
 for(auto kv:{std::pair{"Width",rw},{"Height",rh},{"OutWidth",ow},{"OutHeight",oh},{"CreationNodeMask",1u},{"VisibilityNodeMask",1u}})SetUInt(parent_params,kv.first,kv.second);
 SetInt(parent_params,"PerfQualityValue",2);SetInt(parent_params,"RayReconstruction.Hint.Render.Preset.Quality",0);SetInt(parent_params,"DLSS.Feature.Create.Flags",75);SetInt(parent_params,"DLSS.Denoise.Mode",1);SetUInt(parent_params,"DLSS.Roughness.Mode",0);SetUInt(parent_params,"DLSS.Use.HW.Depth",1);SetInt(parent_params,"DLSS.Enable.Output.Subrects",0);
 ScopedNgxLayer layer(NgxLayer::Core);g_original_core_create=create;NgxHandle* parent=nullptr;Flight parent_flight;
 EnterCriticalSection(&g_chain_cs);pipeline.Bind(list,heap);parent_flight.Pin(f,list,owner,0,"default-parent-create");active_rr_consumer=&parent_flight;GuideState(list.native,*owner);
 Check(envelope.EnvelopeApi().capture(&envelope,list.native),"independent true native parent Create envelope");++parent_create_attempts;
 const auto made=HookCoreCreate(list.native,13,parent_params,&parent);std::printf("PARENT_CREATE feature=13 result=%08x calls=1 quality=2 preset=0 flags=75\n",made);OpaqueResult(made,envelope,"default-parent-create");ObserveAfterOpaque(list.native,*owner,0,"default-parent-create");Check(parent,"real native parent handle");LeaveCriticalSection(&g_chain_cs);
 parent_flight.Finish(f,list,"default-parent-create");active_rr_consumer=nullptr;
 SceneFactory factory;factory.Resolve(f,parent);factory.eval_params=factory.Allocate("default-eval-params");post_factory=&factory;
 NrRuntimeGate runtime_gate;runtime_gate.expected_evals=3;if(enabled)runtime_gate.Initialize(f.device,factory);
 else Check(!g_nr_api.initialized&&!GetModuleHandleW(L"nvngx_dlssnr.dll")&&!nr_processing_runtime::work_enabled.load(),"masteroff no NR init/module or source work");
 g_original_core_evaluate=CountOriginalRr;g_original_core_release=release;RrOutput output;output.Make(f,*owner);Flight consumer;
 unsigned long long instance=0;NrContext* production=nullptr;
 for(unsigned frame=0;frame<3;++frame){post_frame=frame;scene.Record(list.native,*owner,frame);factory.Bind(owner,frame);factory.Guard("default-entry");Check(scene.Matches(list.native,*owner,frame),"default entry exact same-frame scene producer");
  EnterCriticalSection(&g_chain_cs);pipeline.Bind(list,heap);GuideState(list.native,*owner,frame);consumer.Pin(f,list,owner,frame,"default-native-consumer");post_consumer=&consumer;active_rr_consumer=&consumer;output.Poison(list.native,*owner,frame);
  const auto noncolor=RrNonColorContract(factory,owner,frame);Check(envelope.EnvelopeApi().capture(&envelope,list.native),"renderer own envelope protects genuine masteroff native passthrough too");LeaveCriticalSection(&g_chain_cs);
  const auto successes_before=g_nr_successful_evaluations.load();const auto visible_before=g_nr_transfer_recorded_frames.load();
  const auto result=HookCoreEvaluate(list.native,parent,factory.eval_params,nullptr);
  std::printf("DEFAULT_ENTRY frame=%u master=%u result=%08x nativeCalls=%u coreCalls=%u rrAttempts=%llu rrRecorded=%llu F18Success=%llu visibleRecorded=%llu stage=%d\n",frame,enabled,result,native_rr_evals,g_core_calls.load(),nr_post_settings::rr_attempts.load(),nr_post_settings::rr_recorded.load(),g_nr_successful_evaluations.load(),g_nr_transfer_recorded_frames.load(),nr_post_settings::last_rr_stage.load());
  if(result!=kNgxSuccess)OpaqueResult(result,envelope,"default-HookCoreEvaluate");
  EnterCriticalSection(&g_chain_cs);Check(envelope.EnvelopeApi().restore(&envelope),"renderer restores its independent actual native state after complete entry");
  Check(native_rr_evals==frame+1&&RrNonColorContract(factory,owner,frame)==noncolor,"default entry preserves native guides/scalars/matrices and exactly one original call");CheckColor(factory,owner->colors[0].resource);
  ObserveAfterOpaque(list.native,*owner,frame,"default-entry");production=ActualPostContext(parent,factory.parent_generation);
  if(enabled){
   if(g_nr_successful_evaluations.load()!=successes_before+1||!production||!production->nr_handle){std::printf("UNSUPPORTED defaultPost frame=%u stage=%d beforeState=%llu prepareDeclined=%llu afterState=%llu finishDeclined=%llu lifecycleUnknown=%u\n",frame,nr_post_settings::last_rr_stage.load(),nr_post_settings::rr_before_state_rejected.load(),nr_post_settings::rr_prepare_declined.load(),nr_post_settings::rr_after_state_rejected.load(),nr_post_settings::rr_finish_declined.load(),g_lifecycle_tracking_uncertain.load());Fail("default-production-NR-admission-unavailable",production);}
   Check(production->mode==1&&production->width==ow&&production->height==oh&&production->evaluations==frame+1&&production->successful_evaluations_since_reset==frame+1,"true production O-grid post context and model history");
   if(!instance)instance=production->instance_id;Check(instance&&production->instance_id==instance,"one actual production F18 model instance");runtime_gate.context=production;
   Check(nr_post_settings::rr_attempts.load()==frame+1&&nr_post_settings::rr_recorded.load()==(frame==2?1u:0u)&&g_nr_transfer_recorded_frames.load()==visible_before+(frame==2?1u:0u),"original two warmups then actual default visible transfer");
   unsigned slots=0;for(const auto& slot:production->slots)if(slot.result){++slots;Check(slot.result==owner->clean.resource&&!CanMutateSlotLocked(slot)&&slot.recording_command_lists==1,"real production slot pins native O output until original exact flight");}
   Check(slots==1&&production->recording_command_lists==1,"one production O slot and exact recording owner");
  }else Check(!production&&g_nr_successful_evaluations.load()==0&&nr_post_settings::rr_attempts.load()==0&&g_core_calls.load()==0&&!g_nr_api.initialized&&!GetModuleHandleW(L"nvngx_dlssnr.dll")&&!nr_processing_runtime::work_enabled.load(),"true masteroff native passthrough skips ProcessRrPost/source/Feature18 entirely");
  output.Record(list.native,*owner,frame);LeaveCriticalSection(&g_chain_cs);consumer.Finish(f,list,"default-native-consumer");active_rr_consumer=nullptr;post_consumer=nullptr;
  EnterCriticalSection(&g_chain_cs);if(production)Check(CanDestroyContextLocked(*production)&&CanDestroyBindingsLocked(*production)&&!production->recording_command_lists&&!production->awaiting_fence_signal,"actual production context/packets exact-fence and Reset retire without manual detachment");LeaveCriticalSection(&g_chain_cs);
  Check(g_nr_transfer_submitted_frames.load()==(enabled&&frame==2?1u:0u),"visible production transfer has actual native Execute and exact private Signal");output.Save(frame);scene.Save(frame);
 }
 Check(native_rr_evals==3&&parent_create_attempts==1&&scene.dispatches==4&&scene.saves==3,"complete bounded default-entry three-frame control");
 const auto recorded=nr_post_settings::rr_recorded.load(),submitted=g_nr_transfer_submitted_frames.load(),evaluated=g_nr_successful_evaluations.load();
 // Real native Release publishes the production context retirement request.
 ++parent_release_calls;Check(HookCoreRelease(parent)==kNgxSuccess,"actual production parent Release event");
 EnterCriticalSection(&g_chain_cs);CollectRetiredContextsLocked();LeaveCriticalSection(&g_chain_cs);DrainPendingContextDestructions();
 EnterCriticalSection(&g_chain_cs);Check(!ActualPostContext(parent,factory.parent_generation),"original collector and outside-lock Drain release production context");for(const auto& pending:g_pending_destroy_contexts)Check(!pending.sr_handle,"no pending production model destruction");LeaveCriticalSection(&g_chain_cs);
 if(enabled){Check(runtime_gate.ready&&!g_lifecycle_tracking_uncertain.load()&&evaluated==3&&recorded==1&&submitted==1,"production model owners released only after exact flights and real release event");for(auto& entry:runtime_gate.exports){FreeLibrary(entry.owner);entry.owner=nullptr;}ReleaseCom(runtime_gate.identity);ReleaseCom(runtime_gate.device);runtime_gate.ready=false;active_nr_runtime_gate=nullptr;}
 {ScopedSrwExclusive lock(&g_nr_api_lock);nr_private_sr_core::Scope tls;Check(factory.exports.destroy(factory.eval_params)==kNgxSuccess,"original eval params destroy under retained module owner");factory.eval_params=nullptr;}
 Check(destroy(parent_params)==kNgxSuccess,"actual parent params destroy");precreate.Drop(destroy);FreeLibrary(factory.callback_owner);FreeLibrary(factory.capability_owner);nr_private_sr_core::ReleaseExports(factory.exports);post_factory=nullptr;
 Check(SUCCEEDED(ID3D12Device_GetDeviceRemovedReason(f.device)),"actual device remains healthy");scene_renderer_state::Drop();DestroyBindings(context);output.Drop();scene.Drop();owner->Drop();ReleaseCom(heap);ReleaseCom(alternate_heap);
 std::filesystem::copy_file("native-rr-output-rgba16f-2.bin","frame-0000.rgba16f",std::filesystem::copy_options::none);
 std::ofstream receipt("result.json");receipt<<"{\n\"schema\":\"dlss5-default-rr-post-functional-v1\",\n\"status\":\"passed-functional\",\n\"route\":\"HookCoreEvaluate-native-RR-then-production-NR\",\n\"coreSource\":\"45df68dc80138e89778e544d49f15109ec437a4f\",\n\"coreSrcTree\":\"7fe3819be240c0fad0eb16526e9d4b3ce0a67f87\",\n\"coreLoad\":\"compiled-full-Core-host;not-addon-load\",\n\"R\":[1280,720],\"N\":[1920,1080],\"O\":[1920,1080],\n\"nrEnabled\":"<<(enabled?"true":"false")<<",\"layers\":1,\"warm\":2,\"frames\":1,\"totalFrames\":3,\n\"counts\":{\"nativeCreate\":1,\"nativeEval\":"<<native_rr_evals<<",\"nrSuccessfulCreateInstances\":"<<(instance?1:0)<<",\"nrSuccessfulEval\":"<<evaluated<<",\"productionVisibleRecorded\":"<<recorded<<",\"productionVisibleSubmitted\":"<<submitted<<"},\n\"retirement\":{\"exactFenceAndSuccessfulReset\":true,\"productionContextReleasedByCollectorDrain\":true,\"nativeReleased\":true},\n\"output\":{\"file\":\"frame-0000.rgba16f\",\"format\":\"R16G16B16A16_FLOAT\",\"width\":1920,\"height\":1080,\"stride\":15360,\"stage\":\"after-complete-default-entry-before-NIS-tonemap\"},\n\"performance\":false,\"qualityVerdict\":\"not-evaluated\",\"gameAcceptance\":false\n}\n";Check(bool(receipt),"complete default-entry result written");receipt.close();
 std::printf("PASS DEFAULT_RR_POST master=%u native=3 F18SuccessfulEval=%llu instance=%llu visibleRecorded=%llu submitted=%llu exactRetired=1 sourceStateInjection=0\n",enabled,evaluated,instance,recorded,submitted);
}
}
