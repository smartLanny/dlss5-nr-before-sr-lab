// Private native-SR-only three-frame history. No product Evaluate entry or NR.
void SrOpaqueResult(NgxResult result,Adapter& envelope,const char* stage){
 if(result==kNgxSuccess){Check(envelope.EnvelopeApi().restore(&envelope),"SR restores actual captured native command state");return;}
 if(envelope.state)envelope.state->AbandonAfterOpaqueFailure();context.lifecycle_dependencies_quarantined=true;
 MarkLifecycleUncertain("native SR opaque failure retains exact owners; no fallback/retry");
 std::printf("SR_STOP stage=%s result=%08x Feature1Create=%u Feature1Eval=%u Feature13Eval=%u NRInit=%u NRCreate=%u NREval=%u noCleanup=1\n",stage,result,parent_create_attempts,native_sr_evals,native_rr_evals,nr_init_attempts,nr_creates,nr_evals);
 PrintOneRrClosure();probe_terminal::Exit(42,"native-SR-opaque-stop");
}
void NativeSrEvaluate(queue_test::List& list,Adapter& envelope,SrFactory& factory,const std::shared_ptr<Inputs>& owner,unsigned frame,Flight& flight){
 active_rr_consumer=&flight;factory.Guard("native-SR-Evaluate");
 const auto live=SnapshotPostBoundaryList(list.native);unsigned long long token=0;
 Check(flight.packet&&flight.cpu_owner==owner&&flight.packet->lease.recording_command_lists==1&&native_sr_evals==frame&&ReadLifecycleToken(list.native,token)&&token==flight.nr_recording_token&&live.known&&live.token==flight.native_lifetime_token&&live.reset==flight.native_reset,"SR exact live recording and immutable lease before one Eval");
 auto* record=FindLifecycleCommandListLocked(token);Check(record&&!record->detached&&!record->submission_count&&!g_lifecycle_tracking_uncertain.load(),"SR original ledger is certain and unsubmitted");
 HMODULE actual_owner=nullptr;const auto address=GetProcAddress(factory.exports.domain_core,"NVSDK_NGX_D3D12_EvaluateFeature");
 Check(address==reinterpret_cast<FARPROC>(factory.exports.evaluate)&&factory.exports.retained_modules[1]==factory.exports.domain_core&&GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(address),&actual_owner)&&actual_owner==factory.exports.domain_core,"SR actual Eval still has retained original factory owner");
 ID3D12Device* device=nullptr;Hr(ID3D12GraphicsCommandList_GetDevice(list.native,IID_ID3D12Device,(void**)&device),"SR actual list device");Check(IsSameRrDepthDevice(device,factory.exports.domain_device),"SR list and initialized parent have one device");ReleaseCom(device);
 const auto color=ID3D12Resource_GetDesc(owner->colors[0].resource),depth=ID3D12Resource_GetDesc(owner->depth.resource),motion=ID3D12Resource_GetDesc(owner->motions[0].resource);
 Check(color.Width==rw&&color.Height==rh&&color.Format==DXGI_FORMAT_R16G16B16A16_FLOAT&&depth.Width==rw&&depth.Height==rh&&depth.Format==DXGI_FORMAT_R32_FLOAT&&motion.Width==rw&&motion.Height==rh&&motion.Format==DXGI_FORMAT_R32G32_FLOAT&&owner->clean.resource!=owner->colors[0].resource,"SR same-frame exact R color/hardware-depth/physical-MV formats and distinct O");
 for(auto* resource:{owner->colors[0].resource,owner->depth.resource,owner->motions[0].resource}){nr_source_runtime::Lock lock;unsigned state=0;Check(nr_source_runtime::StateOf(nr_source_runtime::RecordingOf(list.native),nr_source_runtime::ResourceOf(resource),state)&&(state&D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),"SR input actually readable in exact recording");}
 const auto contract=factory.Contract(owner,frame);Check(envelope.EnvelopeApi().capture(&envelope,list.native),"SR actual state envelope after producer and before opaque Eval");
 NgxResult result=0;{ScopedSrwExclusive lock(&g_nr_api_lock);nr_private_sr_core::Scope tls;++native_sr_evals;result=factory.exports.evaluate(list.native,factory.parent,factory.eval_params,nullptr);}
 std::printf("NATIVE_SR_EVAL frame=%u feature=1 result=%08x calls=%u token=%llu nativeToken=%llu nativeReset=%llu parentGeneration=%llu domain=%llu contract=%s\n",frame,result,native_sr_evals,token,live.token,live.reset,factory.parent_generation,factory.exports.domain_epoch,contract.c_str());
 SrOpaqueResult(result,envelope,"native-SR-Eval");Check(factory.Contract(owner,frame)==contract,"native SR preserved actual inputs/scalars/subrect contract");ObserveAfterOpaque(list.native,*owner,frame,"native-SR-Evaluate");
}
void RunNativeSr(){
 Check(scene_bench::Functional()&&scene_bench::Total()==3,"only SR-off bounded functional slice is implemented");VerifyRuntime(L"runtime.sha256",false);
 one_rr_variant=false;terminal_observer=PrintOneRrClosure;control="probe";
 std::array<PhaseFrame,3> phases{};for(unsigned i=0;i<3;++i){phases[i]=phase_J[i];phases[i].reset=i==0;}phase_frames=phases.data();phase_input_root.clear();
 ++device_entries;queue_test::Fixture f;Configure();
 Check(g_config.enabled&&g_enabled_fast.load()&&nr_processing_runtime::work_enabled.load()&&!g_config.private_residual_sr&&!g_nr_api.initialized&&!GetModuleHandleW(L"nvngx_dlssnr.dll"),"research CPU observer is enabled, NR model is off; not product master-off");
 GetCurrentDirectoryW(MAX_PATH,g_log_path);wcscat_s(g_log_path,L"\\native-sr-core.log");
 std::printf("SR_RESEARCH_CONFIG CPUobserver=1 productMasterOffProof=0 NRInit=0 NRGPU=0 Feature1=1 Feature13=0 R=1280x720 O=1920x1080 layers=0 warm=2 observe=1\n");
 auto library=LoadLibraryW(L"nrchain_nvngx.dll");Check(library,"pinned native bootstrap bridge");auto bootstrap=reinterpret_cast<unsigned(*)(ID3D12Device*)>(GetProcAddress(library,"NRChain_Bootstrap"));Check(bootstrap,"actual native bootstrap export");const auto initialized=bootstrap(f.device);
 std::printf("SR_BOOTSTRAP result=%08x count=1\n",initialized);Check(initialized==kNgxSuccess,"sole successful native initialization");
 auto* ngx=GetModuleHandleW(L"_nvngx.dll");Check(ngx,"actual initialized original Core");auto allocate=reinterpret_cast<nr_private_sr_core::Allocate>(GetProcAddress(ngx,"NVSDK_NGX_D3D12_AllocateParameters"));auto create=reinterpret_cast<nr_private_sr_core::Create>(GetProcAddress(ngx,"NVSDK_NGX_D3D12_CreateFeature"));auto destroy=reinterpret_cast<nr_private_sr_core::Destroy>(GetProcAddress(ngx,"NVSDK_NGX_D3D12_DestroyParameters"));Check(allocate&&create&&destroy,"real original SR factory exports");
 SrPreCreateCapability capability;capability.Query(ngx,f.device);
 const auto hooks=MH_Initialize();Check(hooks==MH_OK||hooks==MH_ERROR_ALREADY_INITIALIZED,"existing real native observation hooks");Check(NrDescriptorConfigureStartup(false),"descriptor observer startup");descriptor_warp_test::InstallCommandObservation(f);TrackGraphicsQueueLocked(f.a);queue_test::List list(f);
 const auto coverage=EnsurePostBoundaryNativeHooks(list.native);Check((coverage&(nr_post_boundary::Legacy|nr_post_boundary::Operations))==(nr_post_boundary::Legacy|nr_post_boundary::Operations)&&!(coverage&(nr_post_boundary::HookMissing|nr_post_boundary::InterfaceUnknown)),"actual SR legacy/operation coverage with known enhanced interface");
 nr_source_runtime::SeedCoreRecordingDevice(list.native);list.Close();list.ResetWithNewAllocator(f);Check(EnsureCodec(f.device),"unchanged full-Core resource/descriptor setup; no codec dispatch or NR inference");
 descriptor_warp_test::Pipeline pipeline(f),alternate(f);auto* heap=descriptor_warp_test::Heap(f,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,true);auto* alternate_heap=descriptor_warp_test::Heap(f,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,true);
 Adapter envelope(f);envelope.list=list.native;envelope.alternate_pipeline=alternate.state;envelope.alternate_heap=alternate_heap;
 auto owner=std::make_shared<Inputs>();owner->Make(f,ow,oh);SceneFrame scene;scene.Make(f,*owner);scene.Record(list.native,*owner,0);
 NgxParams* parent_params=nullptr;Check(allocate(&parent_params)==kNgxSuccess&&parent_params,"actual SR creation params");
 for(auto kv:{std::pair{"Width",rw},{"Height",rh},{"OutWidth",ow},{"OutHeight",oh},{"CreationNodeMask",1u},{"VisibilityNodeMask",1u}})SetUInt(parent_params,kv.first,kv.second);
 SetInt(parent_params,"PerfQualityValue",2);SetInt(parent_params,"DLSS.Hint.Render.Preset.Quality",0);SetInt(parent_params,"DLSS.Feature.Create.Flags",75);SetInt(parent_params,"DLSS.Enable.Output.Subrects",0);
 for(auto kv:{std::pair{"PerfQualityValue",2},{"DLSS.Hint.Render.Preset.Quality",0},{"DLSS.Feature.Create.Flags",75},{"DLSS.Enable.Output.Subrects",0}}){int value=-1;Check(GetInt(parent_params,kv.first,&value)&&value==kv.second,"actual Feature1 quality/default-preset/create-flags getters");std::printf("SR_CREATE_GET key=%s value=%d\n",kv.first,value);}
 Check(capability.proven&&parent_create_attempts==0,"official SR capability/optimal is proven before any model Create");
 ScopedNgxLayer layer(NgxLayer::Core);g_original_core_create=create;NgxHandle* parent=nullptr;Flight parent_flight;
 EnterCriticalSection(&g_chain_cs);pipeline.Bind(list,heap);parent_flight.Pin(f,list,owner,0,"SR-parent-create");active_rr_consumer=&parent_flight;GuideState(list.native,*owner);Check(envelope.EnvelopeApi().capture(&envelope,list.native),"SR actual parent Create state envelope and pinned inputs");
 ++parent_create_attempts;const auto made=HookCoreCreate(list.native,1,parent_params,&parent);std::printf("NATIVE_SR_CREATE feature=1 result=%08x calls=%u quality=2 preset=0 flags=75\n",made,parent_create_attempts);SrOpaqueResult(made,envelope,"native-SR-Create");ObserveAfterOpaque(list.native,*owner,0,"native-SR-Create");Check(parent&&FindFeature(parent).feature_id==1,"actual registered Feature1 handle, never relabel Feature13");LeaveCriticalSection(&g_chain_cs);
 parent_flight.Finish(f,list,"SR-parent-create");active_rr_consumer=nullptr;
 SrFactory factory;factory.Resolve(f,parent);SrOutput output;output.Make(f,*owner);Flight consumer;
 for(unsigned frame=0;frame<3;++frame){scene.Record(list.native,*owner,frame);factory.Bind(owner,frame);Check(scene.Matches(list.native,*owner,frame),"native SR uses exact same-frame finite scene production");
  EnterCriticalSection(&g_chain_cs);pipeline.Bind(list,heap);GuideState(list.native,*owner,frame);consumer.Pin(f,list,owner,frame,"native-SR-consumer");active_rr_consumer=&consumer;output.Poison(list.native,*owner,frame);
  NativeSrEvaluate(list,envelope,factory,owner,frame,consumer);output.Record(list.native,*owner,frame);LeaveCriticalSection(&g_chain_cs);consumer.Finish(f,list,"native-SR-consumer");active_rr_consumer=nullptr;
  output.Save(frame);scene.Save(frame);
  Check(!g_nr_api.initialized&&!GetModuleHandleW(L"nvngx_dlssnr.dll")&&!nr_init_attempts&&!nr_creates&&!nr_evals&&!probe_combines&&native_rr_evals==0,"SR-off no NR initialization/model/codec combine or RR Eval");
 }
 Check(parent_create_attempts==1&&native_sr_evals==3&&scene.dispatches==4&&scene.saves==3&&!active_rr_consumer,"bounded native SR history and one reusable seven-plane producer pool");Check(CanDestroyBindingsLocked(context)&&!context.recording_command_lists&&!g_lifecycle_tracking_uncertain.load(),"all actual SR consumers retired before model release");
 {ScopedSrwExclusive lock(&g_nr_api_lock);nr_private_sr_core::Scope tls;factory.Guard("SR-release");Check(factory.exports.destroy(factory.eval_params)==kNgxSuccess,"actual SR eval params destroyed under retained owner");factory.eval_params=nullptr;++parent_release_calls;Check(factory.exports.release(parent)==kNgxSuccess,"actual Feature1 Release after exact flights retired");}
 nr_private_sr_core::ForgetParent(parent);ForgetFeature(parent);Check(destroy(parent_params)==kNgxSuccess,"original SR create params released");capability.Drop(destroy);nr_private_sr_core::ReleaseExports(factory.exports);
 Check(SUCCEEDED(ID3D12Device_GetDeviceRemovedReason(f.device)),"actual native SR device remains healthy");DestroyBindings(context);output.Drop();scene.Drop();owner->Drop();ReleaseCom(heap);ReleaseCom(alternate_heap);
 std::filesystem::copy_file("native-sr-output-rgba16f-2.bin","frame-0000.rgba16f",std::filesystem::copy_options::none);
 std::printf("PASS NATIVE_SR_RESEARCH Feature1Create=1 Feature1Eval=3 Feature13Create=0 Feature13Eval=0 NRInit=0 NRCreate=0 NREval=0 sceneDispatch=4 exactRetired=1 sourceStateInjection=0 productMasterOffProof=0\n");
}
}
