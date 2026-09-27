// Real GPU invariants for the production recorded shader, independent of its formula.
// Synthetic fixtures measure preservation/rejection; they do not establish game quality.
namespace {
void quality_invariants(Host& host){
 constexpr unsigned w=65,h=49;constexpr Id config=7,instance=42;
 for(auto mode:{Mode::Adaptive,Mode::LowFrequency})for(unsigned scenario=0;scenario<5;++scenario){
  RecordedTemporal owner(host.device.Get(),host.queue.Get(),w,h,Backend::NativeMotion,Scope::WholeChain,128ull<<20);
  check(owner.initialized(),owner.status());
  for(unsigned seq=1;seq<=3;++seq){
   std::vector<std::uint16_t> base(w*h*4),target(w*h*4);
   std::vector<float> mv(w*h*2,0),depth(w*h,.2f);
   for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){const unsigned p=y*w+x;
    float b=.4f,edit=.06f;
    if(scenario==1)edit=x==32?.12f:0; // Persistent one-pixel detail must not blur.
    if(scenario==2){b=x<32?.2f:.8f;mv[p*2]=seq>1?(x<32?40.f:-40.f):0;}
    if(scenario==3)edit=seq==1?.08f:-.08f; // Opposite signed edits cannot leave a positive trail.
    if(scenario==4){edit=seq==1?.12f:.02f;depth[p]=seq==1?.2f:.8f;}
    for(unsigned c=0;c<3;++c){base[p*4+c]=half(b);target[p*4+c]=half(stored(b)+edit);}
    base[p*4+3]=target[p*4+3]=half(1);
   }
   auto b=upload_pixels(host,w,h,DXGI_FORMAT_R16G16B16A16_FLOAT,8,base.data());
   auto z=upload_pixels(host,w,h,DXGI_FORMAT_R16G16B16A16_FLOAT,8,target.data(),true);
   auto motion=upload_pixels(host,w,h,DXGI_FORMAT_R32G32_FLOAT,8,mv.data());
   auto d=upload_pixels(host,w,h,DXGI_FORMAT_R32_FLOAT,4,depth.data());
   RecordedInputs in;in.settings.enabled=true;in.settings.validation=Validation::Basic065;in.settings.mode=mode;
   in.frame={{101+scenario,1,seq},{instance,(config<<32)|1,kResidualSdrCodeValues,w,h,Backend::NativeMotion},seq*16000ull,false};
   in.original=b.Get();in.original_state=D3D12_RESOURCE_STATE_COPY_SOURCE;
   in.candidate=z.Get();in.candidate_format=DXGI_FORMAT_R16G16B16A16_FLOAT;in.candidate_state=D3D12_RESOURCE_STATE_COPY_SOURCE;
   in.native={in.frame.source,motion.Get(),DXGI_FORMAT_R32G32_FLOAT,D3D12_RESOURCE_STATE_COPY_SOURCE,0,0,w,h,1,1,true};
   in.native.depth=d.Get();in.native.depth_state=D3D12_RESOURCE_STATE_COPY_SOURCE;
   in.native.depth_source=in.frame.source;in.native.depth_phase_proven=true;
   host.begin();
   if(seq==1){auto wrong=in;wrong.native.depth_source.sequence++;
    check(owner.record(host.list.Get(),wrong)==RecordResult::Unavailable,"QUALITY_DEPTH_SOURCE_MISMATCH_ORACLE");}
   check(owner.record(host.list.Get(),in)==RecordResult::Recorded,owner.status());
   host.execute();check(owner.submitted(host.done.Get(),host.value),"quality actual Execute Signal");
   const auto output=host.readback(z.Get(),8),original=host.readback(b.Get(),8);
   check(std::memcmp(original.data(),base.data(),original.size())==0,"QUALITY_ORIGINAL_B_UNCHANGED_ORACLE");
   for(unsigned p=0;p<w*h;++p)for(unsigned c=0;c<3;++c){
    const float value=unhalf(output.data()+p*8+c*2),expected=unhalf(reinterpret_cast<unsigned char*>(&target[p*4+c]));
    check(std::isfinite(value),"QUALITY_FINITE_ORACLE");
    if(scenario==3&&seq>=2){const float current_base=unhalf(reinterpret_cast<unsigned char*>(&base[p*4+c]));
     check(value-current_base<-.06f,"QUALITY_OPPOSITE_SIGN_NO_TRAIL_ORACLE");}
    else check(std::abs(value-expected)<.001f,scenario==1?"QUALITY_FINE_LINE_PRESERVATION_ORACLE":scenario==2?"QUALITY_COLOR_EDGE_REJECTION_ORACLE":scenario==4?"QUALITY_DEPTH_REJECTION_ORACLE":"QUALITY_UNIFORM_EDIT_PRESERVATION_ORACLE");
   }
   const auto stats=owner.stats();
   check(stats.gpu_frames==seq&&stats.gpu_configuration==config&&stats.gpu_owner==instance,"QUALITY_GPU_COMPLETION_IDENTITY_ORACLE");
   check(stats.gpu_warmup==((w+7)/8)*((h+7)/8),"QUALITY_GPU_WARMUP_SAMPLE_ORACLE");
   check(stats.gpu_tested==stats.gpu_adopted+stats.gpu_rejected,"QUALITY_GPU_DECISION_ACCOUNTING_ORACLE");
   if(seq==2&&scenario==0)check(stats.gpu_adopted>0,"QUALITY_REAL_HISTORY_ADOPTED_ORACLE");
   if(seq==2&&scenario==2)check(stats.gpu_adopted==0&&stats.gpu_rejected>0,"QUALITY_EDGE_HISTORY_REJECTED_ORACLE");
   if(seq==2&&scenario==4)check(stats.gpu_adopted==0&&stats.gpu_depth_rejected==stats.gpu_tested&&stats.gpu_tested>0,"QUALITY_DEPTH_STATS_ORACLE");
   if(seq==3)std::printf("PASS quality mode=%u scenario=%u frames=%llu sampled_tested=%llu adopted=%llu rejected=%llu warmup=%llu depthRejected=%llu config=%llu owner=%llu\n",unsigned(mode),scenario,stats.gpu_frames,stats.gpu_tested,stats.gpu_adopted,stats.gpu_rejected,stats.gpu_warmup,stats.gpu_depth_rejected,stats.gpu_configuration,stats.gpu_owner);
  }
 }
}
}
