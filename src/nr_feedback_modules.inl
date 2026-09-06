// Worker-only, four named components, no binary payloads and no exported local paths.
// Included inside nr_core_feedback after Handle is defined.
const char *ModulePathClass(const wchar_t *path, const CoreSession &session) {
    const auto folder = Beside(path, L"");
    if (!folder.empty() && (_wcsicmp(folder.c_str(), Beside(session.exe, L"").c_str()) == 0 ||
                           _wcsicmp(folder.c_str(), Beside(g_log_path, L"").c_str()) == 0)) return "game-or-addon-directory";
    wchar_t windows[MAX_PATH]{};
    const UINT length = GetWindowsDirectoryW(windows, MAX_PATH);
    if (length && length < MAX_PATH && _wcsnicmp(path, windows, length) == 0 && path[length] == L'\\') return "windows-driver-or-system";
    return "other-loaded-path";
}
std::string ModuleFileVersion(const wchar_t *path) {
    HMODULE library = LoadLibraryExW(L"version.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library) return "unavailable";
    using Size = DWORD (WINAPI *)(LPCWSTR, LPDWORD);
    using Read = BOOL (WINAPI *)(LPCWSTR, DWORD, DWORD, LPVOID);
    using Query = BOOL (WINAPI *)(LPCVOID, LPCWSTR, LPVOID *, PUINT);
    const auto size_fn = reinterpret_cast<Size>(GetProcAddress(library, "GetFileVersionInfoSizeW"));
    const auto read_fn = reinterpret_cast<Read>(GetProcAddress(library, "GetFileVersionInfoW"));
    const auto query_fn = reinterpret_cast<Query>(GetProcAddress(library, "VerQueryValueW"));
    std::string result = "unavailable";
    DWORD unused = 0;
    const DWORD bytes = size_fn ? size_fn(path, &unused) : 0;
    if (read_fn && query_fn && bytes > 0 && bytes <= 1024 * 1024) {
        auto data = std::unique_ptr<unsigned char[]>(new (std::nothrow) unsigned char[bytes]);
        void *raw = nullptr; UINT actual = 0;
        if (data && read_fn(path, 0, bytes, data.get()) && query_fn(data.get(), L"\\", &raw, &actual) && raw && actual >= 52) {
            // VS_FIXEDFILEINFO is thirteen DWORDs; copy, do not assume alignment.
            DWORD fixed[13]{}; std::memcpy(fixed, raw, sizeof(fixed));
            if (fixed[0] == 0xFEEF04BDu)
                result = std::to_string(HIWORD(fixed[2])) + "." + std::to_string(LOWORD(fixed[2])) + "." +
                    std::to_string(HIWORD(fixed[3])) + "." + std::to_string(LOWORD(fixed[3]));
        }
    }
    FreeLibrary(library);
    return result;
}
void FingerprintRuntimeFiles(FeedbackSession &s, const CoreSession &session) {
    constexpr const wchar_t *modules[] = {L"_nvngx.dll", L"nvngx_dlssnr.dll", L"nvngx_dlss.dll", L"nrchain_nvngx.dll"};
    const ULONGLONG deadline = GetTickCount64() + 6000;
    auto path = std::unique_ptr<wchar_t[]>(new (std::nothrow) wchar_t[1024]);
    auto buffer = std::unique_ptr<unsigned char[]>(new (std::nothrow) unsigned char[65536]);
    if (!path || !buffer) { FeedbackAppend(s, "runtimeFiles.status=allocation-unavailable\n"); return; }
    for (unsigned i = 0; i < 4; ++i) {
        char name[64]{}; WideToUtf8(modules[i], name, sizeof(name));
        HMODULE module = GetModuleHandleW(modules[i]);
        const DWORD length = module ? GetModuleFileNameW(module, path.get(), 1024) : 0;
        FeedbackAppend(s, "runtimeFile.%u.name=%s\nruntimeFile.%u.loaded=%d\n", i, name, i, module ? 1 : 0);
        if (!length || length >= 1024 || !nr_bundle::LocalAbsolutePath(path.get())) {
            FeedbackAppend(s, "runtimeFile.%u.status=%s\n", i, module ? "path-unavailable" : "not-loaded"); continue;
        }
        if (g_cancel.load() || GetTickCount64() >= deadline) { FeedbackAppend(s, "runtimeFile.%u.status=cancelled-or-time-budget\n", i); continue; }
        std::wstring native(path.get());
        if (native.compare(0, 4, L"\\\\?\\") != 0) native.insert(0, L"\\\\?\\");
        Handle file(CreateFileW(native.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
        BY_HANDLE_FILE_INFORMATION info{};
        if (file.value == INVALID_HANDLE_VALUE || GetFileType(file.value) != FILE_TYPE_DISK ||
            !GetFileInformationByHandle(file.value, &info) ||
            (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
            FeedbackAppend(s, "runtimeFile.%u.status=file-unavailable\n", i); continue;
        }
        const UINT64 size = (static_cast<UINT64>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
        const std::string version = ModuleFileVersion(path.get());
        FeedbackAppend(s, "runtimeFile.%u.version=%s\nruntimeFile.%u.source=%s\nruntimeFile.%u.bytes=%llu\n",
            i, version.c_str(), i, ModulePathClass(path.get(), session), i, size);
        if (size > nr_bundle::kHashLimit) { FeedbackAppend(s, "runtimeFile.%u.status=hash-size-budget\n", i); continue; }
        nr_bundle::Sha256 hash; UINT64 read = 0; bool okay = true;
        while (read < size && !g_cancel.load() && GetTickCount64() < deadline) {
            DWORD got = 0;
            if (!ReadFile(file.value, buffer.get(), static_cast<DWORD>(std::min<UINT64>(65536, size - read)), &got, nullptr) || !got) { okay = false; break; }
            hash.Update(buffer.get(), got); read += got;
        }
        char digest[65]{};
        if (okay && read == size) hash.Finish(digest);
        FeedbackAppend(s, "runtimeFile.%u.sha256=%s\nruntimeFile.%u.status=%s\n", i, digest, i,
            digest[0] ? "loaded-path-file-hashed" : "read-failed-or-time-budget");
    }
    FeedbackAppend(s, "runtimeFiles.hashMeaning=on-disk bytes at loaded path, not in-memory patch verification\n"
        "runtimeFiles.route=addon calls signed NR exports through nrchain; internal driver dispatch remains unobserved\n");
}
