#pragma once
// Test hosts only. CI pins and hashes the file before/after each GPU step; this
// helper proves the identity actually loaded by each native WARP device host.
// It never changes the product DLL search path or redistributes the runtime.
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <string>

namespace nr_test_warp {
namespace detail {
inline HRESULT Failure(const char* reason, DWORD error = ERROR_INVALID_DATA) {
    std::fprintf(stderr, "CI_WARP_FAILURE reason=%s win32=%lu\n", reason,
                 static_cast<unsigned long>(error));
    return HRESULT_FROM_WIN32(error ? error : ERROR_INVALID_DATA);
}

inline bool Present(const wchar_t* name) {
    SetLastError(ERROR_SUCCESS);
    const DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
    return length != 0 || GetLastError() != ERROR_ENVVAR_NOT_FOUND;
}

inline bool ReadEnvironment(const wchar_t* name, std::wstring& value) {
    const DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
    if (length < 2 || length > 32768) return false;
    value.assign(length, L'\0');
    const DWORD copied = GetEnvironmentVariableW(name, &value[0], length);
    if (copied == 0 || copied >= length) return false;
    value.resize(copied);
    return value.find_first_of(L"\r\n") == std::wstring::npos;
}

inline bool EqualPath(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
}

inline bool FullPath(const std::wstring& input, std::wstring& output) {
    // Reject rooted-but-drive-relative paths as well as bare relative paths.
    const bool drive = input.size() >= 3 &&
        ((input[0] >= L'A' && input[0] <= L'Z') ||
         (input[0] >= L'a' && input[0] <= L'z')) && input[1] == L':' &&
        (input[2] == L'\\' || input[2] == L'/');
    const bool unc = input.size() > 4 && input[0] == L'\\' && input[1] == L'\\';
    if ((!drive && !unc) || input.find_first_of(L"\r\n") != std::wstring::npos)
        return false;
    output.assign(32768, L'\0');
    const DWORD copied = GetFullPathNameW(input.c_str(),
        static_cast<DWORD>(output.size()), &output[0], nullptr);
    if (copied == 0 || copied >= output.size()) return false;
    output.resize(copied);
    for (auto& c : output) if (c == L'/') c = L'\\';
    return true;
}

inline bool ModulePath(HMODULE module, std::wstring& output) {
    std::wstring path(32768, L'\0');
    const DWORD copied = GetModuleFileNameW(module, &path[0],
                                          static_cast<DWORD>(path.size()));
    if (copied == 0 || copied >= path.size()) return false;
    path.resize(copied);
    return FullPath(path, output);
}

inline HRESULT InspectModules(const std::wstring& expected, bool require_one,
                              std::wstring& actual) {
    HANDLE snapshot = INVALID_HANDLE_VALUE;
    for (unsigned retry = 0; retry != 4; ++retry) {
        snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                            GetCurrentProcessId());
        if (snapshot != INVALID_HANDLE_VALUE || GetLastError() != ERROR_BAD_LENGTH)
            break;
    }
    if (snapshot == INVALID_HANDLE_VALUE) return Failure("module-snapshot", GetLastError());
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    unsigned count = 0;
    HRESULT result = S_OK;
    BOOL found = Module32FirstW(snapshot, &entry);
    while (found) {
        if (CompareStringOrdinal(entry.szModule, -1, L"d3d10warp.dll", -1, TRUE) == CSTR_EQUAL) {
            ++count;
            if (!ModulePath(entry.hModule, actual)) {
                result = Failure("module-path", GetLastError());
                break;
            }
            if (!EqualPath(actual, expected)) {
                result = Failure("unexpected-warp-module");
                break;
            }
        }
        found = Module32NextW(snapshot, &entry);
    }
    const DWORD enumeration_error = GetLastError();
    CloseHandle(snapshot);
    if (FAILED(result)) return result;
    if (enumeration_error != ERROR_NO_MORE_FILES)
        return Failure("module-enumeration", enumeration_error);
    if (count > 1 || (require_one && count != 1))
        return Failure("warp-module-count");
    return S_OK;
}

struct State {
    bool configured = false;
    HRESULT result = S_OK;
    std::wstring expected, evidence;
    std::string sha256;
    // Deliberately retained until process exit: do not unload under a device.
    HMODULE module = nullptr;
    volatile LONG ordinal = 0;

    State() noexcept {
        try { result = Initialize(); }
        catch (...) { result = Failure("configuration-allocation", ERROR_NOT_ENOUGH_MEMORY); }
    }

    HRESULT Initialize() {
        configured = Present(L"DLSS5_TEST_WARP_DLL") ||
            Present(L"DLSS5_TEST_WARP_SHA256") || Present(L"DLSS5_TEST_WARP_EVIDENCE_DIR");
        if (!configured) return S_OK;
        std::wstring dll, directory, hash;
        if (!ReadEnvironment(L"DLSS5_TEST_WARP_DLL", dll) ||
            !ReadEnvironment(L"DLSS5_TEST_WARP_SHA256", hash) ||
            !ReadEnvironment(L"DLSS5_TEST_WARP_EVIDENCE_DIR", directory))
            return Failure("incomplete-configuration");
        if (hash.size() != 64) return Failure("sha256-format");
        for (wchar_t c : hash) {
            if (c >= L'A' && c <= L'F') c += L'a' - L'A';
            if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f')))
                return Failure("sha256-format");
            sha256.push_back(static_cast<char>(c));
        }
        if (!FullPath(dll, expected) || !FullPath(directory, evidence))
            return Failure("absolute-path-required");
        const auto separator = expected.find_last_of(L'\\');
        if (separator == std::wstring::npos ||
            !EqualPath(expected.substr(separator + 1), L"d3d10warp.dll"))
            return Failure("warp-filename");
        const DWORD file_attributes = GetFileAttributesW(expected.c_str());
        const DWORD directory_attributes = GetFileAttributesW(evidence.c_str());
        if (file_attributes == INVALID_FILE_ATTRIBUTES ||
            (file_attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            return Failure("warp-file-missing");
        if (directory_attributes == INVALID_FILE_ATTRIBUTES ||
            (directory_attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
            return Failure("evidence-directory-missing");
        std::wstring actual;
        const HRESULT before = InspectModules(expected, false, actual);
        if (FAILED(before)) return before;
        module = LoadLibraryExW(expected.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) return Failure("absolute-load", GetLastError());
        if (!ModulePath(module, actual) || !EqualPath(actual, expected))
            return Failure("loaded-module-path");
        return InspectModules(expected, true, actual);
    }
};

inline State& Runtime() {
    static State state;
    return state;
}

inline bool Utf8(const std::wstring& text, std::string& output) {
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.c_str(),
        static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) return false;
    output.assign(static_cast<size_t>(length), '\0');
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.c_str(),
        static_cast<int>(text.size()), &output[0], length, nullptr, nullptr) == length;
}

inline HRESULT Evidence(State& state, const char* site, const std::wstring& actual) {
    size_t site_length = 0;
    if (site) for (; site_length < 97 && site[site_length] != '\0'; ++site_length) {
        const char c = site[site_length];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_'))
            return Failure("site-format");
    }
    if (site_length == 0 || site_length > 96) return Failure("site-format");
    std::wstring executable;
    std::string host_utf8, actual_utf8;
    if (!ModulePath(nullptr, executable) || !Utf8(executable, host_utf8) ||
        !Utf8(actual, actual_utf8)) return Failure("evidence-utf8-path");
    const DWORD pid = GetCurrentProcessId();
    const LONG ordinal = InterlockedIncrement(&state.ordinal);
    if (ordinal <= 0) return Failure("evidence-ordinal");
    const std::wstring path = state.evidence + L"\\warp-" + std::to_wstring(pid) +
        L"-" + std::to_wstring(ordinal) + L".txt";
    const std::string body = "schema=1\nhost_exe=" + host_utf8 + "\npid=" +
        std::to_string(pid) + "\nsite=" + site + "\nexpected_sha256=" + state.sha256 +
        "\nactual_module=" + actual_utf8 + "\nwarp_instances=1\nverified=true\n";
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return Failure("evidence-create", GetLastError());
    DWORD written = 0;
    const BOOL wrote = WriteFile(file, body.data(), static_cast<DWORD>(body.size()), &written, nullptr);
    DWORD error = GetLastError();
    bool ok = wrote && written == body.size();
    if (ok) { ok = FlushFileBuffers(file) != FALSE; error = GetLastError(); }
    if (!CloseHandle(file) && ok) { ok = false; error = GetLastError(); }
    if (!ok) {
        DeleteFileW(path.c_str());
        return Failure("evidence-write", error);
    }
    // Success is recorded only in the file. Existing hosts may have a strict
    // stdout format or PS5 stderr-as-error handling; preserve both contracts.
    return S_OK;
}
} // namespace detail

// The process configuration is captured once, including failure. Calling this
// also prepares the runtime; callers still must check Prepare's HRESULT.
inline bool Configured() { return detail::Runtime().configured; }
inline HRESULT Prepare() { return detail::Runtime().result; }
inline HRESULT Verify(const char* site) {
    auto& state = detail::Runtime();
    if (FAILED(state.result) || !state.configured) return state.result;
    try {
        std::wstring actual;
        const HRESULT result = detail::InspectModules(state.expected, true, actual);
        if (FAILED(result)) return result;
        return detail::Evidence(state, site, actual);
    } catch (...) { return detail::Failure("verification-allocation", ERROR_NOT_ENOUGH_MEMORY); }
}
} // namespace nr_test_warp
