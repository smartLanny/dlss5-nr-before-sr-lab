#pragma once
#include <cmath>
#include <cerrno>
#include <cwchar>
#include <cwctype>

// Work-policy parsing only. Model dimensions and 1..49% admission stay in
// their existing resolvers; this does not reinterpret old output presets.
namespace nr_work_config {
struct Work { int mode=0; float scale=1.0f; };
enum class Status { Accepted, Rejected, Disable };
struct Request { Work work; Status status=Status::Accepted; };
inline Work Initial(bool file_proven_absent) {
    return file_proven_absent ? Work{0,1.0f} : Work{1,0.75f};
}
inline bool Valid(Work value) {
    return value.mode>=0 && value.mode<=5 && std::isfinite(value.scale) &&
           value.scale>=0.5f && value.scale<=1.0f;
}
inline Request Resolve(Work requested, Work previous) {
    if (!Valid(previous)) previous={0,1.0f};
    if (requested.mode==5 && requested.scale==0.0f)
        return {previous,Status::Disable};
    return Valid(requested) ? Request{requested,Status::Accepted} :
                              Request{previous,Status::Rejected};
}
inline bool Finished(const wchar_t* begin, const wchar_t* end) {
    if (begin==end) return false;
    while (std::iswspace(*end)) ++end;
    return *end==L'\0';
}
// nullptr means missing key. Empty, nonfinite, partial and out-of-range values
// are invalid requests, not missing-key defaults or hidden clamps.
inline Request Parse(const wchar_t* mode, const wchar_t* scale, Work previous) {
    Work requested=previous;
    if (mode) {
        wchar_t* end=nullptr;
        const long value=std::wcstol(mode,&end,10);
        if (!Finished(mode,end) || value<0 || value>5)
            return {previous,Status::Rejected};
        requested.mode=static_cast<int>(value);
    }
    if (scale) {
        wchar_t* end=nullptr;
        errno=0;
        requested.scale=std::wcstof(scale,&end);
        if (errno==ERANGE || !Finished(scale,end)) return {previous,Status::Rejected};
    }
    return Resolve(requested,previous);
}
} // namespace nr_work_config
