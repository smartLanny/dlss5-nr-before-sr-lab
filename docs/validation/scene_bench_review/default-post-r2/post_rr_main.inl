int wmain(int argc,wchar_t** argv){
 std::setvbuf(stdout,nullptr,_IONBF,0);std::map<std::wstring,std::wstring> options;bool go=false;
 for(int i=1;i<argc;++i){const std::wstring key=argv[i];if(key==L"--root-exclusive-gpu-go"){if(go)return 2;go=true;continue;}
  if(key!=L"--mode"&&key!=L"--route"&&key!=L"--nr"&&key!=L"--frames"&&key!=L"--warm"&&key!=L"--out")return 2;
  if(++i>=argc||!options.emplace(key,argv[i]).second)return 2;
 }
 if(!go||options.size()!=6||options[L"--mode"]!=L"functional"||options[L"--route"]!=L"RR-post-production"||(options[L"--nr"]!=L"on"&&options[L"--nr"]!=L"off")||options[L"--frames"]!=L"1"||options[L"--warm"]!=L"2")return 2;
 const auto out=std::filesystem::path(options[L"--out"]);if(!out.is_absolute()||!std::filesystem::equivalent(out,std::filesystem::current_path()))return 2;
 scene_bench::mode=scene_bench::Mode::Functional;scene_bench::warm=2;scene_bench::observed=1;
 raw_nr_probe::RunDefaultPost(options[L"--nr"]==L"on");return 0;
}
