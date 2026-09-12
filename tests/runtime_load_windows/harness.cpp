#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cwchar>

#include "nr_runtime_load_evidence.inl"

int wmain(int argc, wchar_t **argv) {
    if (argc != 3) {
        std::fwprintf(stderr, L"usage: runtime-load-windows.exe <dll> <expected-class>\n");
        return 2;
    }
    const unsigned expected = static_cast<unsigned>(std::wcstoul(argv[2], nullptr, 10));
    const bool present = RuntimeRegularFile(argv[1]);
    HMODULE module = nullptr;
    DWORD error = ERROR_SUCCESS;
    const bool loaded = RuntimeLoadRestricted(argv[1], &module, &error);
    const unsigned actual = RuntimeClassifyLoad(present, loaded, error);
    std::printf("runtime-load-windows: present=%d loaded=%d win32=%lu class=%s\n",
        present ? 1 : 0, loaded ? 1 : 0, static_cast<unsigned long>(error),
        RuntimeLoadClassName(actual));
    if (module != nullptr) FreeLibrary(module);
    if (actual != expected) return 3;
    if (expected == 1 && error != ERROR_SUCCESS) return 4;
    if ((expected == 2 || expected == 3) && error != ERROR_MOD_NOT_FOUND) return 5;
    return 0;
}
