// Include after windows.h and the standard atomic/cstdint/cstdio/cstring/
// cwchar/new headers. This module owns no render state, GPU objects or threads.
// Run must execute on the caller's process-pinned, non-DllMain worker. The
// caller must keep Job alive until Run returns, and must not edit it meanwhile.
namespace nr_bundle {

constexpr unsigned int kMaxFiles = 64;
constexpr unsigned int kMaxPath = 1024;
constexpr unsigned int kMaxName = 240;
constexpr unsigned int kMetadataBytes = 32768;
constexpr unsigned long long kCopyLimit = 64ull * 1024 * 1024;
constexpr unsigned long long kLogLimit = 8ull * 1024 * 1024;
constexpr unsigned long long kHashLimit = 512ull * 1024 * 1024;
constexpr unsigned long long kArchiveLimit = 512ull * 1024 * 1024;
constexpr unsigned long long kReadLimit = 2ull * 1024 * 1024 * 1024;
constexpr unsigned long long kTimeLimitMs = 60000;

enum class FileKind { Copy, LogTail, HashOnly };
enum class Status { Failed, Complete, Partial };
struct Result {
    Status status = Status::Failed;
    DWORD win32_error = ERROR_SUCCESS;
    wchar_t zip_path[kMaxPath] = {};
    unsigned int included_files = 0;
    unsigned int hashed_modules = 0;
    unsigned int missing_files = 0;
    unsigned int truncated_files = 0;
    bool required_complete = true;
};

struct Artifact {
    wchar_t source[kMaxPath] = {};
    char name[kMaxName] = {};
    char reason[256] = {};
    char sha256[65] = {};
    FileKind kind = FileKind::Copy;
    bool required = false;
    bool supplied_missing = false;
    bool included = false;
    bool truncated = false;
    bool source_changed = false;
    DWORD error = ERROR_SUCCESS;
    unsigned long long limit = 0;
    unsigned long long source_size = 0;
    unsigned long long source_offset = 0;
    unsigned long long bytes = 0;
    std::uint32_t crc = 0;
};

struct CentralEntry {
    char name[kMaxName] = {};
    std::uint32_t size = 0, crc = 0, offset = 0;
};

struct Job {
    wchar_t directory[kMaxPath] = {};
    char metadata[kMetadataBytes] = {};
    Artifact files[kMaxFiles] = {};
    unsigned int count = 0, rejected_inputs = 0;
    std::atomic<bool> cancelled{false};
    bool executed = false;
    Result result{};
    // Heap-owned fixed buffers: no per-artifact image copies or large stacks.
    unsigned char buffer[64 * 1024] = {};
    std::uint32_t crc_table[256] = {};
    CentralEntry central[kMaxFiles + 2] = {};
    unsigned int central_count = 0;
    char manifest[384 * 1024] = {};
    std::size_t manifest_length = 0;
    HANDLE archive = INVALID_HANDLE_VALUE;
    unsigned long long archive_limit = kArchiveLimit; // May lower, never raise, the global ceiling.
    unsigned long long archive_bytes = 0, read_bytes = 0, started = 0;
};

static std::atomic<bool> g_running{false};

bool CopyText(char *destination, std::size_t capacity, const char *source) {
    if (source == nullptr) return false;
    std::size_t n = 0;
    while (n < capacity && source[n] != '\0') ++n;
    if (n == capacity) return false;
    std::memcpy(destination, source, n + 1);
    return true;
}

bool CopyPath(wchar_t *destination, const wchar_t *source) {
    if (source == nullptr) return false;
    std::size_t n = 0;
    while (n < kMaxPath && source[n] != L'\0') ++n;
    if (n == kMaxPath) return false;
    std::memcpy(destination, source, (n + 1) * sizeof(wchar_t));
    return true;
}

bool ValidUtf8(const char *text) {
    return text != nullptr && MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0) > 0;
}

bool ValidName(const char *name) {
    if (name == nullptr || name[0] == '\0') return false;
    std::size_t n = 0;
    while (n < kMaxName && name[n] != '\0') ++n;
    if (n == kMaxName || !ValidUtf8(name)) return false;
    if (name[0] == '/' || name[n - 1] == '/') return false;
    std::size_t component = 0;
    for (std::size_t i = 0; i <= n; ++i) {
        const unsigned char ch = static_cast<unsigned char>(name[i]);
        if (i != n && (ch < 32 || ch == 127 || ch == '\\' || ch == ':' ||
            ch == '*' || ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|'))
            return false;
        if (i == n || ch == '/') {
            if (i == component || name[i - 1] == '.' || name[i - 1] == ' ')
                return false;
            // Windows device names are unsafe even as ZIP entries.
            char base[16] = {};
            std::size_t k = 0;
            while (component + k < i && name[component + k] != '.' && k < 15) {
                char c = name[component + k];
                base[k++] = c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c;
            }
            if (std::strcmp(base, "CON") == 0 || std::strcmp(base, "PRN") == 0 ||
                std::strcmp(base, "AUX") == 0 || std::strcmp(base, "NUL") == 0 ||
                ((std::strncmp(base, "COM", 3) == 0 || std::strncmp(base, "LPT", 3) == 0) &&
                 k == 4 && base[3] >= '1' && base[3] <= '9')) return false;
            component = i + 1;
        }
    }
    return true;
}

bool ConflictingName(const char *a, const char *b) {
    wchar_t wa[kMaxName] = {}, wb[kMaxName] = {};
    const int na = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, a, -1, wa, kMaxName) - 1;
    const int nb = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, b, -1, wb, kMaxName) - 1;
    if (na < 0 || nb < 0) return true;
    const int common = na < nb ? na : nb;
    if (CompareStringOrdinal(wa, common, wb, common, TRUE) != CSTR_EQUAL) return false;
    return na == nb || (na < nb ? wb[na] == L'/' : wa[nb] == L'/');
}

bool NativePath(wchar_t *destination, const wchar_t *source) {
    if (std::wcsncmp(source, L"\\\\?\\", 4) == 0) return CopyPath(destination, source);
    return swprintf_s(destination, kMaxPath, L"\\\\?\\%ls", source) >= 0;
}

bool LocalAbsolutePath(const wchar_t *path) {
    // No UNC, network shares, device namespaces, ADS, or relative paths.
    if (path == nullptr) return false;
    const wchar_t *p = path;
    if (std::wcsncmp(p, L"\\\\?\\", 4) == 0) p += 4;
    if (!((p[0] >= L'A' && p[0] <= L'Z') || (p[0] >= L'a' && p[0] <= L'z')) ||
        p[1] != L':' || p[2] != L'\\') return false;
    for (const wchar_t *s = p + 3; *s; ++s) {
        if (*s == L':' || *s == L'/' || *s < 32) return false;
        if ((s == p + 3 || s[-1] == L'\\') && s[0] == L'.' &&
            (s[1] == L'\\' || s[1] == 0 ||
             (s[1] == L'.' && (s[2] == L'\\' || s[2] == 0)))) return false;
    }
    wchar_t root[4] = {p[0], L':', L'\\', 0};
    return GetDriveTypeW(root) == DRIVE_FIXED;
}

Job *CreateJob(const wchar_t *existing_staging_directory, const char *metadata_utf8) {
    auto *job = new (std::nothrow) Job{};
    if (job == nullptr) return nullptr;
    if (!CopyPath(job->directory, existing_staging_directory) ||
        !LocalAbsolutePath(job->directory) ||
        !CopyText(job->metadata, sizeof(job->metadata), metadata_utf8) ||
        !ValidUtf8(job->metadata)) { delete job; return nullptr; }
    wchar_t native[kMaxPath] = {};
    if (!NativePath(native, job->directory)) { delete job; return nullptr; }
    const DWORD attributes = GetFileAttributesW(native);
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) { delete job; return nullptr; }
    return job;
}

Artifact *ReserveArtifact(Job &job, const char *name) {
    if (job.executed || job.count >= kMaxFiles || !ValidName(name) ||
        ConflictingName(name, "manifest.json") || ConflictingName(name, "capture-info.txt")) {
        ++job.rejected_inputs; return nullptr;
    }
    for (unsigned int i = 0; i < job.count; ++i) {
        if (ConflictingName(name, job.files[i].name)) { ++job.rejected_inputs; return nullptr; }
    }
    Artifact *item = &job.files[job.count++];
    CopyText(item->name, sizeof(item->name), name);
    return item;
}

bool AddFile(Job &job, const wchar_t *source_path, const char *zip_name_utf8,
             FileKind kind, bool required = false, unsigned long long max_bytes = 0) {
    if (kind != FileKind::Copy && kind != FileKind::LogTail && kind != FileKind::HashOnly) {
        ++job.rejected_inputs; return false;
    }
    Artifact *item = ReserveArtifact(job, zip_name_utf8);
    if (item == nullptr) return false;
    item->kind = kind;
    item->required = required;
    const auto hard_limit = kind == FileKind::HashOnly ? kHashLimit :
                            (kind == FileKind::LogTail ? kLogLimit : kCopyLimit);
    item->limit = max_bytes == 0 || max_bytes > hard_limit ? hard_limit : max_bytes;
    if (!CopyPath(item->source, source_path) || !LocalAbsolutePath(item->source)) {
        item->supplied_missing = true;
        CopyText(item->reason, sizeof(item->reason), "invalid-or-nonlocal-source-path");
    }
    return true;
}

bool AddMissing(Job &job, const char *zip_name_utf8, const char *reason_utf8,
                bool required = false) {
    Artifact *item = ReserveArtifact(job, zip_name_utf8);
    if (item == nullptr) return false;
    item->required = required;
    item->supplied_missing = true;
    if (!CopyText(item->reason, sizeof(item->reason), reason_utf8) ||
        !ValidUtf8(item->reason) || item->reason[0] == '\0')
        CopyText(item->reason, sizeof(item->reason), "unavailable-reason-invalid");
    return true;
}

void RequestCancel(Job &job) { job.cancelled.store(true, std::memory_order_release); }
void DestroyJob(Job *job) { delete job; }

// Standard SHA-256 (FIPS 180-4), incremental and independent of a crypto DLL.
struct Sha256 {
    std::uint32_t state[8] = {0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
                            0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
    unsigned char block[64] = {};
    unsigned int used = 0;
    unsigned long long length = 0;
    static std::uint32_t R(std::uint32_t x, unsigned int n) { return (x >> n) | (x << (32 - n)); }
    void Transform() {
        static constexpr std::uint32_t k[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        std::uint32_t w[64] = {};
        for (unsigned int i = 0; i < 16; ++i)
            w[i] = (static_cast<std::uint32_t>(block[i*4]) << 24) |
                (static_cast<std::uint32_t>(block[i*4+1]) << 16) |
                (static_cast<std::uint32_t>(block[i*4+2]) << 8) | block[i*4+3];
        for (unsigned int i = 16; i < 64; ++i) {
            const auto a = R(w[i-15],7) ^ R(w[i-15],18) ^ (w[i-15] >> 3);
            const auto b = R(w[i-2],17) ^ R(w[i-2],19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + a + w[i-7] + b;
        }
        auto a=state[0],b=state[1],cc=state[2],d=state[3];
        auto e=state[4],f=state[5],g=state[6],h=state[7];
        for (unsigned int i=0;i<64;++i) {
            const auto t1=h+(R(e,6)^R(e,11)^R(e,25))+((e&f)^(~e&g))+k[i]+w[i];
            const auto t2=(R(a,2)^R(a,13)^R(a,22))+((a&b)^(a&cc)^(b&cc));
            h=g;g=f;f=e;e=d+t1;d=cc;cc=b;b=a;a=t1+t2;
        }
        state[0]+=a;state[1]+=b;state[2]+=cc;state[3]+=d;
        state[4]+=e;state[5]+=f;state[6]+=g;state[7]+=h;
    }
    void Update(const unsigned char *data, std::size_t size) {
        length += size;
        while (size != 0) {
            const std::size_t count = size < 64-used ? size : 64-used;
            std::memcpy(block+used,data,count);used+=static_cast<unsigned int>(count);
            data+=count;size-=count;
            if (used==64) { Transform();used=0; }
        }
    }
    void Finish(char hex[65]) {
        const auto bits=length*8;
        block[used++]=0x80;
        if (used>56) { std::memset(block+used,0,64-used);Transform();used=0; }
        std::memset(block+used,0,56-used);
        for (unsigned int i=0;i<8;++i) block[63-i]=static_cast<unsigned char>(bits>>(i*8));
        Transform();
        static constexpr char digits[]="0123456789abcdef";
        for (unsigned int i=0;i<32;++i) {
            const auto byte=static_cast<unsigned char>(state[i/4]>>(24-(i%4)*8));
            hex[i*2]=digits[byte>>4];hex[i*2+1]=digits[byte&15];
        }
        hex[64]=0;
    }
};

std::uint32_t UpdateCrc(const Job &job, std::uint32_t crc,
                        const unsigned char *data, std::size_t size) {
    for (std::size_t i=0;i<size;++i) crc=job.crc_table[(crc^data[i])&255]^(crc>>8);
    return crc;
}

unsigned long long ArchiveLimit(const Job &job) { return job.archive_limit < kArchiveLimit ? job.archive_limit : kArchiveLimit; }

bool Write(Job &job, const void *data, DWORD size) {
    if (job.archive_bytes > ArchiveLimit(job) || size > ArchiveLimit(job) - job.archive_bytes) { SetLastError(ERROR_FILE_TOO_LARGE); return false; }
    DWORD written=0;
    if (!WriteFile(job.archive,data,size,&written,nullptr)) return false;
    if (written!=size) { SetLastError(ERROR_WRITE_FAULT); return false; }
    job.archive_bytes+=size;
    return true;
}
bool W16(Job &job, std::uint16_t value) {
    const unsigned char bytes[2]={static_cast<unsigned char>(value),static_cast<unsigned char>(value>>8)};
    return Write(job,bytes,2);
}
bool W32(Job &job, std::uint32_t value) {
    const unsigned char bytes[4]={static_cast<unsigned char>(value),static_cast<unsigned char>(value>>8),
        static_cast<unsigned char>(value>>16),static_cast<unsigned char>(value>>24)};
    return Write(job,bytes,4);
}
bool LocalHeader(Job &job,const char *name) {
    const auto n=static_cast<std::uint16_t>(std::strlen(name));
    // UTF-8 + data descriptor; Store method; fixed valid DOS date 1980-01-01.
    return W32(job,0x04034b50)&&W16(job,20)&&W16(job,0x0808)&&W16(job,0)&&
        W16(job,0)&&W16(job,33)&&W32(job,0)&&W32(job,0)&&W32(job,0)&&
        W16(job,n)&&W16(job,0)&&Write(job,name,n);
}
bool EndEntry(Job &job,const char *name,unsigned long long offset,
               unsigned long long size,std::uint32_t crc) {
    if (job.central_count>=kMaxFiles+2 || offset>UINT32_MAX || size>UINT32_MAX) return false;
    if (!W32(job,0x08074b50)||!W32(job,crc)||!W32(job,static_cast<std::uint32_t>(size))||
        !W32(job,static_cast<std::uint32_t>(size))) return false;
    auto &entry=job.central[job.central_count++];
    CopyText(entry.name,sizeof(entry.name),name);
    entry.offset=static_cast<std::uint32_t>(offset);
    entry.size=static_cast<std::uint32_t>(size);entry.crc=crc;
    return true;
}
bool Rollback(Job &job,unsigned long long offset) {
    LARGE_INTEGER target{};target.QuadPart=static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(job.archive,target,nullptr,FILE_BEGIN)||!SetEndOfFile(job.archive)) return false;
    job.archive_bytes=offset;return true;
}

void Missing(Artifact &item,const char *reason,DWORD error=ERROR_SUCCESS) {
    item.included=false;item.error=error;item.sha256[0]=0;
    CopyText(item.reason,sizeof(item.reason),reason);
}
const char *StopReason(const Job &job) {
    if (job.cancelled.load(std::memory_order_acquire)) return "request-cancelled";
    if (GetTickCount64()-job.started>=kTimeLimitMs) return "collection-time-limit";
    return nullptr;
}

bool Collect(Job &job,Artifact &item) {
    if (item.supplied_missing) return true;
    if (const char *stop=StopReason(job)) { Missing(item,stop);return true; }
    const DWORD share=FILE_SHARE_READ|FILE_SHARE_DELETE|
        (item.kind==FileKind::LogTail ? FILE_SHARE_WRITE : 0);
    wchar_t native_source[kMaxPath]={};
    if (!NativePath(native_source,item.source)) { Missing(item,"source-path-too-long");return true; }
    HANDLE file=CreateFileW(native_source,GENERIC_READ,share,nullptr,OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL|FILE_FLAG_SEQUENTIAL_SCAN|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if (file==INVALID_HANDLE_VALUE) { Missing(item,"source-open-failed",GetLastError());return true; }
    BY_HANDLE_FILE_INFORMATION before{},after{};
    if (!GetFileInformationByHandle(file,&before) || GetFileType(file)!=FILE_TYPE_DISK ||
        (before.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))!=0) {
        Missing(item,"source-is-not-regular-file",GetLastError());CloseHandle(file);return true;
    }
    item.source_size=(static_cast<unsigned long long>(before.nFileSizeHigh)<<32)|before.nFileSizeLow;
    if (item.source_size>item.limit && item.kind!=FileKind::LogTail) {
        Missing(item,"file-size-limit");CloseHandle(file);return true;
    }
    const auto bytes=item.source_size>item.limit ? item.limit : item.source_size;
    item.source_offset=item.source_size-bytes;
    item.truncated=item.source_offset!=0;
    if (bytes>kReadLimit-job.read_bytes) { Missing(item,"collection-byte-limit");CloseHandle(file);return true; }
    // Reserve central-directory and bounded manifest space before adding data.
    if (item.kind!=FileKind::HashOnly && (job.archive_bytes > ArchiveLimit(job) ||
        bytes+sizeof(job.manifest)+65536 > ArchiveLimit(job)-job.archive_bytes)) { Missing(item,"archive-size-limit");CloseHandle(file);return true; }
    LARGE_INTEGER start{};start.QuadPart=static_cast<LONGLONG>(item.source_offset);
    if (!SetFilePointerEx(file,start,nullptr,FILE_BEGIN)) {
        Missing(item,"source-seek-failed",GetLastError());CloseHandle(file);return true;
    }
    const auto entry_start=job.archive_bytes;
    const bool archive_file=item.kind!=FileKind::HashOnly;
    if (archive_file && !LocalHeader(job,item.name)) { CloseHandle(file);return false; }
    Sha256 sha{};std::uint32_t crc=0xffffffffu;
    unsigned long long remaining=bytes;
    const char *failure=nullptr;DWORD failure_code=ERROR_SUCCESS;
    while (remaining!=0) {
        if ((failure=StopReason(job))!=nullptr) break;
        const DWORD count=remaining<sizeof(job.buffer) ? static_cast<DWORD>(remaining) : sizeof(job.buffer);
        DWORD read=0;
        if (!ReadFile(file,job.buffer,count,&read,nullptr) || read!=count) {
            failure="source-read-incomplete";failure_code=GetLastError();break;
        }
        job.read_bytes+=read;
        if (archive_file && !Write(job,job.buffer,read)) { CloseHandle(file);return false; }
        sha.Update(job.buffer,read);crc=UpdateCrc(job,crc,job.buffer,read);remaining-=read;
    }
    if (!GetFileInformationByHandle(file,&after)) {
        if (failure==nullptr) { failure="source-recheck-failed";failure_code=GetLastError(); }
    } else {
        item.source_changed=before.nFileSizeHigh!=after.nFileSizeHigh || before.nFileSizeLow!=after.nFileSizeLow ||
            CompareFileTime(&before.ftLastWriteTime,&after.ftLastWriteTime)!=0;
        if (item.source_changed && item.kind!=FileKind::LogTail && failure==nullptr)
            failure="source-changed-during-read";
    }
    CloseHandle(file);
    if (failure!=nullptr) {
        Missing(item,failure,failure_code);
        return !archive_file || Rollback(job,entry_start);
    }
    item.bytes=bytes;item.crc=crc^0xffffffffu;sha.Finish(item.sha256);
    if (archive_file && !EndEntry(job,item.name,entry_start,bytes,item.crc)) return false;
    item.included=true;
    return true;
}

bool Text(Job &job,const char *text) {
    const auto length=std::strlen(text);
    if (length>=sizeof(job.manifest)-job.manifest_length) return false;
    std::memcpy(job.manifest+job.manifest_length,text,length);
    job.manifest_length+=length;job.manifest[job.manifest_length]=0;return true;
}
bool JsonString(Job &job,const char *text) {
    if (!Text(job,"\"")) return false;
    for (const unsigned char *p=reinterpret_cast<const unsigned char *>(text);*p;++p) {
        char escaped[7]={};
        if (*p=='"'||*p=='\\') { escaped[0]='\\';escaped[1]=static_cast<char>(*p); }
        else if (*p<32) std::snprintf(escaped,sizeof(escaped),"\\u%04x",*p);
        else escaped[0]=static_cast<char>(*p);
        if (!Text(job,escaped)) return false;
    }
    return Text(job,"\"");
}
bool Number(Job &job,unsigned long long value) {
    char text[32]={};std::snprintf(text,sizeof(text),"%llu",value);return Text(job,text);
}
bool Manifest(Job &job) {
    auto &r=job.result;
    for (unsigned int i=0;i<job.count;++i) {
        const auto &a=job.files[i];
        if (a.included) {
            if (a.kind==FileKind::HashOnly) ++r.hashed_modules;else ++r.included_files;
            if (a.truncated) ++r.truncated_files;
        } else { ++r.missing_files;if (a.required) r.required_complete=false; }
    }
    r.status=r.missing_files||r.truncated_files||job.rejected_inputs ? Status::Partial : Status::Complete;
    if (!Text(job,"{\n\"format\":\"DLSS5-DiagnosticBundle-v1\",\n\"complete\":") ||
        !Text(job,r.status==Status::Complete ? "true" : "false") ||
        !Text(job,",\n\"required_complete\":")||!Text(job,r.required_complete ? "true" : "false") ||
        !Text(job,",\n\"input_rejections\":")||!Number(job,job.rejected_inputs)||
        !Text(job,",\n\"metadata_entry\":\"capture-info.txt\",\n\"metadata_text\":")||!JsonString(job,job.metadata)||
        !Text(job,",\n\"collection\":\"explicit allowlist only; no directory recursion, uploads or process dumps\",\n"
                  "\"live_log_contract\":\"initial-length bounded byte snapshot; concurrent writes may be non-atomic; SHA256 covers copied bytes only\",\n"
                  "\"module_hash_contract\":\"hash of opened loaded-module path file; not a memory-image hash; DLL bytes are not included\",\n"
                  "\"entries\":[\n")) return false;
    for (unsigned int i=0;i<job.count;++i) {
        const auto &a=job.files[i];
        if ((i!=0&&!Text(job,",\n"))||!Text(job,"{\"name\":")||!JsonString(job,a.name)||
            !Text(job,",\"kind\":")||!JsonString(job,a.kind==FileKind::Copy ? "copy" :
                (a.kind==FileKind::LogTail ? "log-tail" : "module-hash-only"))||
            !Text(job,",\"status\":")||!JsonString(job,a.included ?
                (a.truncated ? "included-truncated" : (a.kind==FileKind::HashOnly ? "hashed" : "included")) : "missing")||
            !Text(job,",\"required\":")||!Text(job,a.required ? "true" : "false")||
            !Text(job,",\"reason\":")||!JsonString(job,a.reason)||
            !Text(job,",\"win32_error\":")||!Number(job,a.error)||
            !Text(job,",\"source_size\":")||!Number(job,a.source_size)||
            !Text(job,",\"source_offset\":")||!Number(job,a.source_offset)||
            !Text(job,",\"bytes\":")||!Number(job,a.bytes)||
            !Text(job,",\"source_changed_during_read\":")||!Text(job,a.source_changed ? "true" : "false")||
            !Text(job,",\"sha256\":")||!JsonString(job,a.sha256)||
            !Text(job,",\"crc32\":")||!Number(job,a.crc)||!Text(job,"}")) return false;
    }
    return Text(job,"\n]}\n");
}

bool FinishZip(Job &job) {
    const auto start=job.archive_bytes;
    for (unsigned int i=0;i<job.central_count;++i) {
        const auto &e=job.central[i];const auto n=static_cast<std::uint16_t>(std::strlen(e.name));
        if (!W32(job,0x02014b50)||!W16(job,20)||!W16(job,20)||!W16(job,0x0808)||!W16(job,0)||
            !W16(job,0)||!W16(job,33)||!W32(job,e.crc)||!W32(job,e.size)||!W32(job,e.size)||
            !W16(job,n)||!W16(job,0)||!W16(job,0)||!W16(job,0)||!W16(job,0)||
            !W32(job,0)||!W32(job,e.offset)||!Write(job,e.name,n)) return false;
    }
    const auto bytes=job.archive_bytes-start;
    return W32(job,0x06054b50)&&W16(job,0)&&W16(job,0)&&
        W16(job,static_cast<std::uint16_t>(job.central_count))&&
        W16(job,static_cast<std::uint16_t>(job.central_count))&&
        W32(job,static_cast<std::uint32_t>(bytes))&&W32(job,static_cast<std::uint32_t>(start))&&W16(job,0);
}

Result FailedResult(Job &job,DWORD error) {
    job.result.status=Status::Failed;
    job.result.required_complete=false;
    job.result.win32_error=error;
    // A path is only handed back once this session published a complete ZIP.
    // In particular, an existing ZIP belongs to another session and is not ours.
    job.result.zip_path[0]=0;
    return job.result;
}

Result Run(Job &job) {
    Result failed{};
    failed.required_complete=false;
    bool expected=false;
    if (!g_running.compare_exchange_strong(expected,true,std::memory_order_acq_rel)) {
        failed.win32_error=ERROR_BUSY;return failed;
    }
    struct Guard { ~Guard() { g_running.store(false,std::memory_order_release); } } guard;
    if (job.executed) { failed.win32_error=ERROR_INVALID_STATE;return failed; }
    job.executed=true;job.started=GetTickCount64();
    // No thread is created here: the caller already owns the pinned worker.
    wchar_t partial[kMaxPath]={},native_partial[kMaxPath]={},native_zip[kMaxPath]={},native_directory[kMaxPath]={};
    if (swprintf_s(job.result.zip_path,L"%ls\\feedback.zip",job.directory)<0 ||
        swprintf_s(partial,L"%ls\\feedback.zip.partial",job.directory)<0 ||
        !NativePath(native_partial,partial)||!NativePath(native_zip,job.result.zip_path)||
        !NativePath(native_directory,job.directory)) {
        return FailedResult(job,ERROR_FILENAME_EXCED_RANGE);
    }
    // Hold the validated final directory against rename/removal while publishing.
    // Reparse points are opened as themselves and rejected, never followed here.
    HANDLE directory=CreateFileW(native_directory,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,
        nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if (directory==INVALID_HANDLE_VALUE) return FailedResult(job,GetLastError());
    struct DirectoryGuard { HANDLE value; ~DirectoryGuard() { CloseHandle(value); } } directory_guard{directory};
    BY_HANDLE_FILE_INFORMATION directory_info{};
    if (!GetFileInformationByHandle(directory,&directory_info)||
        !(directory_info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||
        (directory_info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)) {
        return FailedResult(job,ERROR_DIRECTORY);
    }
    if (GetFileAttributesW(native_zip)!=INVALID_FILE_ATTRIBUTES) {
        return FailedResult(job,ERROR_FILE_EXISTS);
    }
    job.archive=CreateFileW(native_partial,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    if (job.archive==INVALID_HANDLE_VALUE) return FailedResult(job,GetLastError());
    for (unsigned int i=0;i<256;++i) {
        std::uint32_t crc=i;
        for (unsigned int bit=0;bit<8;++bit) crc=(crc>>1)^((crc&1)?0xedb88320u:0u);
        job.crc_table[i]=crc;
    }
    bool ok=true;
    for (unsigned int i=0;ok&&i<job.count;++i) ok=Collect(job,job.files[i]);
    if (ok) {
        const auto offset=job.archive_bytes;
        const auto size=static_cast<DWORD>(std::strlen(job.metadata));
        const auto crc=UpdateCrc(job,0xffffffffu,reinterpret_cast<const unsigned char *>(job.metadata),size)^0xffffffffu;
        ok=LocalHeader(job,"capture-info.txt")&&Write(job,job.metadata,size)&&
            EndEntry(job,"capture-info.txt",offset,size,crc);
    }
    if (ok) ok=Manifest(job);
    if (ok) {
        const auto offset=job.archive_bytes;
        const auto crc=UpdateCrc(job,0xffffffffu,reinterpret_cast<const unsigned char *>(job.manifest),job.manifest_length)^0xffffffffu;
        ok=LocalHeader(job,"manifest.json")&&Write(job,job.manifest,static_cast<DWORD>(job.manifest_length))&&
            EndEntry(job,"manifest.json",offset,job.manifest_length,crc)&&FinishZip(job)&&FlushFileBuffers(job.archive);
    }
    DWORD error=ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(job.archive);job.archive=INVALID_HANDLE_VALUE;
    if (ok && !MoveFileW(native_partial,native_zip)) { ok=false;error=GetLastError(); }
    if (!ok) {
        // Only the exact temporary file created by this Run is removed.
        DeleteFileW(native_partial);
        return FailedResult(job,error==ERROR_SUCCESS ? ERROR_WRITE_FAULT : error);
    }
    return job.result;
}

} // namespace nr_bundle
