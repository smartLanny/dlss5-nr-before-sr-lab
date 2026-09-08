// Real consumer DLL startup failures, no GPU or NGX success stand-in.
#include "src/interop/nr_external_provider_producer.h"
#include <cstdio>
#include <cwchar>

extern "C" __declspec(dllexport) bool ReShadeRegisterAddon(void *, unsigned v) { return v == 18; }
extern "C" __declspec(dllexport) void ReShadeUnregisterAddon(void *) {}
extern "C" __declspec(dllexport) void ReShadeLogMessage(void *, int, const char *) {}
extern "C" __declspec(dllexport) const void *ReShadeGetImGuiFunctionTable(unsigned) {
    static const void *table[1024]{}; return table;
}
int wmain(int argc, wchar_t **argv) {
    if (argc != 3) return 1;
    // For failed mode, argv[2] names a DLL exporting no NGX lifecycle functions.
    const bool missing = wcscmp(argv[2], L"missing") == 0;
    if (!missing && !LoadLibraryW(argv[2])) return 2;
    HMODULE core = LoadLibraryW(argv[1]);
    if (!core) return 3;
    auto query = reinterpret_cast<nr_external_provider_client::QueryFn>(
        GetProcAddress(core, "NRExternalProvider_QueryV1"));
    NrExternalStatusV1 status{};
    const bool ready = nr_external_provider_client::WaitForNativeObserver(
        query, missing ? 25 : 2000, status);
    if (ready || status.reason != (missing ? NrExternalProviderReason::Starting
                                           : NrExternalProviderReason::ObserverFailed)) return 4;
    // Explicit native preflight does not break the independent NR-only export.
    if (!query(kNrExternalProviderAbi, sizeof(status), 0, &status)) return 5;
    std::printf("real Core startup %s rejected; NR-only query retained; no SR Create or GPU work\n",
                missing ? "missing-module/deadline" : "incomplete-exports/install-failure");
    return 0;
}
