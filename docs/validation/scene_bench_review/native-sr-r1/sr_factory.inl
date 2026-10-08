// Project-owned native Feature1 research adapter. Product helpers unchanged.
namespace raw_nr_probe {
unsigned native_sr_evals=0;
struct SrFactory {
 nr_private_sr_core::Exports exports{};NgxHandle* parent=nullptr;NgxParams* eval_params=nullptr;
 unsigned long long parent_generation=0;
 void Guard(const char* stage){
  ScopedNgxLayer layer(NgxLayer::Core);const auto current=FindFeature(parent);
  const bool valid=nr_private_sr_core::DomainAlive(exports)&&current.handle==parent&&current.feature_id==1&&current.generation==parent_generation&&!current.pending&&!current.retiring&&nr_private_sr_core::ParentInDomain(parent,parent_generation,exports.domain_epoch,reinterpret_cast<void*>(exports.create))&&IsSameRrDepthDevice(exports.domain_device,g_codec.device);
  std::printf("SR_PARENT_GUARD stage=%s valid=%u feature=%d generation=%llu domain=%llu\n",stage,valid,current.feature_id,parent_generation,exports.domain_epoch);
  Check(valid,"actual Feature1 parent, generation and original init domain");
 }
 void Resolve(queue_test::Fixture& f,NgxHandle* handle){
  parent=handle;parent_generation=FindFeature(parent).generation;
  Check(FindFeature(parent).feature_id==1&&nr_private_sr_core::ResolveExports(exports,f.device,parent,parent_generation),"original exports resolve a real Feature1 parent");Guard("resolve");
  ScopedSrwExclusive lock(&g_nr_api_lock);nr_private_sr_core::Scope tls;
  Check(exports.allocate(&eval_params)==kNgxSuccess&&eval_params,"actual native SR eval parameters");
 }
 static const char* const* ResourceKeys(size_t& count){
  static const char* keys[]={"Color","Output","Depth","MotionVectors","TransparencyMask","ExposureTexture","DLSS.Input.Bias.Current.Color.Mask","GBuffer.Albedo","GBuffer.Roughness","GBuffer.Metallic","GBuffer.Specular","GBuffer.Subsurface","GBuffer.Normals","GBuffer.ShadingModelId","GBuffer.MaterialId","GBuffer.Attrib.8","GBuffer.Attrib.9","GBuffer.Attrib.10","GBuffer.Attrib.11","GBuffer.Attrib.12","GBuffer.Attrib.13","GBuffer.Attrib.14","GBuffer.Attrib.15","MotionVectors3D","IsParticleMask","AnimatedTextureMask","DepthHighRes","Position.ViewSpace","RayTracingHitDistance","MotionVectorsReflection"};
  count=sizeof(keys)/sizeof(*keys);return keys;
 }
 static ID3D12Resource* Expected(const Inputs& owner,const char* key){
  if(!std::strcmp(key,"Color"))return owner.colors[0].resource;if(!std::strcmp(key,"Output"))return owner.clean.resource;
  if(!std::strcmp(key,"Depth"))return owner.depth.resource;if(!std::strcmp(key,"MotionVectors"))return owner.motions[0].resource;return nullptr;
 }
 void Bind(const std::shared_ptr<Inputs>& owner,unsigned frame){
  Guard("bind");ScopedSrwExclusive lock(&g_nr_api_lock);nr_private_sr_core::Scope tls;
  size_t count=0;const auto keys=ResourceKeys(count);for(size_t i=0;i<count;++i)SetResource(eval_params,keys[i],Expected(*owner,keys[i]));
  for(const char* key:{"DLSS.Input.Color.Subrect.Base.X","DLSS.Input.Color.Subrect.Base.Y","DLSS.Input.Depth.Subrect.Base.X","DLSS.Input.Depth.Subrect.Base.Y","DLSS.Input.MV.Subrect.Base.X","DLSS.Input.MV.Subrect.Base.Y","DLSS.Input.Translucency.Subrect.Base.X","DLSS.Input.Translucency.Subrect.Base.Y","DLSS.Input.Bias.Current.Color.Subrect.Base.X","DLSS.Input.Bias.Current.Color.Subrect.Base.Y","DLSS.Output.Subrect.Base.X","DLSS.Output.Subrect.Base.Y"})SetUInt(eval_params,key,0);
  SetUInt(eval_params,"DLSS.Render.Subrect.Dimensions.Width",rw);SetUInt(eval_params,"DLSS.Render.Subrect.Dimensions.Height",rh);
  for(auto kv:{std::pair{"DLSS.Pre.Exposure",pre_exposure},{"DLSS.Exposure.Scale",exposure_scale},{"MV.Scale.X",1.f},{"MV.Scale.Y",1.f},{"Jitter.Offset.X",phase_frames[frame].jx},{"Jitter.Offset.Y",phase_frames[frame].jy},{"Sharpness",0.f},{"FrameTimeDeltaInMsec",1000.f/60.f}})SetFloat(eval_params,kv.first,kv.second);
  SetInt(eval_params,"Reset",frame==0?1:0);SetInt(eval_params,"DLSS.Indicator.Invert.X.Axis",0);SetInt(eval_params,"DLSS.Indicator.Invert.Y.Axis",0);SetUInt(eval_params,"TonemapperType",0);
  // No RR material/reflection/matrix inputs are passed to native Feature1.
  Check(!phase_frames[frame].reset||frame==0,"SR Reset only starts its independent history");
  std::printf("SR_BIND frame=%u R=%ux%u O=%ux%u preExposure=%.9g exposureScale=%.9g jitter=%.9g,%.9g reset=%u physicalMV=0 materialGuidesBound=0\n",frame,rw,rh,ow,oh,pre_exposure,exposure_scale,phase_frames[frame].jx,phase_frames[frame].jy,frame==0);
 }
 std::string Contract(const std::shared_ptr<Inputs>& owner,unsigned frame){
  std::ostringstream text;size_t count=0;const auto keys=ResourceKeys(count);
  for(size_t i=0;i<count;++i){ID3D12Resource* typed=nullptr;void* untyped=nullptr;const auto t=ParamSlot<GetD3D12Fn>(eval_params,9)(eval_params,keys[i],&typed),u=ParamSlot<GetVoidFn>(eval_params,8)(eval_params,keys[i],&untyped);
   auto* expected=Expected(*owner,keys[i]);Check(t==kNgxSuccess&&typed==expected&&(u!=kNgxSuccess||untyped==expected),"actual SR required resources and explicit optional absence");text<<keys[i]<<'='<<typed<<','<<t<<','<<u<<';';}
  for(auto kv:{std::pair{"DLSS.Pre.Exposure",pre_exposure},{"DLSS.Exposure.Scale",exposure_scale},{"MV.Scale.X",1.f},{"MV.Scale.Y",1.f},{"Jitter.Offset.X",phase_frames[frame].jx},{"Jitter.Offset.Y",phase_frames[frame].jy},{"Sharpness",0.f},{"FrameTimeDeltaInMsec",1000.f/60.f}}){float v=0;Check(GetFloat(eval_params,kv.first,&v)&&std::memcmp(&v,&kv.second,4)==0,"actual SR jitter/exposure/MV scalar bits unchanged");unsigned bits=0;std::memcpy(&bits,&v,4);text<<kv.first<<'='<<bits<<';';}
  for(auto kv:{std::pair{"DLSS.Render.Subrect.Dimensions.Width",rw},{"DLSS.Render.Subrect.Dimensions.Height",rh}}){unsigned v=0;Check(GetUInt(eval_params,kv.first,&v)&&v==kv.second,"actual SR render subrect dimensions");text<<kv.first<<'='<<v<<';';}
  for(const char* key:{"DLSS.Input.Color.Subrect.Base.X","DLSS.Input.Color.Subrect.Base.Y","DLSS.Input.Depth.Subrect.Base.X","DLSS.Input.Depth.Subrect.Base.Y","DLSS.Input.MV.Subrect.Base.X","DLSS.Input.MV.Subrect.Base.Y","DLSS.Output.Subrect.Base.X","DLSS.Output.Subrect.Base.Y"}){unsigned v=1;Check(GetUInt(eval_params,key,&v)&&v==0,"actual SR primary subrect bases zero");text<<key<<'='<<v<<';';}
  int reset=-1;Check(GetInt(eval_params,"Reset",&reset)&&reset==int(frame==0),"actual SR reset getter");text<<"Reset="<<reset;
  const auto bytes=text.str();return Digest(bytes.data(),bytes.size());
 }
};
struct SrPreCreateCapability {
 NgxParams* params=nullptr;HMODULE core_owner=nullptr,callback_owner=nullptr;
 void* callback=nullptr;bool proven=false;
 void Query(HMODULE core,ID3D12Device* device){
  Check(core&&device&&parent_create_attempts==0&&nr_creates==0&&nr_evals==0,"real bootstrap precedes capability; no dummy parent or model");
  auto address=GetProcAddress(core,"NVSDK_NGX_D3D12_GetCapabilityParameters");
  Check(address&&GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(address),&core_owner)&&core_owner==core,"original Core capability export owner retained before parent Create");
  wchar_t path[32768]{};Check(GetModuleFileNameW(core_owner,path,32768)>0,"actual original Core owner path");
  const auto bytes=File(path);const auto sha=Digest(bytes.data(),bytes.size());
  Check(sha=="91d3742f0df3f2dd85141fa39ea257f7b9243c3e6755cd7fea894c721e7491aa","same original Core bytes as previously successful fixed tuple");
  std::printf("SR_PRECREATE_CORE_OWNER address=%p owner=%p device=%p sha256=%s parentCreate=0\n",reinterpret_cast<void*>(address),(void*)core_owner,(void*)device,sha.c_str());
  ScopedSrwExclusive lock(&g_nr_api_lock);nr_private_sr_core::Scope tls;
  const auto result=reinterpret_cast<SceneFactory::Capability>(address)(&params);
  std::printf("SR_PRECREATE_CAPABILITY result=%08x params=%p parentCreate=%u\n",result,(void*)params,parent_create_attempts);
  if(result!=kNgxSuccess||!params)Stop("precreate-capability","GetCapabilityParameters","actual-query-refused",result);
  int available=0,needs=0,initial=0,major=0,minor=0;
  const auto ar=SceneFactory::I(params,"SuperSampling.Available",available);
  const auto nr=SceneFactory::I(params,"SuperSampling.NeedsUpdatedDriver",needs);
  const auto ir=SceneFactory::I(params,"SuperSampling.FeatureInitResult",initial);
  SceneFactory::I(params,"SuperSampling.MinDriverVersionMajor",major);SceneFactory::I(params,"SuperSampling.MinDriverVersionMinor",minor);
  if(ar!=kNgxSuccess||available!=1||(nr==kNgxSuccess&&needs!=0)||(ir==kNgxSuccess&&(unsigned(initial)&0xFFF00000u)==0xBAD00000u))
   Stop("precreate-capability","SuperSampling","unsupported-or-explicit-init-error",ar);
  const auto cr=ParamSlot<GetVoidFn>(params,8)(params,"DLSSOptimalSettingsCallback",&callback);
  if(cr!=kNgxSuccess||!callback||!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(callback),&callback_owner))
   Stop("precreate-optimal","DLSSOptimalSettingsCallback","missing-or-unretained-official-callback",cr);
  Check(GetModuleFileNameW(callback_owner,path,32768)>0,"actual retained SR callback module path");
  const auto cb_bytes=File(path);const auto cb_sha=Digest(cb_bytes.data(),cb_bytes.size());
  const bool in_original=callback_owner==core_owner;
  Check(in_original?cb_sha==sha:(std::filesystem::equivalent(path,std::filesystem::current_path()/"nvngx_dlss.dll")&&cb_sha=="22c4660249da5b352cde0ce4d95178c3761223dd6ac096d0b88e5c03d7a5cdd1"),"official callback belongs to retained original Core or exact adjacent SR runtime");
  const auto utf8=std::filesystem::path(path).u8string();const std::string printable(utf8.begin(),utf8.end());
  std::printf("SR_PRECREATE_CALLBACK_OWNER address=%p module=%p path=%s sha256=%s retained=1\n",callback,(void*)callback_owner,printable.c_str(),cb_sha.c_str());
  SetUInt(params,"Width",ow);SetUInt(params,"Height",oh);SetInt(params,"PerfQualityValue",2);SetInt(params,"RTXValue",0);
  unsigned w=0,h=0;int quality=0,rtx=1;
  Check(SceneFactory::UI(params,"Width",w)==kNgxSuccess&&w==ow&&SceneFactory::UI(params,"Height",h)==kNgxSuccess&&h==oh&&SceneFactory::I(params,"PerfQualityValue",quality)==kNgxSuccess&&quality==2&&SceneFactory::I(params,"RTXValue",rtx)==kNgxSuccess&&rtx==0,"actual O/MaxQuality2 optimal input getters before parent");
  using Optimal=NgxResult(*)(NgxParams*);const auto optimal=reinterpret_cast<Optimal>(callback)(params);
  std::printf("SR_PRECREATE_OPTIMAL result=%08x target=%ux%u quality=2 parentCreate=%u\n",optimal,ow,oh,parent_create_attempts);
  if(optimal!=kNgxSuccess||SceneFactory::UI(params,"OutWidth",w)!=kNgxSuccess||SceneFactory::UI(params,"OutHeight",h)!=kNgxSuccess||w!=rw||h!=rh)
   Stop("precreate-optimal","OutWidth/OutHeight","fixed-R-not-exactly-supported-no-size-scan",optimal);
  for(auto kv:{std::pair{"DLSS.Get.Dynamic.Min.Render.Width",rw},std::pair{"DLSS.Get.Dynamic.Min.Render.Height",rh},std::pair{"DLSS.Get.Dynamic.Max.Render.Width",rw},std::pair{"DLSS.Get.Dynamic.Max.Render.Height",rh}}){
   unsigned v=0;const auto r=SceneFactory::UI(params,kv.first,v);const bool minimum=std::strstr(kv.first,"Min")!=nullptr;
   std::printf("SR_PRECREATE_RANGE key=%s result=%08x value=%u missingIsUnknown=1\n",kv.first,r,v);
   if(r==kNgxSuccess&&(!v||(minimum?v>kv.second:v<kv.second)))Stop("precreate-optimal",kv.first,"explicit-range-excludes-fixed-R",r);
  }
  proven=parent_create_attempts==0;Check(proven,"capability and exact optimal proof completed before sole parent Create");
  std::printf("SR_PRECREATE_SUPPORTED R=%ux%u O=%ux%u quality=2 exactOptimal=1 parentCreate=0 noDummyParent=1\n",rw,rh,ow,oh);
 }
 void Drop(nr_private_sr_core::Destroy destroy){Check(proven&&params&&core_owner&&callback_owner,"precreate owners retained until complete history");Check(destroy(params)==kNgxSuccess,"capability parameters destroyed under their retained original owner");params=nullptr;FreeLibrary(callback_owner);FreeLibrary(core_owner);callback_owner=core_owner=nullptr;}
};
struct SrOutput {
 ID3D12Resource* upload=nullptr;ID3D12Resource* readback=nullptr;D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes=0;
 static constexpr unsigned short poison=0xe4d2; // exact FP16 -1234
 void Make(queue_test::Fixture& f,const Inputs& owner){const auto d=ID3D12Resource_GetDesc(owner.clean.resource);
  Check(d.Width==ow&&d.Height==oh&&d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT&&d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&d.MipLevels==1&&d.DepthOrArraySize==1&&d.SampleDesc.Count==1&&(d.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),"actual distinct O-sized FP16 UAV GetDesc");
  ID3D12Device_GetCopyableFootprints(f.device,&d,0,1,0,&footprint,nullptr,nullptr,&bytes);if(scene_bench::Functional())upload=Buffer(f,bytes,D3D12_HEAP_TYPE_UPLOAD);readback=Buffer(f,bytes,D3D12_HEAP_TYPE_READBACK);
  if(upload){unsigned char* mapped=nullptr;Hr(ID3D12Resource_Map(upload,0,nullptr,(void**)&mapped),"O sentinel upload Map");
  for(unsigned y=0;y<oh;++y)for(unsigned x=0;x<ow*4;++x)std::memcpy(mapped+footprint.Offset+size_t(y)*footprint.Footprint.RowPitch+x*2,&poison,2);ID3D12Resource_Unmap(upload,0,nullptr);}
  std::printf("NATIVE_SR_OUTPUT_DESC resource=%p extent=%ux%u format=%u packedRowBytes=%u copyRowPitch=%u footprintOffset=%llu allocationBytes=%llu fullWriteSentinelEnabled=%u alphaSDKContract=unknown\n",(void*)owner.clean.resource,ow,oh,unsigned(d.Format),ow*8,footprint.Footprint.RowPitch,footprint.Offset,bytes,scene_bench::Functional());
 }
 void Poison(ID3D12GraphicsCommandList* list,Inputs& owner,unsigned frame){unsigned observed=0;
  {nr_source_runtime::Lock lock;Check(nr_source_runtime::StateOf(nr_source_runtime::RecordingOf(list),nr_source_runtime::ResourceOf(owner.clean.resource),observed),"actual current O state before per-frame sentinel");}
  Barrier(list,owner.clean.resource,static_cast<D3D12_RESOURCE_STATES>(observed),D3D12_RESOURCE_STATE_COPY_DEST);
  D3D12_TEXTURE_COPY_LOCATION a{},b{};a.pResource=upload;a.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;a.PlacedFootprint=footprint;b.pResource=owner.clean.resource;b.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  ID3D12GraphicsCommandList_CopyTextureRegion(list,&b,0,0,0,&a,nullptr);Barrier(list,owner.clean.resource,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  std::printf("NATIVE_SR_OUTPUT_POISON frame=%u resource=%p exactUploadCopy=1 wholePlane=1 RGB=e4d2 copyRowPitch=%u\n",frame,(void*)owner.clean.resource,footprint.Footprint.RowPitch);
 }
 void Record(ID3D12GraphicsCommandList* list,Inputs& owner,unsigned frame){unsigned observed=0;
  {nr_source_runtime::Lock lock;Check(nr_source_runtime::StateOf(nr_source_runtime::RecordingOf(list),nr_source_runtime::ResourceOf(owner.clean.resource),observed),"actual post-SR O state before readback");}
  Capture::Copy(list,owner.clean.resource,readback,footprint,static_cast<D3D12_RESOURCE_STATES>(observed));owner.completed_states[owner.clean.resource]=observed;
  std::printf("NATIVE_SR_OUTPUT_COPY frame=%u resource=%p readback=%p state=%u sameRecording=1\n",frame,(void*)owner.clean.resource,(void*)readback,observed);
 }
 void Save(unsigned frame){unsigned char* mapped=nullptr;Hr(ID3D12Resource_Map(readback,0,nullptr,(void**)&mapped),"SR readback only after exact fence plus Reset");std::vector<unsigned char> packed(size_t(ow)*oh*8);
  for(unsigned y=0;y<oh;++y)std::memcpy(packed.data()+size_t(y)*ow*8,mapped+footprint.Offset+size_t(y)*footprint.Footprint.RowPitch,size_t(ow)*8);ID3D12Resource_Unmap(readback,0,nullptr);
  size_t nonfinite=0,unwritten=0,alpha_poison=0;for(size_t i=0;i<size_t(ow)*oh;++i){for(unsigned c=0;c<4;++c){unsigned short h=0;std::memcpy(&h,packed.data()+i*8+c*2,2);if(c<3){nonfinite+=(h&0x7c00)==0x7c00;unwritten+=h==poison;}else alpha_poison+=h==poison;}}
  const auto name="native-sr-output-rgba16f-"+std::to_string(frame)+".bin";std::ofstream out(name,std::ios::binary);out.write((char*)packed.data(),packed.size());Check(bool(out),"complete O readback saved before validation");
  std::printf("NATIVE_SR_OUTPUT frame=%u file=%s sha256=%s bytes=%zu extent=%ux%u packedStride=%u format=R16G16B16A16_FLOAT finiteRGB=%zu nonfiniteRGB=%zu poisonRGB=%zu alphaPoison=%zu alphaGate=unknown\n",frame,name.c_str(),Digest(packed.data(),packed.size()).c_str(),packed.size(),ow,oh,ow*8,size_t(ow)*oh*3-nonfinite,nonfinite,unwritten,alpha_poison);
  Check(nonfinite==0&&(!scene_bench::Functional()||unwritten==0),"captured O RGB finite; per-frame sentinel overwrite gate only in functional mode");
  float minimum=std::numeric_limits<float>::infinity(),maximum=-minimum;size_t nonzero=0;
  for(size_t i=0;i<size_t(ow)*oh;++i)for(unsigned c=0;c<3;++c){unsigned short h=0;std::memcpy(&h,packed.data()+i*8+c*2,2);
   const unsigned e=(h>>10)&31,m=h&1023;const float sign=(h&0x8000)?-1.f:1.f;
   const float value=sign*(e?std::ldexp(1.f+float(m)/1024.f,int(e)-15):std::ldexp(float(m),-24));
   minimum=std::min(minimum,value);maximum=std::max(maximum,value);nonzero+=value!=0;}
  std::printf("NATIVE_SR_OUTPUT_RANGE frame=%u minRGB=%.9g maxRGB=%.9g nonzeroRGB=%zu spatialRange=%.9g alphaContract=unknown\n",frame,minimum,maximum,nonzero,maximum-minimum);
  Check(maximum>minimum&&nonzero>0,"actual SR output nonblack and spatially nonconstant");
 }
 void Drop(){ReleaseCom(upload);ReleaseCom(readback);}
};
