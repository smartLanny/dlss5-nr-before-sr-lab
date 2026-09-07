#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// A controlled marker makes entering DllMain independently observable.
BOOL WINAPI DllMain(HMODULE, DWORD reason, void *) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    wchar_t path[32768];
    DWORD n = GetEnvironmentVariableW(L"NR_PRELOAD_SENTINEL", path, _countof(path));
    if (!n || n >= _countof(path)) return FALSE;
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    DWORD bytes = 0;
    BOOL ok = WriteFile(file, "DllMain entered\n", 16, &bytes, nullptr);
    CloseHandle(file);
    return ok && bytes == 16;
}
