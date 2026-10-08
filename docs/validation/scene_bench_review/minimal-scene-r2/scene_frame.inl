// Private finite renderer. This witness describes actual recorded production;
// completion and bytes are checked only after the existing exact fence/Reset.
namespace raw_nr_probe {
float SceneHalf(unsigned short h){
 const float sign=(h&0x8000)?-1.f:1.f;const unsigned e=(h>>10)&31,m=h&1023;
 if(e==31)return m?std::numeric_limits<float>::quiet_NaN():sign*std::numeric_limits<float>::infinity();
 return sign*(e?std::ldexp(1.f+float(m)/1024.f,int(e)-15):std::ldexp(float(m),-24));
}
struct SceneFrame {
 struct Constants {unsigned width,height,frame,samples;float displacement[2],projection[2],camera[3],storage;};
 static_assert(sizeof(Constants)==48);
 ID3D12RootSignature* root=nullptr;ID3D12PipelineState* pipeline=nullptr;ID3D12DescriptorHeap* heap=nullptr;
 std::array<ID3D12Resource*,7> targets{},readbacks{};
 std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT,7> footprints{};
 const char* roles[7]={"raw-color-rgba16f","depth-r32f","motion-rg32f","normal-rgba16f","diffuse-rgba16f","specular-rgba16f","roughness-r32f"};
 Constants recorded{};ID3D12GraphicsCommandList* recorded_list=nullptr;unsigned long long native_token=0,reset=0;unsigned dispatches=0,saves=0;
 std::string shader_sha,constants_sha;bool completed=false;
 void Make(queue_test::Fixture& f,Inputs& owner){
  targets={owner.colors[0].resource,owner.depth.resource,owner.motions[0].resource,owner.normal.resource,owner.diffuse.resource,owner.specular.resource,owner.roughness.resource};
  D3D12_DESCRIPTOR_RANGE range{};range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_UAV;range.NumDescriptors=7;
  D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[0].DescriptorTable={1,&range};
  params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[1].Constants={0,0,12};
  D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=2;desc.pParameters=params;ID3DBlob *blob=nullptr,*error=nullptr;
  Hr(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error),"finite scene root serialize");
  Hr(ID3D12Device_CreateRootSignature(f.device,0,ID3D10Blob_GetBufferPointer(blob),ID3D10Blob_GetBufferSize(blob),IID_ID3D12RootSignature,(void**)&root),"finite scene root");ReleaseCom(blob);ReleaseCom(error);
  const auto cso=File(L"scene.cso");shader_sha=Digest(cso.data(),cso.size());D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};pso.pRootSignature=root;pso.CS={cso.data(),cso.size()};
  Hr(ID3D12Device_CreateComputePipelineState(f.device,&pso,IID_ID3D12PipelineState,(void**)&pipeline),"finite scene actual CSO pipeline");
  D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=7;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  Hr(ID3D12Device_CreateDescriptorHeap(f.device,&hd,IID_ID3D12DescriptorHeap,(void**)&heap),"single scene UAV descriptor heap");
  auto cpu=ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);const auto stride=ID3D12Device_GetDescriptorHandleIncrementSize(f.device,hd.Type);
  for(unsigned i=0;i<7;++i){const auto d=ID3D12Resource_GetDesc(targets[i]);Check(d.Width==rw&&d.Height==rh&&(d.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),"scene exact producer extent/UAV");
   D3D12_UNORDERED_ACCESS_VIEW_DESC view{};view.Format=d.Format;view.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;ID3D12Device_CreateUnorderedAccessView(f.device,targets[i],nullptr,&view,cpu);cpu.ptr+=stride;
   if(scene_bench::Functional()){UINT64 bytes=0;ID3D12Device_GetCopyableFootprints(f.device,&d,0,1,0,&footprints[i],nullptr,nullptr,&bytes);readbacks[i]=Buffer(f,bytes,D3D12_HEAP_TYPE_READBACK);}
  }
  Check(owner.world_to_view[13]==-1.f&&owner.world_to_view[14]==4.f&&owner.view_to_clip[0]==1.f&&owner.view_to_clip[5]==float(rw)/rh&&owner.view_to_clip[11]==1.f&&owner.view_to_clip[14]==.1f,"actual camera translation and reverse infinite P0 match ray generator");
  std::printf("SCENE_POOL sets=1 producerPlanes=7 shader=%s camera=0,1,-4 projectionXY=1,%.9g near=.1 diffuseOnly=1 physicalMV=0\n",shader_sha.c_str(),float(rw)/rh);
 }
 unsigned State(ID3D12GraphicsCommandList* list,Inputs& owner,ID3D12Resource* resource){unsigned state=0;
  {nr_source_runtime::Lock lock;if(nr_source_runtime::StateOf(nr_source_runtime::RecordingOf(list),nr_source_runtime::ResourceOf(resource),state))return state;}
  auto prior=owner.completed_states.find(resource);Check(prior!=owner.completed_states.end(),"scene state requires successful creation or exact completed predecessor");return prior->second;
 }
 void Record(ID3D12GraphicsCommandList* list,Inputs& owner,unsigned frame){
  Check(frame<scene_bench::Total(),"bounded expanded scene phase schedule");const auto native=SnapshotPostBoundaryList(list);Check(native.known&&native.token,"scene producer actual native recording");
  recorded={rw,rh,frame,1,{phase_frames[frame].sx,phase_frames[frame].sy},{owner.view_to_clip[0],owner.view_to_clip[5]},{0,1,-4},pre_exposure/exposure_scale};
  Check(recorded.samples>=1&&recorded.projection[0]>0&&recorded.projection[1]>0&&phase_frames[frame].jx==-recorded.displacement[0]&&phase_frames[frame].jy==-recorded.displacement[1],"scene sample displacement and native jitter are opposite; zero physical MV separate");
  for(auto* target:targets){const auto before=static_cast<D3D12_RESOURCE_STATES>(State(list,owner,target));
   if(before==D3D12_RESOURCE_STATE_UNORDERED_ACCESS){Barrier(list,target,before,D3D12_RESOURCE_STATE_COPY_SOURCE);Barrier(list,target,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);}
   else Barrier(list,target,before,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  }
  ID3D12GraphicsCommandList_SetDescriptorHeaps(list,1,&heap);ID3D12GraphicsCommandList_SetComputeRootSignature(list,root);
  ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list,0,ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap));ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list,1,12,&recorded,0);ID3D12GraphicsCommandList_SetPipelineState(list,pipeline);
  auto state=std::make_unique<NativeCommandState>();Check(CaptureNativeCommandState(list,*state),"scene actual command state capture");
  Check(state->arguments[0][1].kind==CommandRootKind::Constants&&(state->arguments[0][1].constant_mask&4095)==4095&&std::memcmp(state->arguments[0][1].constants,&recorded,sizeof(recorded))==0,"scene actual 48-byte root constants");ReleaseNativeCommandState(*state);
  ID3D12GraphicsCommandList_Dispatch(list,(rw+7)/8,(rh+7)/8,1);++dispatches;
  for(unsigned i=0;i<7;++i){UavBarrier(list,targets[i]);Barrier(list,targets[i],D3D12_RESOURCE_STATE_UNORDERED_ACCESS,static_cast<D3D12_RESOURCE_STATES>(read_state));if(scene_bench::Functional())Capture::Copy(list,targets[i],readbacks[i],footprints[i],static_cast<D3D12_RESOURCE_STATES>(read_state));}
  // Pin/GuideState below will publish output state; it is not produced by scene.
  recorded_list=list;native_token=native.token;reset=native.reset;constants_sha=Digest(&recorded,sizeof(recorded));completed=false;owner.SelectFrame(frame);
  std::printf("SCENE_RECORD frame=%u nativeToken=%llu reset=%llu constants=%s resources=7 sameCommandList=1 inputBytes=await-exact-fence\n",frame,native_token,reset,constants_sha.c_str());
 }
 bool Matches(ID3D12GraphicsCommandList* list,const Inputs& owner,unsigned frame){const auto native=SnapshotPostBoundaryList(list);
  return list==recorded_list&&recorded.frame==frame&&native.known&&native.token==native_token&&native.reset==reset&&targets[0]==owner.colors[0].resource&&targets[1]==owner.depth.resource&&targets[2]==owner.motions[0].resource&&targets[3]==owner.normal.resource&&targets[4]==owner.diffuse.resource&&targets[5]==owner.specular.resource&&targets[6]==owner.roughness.resource;
 }
 void Save(unsigned frame){Check(recorded.frame==frame&&!completed,"producer readback after this exact frame retirement");
  for(unsigned i=0;i<7;++i){const unsigned bpp=(i==1||i==6)?4:8;std::vector<unsigned char> bytes(size_t(rw)*rh*bpp);unsigned char* mapped=nullptr;Hr(ID3D12Resource_Map(readbacks[i],0,nullptr,(void**)&mapped),"completed scene producer readback");
   for(unsigned y=0;y<rh;++y)std::memcpy(bytes.data()+size_t(y)*rw*bpp,mapped+footprints[i].Offset+size_t(y)*footprints[i].Footprint.RowPitch,size_t(rw)*bpp);ID3D12Resource_Unmap(readbacks[i],0,nullptr);
   float minimum=std::numeric_limits<float>::infinity(),maximum=-minimum;size_t finite=0,nonzero=0;unsigned channels=i==1||i==6?1:i==2?2:4;
   for(size_t p=0;p<size_t(rw)*rh;++p){float normal_length=0;
    for(unsigned c=0;c<channels;++c){float value=0;if(i==1||i==2||i==6)std::memcpy(&value,bytes.data()+p*bpp+c*4,4);else {unsigned short h=0;std::memcpy(&h,bytes.data()+p*bpp+c*2,2);value=SceneHalf(h);}
     Check(std::isfinite(value),"scene producer all samples finite");++finite;nonzero+=value!=0;
     if(channels!=4||c<3){minimum=std::min(minimum,value);maximum=std::max(maximum,value);}
     if(i==1)Check(value>0&&value<=1,"actual hardware reverse depth in range");if(i==2)Check(value==0,"stationary physical motion excludes sample jitter");
     if(i==3&&c<3)normal_length+=value*value;if(i==4&&c<3)Check(value>=0&&value<=1,"linear diffuse material in range");
     if(i==5&&c<3)Check(value==0,"actual scene has no specular lobe");if(i==6)Check(value==1,"actual diffuse-only roughness");
    }if(i==3)Check(normal_length>.99f&&normal_length<1.01f,"generated world normals unit length");
   }
   if(i==0||i==1||i==4)Check(maximum>minimum&&nonzero>0,"real scene spatial signal is nonconstant");
   const auto name=std::string("scene-")+roles[i]+"-"+std::to_string(frame)+".bin";std::ofstream out(name,std::ios::binary);out.write((char*)bytes.data(),bytes.size());Check(bool(out),"scene full producer bytes saved");
   std::printf("SCENE_BYTES frame=%u role=%s file=%s sha256=%s bytes=%zu min=%.9g max=%.9g finite=%zu nonzero=%zu\n",frame,roles[i],name.c_str(),Digest(bytes.data(),bytes.size()).c_str(),bytes.size(),minimum,maximum,finite,nonzero);
  }completed=true;++saves;
 }
 void Drop(){for(auto*& resource:readbacks)ReleaseCom(resource);ReleaseCom(heap);ReleaseCom(pipeline);ReleaseCom(root);}
};
}
