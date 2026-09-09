// A single-use, ordinary-user ReShade loader for a manager-owned profile.
// No game termination, file cleanup, ACL changes, driver or protection bypass.
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <TlHelp32.h>
#include <bcrypt.h>
#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <algorithm>
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "advapi32.lib")

struct Handle {
  HANDLE value = nullptr;
  explicit Handle(HANDLE v = nullptr) : value(v) {}
  ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  explicit operator bool() const { return value && value != INVALID_HANDLE_VALUE; }
};
std::string utf8(const std::wstring& value) {
  const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.c_str(), (int)value.size(), nullptr, 0, nullptr, nullptr);
  if (count <= 0) return {};
  std::string result(count, '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.c_str(), (int)value.size(), result.data(), count, nullptr, nullptr);
  return result;
}
std::string json(const std::wstring& value) {
  std::string output = "\"";
  for (unsigned char ch : utf8(value)) {
    if (ch == '\\' || ch == '"') output += '\\';
    if (ch < 32) output += '?'; else output += (char)ch;
  }
  return output + "\"";
}
bool same(const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }
std::wstring image(HANDLE process) {
  std::wstring value(32768, L'\0'); DWORD size = (DWORD)value.size();
  if (!QueryFullProcessImageNameW(process, 0, value.data(), &size)) return {};
  value.resize(size); return value;
}
bool localPath(const std::wstring& value) {
  if (value.size() < 4 || value.size() >= 32000 || !iswalpha(value[0]) || value[1] != L':' || (value[2] != L'\\' && value[2] != L'/')) return false;
  std::wstring full(32768, L'\0'); DWORD size = GetFullPathNameW(value.c_str(), (DWORD)full.size(), full.data(), nullptr);
  if (!size || size >= full.size()) return false; full.resize(size);
  if (!same(value, full)) return false;
  // Reject reparse points on every path component, including the leaf.
  for (size_t at = 3; at <= value.size(); ++at) if (at == value.size() || value[at] == L'\\' || value[at] == L'/') {
    const DWORD attr = GetFileAttributesW(value.substr(0, at).c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
  }
  return true;
}
bool hex(const std::wstring& text) { return text.size() == 64 && text.find_first_not_of(L"0123456789abcdef") == std::wstring::npos; }
bool uuid(const std::wstring& text) {
  if (text.size() != 36) return false;
  for (size_t i = 0; i < text.size(); ++i) {
    if (i == 8 || i == 13 || i == 18 || i == 23) { if (text[i] != L'-') return false; }
    else if (!iswxdigit(text[i])) return false;
  }
  return true;
}
std::string sha256(const std::wstring& path) {
  Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
  BY_HANDLE_FILE_INFORMATION info{};
  if (!file || !GetFileInformationByHandle(file.value, &info) || info.nNumberOfLinks != 1 || info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return {};
  BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_HASH_HANDLE state = nullptr;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return {};
  DWORD size = 0, returned = 0;
  BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, (PUCHAR)&size, sizeof(size), &returned, 0);
  std::vector<UCHAR> memory(size), buffer(64 * 1024); UCHAR digest[32]{};
  bool ok = BCryptCreateHash(algorithm, &state, memory.data(), size, nullptr, 0, 0) >= 0;
  DWORD read = 0;
  while (ok) { if (!ReadFile(file.value, buffer.data(), (DWORD)buffer.size(), &read, nullptr)) { ok = false; break; }
    if (!read) break; ok = BCryptHashData(state, buffer.data(), read, 0) >= 0; }
  if (ok) ok = BCryptFinishHash(state, digest, sizeof(digest), 0) >= 0;
  if (state) BCryptDestroyHash(state); BCryptCloseAlgorithmProvider(algorithm, 0);
  if (!ok) return {}; const char* chars = "0123456789abcdef"; std::string result;
  for (UCHAR byte : digest) { result += chars[byte >> 4]; result += chars[byte & 15]; } return result;
}
std::vector<BYTE> tokenInfo(HANDLE process, TOKEN_INFORMATION_CLASS kind) {
  HANDLE raw = nullptr; if (!OpenProcessToken(process, TOKEN_QUERY, &raw)) return {}; Handle token(raw);
  DWORD size = 0; GetTokenInformation(token.value, kind, nullptr, 0, &size); std::vector<BYTE> data(size);
  if (!size || !GetTokenInformation(token.value, kind, data.data(), size, &size)) return {}; return data;
}
bool ordinary(HANDLE process) {
  auto data = tokenInfo(process, TokenIntegrityLevel); if (data.empty()) return false;
  auto* label = reinterpret_cast<TOKEN_MANDATORY_LABEL*>(data.data()); PSID sid = label->Label.Sid;
  const DWORD level = *GetSidSubAuthority(sid, (DWORD)(*GetSidSubAuthorityCount(sid) - 1));
  return level < SECURITY_MANDATORY_HIGH_RID;
}
bool sameUser(HANDLE process) {
  auto current = tokenInfo(GetCurrentProcess(), TokenUser), target = tokenInfo(process, TokenUser);
  auto cs = tokenInfo(GetCurrentProcess(), TokenSessionId), ts = tokenInfo(process, TokenSessionId);
  return !current.empty() && !target.empty() && cs.size() == sizeof(DWORD) && ts.size() == sizeof(DWORD) &&
    EqualSid(reinterpret_cast<TOKEN_USER*>(current.data())->User.Sid, reinterpret_cast<TOKEN_USER*>(target.data())->User.Sid) &&
    *reinterpret_cast<DWORD*>(cs.data()) == *reinterpret_cast<DWORD*>(ts.data()) && ordinary(process);
}
unsigned long long creation(HANDLE process) { FILETIME started{}, ended{}, kernel{}, user{}; if (!GetProcessTimes(process, &started, &ended, &kernel, &user)) return 0;
  return (static_cast<unsigned long long>(started.dwHighDateTime) << 32) | started.dwLowDateTime; }
std::vector<DWORD> targets(const std::wstring& target) {
  std::vector<DWORD> result; Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
  const std::wstring name = target.substr(target.find_last_of(L"\\/") + 1); PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
  if (!snapshot) return result;
  for (BOOL more = Process32FirstW(snapshot.value, &entry); more; more = Process32NextW(snapshot.value, &entry)) {
    if (!same(entry.szExeFile, name)) continue;
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID));
    if (process && same(image(process.value), target)) result.push_back(entry.th32ProcessID);
  }
  return result;
}
uintptr_t moduleBase(DWORD pid, const std::wstring& name) {
  Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid)); MODULEENTRY32W module{}; module.dwSize = sizeof(module);
  if (!snapshot) return 0;
  for (BOOL more = Module32FirstW(snapshot.value, &module); more; more = Module32NextW(snapshot.value, &module))
    if (same(module.szModule, name)) return reinterpret_cast<uintptr_t>(module.modBaseAddr);
  return 0;
}
bool loaded(DWORD pid, const std::wstring& file) {
  Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid)); MODULEENTRY32W module{}; module.dwSize = sizeof(module);
  if (!snapshot) return false;
  for (BOOL more = Module32FirstW(snapshot.value, &module); more; more = Module32NextW(snapshot.value, &module)) if (same(module.szExePath, file)) return true;
  return false;
}
bool load(DWORD pid, const std::wstring& target, const std::wstring& loader, unsigned long long earliest, DWORD& error) {
  Handle process(OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION, FALSE, pid));
  if (!process) { error = GetLastError(); return false; }
  BOOL wow = TRUE;
  if (!same(image(process.value), target) || creation(process.value) < earliest || !sameUser(process.value) || !IsWow64Process(process.value, &wow) || wow) { error = ERROR_ACCESS_DENIED; return false; }
  if (loaded(pid, loader)) { error = ERROR_ALREADY_EXISTS; return false; }
  const auto address = reinterpret_cast<uintptr_t>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
  HMODULE containing = nullptr;
  if (!address || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(address), &containing)) { error = ERROR_PROC_NOT_FOUND; return false; }
  wchar_t localFile[MAX_PATH]{}; GetModuleFileNameW(containing, localFile, MAX_PATH);
  const std::wstring localName(localFile), name = localName.substr(localName.find_last_of(L"\\/") + 1);
  const uintptr_t remoteBase = moduleBase(pid, name);
  if (!remoteBase) { error = ERROR_MOD_NOT_FOUND; return false; }
  const size_t bytes = (loader.size() + 1) * sizeof(wchar_t);
  void* memory = VirtualAllocEx(process.value, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (!memory) { error = GetLastError(); return false; }
  SIZE_T written = 0;
  if (!WriteProcessMemory(process.value, memory, loader.c_str(), bytes, &written) || written != bytes) { error = GetLastError(); VirtualFreeEx(process.value, memory, 0, MEM_RELEASE); return false; }
  Handle thread(CreateRemoteThread(process.value, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteBase + address - reinterpret_cast<uintptr_t>(containing)), memory, 0, nullptr));
  if (!thread) { error = GetLastError(); VirtualFreeEx(process.value, memory, 0, MEM_RELEASE); return false; }
  const DWORD wait = WaitForSingleObject(thread.value, 10000);
  // An in-flight LoadLibrary may still read its argument. On timeout the game
  // owns the allocation until exit; never free it early or terminate the game.
  if (wait != WAIT_OBJECT_0) { error = wait == WAIT_TIMEOUT ? WAIT_TIMEOUT : GetLastError(); return false; }
  VirtualFreeEx(process.value, memory, 0, MEM_RELEASE);
  if (!loaded(pid, loader)) { error = ERROR_DLL_INIT_FAILED; return false; }
  error = ERROR_SUCCESS; return true;
}
int wmain(int argc, wchar_t** argv) {
  std::map<std::wstring, std::wstring> args;
  for (int index = 1; index + 1 < argc; index += 2) { if (!args.emplace(argv[index], argv[index + 1]).second) return 2; }
  if (argc != 17 || args.size() != 8) return 2;
  const auto session = args[L"--session"], target = args[L"--target"], targetHash = args[L"--target-sha"], loader = args[L"--loader"], loaderHash = args[L"--loader-sha"], config = args[L"--config"], configHash = args[L"--config-sha"], timeoutText = args[L"--timeout"];
  auto event = [&](const char* type, DWORD pid = 0, DWORD error = 0) {
    std::cout << "{\"version\":1,\"event\":\"" << type << "\",\"sessionId\":" << json(session) << ",\"targetExe\":" << json(target)
      << ",\"configHash\":" << json(configHash) << ",\"helperPid\":" << GetCurrentProcessId() << ",\"gamePid\":" << pid << ",\"error\":" << error << "}" << std::endl;
  };
  wchar_t* timeoutEnd = nullptr; unsigned long timeout = wcstoul(timeoutText.c_str(), &timeoutEnd, 10);
  if (!uuid(session) || !hex(targetHash) || !hex(loaderHash) || !hex(configHash) || timeoutText.empty() || !timeoutEnd || *timeoutEnd ||
      timeoutText.find_first_not_of(L"0123456789") != std::wstring::npos || timeout < 1000 || timeout > 120000 ||
      !localPath(target) || !localPath(loader) || !localPath(config) || !ordinary(GetCurrentProcess())) { event("failed", 0, ERROR_INVALID_PARAMETER); return 2; }
  if (sha256(target) != utf8(targetHash) || sha256(loader) != utf8(loaderHash) || sha256(config) != utf8(configHash)) { event("failed", 0, ERROR_FILE_INVALID); return 3; }
  if (!targets(target).empty()) { event("failed", 0, ERROR_ALREADY_EXISTS); return 4; }
  FILETIME stamp{}; GetSystemTimeAsFileTime(&stamp); const unsigned long long earliest = ((static_cast<unsigned long long>(stamp.dwHighDateTime) << 32) | stamp.dwLowDateTime) - 10000000ULL;
  const ULONGLONG deadline = GetTickCount64() + timeout; event("ready");
  while (GetTickCount64() < deadline) {
    const auto found = targets(target);
    if (found.size() > 1) { event("failed", 0, ERROR_DUP_NAME); return 5; }
    if (found.size() == 1) {
      const DWORD pid = found.front();
      if (!localPath(target) || !localPath(loader) || !localPath(config) || sha256(target) != utf8(targetHash) || sha256(loader) != utf8(loaderHash) || sha256(config) != utf8(configHash)) { event("failed", pid, ERROR_FILE_INVALID); return 3; }
      DWORD error = 0; const bool ok = load(pid, target, loader, earliest, error);
      event(ok ? "attached" : "failed", pid, error); return ok ? 0 : 6;
    }
    Sleep(50);
  }
  event("failed", 0, WAIT_TIMEOUT); return 7;
}
