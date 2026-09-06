// Worker-only companion to the core ZIP. Standard headers belong to the TU.
// Reads only the already-sanitized, hash-verified summary artifacts, never raw INI/images.
namespace nr_feedback_report {
constexpr std::size_t kLimit = 2u * 1024u * 1024u;
struct Output {
    std::shared_ptr<const std::string> text;
    std::wstring path;
    DWORD error = ERROR_SUCCESS;
    bool complete = true;
};
struct File {
    HANDLE h = INVALID_HANDLE_VALUE;
    explicit File(HANDLE value) : h(value) {}
    ~File() { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); }
    File(const File &) = delete;
    File &operator=(const File &) = delete;
};
bool Append(std::string &out, const std::string &value) {
    constexpr std::size_t reserve = 512;
    if (out.size() > kLimit - reserve || value.size() > kLimit - reserve - out.size()) return false;
    out += value;
    return true;
}
std::string SafeLines(const char *value) {
    std::string out;
    if (!value) return out;
    const char *begin = value;
    for (const char *p = value;; ++p) {
        if (*p == '\n' || *p == 0) {
            out += nr_feedback_text::Sanitize(std::string(begin, p));
            out += '\n';
            if (*p == 0) break;
            begin = p + 1;
        }
    }
    return out;
}
bool ReadVerifiedSummary(const nr_bundle::Artifact &item, std::string &text, bool remove_verified_scratch = false) {
    if (!item.included || item.kind != nr_bundle::FileKind::Copy ||
        item.bytes > nr_feedback_text::kSummaryLimit ||
        std::strncmp(item.name, "logs/", 5) != 0 ||
        std::strstr(item.name, "-summary.txt") == nullptr ||
        !nr_bundle::LocalAbsolutePath(item.source)) return false;
    std::wstring path(item.source);
    if (path.compare(0, 4, L"\\\\?\\") != 0) path.insert(0, L"\\\\?\\");
    File file(CreateFileW(path.c_str(), GENERIC_READ | (remove_verified_scratch ? DELETE : 0), FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    if (file.h == INVALID_HANDLE_VALUE || GetFileType(file.h) != FILE_TYPE_DISK ||
        !GetFileInformationByHandle(file.h, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        ((static_cast<UINT64>(info.nFileSizeHigh) << 32) | info.nFileSizeLow) != item.bytes ||
        (remove_verified_scratch && info.nNumberOfLinks != 1)) return false;
    text.resize(static_cast<std::size_t>(item.bytes));
    DWORD read = 0;
    if (!ReadFile(file.h, text.data(), static_cast<DWORD>(text.size()), &read, nullptr) || read != text.size()) return false;
    nr_bundle::Sha256 digest;
    digest.Update(reinterpret_cast<const unsigned char *>(text.data()), text.size());
    char hash[65]{}; digest.Finish(hash);
    if (std::strcmp(hash, item.sha256) != 0 || text.find('\0') != std::string::npos ||
        !nr_bundle::ValidUtf8(text.c_str())) return false;
    if (!remove_verified_scratch) return true;
    // Delete the same verified open file, not a path reopened after hashing.
    // Deny write/delete sharing and refuse reparse points or hard links above.
    FILE_DISPOSITION_INFO disposition{}; disposition.DeleteFile = TRUE;
    return SetFileInformationByHandle(file.h, FileDispositionInfo, &disposition, sizeof(disposition)) != FALSE;
}
bool WriteAtomic(const wchar_t *directory, const std::string &text, Output &output) {
    if (!nr_bundle::LocalAbsolutePath(directory) || text.size() > kLimit || !nr_bundle::ValidUtf8(text.c_str())) {
        output.error = ERROR_INVALID_DATA; return false;
    }
    const std::wstring final = std::wstring(directory) + L"\\feedback-report.txt";
    const std::wstring temporary = final + L".tmp";
    if (temporary.size() >= nr_bundle::kMaxPath) { output.error = ERROR_FILENAME_EXCED_RANGE; return false; }
    auto native = [](const std::wstring &p) { return p.compare(0, 4, L"\\\\?\\") == 0 ? p : L"\\\\?\\" + p; };
    bool okay = false;
    {
        File file(CreateFileW(native(temporary).c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (file.h == INVALID_HANDLE_VALUE) { output.error = GetLastError(); return false; }
        DWORD written = 0;
        okay = WriteFile(file.h, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
            written == text.size() && FlushFileBuffers(file.h);
        if (!okay) { output.error = GetLastError(); if (!output.error) output.error = ERROR_WRITE_FAULT; }
    }
    if (okay) {
        okay = MoveFileExW(native(temporary).c_str(), native(final).c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
        if (!okay) output.error = GetLastError();
    }
    if (!okay) { DeleteFileW(native(temporary).c_str()); return false; }
    output.path = final;
    return true;
}
// Called only after both persisted deliverables exist. Never sweep a directory,
// delete original game logs, or remove summaries needed to recover a failed TXT.
// Failed/locked cleanup is best effort: the two deliverables stay valid and any
// residual scratch may be ignored. No caller needs a third upload.
unsigned CleanupSummaryScratch(const nr_bundle::Job &job, const nr_bundle::Result &zip,
                              const Output &output) noexcept {
    unsigned removed = 0;
    try {
        if (!job.executed || zip.status != nr_bundle::Status::Complete ||
            !zip.required_complete || !output.complete || !output.text ||
            output.error != ERROR_SUCCESS || !nr_bundle::LocalAbsolutePath(job.directory)) return 0;
        const std::wstring directory(job.directory);
        if (std::wstring(zip.zip_path) != directory + L"\\feedback.zip" ||
            output.path != directory + L"\\feedback-report.txt") return 0;
        static constexpr const char *names[] = {
            "logs/nr-current-summary.txt", "logs/nr-previous-summary.txt",
            "logs/reshade-addon-summary.txt", "logs/reshade-game-summary.txt",
            "logs/reshade-module-summary.txt"};
        for (unsigned i = 0; i < job.count; ++i) {
            const auto &item = job.files[i];
            if (!item.included || item.source_changed || item.truncated) continue;
            for (unsigned n = 0; n < 5; ++n) {
                if (std::strcmp(item.name, names[n]) != 0) continue;
                const auto expected = directory + L"\\summary-" + std::to_wstring(n) + L".txt";
                if (std::wstring(item.source) != expected) break;
                std::string verified;
                if (ReadVerifiedSummary(item, verified, true)) ++removed;
                break;
            }
        }
    } catch (...) {
        // Only optional diagnostic scratch cleanup is isolated here. Never
        // catch/retry an NGX call or change the already-persisted report status.
    }
    return removed;
}
Output Build(const nr_bundle::Job &job, const nr_bundle::Result &zip, bool capture_complete) {
    Output output;
    output.complete = capture_complete && zip.required_complete && zip.status != nr_bundle::Status::Failed;
    std::string text = "DLSS5 核心反馈全文 / Core feedback text\n"
        "可复制全文到反馈文档。保留对应 ZIP；图像异常另附 F7。\n"
        "只需提交本 TXT 与 feedback.zip；summary-*.txt 是可忽略的内部中间文件。\n"
        "This text contains the complete bounded core metadata and included sanitized log summaries, not unlimited raw logs.\n"
        "Historical first errors are not current failures. Counters are not FPS or proof of visible image quality.\n";
    text += "format=DLSS5-CoreText-v1\nencoding=UTF-8\n";
    // Archive completeness is independent of required-core/capture completeness.
    text += std::string("zip.status=") + (zip.status == nr_bundle::Status::Failed ? "failed" : zip.status == nr_bundle::Status::Complete ? "complete" : "partial") + "\n";
    text += std::string("zip.requiredComplete=") + (zip.required_complete ? "1" : "0") + "\n";
    text += std::string("capture.complete=") + (capture_complete ? "1" : "0") + "\n";
    text += "zip.win32Error=" + std::to_string(zip.win32_error) + "\n";
    text += "\n===== CURRENT CAPTURE METADATA / 本次采集 =====\n";
    if (!Append(text, SafeLines(job.metadata))) output.complete = false;
    Append(text, "\n===== INCLUDED / MISSING EVIDENCE =====\n");
    for (unsigned i = 0; i < job.count; ++i) {
        const auto &item = job.files[i];
        std::string line = nr_feedback_text::Sanitize(item.name) + " status=" +
            (item.included ? "included" : "missing") + " required=" + (item.required ? "1" : "0") +
            " bytes=" + std::to_string(item.bytes) + " sha256=" + item.sha256 +
            " reason=" + nr_feedback_text::Sanitize(item.reason) + "\n";
        if (!Append(text, line)) output.complete = false;
    }
    for (unsigned i = 0; i < job.count; ++i) {
        const auto &item = job.files[i];
        if (!item.included || std::strncmp(item.name, "logs/", 5) != 0) continue;
        const bool previous = std::strstr(item.name, "previous") != nullptr;
        const std::string heading = std::string("\n===== ") + item.name +
            (previous ? " / PREVIOUS PROCESS SESSION, NOT CURRENT =====\n" : " / CURRENT PROCESS LOG (head/first error/tail distinguished below) =====\n");
        std::string summary;
        if (!Append(text, heading)) { output.complete = false; break; }
        if (!ReadVerifiedSummary(item, summary)) {
            output.complete = false; Append(text, "[summary unavailable or changed since ZIP hashing; not substituted]\n"); continue;
        }
        if (!Append(text, summary)) { output.complete = false; break; }
    }
    text += output.complete ? "\ntext.complete=1\n" : "\ntext.complete=0\ntext.reason=capture partial, summary unavailable/changed, or bounded text budget reached\n";
    text += "END DLSS5 FEEDBACK TEXT\n";
    auto cached = std::make_shared<const std::string>(std::move(text));
    if (WriteAtomic(job.directory, *cached, output)) {
        output.text = std::move(cached);
        CleanupSummaryScratch(job, zip, output);
    }
    return output;
}
} // namespace nr_feedback_report
