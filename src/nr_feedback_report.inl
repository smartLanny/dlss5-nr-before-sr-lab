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
bool ReadVerifiedSummary(const nr_bundle::Artifact &item, std::string &text) {
    if (!item.included || item.kind != nr_bundle::FileKind::Copy ||
        item.bytes > nr_feedback_text::kSummaryLimit ||
        std::strncmp(item.name, "logs/", 5) != 0 ||
        std::strstr(item.name, "-summary.txt") == nullptr ||
        !nr_bundle::LocalAbsolutePath(item.source)) return false;
    std::wstring path(item.source);
    if (path.compare(0, 4, L"\\\\?\\") != 0) path.insert(0, L"\\\\?\\");
    File file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    if (file.h == INVALID_HANDLE_VALUE || GetFileType(file.h) != FILE_TYPE_DISK ||
        !GetFileInformationByHandle(file.h, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        ((static_cast<UINT64>(info.nFileSizeHigh) << 32) | info.nFileSizeLow) != item.bytes) return false;
    text.resize(static_cast<std::size_t>(item.bytes));
    DWORD read = 0;
    if (!ReadFile(file.h, text.data(), static_cast<DWORD>(text.size()), &read, nullptr) || read != text.size()) return false;
    nr_bundle::Sha256 digest;
    digest.Update(reinterpret_cast<const unsigned char *>(text.data()), text.size());
    char hash[65]{}; digest.Finish(hash);
    return std::strcmp(hash, item.sha256) == 0 && text.find('\0') == std::string::npos && nr_bundle::ValidUtf8(text.c_str());
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
Output Build(const nr_bundle::Job &job, const nr_bundle::Result &zip, bool capture_complete) {
    Output output;
    output.complete = capture_complete && zip.required_complete && zip.status != nr_bundle::Status::Failed;
    std::string text = "DLSS5 核心反馈全文 / Core feedback text\n"
        "可复制全文到反馈文档。保留对应 ZIP；图像异常另附 F7。\n"
        "This text contains the complete bounded core metadata and included sanitized log summaries, not unlimited raw logs.\n"
        "Historical first errors are not current failures. Counters are not FPS or proof of visible image quality.\n";
    text += "format=DLSS5-CoreText-v1\nencoding=UTF-8\n";
    text += std::string("zip.status=") + (zip.status == nr_bundle::Status::Failed ? "failed" : zip.required_complete && capture_complete ? "complete" : "partial") + "\n";
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
    if (WriteAtomic(job.directory, *cached, output)) output.text = std::move(cached);
    return output;
}
} // namespace nr_feedback_report
