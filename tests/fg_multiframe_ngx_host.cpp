// Runs the unchanged production-consumer host, then exports every individual
// public NGX multi-frame count/index result recorded by the separate SDK bridge.
#ifndef NR_MFG_FROZEN_HOST
#error Supply a generated copy of the frozen consumer host with only its entry point renamed.
#endif
#include NR_MFG_FROZEN_HOST
int wmain(int argc,wchar_t** argv){
    const int result=frozen_consumer_main(argc,argv);if(result)return result;
    const auto bridge=GetModuleHandleW(L"ngx-bootstrap.dll");
    using Export=unsigned(*)(const wchar_t*);const auto export_mfg=reinterpret_cast<Export>(GetProcAddress(bridge,"HostExportMfg"));
    return export_mfg&&export_mfg(argv[1])==1?0:1;
}
