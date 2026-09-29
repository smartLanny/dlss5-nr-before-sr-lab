// Deterministic continuous-frame quality evidence for the production RecordedTemporal consumer.
// Included by a frozen copy of recorded_temporal_probe.cpp; no shader formula is mirrored here.
#include <string>
#include <limits>
#include <cmath>
#include <cstdio>
namespace {
constexpr unsigned quality_w=192,quality_h=128;
void quality_save(const std::string& name,const std::vector<unsigned char>& data){
 FILE* f=std::fopen(name.c_str(),"wb");check(f!=nullptr,"quality evidence open");
 check(std::fwrite(data.data(),1,data.size(),f)==data.size(),"quality evidence write");
 std::fclose(f);
}
void quality_scene(Host& host,const char* scene,Mode mode,Scope scope,Backend backend,unsigned frames){
 const std::string label=std::string(scene)+(mode==Mode::Adaptive?"-adaptive":"-low")+(scope==Scope::LaterLayers?"-later":"-whole")+(backend==Backend::NativeMotion?"-native":"-amd");
 const unsigned scene_id=std::string(scene)=="static"?1:std::string(scene)=="pan"?2:std::string(scene)=="occlusion"?3:std::string(scene)=="cut"?4:std::string(scene)=="object"?5:std::string(scene)=="hud_reflection"?6:7;
 RecordedTemporal owner(host.device.Get(),host.queue.Get(),quality_w,quality_h,backend,scope,256ull<<20);
 check(owner.initialized(),owner.status());
 for(unsigned seq=1;seq<=frames;++seq){
  std::vector<std::uint16_t> original(quality_w*quality_h*4),candidate(original.size()),anchor(original.size());
  std::vector<float> mv(quality_w*quality_h*2,0),depth(quality_w*quality_h,.8f);
  double input_err=0,output_err=0,tail=0;unsigned noise_pixels=0,tail_pixels=0;
  for(unsigned y=0;y<quality_h;++y)for(unsigned x=0;x<quality_w;++x){
   const unsigned p=y*quality_w+x;
   const int shift=(scene_id==2||scene_id==6)?int(seq-1)*2:0;
   const bool foreground=(scene_id==3||scene_id==5)&&int(x)>=int(seq)*3+40&&int(x)<int(seq)*3+64&&y>=36&&y<92;
   const bool line=(int(x)-shift==96 || ((scene_id==3||scene_id==5)&&foreground&&x==unsigned(int(seq)*3+40)));
   float b=.28f+.001f*float((int(x)-shift+256)%32)+.0007f*float(y);
   if(line)b+=.28f;
   if(foreground){b=.72f;if(scene_id==3)depth[p]=.2f;}
   if(scene_id==4&&seq>=4)b=.78f-b*.5f;
   // Screen-space HUD remains fixed while the reflected patch changes with the source frame.
   if(scene_id==6&&x==20&&y>=20&&y<108)b=.9f;
   if(scene_id==6&&x>=132&&x<176&&y>=72&&y<112)b+=seq%2?.1f:-.1f;
   // True dark content: the clean target stays below 0.05 even on the line.
   // Flat, single-pixel line and low-amplitude edited patch have distinct ROIs.
   if(scene_id==7){
    b=.018f+.000025f*float((x+256)%32)+.000015f*float(y);
    if(x==96&&y>=24&&y<104)b+=.012f;
    if(x>=112&&x<152&&y>=40&&y<88)b+=.005f;
   }
   const float stable=scene_id==7?.006f:.06f;
   const int noise_code=int((x*73u+y*151u+seq*97u)%17u)-8;
   const float noise=float(noise_code)*(scene_id==7?.00075f:.0025f);
   const float z=b+stable+noise;
   for(unsigned c=0;c<3;++c){
    original[p*4+c]=half(b);anchor[p*4+c]=half(b+(scene_id==7?.002f:.018f));
    candidate[p*4+c]=half(z);
   }
   original[p*4+3]=anchor[p*4+3]=candidate[p*4+3]=half(1);
   if(scene_id==2&&seq>1)mv[p*2]=-2.f;
   if((scene_id==3||scene_id==5)&&seq>1&&foreground)mv[p*2]=-3.f;
  }
  auto b=upload_pixels(host,quality_w,quality_h,DXGI_FORMAT_R16G16B16A16_FLOAT,8,original.data());
  auto z=upload_pixels(host,quality_w,quality_h,DXGI_FORMAT_R16G16B16A16_FLOAT,8,candidate.data(),true);
  auto m=upload_pixels(host,quality_w,quality_h,DXGI_FORMAT_R32G32_FLOAT,8,mv.data());
  auto d=upload_pixels(host,quality_w,quality_h,DXGI_FORMAT_R32_FLOAT,4,depth.data());
  RecordedInputs in;in.settings.enabled=true;in.settings.mode=mode;in.settings.scope=scope;
  in.settings.validation=Validation::Basic065;in.layers=scope==Scope::LaterLayers?2:1;
  in.frame={{1000+scene_id,1,seq},{2000+scene_id,(3ull<<32)|1,kResidualSdrCodeValues,quality_w,quality_h,backend,scope},seq*16000ull,scene_id==4&&seq==4};
  in.original=b.Get();in.original_state=D3D12_RESOURCE_STATE_COPY_SOURCE;
  in.candidate=z.Get();in.candidate_format=DXGI_FORMAT_R16G16B16A16_FLOAT;in.candidate_state=D3D12_RESOURCE_STATE_COPY_SOURCE;
  in.native={in.frame.source,m.Get(),DXGI_FORMAT_R32G32_FLOAT,D3D12_RESOURCE_STATE_COPY_SOURCE,0,0,quality_w,quality_h,1,1,true};
  in.native.depth=d.Get();in.native.depth_state=D3D12_RESOURCE_STATE_COPY_SOURCE;
  in.native.depth_source=in.frame.source;in.native.depth_phase_proven=true;
  if(scope==Scope::LaterLayers){
   auto a=upload_pixels(host,quality_w,quality_h,DXGI_FORMAT_R16G16B16A16_FLOAT,8,anchor.data());
   host.begin();transition(host.list.Get(),owner.anchor(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);
   host.list->CopyResource(owner.anchor(),a.Get());
   transition(host.list.Get(),owner.anchor(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);host.execute();
   in.anchor_source=in.frame.source;
  }
  host.begin();check(owner.record(host.list.Get(),in)==RecordResult::Recorded,owner.status());
  host.execute();check(owner.submitted(host.done.Get(),host.value),"quality actual completion receipt");
  const auto raw_b=host.readback(b.Get(),8);
  const auto raw_out=host.readback(z.Get(),8);
  quality_save(label+"-"+std::to_string(seq)+"-B.f16",raw_b);
  // Candidate upload is preserved separately: the production consumer writes Z in place.
  std::vector<unsigned char> raw_input(candidate.size()*2);
  std::memcpy(raw_input.data(),candidate.data(),raw_input.size());
  quality_save(label+"-"+std::to_string(seq)+"-Z.f16",raw_input);
  quality_save(label+"-"+std::to_string(seq)+"-out.f16",raw_out);
  for(unsigned y=8;y<quality_h-8;++y)for(unsigned x=8;x<quality_w-8;++x){
   const unsigned p=y*quality_w+x;const float base=unhalf(raw_b.data()+p*8);
   const float current=unhalf(raw_input.data()+p*8);
   const float output=unhalf(raw_out.data()+p*8);
   const float clean=base+(scene_id==7?.006f:.06f);
   input_err+=std::abs(current-clean);output_err+=std::abs(output-clean);++noise_pixels;
   if((scene_id==3||scene_id==5)&&seq>1&&x>=unsigned(int(seq-1)*3+40)&&x<unsigned(int(seq)*3+40)&&y>=36&&y<92){
    tail+=std::max(0.f,output-current);++tail_pixels;
   }
  }
  const auto stats=owner.stats();
  check(stats.submitted==seq&&stats.gpu_frames==seq,"quality actual submitted and completed frame identity");
  check(stats.gpu_tested==stats.gpu_adopted+stats.gpu_rejected,"quality history decision accounting");
  std::printf("QUALITY scene=%s mode=%s scope=%s backend=%s frame=%u source=%u:1:%u config=%llu owner=%llu input_mae=%.8f output_mae=%.8f tail=%.8f tested=%llu adopted=%llu rejected=%llu depth_rejected=%llu seeded=%llu\n",
   scene,mode==Mode::Adaptive?"adaptive":"low",scope==Scope::LaterLayers?"later":"whole",backend==Backend::NativeMotion?"native":"amd",seq,1000+scene_id,seq,
   stats.gpu_configuration,stats.gpu_owner,input_err/noise_pixels,output_err/noise_pixels,tail_pixels?tail/tail_pixels:0.,
   stats.gpu_tested,stats.gpu_adopted,stats.gpu_rejected,stats.gpu_depth_rejected,stats.seeded);
 }
}
}
