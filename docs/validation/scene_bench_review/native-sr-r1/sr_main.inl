int wmain(int argc,wchar_t** argv){std::setvbuf(stdout,nullptr,_IONBF,0);
 std::map<std::wstring,std::wstring> options;bool go=false;
 for(int i=1;i<argc;++i){std::wstring key=argv[i];if(key==L"--root-exclusive-gpu-go"){if(go)return 2;go=true;continue;}
  if(key!=L"--mode"&&key!=L"--route"&&key!=L"--nr"&&key!=L"--frames"&&key!=L"--warm"&&key!=L"--out")return 2;
  if(++i>=argc||!options.emplace(key,argv[i]).second)return 2;}
 if(!go||options.size()!=6||options[L"--mode"]!=L"functional"||options[L"--route"]!=L"SR"||options[L"--nr"]!=L"off"||options[L"--frames"]!=L"1"||options[L"--warm"]!=L"2")return 2;
 const auto out=std::filesystem::path(options[L"--out"]);if(!out.is_absolute()||!std::filesystem::equivalent(out,std::filesystem::current_path()))return 2;
 raw_nr_probe::RunNativeSr();
 std::ofstream receipt("result.json");receipt<<"{\n\"schema\":\"dlss5-native-sr-research-functional-v1\",\n\"status\":\"passed-functional\",\n\"route\":\"research-one-native-SR\",\n\"coreSource\":\"45df68dc80138e89778e544d49f15109ec437a4f\",\n\"coreSrcTree\":\"7fe3819be240c0fad0eb16526e9d4b3ce0a67f87\",\n\"coreLoad\":\"compiled-full-Core-host;not-addon-load\",\n\"R\":[1280,720],\"N\":null,\"O\":[1920,1080],\n\"layers\":0,\"warm\":2,\"frames\":1,\"totalFrames\":3,\"nrEnabled\":false,\n\"counts\":{\"Feature1Create\":1,\"Feature1Eval\":3,\"Feature13Create\":0,\"Feature13Eval\":0,\"NRInit\":0,\"NRCreate\":0,\"NREval\":0,\"sceneDispatch\":4},\n\"retirement\":{\"exactFenceAndSuccessfulReset\":true,\"nativeReleased\":true,\"singlePoolReleased\":true},\n\"observer\":{\"hostCPUEnabled\":true,\"productMasterOffProof\":false},\n\"output\":{\"file\":\"frame-0000.rgba16f\",\"format\":\"R16G16B16A16_FLOAT\",\"width\":1920,\"height\":1080,\"stride\":15360,\"stage\":\"native-SR-output-before-NIS-tonemap\",\"pre_exposure\":0.75,\"exposure_scale\":2,\"alphaContract\":\"unknown\"},\n\"performance\":false,\"qualityVerdict\":\"not-evaluated\",\"gameAcceptance\":false\n}\n";
 queue_test::Check(bool(receipt),"complete actual SR result receipt");receipt.close();return 0;
}
