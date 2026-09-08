// CPU-only load/registration probe. Creates no graphics device or game window.
#include <windows.h>
#include <cstdio>
using Init = bool (*)(HMODULE, HMODULE);
using Uninit = void (*)(HMODULE, HMODULE);
int wmain(int argc, wchar_t **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc != 3) return 2;
    HMODULE reshade = LoadLibraryExW(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!reshade) { std::printf("ReShade LoadLibrary failed: %lu\n", GetLastError()); return 3; }
    HMODULE addon = LoadLibraryExW(argv[2], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!addon) { std::printf("Luma LoadLibrary failed: %lu\n", GetLastError()); return 4; }
    auto init = reinterpret_cast<Init>(GetProcAddress(addon, "AddonInit"));
    auto uninit = reinterpret_cast<Uninit>(GetProcAddress(addon, "AddonUninit"));
    auto name = reinterpret_cast<const char *const *>(GetProcAddress(addon, "NAME"));
    if (!init || !uninit || !name || !*name) return 5;
    if (!init(addon, reshade)) return 6;
    std::printf("PASS Luma AddonInit returned; NAME=%s; no device/GPU/game created\n", *name);
    uninit(addon, reshade);
    FreeLibrary(addon);
    FreeLibrary(reshade);
    std::puts("PASS Luma unload returned");
    return 0;
}
