// Actual NGX parameter-object check, not a typed mock or visual-mask test.
// Reuse the fixed SDK/device bootstrap; never create NR/SR or submit commands.
#define wmain unused_direct_sr_wmain
#include <direct_sr_gpu.cpp>
#undef wmain

int wmain(int argc, wchar_t **argv) {
    if (argc != 2) return 1; // Explicit private runtime/data directory.
    auto ctx = MakeDevice(false);
    const wchar_t *paths[] = {argv[1]};
    NVSDK_NGX_FeatureCommonInfo common{};
    common.PathListInfo.Path = paths; common.PathListInfo.Length = 1;
    const auto initialized = NVSDK_NGX_D3D12_Init_with_ProjectID(
        "7a94da60-f8c9-4b8b-9d8a-22f0293c4801", NVSDK_NGX_ENGINE_TYPE_CUSTOM,
        "parameter-type-readonly", argv[1], ctx.device.Get(), &common, NVSDK_NGX_Version_API);
    if (NVSDK_NGX_FAILED(initialized)) return 2;
    NVSDK_NGX_Parameter *params = nullptr;
    const auto allocated = NVSDK_NGX_D3D12_AllocateParameters(&params);
    if (NVSDK_NGX_FAILED(allocated) || !params) {
        NVSDK_NGX_D3D12_Shutdown1(ctx.device.Get());
        return 3;
    }
    bool passed = true;
    unsigned cases = 0;
    for (const char *key : {"DLSSNR.UseAutoMask", "DLSSNR.UICorrection"})
    for (int value : {0, 1}) for (bool unsigned_writer : {false, true}) {
        params->Reset();
        if (unsigned_writer) params->Set(key, static_cast<unsigned>(value));
        else params->Set(key, value);
        int signed_value = -123; unsigned unsigned_value = 999;
        const auto signed_result = params->Get(key, &signed_value);
        const auto unsigned_result = params->Get(key, &unsigned_value);
        const bool okay = !NVSDK_NGX_FAILED(signed_result) &&
            !NVSDK_NGX_FAILED(unsigned_result) && signed_value == value &&
            unsigned_value == static_cast<unsigned>(value);
        passed &= okay; ++cases;
        std::cout << key << " set=" << (unsigned_writer ? "uint" : "int")
                  << ':' << value << " int=" << signed_value << " uint=" << unsigned_value
                  << " verified=" << okay << '\n';
    }
    passed &= !NVSDK_NGX_FAILED(NVSDK_NGX_D3D12_DestroyParameters(params));
    passed &= !NVSDK_NGX_FAILED(NVSDK_NGX_D3D12_Shutdown1(ctx.device.Get()));
    std::cout << "parameter_roundtrip=" << (passed ? "passed" : "failed")
              << " cases=" << cases << " NR=0 SR=0 application_gpu_submissions=0\n";
    return passed ? 0 : 4;
}
