// Included inside the core namespace after the state/feedback definitions.
// Filename observations are NOT proof of a usable SR carrier or GPU delivery.
namespace nr_dx11_deployment {
SRWLOCK lock = SRWLOCK_INIT;
struct Snapshot {
    bool complete = false;
    unsigned carriers = 0, matching = 0;
    ULONGLONG at = 0;
};
Snapshot current{};
ULONGLONG next_scan = 0;
std::atomic<bool> duplicate_carriers{false};

void Poll(bool force) {
    if (!TryAcquireSRWLockExclusive(&lock)) return;
    const auto now = GetTickCount64();
    if (!force && now < next_scan) { ReleaseSRWLockExclusive(&lock); return; }
    // Forced preparation is still bounded; it just avoids the initial delay.
    if (current.at && now < next_scan) { ReleaseSRWLockExclusive(&lock); return; }
    next_scan = now + 1000;
    Snapshot next{}; next.at = now;
    wchar_t competing[160]{};
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snapshot != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W entry{}; entry.dwSize = sizeof(entry);
        for (BOOL ok = Module32FirstW(snapshot, &entry); ok; ok = Module32NextW(snapshot, &entry)) {
            if (entry.hModule == g_addon_module.load(std::memory_order_acquire)) continue;
            if (nr_deployment_names::Carrier(entry.szModule)) ++next.carriers;
            if (nr_deployment_names::MatchingCarrier(entry.szModule)) ++next.matching;
            if (nr_deployment_names::GenericNr(entry.szModule))
                wcsncpy_s(competing, entry.szModule, _TRUNCATE);
        }
        next.complete = GetLastError() == ERROR_NO_MORE_FILES;
        CloseHandle(snapshot);
    }
    const auto previous = current;
    current = next;
    ReleaseSRWLockExclusive(&lock);
    // A positive sighting is useful even if enumeration later failed. Absence
    // is claimed only by a complete snapshot. No second D3D11 hook is installed.
    if (competing[0]) {
        bool expected = false;
        if (g_generic_renodx_conflict.compare_exchange_strong(expected, true)) {
            Log("[dx11-deployment] competing generic NR addon=%ls; native path retained. Remove the duplicate, restart, and collect F8 if bypass persists.", competing);
            SetBypassReason(BypassReason::GenericRenoDxConflict);
        }
    }
    if (next.carriers > 1 && !duplicate_carriers.exchange(true))
        Log("[dx11-deployment] multiple carrier filenames detected; companion consumer refused until restart. Keep ONE matching bridge and collect F8.");
    if (!previous.at || previous.complete != next.complete || previous.carriers != next.carriers || previous.matching != next.matching)
        Log("[dx11-deployment] module snapshot complete=%d carriers=%u matching045=%u; filename evidence only, not NR readiness or GPU completion.",
            next.complete ? 1 : 0, next.carriers, next.matching);
}
Snapshot Read() {
    AcquireSRWLockShared(&lock);
    const auto result = current;
    ReleaseSRWLockShared(&lock);
    return result;
}
} // namespace nr_dx11_deployment

void NrDx11DeploymentPoll(bool force) { nr_dx11_deployment::Poll(force); }
bool NrDx11DeploymentBlocked() {
    return nr_dx11_deployment::duplicate_carriers.load(std::memory_order_acquire) ||
        !nr_dx11_deployment::Read().complete;
}
void DrawDx11DeploymentStatus() {
    const bool zh = UseChineseUi();
    const auto ui = [zh](const char* en, const char* cn) { return zh ? cn : en; };
    const auto s = nr_dx11_deployment::Read();
    if (nr_dx11_deployment::duplicate_carriers.load(std::memory_order_acquire))
        ImGui::TextWrapped("%s", ui("Multiple DX11 carriers were detected. Keep one matching companion and restart; collect F8 if the safe bypass remains.",
            "检测到多个 DX11 桥。请只保留一份配套桥后重启；仍安全旁路时按 F8 收集反馈并上传 BUG。"));
    else if (!s.complete)
        ImGui::TextWrapped("%s", ui("DX11 companion inventory is incomplete; absence is not established. F8 records this state.",
            "DX11 配套组件尚未完整枚举，不能据此认定缺失。请用 F8 记录状态并反馈。"));
    else if (!s.matching)
        ImGui::TextWrapped("%s", ui("Matching DX11 companion filename was not observed. Use the same 0.4.5 DX11 package, not the D3D12-only OTA. Keep the game's API; do not add another NR renderer to fix this. F8 for feedback.",
            "未观察到本版配套 DX11 桥文件名。请使用同一份 0.4.5 DX11 包，而非仅 D3D12 的 OTA；不必切换游戏 API，也不要额外安装另一套 NR。仍无效请按 F8 上传 BUG。"));
    else
        ImGui::TextWrapped("%s", ui("DX11 companion module observed. Native or mod-provided DLSS still needs to supply a valid contract; compare NR/submission activity and F6 pictures. A loaded module alone does not prove compatibility.",
            "已观察到配套 DX11 桥。仍需游戏或模组提供有效 DLSS 输入；请结合上方 NR/提交活动与 F6 画面对比，不能仅凭模块加载判定兼容。"));
}
