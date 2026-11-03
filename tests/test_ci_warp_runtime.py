"""Execute the actual test-runtime header with explicit Win32 API doubles.

These are CPU control-flow/receipt tests, not evidence of the real Windows
loader, D3D12, or WARP. The hosted native probe supplies that separate evidence.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CREATED = str((1 << 60) + 12345)  # Adjacent values must survive beyond double precision.
WIN32 = r'''
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <codecvt>
#include <filesystem>
#include <locale>
#include <map>
#include <string>
#include <vector>
using DWORD = std::uint32_t; using LONG = std::int32_t; using HRESULT = std::int32_t;
using ULONGLONG = std::uint64_t;
using BOOL = int; using HANDLE = void*; using HMODULE = void*;
struct FILETIME { DWORD dwLowDateTime, dwHighDateTime; };
#define TRUE 1
#define FALSE 0
#define S_OK 0
#define FAILED(x) ((x) < 0)
#define SUCCEEDED(x) ((x) >= 0)
#define HRESULT_FROM_WIN32(x) static_cast<HRESULT>(0x80070000u | (x))
#define INVALID_HANDLE_VALUE reinterpret_cast<HANDLE>(std::intptr_t(-1))
#define ERROR_SUCCESS 0u
#define ERROR_FILE_NOT_FOUND 2u
#define ERROR_ACCESS_DENIED 5u
#define ERROR_NOT_ENOUGH_MEMORY 8u
#define ERROR_INVALID_DATA 13u
#define ERROR_NO_MORE_FILES 18u
#define ERROR_BAD_LENGTH 24u
#define ERROR_ENVVAR_NOT_FOUND 203u
#define ERROR_ALREADY_EXISTS 183u
#define INVALID_FILE_ATTRIBUTES 0xffffffffu
#define FILE_ATTRIBUTE_DIRECTORY 0x10u
#define FILE_ATTRIBUTE_NORMAL 0x80u
#define GENERIC_WRITE 0x40000000u
#define FILE_SHARE_READ 1u
#define CREATE_NEW 1u
#define CP_UTF8 65001u
#define WC_ERR_INVALID_CHARS 0x80u
#define CSTR_EQUAL 2
#define LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR 0x100u
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x800u
#define TH32CS_SNAPMODULE 0x8u
#define TH32CS_SNAPMODULE32 0x10u
struct MODULEENTRY32W { DWORD dwSize; HMODULE hModule; wchar_t szModule[256]; };
namespace fake {
inline DWORD error = 0;
inline std::map<std::wstring, std::wstring> env;
inline const std::wstring expected = L"C:\\pinned\\模型\\d3d10warp.dll";
inline const std::wstring evidence = L"C:\\evidence";
inline const std::wstring system = L"C:\\Windows\\System32\\d3d10warp.dll";
inline std::vector<std::wstring> modules;
inline std::string scenario, output;
inline unsigned index = 0, loads = 0;
inline unsigned process_times_calls = 0;
inline ULONGLONG created = (1ull << 60) + 12345;
inline bool snapshot_fail = false, path_fail = false;
inline HANDLE snapshot = reinterpret_cast<HANDLE>(std::intptr_t(-2));
inline std::string utf8(const std::wstring& text) {
    return std::wstring_convert<std::codecvt_utf8<wchar_t>>().to_bytes(text);
}
inline void entry(MODULEENTRY32W* row) {
    row->hModule = reinterpret_cast<HMODULE>(std::uintptr_t(index + 1));
    std::wcscpy(row->szModule, L"d3d10warp.dll");
}
}
inline void SetLastError(DWORD error) { fake::error = error; }
inline DWORD GetLastError() { return fake::error; }
inline DWORD GetEnvironmentVariableW(const wchar_t* name, wchar_t* data, DWORD capacity) {
    const auto it = fake::env.find(name);
    if (it == fake::env.end()) { SetLastError(ERROR_ENVVAR_NOT_FOUND); return 0; }
    if (it->second.empty()) { SetLastError(ERROR_SUCCESS); return 0; }
    const DWORD length = static_cast<DWORD>(it->second.size());
    if (capacity <= length) return length + 1;
    std::wcscpy(data, it->second.c_str()); return length;
}
inline int CompareStringOrdinal(const wchar_t* a, int, const wchar_t* b, int, BOOL) {
    std::wstring x(a), y(b);
    for (auto& c : x) c = std::towlower(c);
    for (auto& c : y) c = std::towlower(c);
    return x == y ? CSTR_EQUAL : 1;
}
inline DWORD GetFullPathNameW(const wchar_t* name, DWORD capacity, wchar_t* data, void*) {
    std::wstring path(name); for (auto& c : path) if (c == L'/') c = L'\\';
    if (capacity <= path.size()) return static_cast<DWORD>(path.size() + 1);
    std::wcscpy(data, path.c_str()); return static_cast<DWORD>(path.size());
}
inline DWORD GetModuleFileNameW(HMODULE module, wchar_t* data, DWORD capacity) {
    if (fake::path_fail) { SetLastError(ERROR_ACCESS_DENIED); return 0; }
    std::wstring path = module ? fake::modules.at(reinterpret_cast<std::uintptr_t>(module) - 1) : L"C:\\host\\probe.exe";
    if (capacity <= path.size()) return capacity;
    std::wcscpy(data, path.c_str()); return static_cast<DWORD>(path.size());
}
inline DWORD GetFileAttributesW(const wchar_t* name) {
    if (std::wstring(name) == fake::expected && fake::scenario != "missing-file") return FILE_ATTRIBUTE_NORMAL;
    if (std::wstring(name) == fake::evidence && fake::scenario != "missing-directory") return FILE_ATTRIBUTE_DIRECTORY;
    return INVALID_FILE_ATTRIBUTES;
}
inline DWORD GetCurrentProcessId() { return 1234; }
inline HANDLE GetCurrentProcess() { return reinterpret_cast<HANDLE>(std::intptr_t(-1)); }
inline BOOL GetProcessTimes(HANDLE process, FILETIME* created, FILETIME* exited,
                           FILETIME* kernel, FILETIME* user) {
    if (process != GetCurrentProcess()) std::exit(97);
    ++fake::process_times_calls;
    if (fake::scenario == "process-times-failure") {
        SetLastError(ERROR_ACCESS_DENIED); return FALSE;
    }
    const ULONGLONG value = fake::scenario == "zero-created" ? 0 : fake::created;
    *created = {static_cast<DWORD>(value), static_cast<DWORD>(value >> 32)};
    *exited = {}; *kernel = {}; *user = {};
    return TRUE;
}
inline HANDLE CreateToolhelp32Snapshot(DWORD flags, DWORD pid) {
    if (flags != (TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32) || pid != 1234) std::exit(90);
    if (fake::snapshot_fail) { SetLastError(ERROR_ACCESS_DENIED); return INVALID_HANDLE_VALUE; }
    return fake::snapshot;
}
inline BOOL Module32FirstW(HANDLE, MODULEENTRY32W* row) {
    if (row->dwSize != sizeof(*row)) std::exit(91);
    if (fake::scenario == "enumeration-failure") { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    fake::index = 0;
    if (fake::modules.empty()) { SetLastError(ERROR_NO_MORE_FILES); return FALSE; }
    fake::entry(row); return TRUE;
}
inline BOOL Module32NextW(HANDLE, MODULEENTRY32W* row) {
    if (++fake::index >= fake::modules.size()) { SetLastError(ERROR_NO_MORE_FILES); return FALSE; }
    fake::entry(row); return TRUE;
}
inline HMODULE LoadLibraryExW(const wchar_t* path, HANDLE file, DWORD flags) {
    ++fake::loads;
    if (std::wstring(path) != fake::expected || file ||
        flags != (LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)) std::exit(92);
    if (fake::scenario == "load-failure") { SetLastError(ERROR_ACCESS_DENIED); return nullptr; }
    const std::wstring actual = fake::scenario == "load-wrong" ? fake::system : fake::expected;
    for (size_t i = 0; i < fake::modules.size(); ++i)
        if (fake::modules[i] == actual) return reinterpret_cast<HMODULE>(i + 1);
    fake::modules.push_back(actual); return reinterpret_cast<HMODULE>(fake::modules.size());
}
inline int WideCharToMultiByte(unsigned code, DWORD flags, const wchar_t* data, int count,
                               char* result, int capacity, void*, void*) {
    if (code != CP_UTF8 || flags != WC_ERR_INVALID_CHARS) std::exit(93);
    const auto text = fake::utf8(std::wstring(data, static_cast<size_t>(count)));
    if (capacity) { if (static_cast<size_t>(capacity) < text.size()) return 0; std::copy(text.begin(), text.end(), result); }
    return static_cast<int>(text.size());
}
inline LONG InterlockedIncrement(volatile LONG* value) { return ++*value; }
inline std::filesystem::path EvidencePath(const wchar_t* value) {
    std::wstring path(value); return std::filesystem::path(fake::output) / fake::utf8(path.substr(path.find_last_of(L'\\') + 1));
}
inline HANDLE CreateFileW(const wchar_t* path, DWORD access, DWORD share, void*, DWORD creation, DWORD flags, HANDLE) {
    if (access != GENERIC_WRITE || share != FILE_SHARE_READ || creation != CREATE_NEW || flags != FILE_ATTRIBUTE_NORMAL) std::exit(94);
    if (fake::scenario == "create-failure") { SetLastError(ERROR_ACCESS_DENIED); return INVALID_HANDLE_VALUE; }
    if (std::filesystem::exists(EvidencePath(path))) { SetLastError(ERROR_ALREADY_EXISTS); return INVALID_HANDLE_VALUE; }
    const auto file = std::fopen(EvidencePath(path).c_str(), "wb"); return file ? file : INVALID_HANDLE_VALUE;
}
inline BOOL WriteFile(HANDLE file, const void* data, DWORD count, DWORD* written, void*) {
    if (fake::scenario == "write-failure") { SetLastError(ERROR_ACCESS_DENIED); *written = 0; return FALSE; }
    *written = static_cast<DWORD>(std::fwrite(data, 1, fake::scenario == "short-write" ? count - 1 : count, static_cast<FILE*>(file)));
    return TRUE;
}
inline BOOL FlushFileBuffers(HANDLE file) {
    if (fake::scenario == "flush-failure") { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    return std::fflush(static_cast<FILE*>(file)) == 0;
}
inline BOOL CloseHandle(HANDLE handle) {
    return handle == fake::snapshot || std::fclose(static_cast<FILE*>(handle)) == 0;
}
inline BOOL DeleteFileW(const wchar_t* path) { return std::filesystem::remove(EvidencePath(path)); }
'''

HOST = r'''
#include "ci_warp_runtime.h"
int main(int argc, char** argv) {
    if (argc != 4) return 99;
    fake::scenario = argv[1]; fake::output = argv[2];
    fake::created = std::strtoull(argv[3], nullptr, 10);
    fake::env = {{L"DLSS5_TEST_WARP_DLL", fake::expected},
                 {L"DLSS5_TEST_WARP_SHA256", std::wstring(64, L'A')},
                 {L"DLSS5_TEST_WARP_EVIDENCE_DIR", fake::evidence}};
    const auto& s = fake::scenario;
    if (s == "no-env") fake::env.clear();
    if (s == "missing-dll") fake::env.erase(L"DLSS5_TEST_WARP_DLL");
    if (s == "missing-sha") fake::env.erase(L"DLSS5_TEST_WARP_SHA256");
    if (s == "missing-evidence") fake::env.erase(L"DLSS5_TEST_WARP_EVIDENCE_DIR");
    if (s == "empty-dll") fake::env[L"DLSS5_TEST_WARP_DLL"] = L"";
    if (s == "invalid-sha") fake::env[L"DLSS5_TEST_WARP_SHA256"] = std::wstring(64, L'g');
    if (s == "short-sha") fake::env[L"DLSS5_TEST_WARP_SHA256"] = std::wstring(63, L'a');
    if (s == "relative-dll") fake::env[L"DLSS5_TEST_WARP_DLL"] = L"C:d3d10warp.dll";
    if (s == "relative-evidence") fake::env[L"DLSS5_TEST_WARP_EVIDENCE_DIR"] = L"evidence";
    if (s == "newline-dll") fake::env[L"DLSS5_TEST_WARP_DLL"] += L"\n";
    if (s == "wrong-name") fake::env[L"DLSS5_TEST_WARP_DLL"] = L"C:\\pinned\\other.dll";
    if (s == "pre-wrong") fake::modules = {fake::system};
    if (s == "pre-duplicate") fake::modules = {fake::expected, fake::expected};
    if (s == "pre-same") fake::modules = {fake::expected};
    if (s == "snapshot-failure") fake::snapshot_fail = true;
    const HRESULT prepared = nr_test_warp::Prepare();
    if (FAILED(prepared)) return 10;
    if (s == "post-wrong") fake::modules = {fake::system};
    if (s == "post-duplicate") fake::modules = {fake::expected, fake::expected};
    if (s == "post-missing") fake::modules.clear();
    if (s == "post-snapshot-failure") fake::snapshot_fail = true;
    if (s == "post-path-failure") fake::path_fail = true;
    const char* site = s == "site-injection" ? "site\nverified=true" : "actual-header";
    const HRESULT verified = nr_test_warp::Verify(site);
    if (FAILED(verified)) return 20;
    if (s == "no-env") return fake::loads == 0 && fake::process_times_calls == 0 && !nr_test_warp::Configured() ? 0 : 95;
    if (fake::loads != 1 || fake::process_times_calls != 1 || !nr_test_warp::Configured() || FAILED(nr_test_warp::Prepare())) return 96;
    if (s == "good" || s == "pre-same") {
        if (FAILED(nr_test_warp::Verify("second-device"))) return 21;
    }
    return 0;
}
'''


class WarpRuntimeHeaderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = shutil.which(os.environ.get('CXX', 'g++'))
        if not cls.compiler:
            raise unittest.SkipTest('C++17 compiler unavailable')
        cls.temp = tempfile.TemporaryDirectory(prefix='ci-warp-header-')
        cls.work = Path(cls.temp.name)
        cls.header = (ROOT / 'tests/ci_warp_runtime.h').read_text(encoding='utf-8')
        cls.exe = cls.build('actual', cls.header)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    @classmethod
    def build(cls, name, header):
        out = cls.work / name
        out.mkdir()
        (out / 'windows.h').write_text(WIN32, encoding='utf-8')
        (out / 'tlhelp32.h').write_text('#include "windows.h"\n', encoding='utf-8')
        (out / 'ci_warp_runtime.h').write_text(header, encoding='utf-8')
        (out / 'host.cpp').write_text(HOST, encoding='utf-8')
        exe = out / 'host'
        compiled = subprocess.run([cls.compiler, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror',
                                   '-I', str(out), str(out / 'host.cpp'), '-o', str(exe)],
                                  capture_output=True, text=True)
        if compiled.returncode:
            raise AssertionError(compiled.stdout + compiled.stderr)
        return exe

    def run_case(self, scenario, exe=None, collision=False, created=CREATED, out=None):
        out = out or Path(tempfile.mkdtemp(dir=self.work, prefix=scenario + '-'))
        if collision:
            (out / f'warp-1234-{created}-1.txt').write_text('existing evidence', encoding='utf-8')
        run = subprocess.run([str(exe or self.exe), scenario, str(out), created], capture_output=True, text=True)
        return run, out

    def test_no_configuration_preserves_local_behavior(self):
        run, out = self.run_case('no-env')
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(run.stdout + run.stderr, '')
        self.assertEqual(list(out.iterdir()), [])

    def test_actual_identity_and_utf8_receipt_per_device(self):
        for scenario in ('good', 'pre-same'):
            with self.subTest(scenario=scenario):
                run, out = self.run_case(scenario)
                self.assertEqual(run.returncode, 0, run.stderr)
                self.assertEqual(run.stdout + run.stderr, '')
                self.assertEqual(sorted(p.name for p in out.iterdir()), [f'warp-1234-{CREATED}-1.txt', f'warp-1234-{CREATED}-2.txt'])
                for ordinal, site in ((1, 'actual-header'), (2, 'second-device')):
                    fields = dict(line.split('=', 1) for line in (out / f'warp-1234-{CREATED}-{ordinal}.txt').read_text(encoding='utf-8').splitlines())
                    self.assertEqual(fields, {'schema': '2', 'host_exe': r'C:\host\probe.exe', 'pid': '1234', 'process_created': CREATED,
                        'site': site, 'expected_sha256': 'a' * 64,
                        'actual_module': 'C:\\pinned\\模型\\d3d10warp.dll', 'warp_instances': '1', 'verified': 'true'})

    def test_prepare_fails_closed_before_any_device(self):
        cases = {
            'missing-dll': 'incomplete-configuration', 'missing-sha': 'incomplete-configuration',
            'missing-evidence': 'incomplete-configuration', 'empty-dll': 'incomplete-configuration',
            'newline-dll': 'incomplete-configuration', 'invalid-sha': 'sha256-format', 'short-sha': 'sha256-format',
            'relative-dll': 'absolute-path-required', 'relative-evidence': 'absolute-path-required',
            'wrong-name': 'warp-filename', 'missing-file': 'warp-file-missing',
            'missing-directory': 'evidence-directory-missing', 'pre-wrong': 'unexpected-warp-module',
            'pre-duplicate': 'warp-module-count', 'load-failure': 'absolute-load',
            'load-wrong': 'loaded-module-path', 'snapshot-failure': 'module-snapshot',
            'enumeration-failure': 'module-enumeration',
            'process-times-failure': 'process-creation-time', 'zero-created': 'process-creation-time',
        }
        for scenario, reason in cases.items():
            with self.subTest(scenario=scenario):
                run, out = self.run_case(scenario)
                self.assertEqual(run.returncode, 10, run.stderr)
                self.assertIn('reason=' + reason, run.stderr)
                self.assertEqual(list(out.iterdir()), [])

    def test_verify_fails_closed_and_removes_incomplete_receipt(self):
        cases = {'post-wrong': 'unexpected-warp-module', 'post-duplicate': 'warp-module-count',
            'post-missing': 'warp-module-count', 'post-snapshot-failure': 'module-snapshot',
            'post-path-failure': 'module-path', 'site-injection': 'site-format',
            'create-failure': 'evidence-create', 'write-failure': 'evidence-write',
            'short-write': 'evidence-write', 'flush-failure': 'evidence-write'}
        for scenario, reason in cases.items():
            with self.subTest(scenario=scenario):
                run, out = self.run_case(scenario)
                self.assertEqual(run.returncode, 20, run.stderr)
                self.assertIn('reason=' + reason, run.stderr)
                self.assertEqual(list(out.iterdir()), [])
        run, out = self.run_case('good', collision=True)
        self.assertEqual(run.returncode, 20, run.stderr)
        self.assertEqual((out / f'warp-1234-{CREATED}-1.txt').read_text(), 'existing evidence')

    def test_reused_pid_preserves_distinct_processes_and_rejects_same_identity(self):
        first, out = self.run_case('good')
        self.assertEqual(first.returncode, 0, first.stderr)
        earlier = {path.name: path.read_bytes() for path in out.iterdir()}
        following = str(int(CREATED) + 1)
        second, _ = self.run_case('good', created=following, out=out)
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertEqual(len(list(out.iterdir())), 4)
        for name, data in earlier.items():
            self.assertEqual((out / name).read_bytes(), data)
        for ordinal in (1, 2):
            record = (out / f'warp-1234-{following}-{ordinal}.txt').read_text()
            self.assertIn(f'\nprocess_created={following}\n', record)
        preserved = {path.name: path.read_bytes() for path in out.iterdir()}
        duplicate, _ = self.run_case('good', out=out)
        self.assertEqual(duplicate.returncode, 20, duplicate.stderr)
        self.assertIn('reason=evidence-create', duplicate.stderr)
        self.assertEqual({path.name: path.read_bytes() for path in out.iterdir()}, preserved)

    def test_old_pid_only_filename_precisely_recreates_collision(self):
        old = 'L"-" + std::to_wstring(state.process_created) + L"-" + std::to_wstring(ordinal) + L".txt";'
        self.assertEqual(self.header.count(old), 1)
        exe = self.build('pid-only-name', self.header.replace(old, 'L"-" + std::to_wstring(ordinal) + L".txt";'))
        first, out = self.run_case('good', exe)
        self.assertEqual(first.returncode, 0, first.stderr)
        second, _ = self.run_case('good', exe, created=str(int(CREATED) + 1), out=out)
        self.assertEqual(second.returncode, 20, second.stderr)
        self.assertIn('reason=evidence-create', second.stderr)
        self.assertEqual(len(list(out.iterdir())), 2)

    def test_precise_regressions_are_detected(self):
        mutations = {
            'allow-duplicate': ('if (count > 1 || (require_one && count != 1))', 'if (require_one && count == 0)', 'pre-duplicate', 10),
            'ignore-actual-path': ('if (!EqualPath(actual, expected))', 'if ((void)expected, false)', 'post-wrong', 20),
            'bare-load': ('LoadLibraryExW(expected.c_str(), nullptr,', 'LoadLibraryExW(L"d3d10warp.dll", nullptr,', 'good', 0),
        }
        for name, (old, new, scenario, expected) in mutations.items():
            with self.subTest(mutation=name):
                self.assertEqual(self.header.count(old), 1)
                exe = self.build(name, self.header.replace(old, new))
                run, _ = self.run_case(scenario, exe)
                self.assertNotEqual(run.returncode, expected, run.stderr)
        old = 'return detail::Evidence(state, site, actual);'
        self.assertEqual(self.header.count(old), 1)
        exe = self.build('omit-receipt', self.header.replace(old, '(void)site; return S_OK;'))
        run, out = self.run_case('good', exe)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertNotEqual(len(list(out.glob('warp-*.txt'))), 2)


if __name__ == '__main__':
    unittest.main()
