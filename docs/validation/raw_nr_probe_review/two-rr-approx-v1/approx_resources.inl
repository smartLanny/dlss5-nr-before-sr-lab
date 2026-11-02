namespace raw_nr_probe {
struct ApproxChild:SceneFactory {
 void Prepare();NgxResult Create(ID3D12GraphicsCommandList*);
 NgxResult Evaluate(ID3D12GraphicsCommandList*,unsigned);void ReleaseChild();
 void BindClean(const std::shared_ptr<Inputs>& owner,unsigned frame,ID3D12Resource* clean){
  Bind(owner,frame);SetResource(eval_params,"Output",clean);
  ID3D12Resource* typed=nullptr;void* untyped=nullptr;
  Check(GetResource(eval_params,"Output",&typed)&&typed==clean,"private child actual clean Output override");
  Check(ParamSlot<GetVoidFn>(eval_params,8)(eval_params,"Output",&untyped)!=kNgxSuccess||untyped==clean,"private child typed/untyped Output aliases");
 }
};
struct ApproxResources {
 Plane clean,composite;ID3D12PipelineState* pipeline=nullptr;
 std::array<ID3D12Resource*,3> readbacks{};std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT,3> footprints{};
 D3D12_RESOURCE_STATES clean_state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS,composite_state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
 void Make(queue_test::Fixture& f,ID3D12GraphicsCommandList* list){
  clean.Make(f,rw,rh,DXGI_FORMAT_R16G16B16A16_FLOAT,true);composite.Make(f,rw,rh,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
  clean.upload=Buffer(f,clean.bytes,D3D12_HEAP_TYPE_UPLOAD);
  auto code=File(L"ratio-before-shift.cso");D3D12_COMPUTE_PIPELINE_STATE_DESC d{};d.pRootSignature=g_codec.root;d.CS={code.data(),code.size()};
  Hr(ID3D12Device_CreateComputePipelineState(f.device,&d,IID_ID3D12PipelineState,(void**)&pipeline),"private independently authored ratio PSO");
  auto desc=ID3D12Resource_GetDesc(clean.resource);for(unsigned i=0;i<3;++i){UINT64 bytes=0;ID3D12Device_GetCopyableFootprints(f.device,&desc,0,1,0,&footprints[i],nullptr,nullptr,&bytes);readbacks[i]=Buffer(f,bytes,D3D12_HEAP_TYPE_READBACK);}
  // Establish observed creation states before the first opaque call.
  Move(list,clean.resource,clean_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  Move(list,composite.resource,composite_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
 }
 static void Move(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES& state,D3D12_RESOURCE_STATES wanted){
  {nr_source_runtime::Lock lock;unsigned actual=0;if(nr_source_runtime::StateOf(nr_source_runtime::RecordingOf(list),nr_source_runtime::ResourceOf(resource),actual))state=static_cast<D3D12_RESOURCE_STATES>(actual);}
  Barrier(list,resource,state,wanted);state=wanted;
 }
 void PoisonClean(ID3D12GraphicsCommandList* list){
  Move(list,clean.resource,clean_state,D3D12_RESOURCE_STATE_COPY_DEST);
  const unsigned short pixel[4]={0xe4d2,0xe4d2,0xe4d2,0xe4d2};clean.FillAndRecord(list,Constant(8,pixel));
  clean_state=static_cast<D3D12_RESOURCE_STATES>(read_state);Move(list,clean.resource,clean_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
 }
 void ObserveClean(ID3D12GraphicsCommandList* list){
  nr_source_runtime::Lock lock;unsigned actual=0;Check(nr_source_runtime::StateOf(nr_source_runtime::RecordingOf(list),nr_source_runtime::ResourceOf(clean.resource),actual),"private child actual clean state observer");clean_state=static_cast<D3D12_RESOURCE_STATES>(actual);
 }
 NrBindingPacket* PinComposition(queue_test::Fixture& f,ID3D12GraphicsCommandList* list,NrContext& nr,NrSlot& slot,const std::shared_ptr<Inputs>& owner,unsigned frame){
  nr_binding::Plan plan{};plan.primary_owner=&slot;plan.secondary_owner=this;auto& table=plan.heaps[nr_binding::Codec];
  table.Srv(0,owner->colors[frame].resource,DXGI_FORMAT_R16G16B16A16_FLOAT);table.Srv(1,clean.resource,DXGI_FORMAT_R16G16B16A16_FLOAT);table.Srv(2,slot.result,DXGI_FORMAT_R16G16B16A16_FLOAT);table.Uav(kCodecSrvCount,composite.resource,DXGI_FORMAT_R16G16B16A16_FLOAT);
  static_assert(std::size(plan.retained)==2);plan.retained[0]=readbacks[0];plan.retained[1]=readbacks[1]; // third readback is retained by the same-recording child packet
  nr_binding::Refusal refusal{};auto* packet=AcquireBindingPacket(f.device,list,nr,plan,&refusal);if(!packet)Fail(nr_binding::RefusalName(refusal),&nr);return packet;
 }
 void Compose(ID3D12GraphicsCommandList* list,NrBindingPacket& packet,Inputs& owner,unsigned frame,NrSlot& slot){
  Check(clean.resource!=composite.resource&&clean.resource!=slot.result&&composite.resource!=slot.result,"three distinct clean edited composite resources");
  auto* raw=owner.colors[frame].resource;auto state=static_cast<D3D12_RESOURCE_STATES>(owner.completed_states.at(raw));
  Move(list,raw,state,D3D12_RESOURCE_STATE_COPY_SOURCE);Move(list,composite.resource,composite_state,D3D12_RESOURCE_STATE_COPY_DEST);
  ID3D12GraphicsCommandList_CopyResource(list,composite.resource,raw);Move(list,raw,state,static_cast<D3D12_RESOURCE_STATES>(read_state));owner.completed_states[raw]=state;
  Move(list,clean.resource,clean_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);Transition(list,slot.result,slot.result_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  Move(list,composite.resource,composite_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  struct Args{unsigned w,h;float x,y;} args{rw,rh,-phase_frames[frame].jx,-phase_frames[frame].jy};
  unsigned constants[60]{};std::memcpy(constants,&args,sizeof(args));auto* heap=packet.heaps[nr_binding::Codec];
  ID3D12GraphicsCommandList_SetDescriptorHeaps(list,1,&heap);ID3D12GraphicsCommandList_SetComputeRootSignature(list,g_codec.root);ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list,0,ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap));ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list,1,60,constants,0);ID3D12GraphicsCommandList_SetPipelineState(list,pipeline);ID3D12GraphicsCommandList_Dispatch(list,(rw+7)/8,(rh+7)/8,1);
  UavBarrier(list,composite.resource);Move(list,composite.resource,composite_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  Capture::Copy(list,clean.resource,readbacks[0],footprints[0],clean_state);Capture::Copy(list,slot.result,readbacks[1],footprints[1],slot.result_state);Capture::Copy(list,composite.resource,readbacks[2],footprints[2],composite_state);
  std::printf("APPROX_COMPOSE frame=%u shift=%.9g,%.9g filter=positive-bilinear ratioBeforeShift=1 exactCopyFallback=1 rawGuides=approximate cleanVisibilityExact=0\n",frame,args.x,args.y);
 }
 void Save(unsigned frame){const char* roles[]={"clean-rr-rgba16f","clean-edited-rgba16f","raw-composite-rgba16f"};for(unsigned i=0;i<3;++i)Capture::Save(readbacks[i],footprints[i],8,frame,roles[i]);}
 void Drop(){clean.Drop();composite.Drop();for(auto*& p:readbacks)ReleaseCom(p);ReleaseCom(pipeline);}
};
Flight* active_child_consumer=nullptr;NrBindingPacket* active_composition_packet=nullptr;
void PrintApproxClosure(){PrintOneRrClosure();if(active_child_consumer||active_composition_packet){context.lifecycle_dependencies_quarantined=true;MarkLifecycleUncertain("two-RR failure retains child/composition closure");std::printf("APPROX_FATAL childPacket=%p compositionPacket=%p noCleanup=1\n",active_child_consumer?(void*)active_child_consumer->packet:nullptr,(void*)active_composition_packet);}}
}
