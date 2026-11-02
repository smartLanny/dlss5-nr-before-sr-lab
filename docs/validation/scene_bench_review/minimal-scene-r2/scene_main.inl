int wmain(int argc,wchar_t** argv){std::setvbuf(stdout,nullptr,_IONBF,0);
 std::map<std::wstring,std::wstring> options;bool go=false;
 for(int i=1;i<argc;++i){std::wstring key=argv[i];if(key==L"--root-exclusive-gpu-go"){if(go)return 2;go=true;continue;}
  if(key!=L"--mode"&&key!=L"--route"&&key!=L"--nr"&&key!=L"--frames"&&key!=L"--warm"&&key!=L"--out")return 2;
  if(++i>=argc||!options.emplace(key,argv[i]).second)return 2;}
 if(!go||options.size()!=6||options[L"--route"]!=L"RR"||(options[L"--nr"]!=L"off"&&options[L"--nr"]!=L"on"))return 2;
 const auto selected=options[L"--mode"];
 if(selected==L"functional")scene_bench::mode=scene_bench::Mode::Functional;
 else if(selected==L"capture")scene_bench::mode=scene_bench::Mode::Capture;
 else if(selected==L"perf")scene_bench::mode=scene_bench::Mode::Perf;else return 2;
 auto count=[](const std::wstring& text,unsigned& value){if(text.empty()||text.size()>3)return false;value=0;for(auto c:text){if(c<L'0'||c>L'9')return false;value=value*10+unsigned(c-L'0');}return true;};
 if(!count(options[L"--warm"],scene_bench::warm)||!count(options[L"--frames"],scene_bench::observed)||scene_bench::warm<2||scene_bench::warm>120||!scene_bench::observed||scene_bench::observed>120)return 2;
 if(scene_bench::Functional()&&(scene_bench::warm!=2||scene_bench::observed!=1))return 2;
 const auto out=std::filesystem::path(options[L"--out"]);if(!out.is_absolute()||!std::filesystem::equivalent(out,std::filesystem::current_path()))return 2;
 const bool nr_on=options[L"--nr"]==L"on";
 raw_nr_probe::RunOneRr(nr_on?"variant":"baseline",L"nrchain_nvngx.dll",L"runtime.sha256");
 if(scene_bench::Functional())std::filesystem::copy_file("native-rr-output-rgba16f-2.bin","frame-0000.rgba16f",std::filesystem::copy_options::none);
 queue_test::Check(!scene_bench::active_measurement,"no pending timestamp flight after all real retirement");
 if(!scene_bench::Functional())queue_test::Check(scene_bench::measurements.Complete(scene_bench::Total()),"one exact completed timestamp pair for every actual frame");
 queue_test::Check(scene_bench::capture_files.size()==(scene_bench::Capture()?scene_bench::observed:0u),"all and only observed capture frames; no perf capture");
 std::ostringstream runtime_identity;runtime_identity<<'{';bool first_runtime=true;
 for(const wchar_t* name:{L"nrchain_nvngx.dll",L"nvngx_dlss.dll",L"nvngx_dlssd.dll",L"nvngx_dlssnr.dll"}){
  if(!nr_on&&std::wstring(name)==L"nvngx_dlssnr.dll")continue;
  const auto bytes=raw_nr_probe::File(name);const std::wstring wide=name;const std::string short_name(wide.begin(),wide.end());
  if(!first_runtime)runtime_identity<<',';first_runtime=false;runtime_identity<<'"'<<short_name<<"\":\""<<rr_identity_native_test::Digest(bytes.data(),bytes.size())<<'"';
 }runtime_identity<<'}';
 std::ostringstream files;files<<'[';for(size_t i=0;i<scene_bench::capture_files.size();++i){if(i)files<<',';files<<'"'<<scene_bench::capture_files[i]<<'"';}files<<']';
 const char* mode=scene_bench::Functional()?"functional":scene_bench::Capture()?"capture":"perf";
 std::ostringstream result;result<<"{\n"
  "  \"schema\":\"dlss5-scene-functional-v1\",\n"
  "  \"status\":\"passed-"<<mode<<"\",\n"
  "  \"route\":\"research-NR-then-one-native-RR\",\n"
  "  \"coreSource\":\"45df68dc80138e89778e544d49f15109ec437a4f\",\n"
  "  \"coreSrcTree\":\"7fe3819be240c0fad0eb16526e9d4b3ce0a67f87\",\n"
  "  \"coreLoad\":\"compiled-full-Core-host;not-addon-load\",\n"
  "  \"R\":[1280,720],\"N\":[1280,720],\"O\":[1920,1080],\n"
  "  \"layers\":1,\"warm\":"<<scene_bench::warm<<",\"frames\":"<<scene_bench::observed<<",\"totalFrames\":"<<scene_bench::Total()<<",\n"
  "  \"nrEnabled\":"<<(nr_on?"true":"false")<<",\n"
  "  \"runtimes\":"<<runtime_identity.str()<<",\n"
  "  \"counts\":{\"nativeCreate\":"<<scene_rr_probe_test::parent_create_attempts<<",\"nativeEval\":"<<raw_nr_probe::native_rr_evals<<",\"nrCreate\":"<<raw_nr_probe::nr_creates<<",\"nrEval\":"<<raw_nr_probe::nr_evals<<",\"actualCombines\":"<<raw_nr_probe::probe_combines<<"},\n"
  "  \"retirement\":{\"exactFenceAndSuccessfulReset\":true,\"nativeReleased\":"<<(raw_nr_probe::parent_release_calls==1?"true":"false")<<",\"nrContextDestroyed\":"<<(raw_nr_probe::context_destroy_calls==1?"true":"false")<<"},\n"
  "  \"output\":{\"file\":"<<(scene_bench::Perf()?"null":"\"frame-0000.rgba16f\"")<<",\"format\":\"R16G16B16A16_FLOAT\",\"width\":1920,\"height\":1080,\"stride\":15360,\"bytes\":16588800,\"colorSpace\":\"stored-scene-linear-HDR;preExposure.75/exposureScale2\",\"stage\":\"native-RR-output-before-NIS-tonemap\",\"alphaContract\":\"unknown\"},\n"
  "  \"capture\":{\"enabled\":"<<(scene_bench::Capture()?"true":"false")<<",\"width\":1920,\"height\":1080,\"stride\":15360,\"format\":\"R16G16B16A16_FLOAT\",\"files\":"<<files.str()<<",\"pre_exposure\":0.75,\"exposure_scale\":2,\"simulation_fps\":60},\n"
  "  \"measurement\":{\"performance\":"<<(scene_bench::Perf()?"true":"false")<<",\"gpuTimestamps\":"<<(scene_bench::Functional()?"null":"true")<<",\"processMemory\":"<<(scene_bench::Functional()?"null":"true")<<",\"serial_wall_scope\":\"CPU recording, host validation/logging, actual submission and exact completion/Reset; capture mode includes queued image copy; not Present FPS\"},\n"
  "  \"qualityVerdict\":\"not-evaluated\",\"gameAcceptance\":false";
 FILE* file=std::fopen("result.json","wb");queue_test::Check(file!=nullptr,"result output opened");const auto text=result.str();queue_test::Check(std::fwrite(text.data(),1,text.size(),file)==text.size(),"complete identity and adoption fields");
 if(!scene_bench::Functional()){queue_test::Check(std::fputs(",\n",file)>=0&&scene_bench::measurements.WriteFields(file,scene_bench::Total()),"complete actual timing and process memory fields");}
 queue_test::Check(std::fputs("\n}\n",file)>=0&&std::fclose(file)==0,"result closed");scene_bench::measurements.Drop();return 0;
}
