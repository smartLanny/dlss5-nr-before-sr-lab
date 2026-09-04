// F8 controlled comparison session.  This file is included by the addon after
// nr_feedback_capture.inl, so it deliberately uses only fixed-size state on
// render/Preset callbacks.  The worker owns all filesystem and ZIP work.
#ifndef NR_HAS_DETAIL_STABILITY
#define NR_HAS_DETAIL_STABILITY 0
#endif

namespace nr_f8 {

constexpr unsigned int kGroupCount = 3;
constexpr unsigned int kFrameCount = 16;
constexpr unsigned int kMaxReport = 32768;
constexpr unsigned int kMaxPath = 1024;
constexpr unsigned int kMaxSequenceBytes = 64u * 1024u * 1024u;
constexpr unsigned int kRawTileSide = 256;
constexpr unsigned int kRawTileValues = kRawTileSide * kRawTileSide * 4;
constexpr ULONGLONG kCountdownMs = 3000;
constexpr ULONGLONG kGroupTimeoutMs = 30000;

enum class Status : unsigned int { Idle, Requested, Running, Restoring,
    Packaging, Complete, Partial, Failed };
enum class GroupMode : unsigned int { NativeSr, NrToSr, SrToNr };

struct Options {
    unsigned int issue = 0; // all, detail, halo, flicker, colour
    float intensity = 1.0f;
    bool intensity_override = false;
    bool region_protection = false;
    bool nr_jitter_experiment = false;
    unsigned int roi_x = 0;
    unsigned int roi_y = 0;
    bool custom_roi = false;
};

struct Frame {
    NrDiagnosticIdentity identity = {};
    unsigned int width = 0, height = 0;
    unsigned int reset = 0;
    float jitter_x = NAN, jitter_y = NAN;
    float mv_x = NAN, mv_y = NAN;
    float exposure_scale = NAN;
    bool exposure_valid = false;
    bool params_known = false;
    bool reset_known = false;
    // The means remain useful when a GPU stage callback is unavailable. When
    // available, raw_planes carries four actual RGBA32F ROI tiles per frame.
    float plane_mean[4] = {NAN, NAN, NAN, NAN};
    unsigned int raw_width = 0, raw_height = 0, raw_stride = 0;
    bool raw_available = false;
};

struct Group {
    GroupMode mode = GroupMode::NativeSr;
    ChainConfig requested = {};
    unsigned int config_generation = 0;
    ULONGLONG started = 0, deadline = 0;
    bool applied = false;
    bool screenshot_requested = false;
    bool screenshot_received = false;
    bool sequence_complete = false;
    bool partial = false;
    bool stage_available = false;
    bool stage_complete = false;
    unsigned long long stage_request = 0;
    unsigned int raw_stage_count = 0;
    bool raw_stage_required = false;
    char stage_reason[192] = {};
    char break_reason[192] = {};
    wchar_t screenshot[kMaxPath] = {};
    wchar_t stage_directory[kMaxPath] = {};
    Frame frames[kFrameCount] = {};
    unsigned int frame_count = 0;
    unsigned int segment_count = 0;
    unsigned int dropped_frames = 0;
    unsigned int roi_x = 0, roi_y = 0, roi_width = 0, roi_height = 0;
    Frame pending_params = {};
    bool pending_params_valid = false;
};

struct Session {
    ULONGLONG started = 0, group_started = 0;
    ChainConfig original = {};
    Options options = {};
    Group groups[kGroupCount] = {};
    unsigned int group = 0;
    bool restore_issued = false;
    bool restore_verified = false;
    reshade::api::effect_runtime *runtime_identity = nullptr;
    bool report_truncated = false;
    bool partial = false;
    unsigned int report_used = 0;
    char report[kMaxReport] = {};
    float *raw_planes = nullptr; // [group][frame][plane][RGBA tile]
};

std::atomic<Status> g_status{Status::Idle};
std::atomic<bool> g_request{false};
std::atomic<bool> g_cancel{false};
SRWLOCK g_lock = SRWLOCK_INIT;
Session *g_session = nullptr;
wchar_t g_last_zip[kMaxPath] = {};
std::atomic<bool> g_config_hold{false};
std::atomic<bool> g_raw_stage_ready{false};
std::atomic<bool> g_window_open{false};
// Present uses this while publishing a new group or restoring the original
// config. Diagnostic callbacks must not inspect a half-published group.
std::atomic<bool> g_group_transition{false};
std::atomic<unsigned int> g_active_roi_x{0};
std::atomic<unsigned int> g_active_roi_y{0};
std::atomic<unsigned int> g_active_roi_width{0};
std::atomic<unsigned int> g_active_roi_height{0};

void GetActiveRoi(unsigned int &x, unsigned int &y, unsigned int &width,
                  unsigned int &height) {
    x = g_active_roi_x.load(std::memory_order_acquire);
    y = g_active_roi_y.load(std::memory_order_acquire);
    width = g_active_roi_width.load(std::memory_order_acquire);
    height = g_active_roi_height.load(std::memory_order_acquire);
}

float *RawPlane(Session &session, unsigned int group, unsigned int frame,
                unsigned int plane) {
    const std::size_t index = (((static_cast<std::size_t>(group) * kFrameCount +
        frame) * 4u + plane) * kRawTileValues);
    return session.raw_planes != nullptr ? session.raw_planes + index : nullptr;
}

const float *RawPlane(const Session &session, unsigned int group,
                      unsigned int frame, unsigned int plane) {
    const std::size_t index = (((static_cast<std::size_t>(group) * kFrameCount +
        frame) * 4u + plane) * kRawTileValues);
    return session.raw_planes != nullptr ? session.raw_planes + index : nullptr;
}

void Append(Session &s, const char *format, ...) {
    if (s.report_used >= sizeof(s.report) - 256) {
        s.report_truncated = true;
        return;
    }
    va_list args;
    va_start(args, format);
    const int n = std::vsnprintf(s.report + s.report_used,
        sizeof(s.report) - s.report_used - 256, format, args);
    va_end(args);
    if (n < 0 || static_cast<std::size_t>(n) >= sizeof(s.report) -
            s.report_used - 256) {
        s.report_truncated = true;
        s.report[s.report_used] = 0;
    } else {
        s.report_used += static_cast<unsigned int>(n);
    }
}

const char *ModeName(GroupMode mode) {
    return mode == GroupMode::NativeSr ? "native-sr" :
        mode == GroupMode::NrToSr ? "mode2-nr-to-sr" : "mode1-sr-to-nr";
}

const char *IssueName(unsigned int issue) {
    switch (issue) {
    case 1: return "detail";
    case 2: return "halo";
    case 3: return "flicker";
    case 4: return "colour";
    default: return "all";
    }
}

const char *StatusName(Status status) {
    switch (status) {
    case Status::Requested: return "requested";
    case Status::Running: return "running";
    case Status::Restoring: return "restoring";
    case Status::Packaging: return "packaging";
    case Status::Complete: return "complete";
    case Status::Partial: return "partial";
    case Status::Failed: return "failed";
    default: return "idle";
    }
}

void SetConfigHold(bool enabled) {
    g_config_hold.store(enabled, std::memory_order_release);
}

bool KeepStageContext() {
    const Status state = g_status.load(std::memory_order_acquire);
    return state == Status::Running;
}

bool IsActive() {
    const Status state = g_status.load(std::memory_order_acquire);
    return state == Status::Requested || state == Status::Running ||
        state == Status::Restoring;
}

// UI choices are published before Present consumes the request. This remains
// a tiny fixed record and does not call ApplyConfig under the ImGui lock.
Options g_pending_options = {};

bool RequestWithOptions(const Options &options) {
    AcquireSRWLockExclusive(&g_lock);
    const Status state = g_status.load(std::memory_order_relaxed);
    if ((state != Status::Idle && state != Status::Complete &&
         state != Status::Partial && state != Status::Failed) ||
        g_request.load(std::memory_order_relaxed)) {
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    g_pending_options = options;
    g_request.store(true, std::memory_order_release);
    g_window_open.store(false, std::memory_order_release);
    g_status.store(Status::Requested, std::memory_order_release);
    ReleaseSRWLockExclusive(&g_lock);
    return true;
}

void OpenWindow() {
    if (!IsActive()) g_window_open.store(true, std::memory_order_release);
}

void Cancel() {
    AcquireSRWLockExclusive(&g_lock);
    const Status state = g_status.load(std::memory_order_relaxed);
    if (state == Status::Requested && g_session == nullptr) {
        g_pending_options = {};
        g_request.store(false, std::memory_order_release);
        g_status.store(Status::Idle, std::memory_order_release);
        g_window_open.store(false, std::memory_order_release);
    } else if (state == Status::Running || state == Status::Restoring) {
        g_cancel.store(true, std::memory_order_release);
    } else if (state == Status::Idle) {
        g_window_open.store(false, std::memory_order_release);
    }
    ReleaseSRWLockExclusive(&g_lock);
}

void ResetWindowAfterStart() {
    g_window_open.store(false, std::memory_order_release);
}

ChainConfig GroupConfig(const ChainConfig &base, GroupMode mode,
                        const Options &options) {
    ChainConfig config = base;
    config.enabled = mode != GroupMode::NativeSr;
    config.mode = mode == GroupMode::SrToNr ? 1 : 2;
    if (options.intensity_override) config.intensity = options.intensity;
#if NR_HAS_DETAIL_STABILITY
    config.detail_stability = options.region_protection ? 1.0f : 0.0f;
    config.motion_stability = options.region_protection;
#else
    (void)options;
#endif
    return config;
}

void MarkBreak(Group &group, const char *reason) {
    group.partial = true;
    group.sequence_complete = false;
    std::strncpy(group.break_reason, reason, sizeof(group.break_reason) - 1);
    group.break_reason[sizeof(group.break_reason) - 1] = 0;
    ++group.segment_count;
    group.frame_count = 0;
}

bool IdentityMatches(const Group &group, const NrDiagnosticIdentity &identity,
                     unsigned int width, unsigned int height) {
    if (group.frame_count == 0) return true;
    const Frame &previous = group.frames[group.frame_count - 1];
    return previous.identity.instance_id == identity.instance_id &&
        previous.identity.config_generation == identity.config_generation &&
        identity.frame_id == previous.identity.frame_id + 1 &&
        previous.width == width && previous.height == height;
}

void ObserveParams(const NgxParams *params, const FeatureRecord &, unsigned long long) {
    if (!IsActive() || g_group_transition.load(std::memory_order_acquire) ||
        params == nullptr) return;
    // CompleteNrDiagnosticEvaluation runs under g_chain_cs. Keep this path
    // try-only so Present's short session lock cannot invert the chain lock.
    if (!TryAcquireSRWLockExclusive(&g_lock)) return;
    if (g_session != nullptr && g_session->group < kGroupCount) {
        Group &group = g_session->groups[g_session->group];
        // Store the newest parameters in an unused frame until the matching
        // successful identity arrives. A successful callback then consumes it.
        Frame &frame = group.pending_params;
        frame = {};
        frame.params_known = true;
        int reset_value = 0;
        frame.reset_known = GetInt(params, "Reset", &reset_value) != false;
        frame.reset = reset_value != 0 ? 1u : 0u;
        frame.exposure_valid = GetFloat(params, "DLSS.Exposure.Scale", &frame.exposure_scale) &&
            std::isfinite(frame.exposure_scale) && frame.exposure_scale > 0.0f;
        GetFloat(params, "Jitter.Offset.X", &frame.jitter_x);
        GetFloat(params, "Jitter.Offset.Y", &frame.jitter_y);
        GetFloat(params, "MV.Scale.X", &frame.mv_x);
        GetFloat(params, "MV.Scale.Y", &frame.mv_y);
        group.pending_params_valid = true;
    }
    ReleaseSRWLockExclusive(&g_lock);
}

void ObserveSuccess(const NrDiagnosticIdentity &identity, unsigned int width,
                    unsigned int height, bool reset) {
    if (g_status.load(std::memory_order_relaxed) != Status::Running ||
        g_group_transition.load(std::memory_order_acquire)) return;
    // CompleteNrDiagnosticEvaluation runs under g_chain_cs. Keep this path
    // try-only so Present's short session lock cannot invert the chain lock.
    if (!TryAcquireSRWLockExclusive(&g_lock)) return;
    Session *session = g_session;
    if (session == nullptr || session->group >= kGroupCount) {
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }
    Group &group = session->groups[session->group];
    if (group.mode == GroupMode::NativeSr) {
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }
    if (!group.applied && identity.config_generation == group.config_generation)
        group.applied = true;
    if (!group.applied || !identity.warmed_up) {
        group.pending_params_valid = false;
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }
    group.roi_width = std::min(256u, width);
    group.roi_height = std::min(256u, height);
    group.roi_x = group.roi_x < width ? group.roi_x : 0;
    group.roi_y = group.roi_y < height ? group.roi_y : 0;
    if (group.roi_x + group.roi_width > width)
        group.roi_x = width > group.roi_width ? width - group.roi_width : 0;
    if (group.roi_y + group.roi_height > height)
        group.roi_y = height > group.roi_height ? height - group.roi_height : 0;
    if (identity.config_generation != group.config_generation) {
        MarkBreak(group, "config generation changed; sequence restarted");
        group.applied = false;
        group.pending_params_valid = false;
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }
    if (reset) {
        MarkBreak(group, "Reset boundary; sequence restarted");
        group.pending_params_valid = false;
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }
    if (!IdentityMatches(group, identity, width, height)) {
        MarkBreak(group, "instance or extent changed; sequence restarted");
    }
    if (group.frame_count < kFrameCount) {
        Frame pending = group.pending_params_valid ? group.pending_params : Frame{};
        group.pending_params_valid = false;
        Frame &frame = group.frames[group.frame_count++];
        frame = pending;
        frame.identity = identity;
        frame.width = width;
        frame.height = height;
        frame.reset = reset ? 1u : 0u;
        // If ObserveParams supplied a matching slot, preserve those fields;
        // otherwise they remain NaN/unknown. No values are invented.
        frame.params_known = frame.params_known && frame.identity.frame_id != 0;
        frame.plane_mean[0] = g_last_diagnostic_luma[0].load(std::memory_order_relaxed);
        frame.plane_mean[1] = g_last_diagnostic_luma[1].load(std::memory_order_relaxed);
        frame.plane_mean[2] = g_last_diagnostic_luma[2].load(std::memory_order_relaxed);
        frame.plane_mean[3] = g_last_diagnostic_luma[3].load(std::memory_order_relaxed);
        if (group.frame_count == kFrameCount) group.sequence_complete = true;
    }
    ReleaseSRWLockExclusive(&g_lock);
}

void StageExported(unsigned long long request, const wchar_t *directory, bool ok) {
    if (g_group_transition.load(std::memory_order_acquire)) return;
    AcquireSRWLockExclusive(&g_lock);
    if (g_session != nullptr) {
      for (unsigned int i = 0; i < kGroupCount; ++i) {
        Group &group = g_session->groups[i];
        if (group.stage_request != request) continue;
        group.stage_complete = true;
        group.stage_available = ok;
        if (ok && directory != nullptr) wcsncpy_s(group.stage_directory,
            directory, _TRUNCATE);
        std::strcpy(group.stage_reason, ok ? "exported after exact GPU fence" :
            "stage export failed; raw four-plane sample unavailable");
        break;
      }
    }
    ReleaseSRWLockExclusive(&g_lock);
}

void StageCaptured(const NrStageCaptureInfo &info, const wchar_t *directory,
                   const float *mapped, UINT64 bytes, bool ok) {
    if (g_group_transition.load(std::memory_order_acquire)) return;
    AcquireSRWLockExclusive(&g_lock);
    if (g_session != nullptr) {
        for (unsigned int i = 0; i < kGroupCount; ++i) {
            Group &group = g_session->groups[i];
            if (group.stage_request != info.request_id) continue;
            group.stage_complete = true;
            const bool ordinary_stage_export = !KeepStageContext();
            group.stage_available = group.stage_available || (ok && ordinary_stage_export);
            if (ok) {
                ++group.raw_stage_count;
                if (directory != nullptr) wcsncpy_s(group.stage_directory,
                    directory, _TRUNCATE);
                for (unsigned int frame_index = 0;
                     frame_index < group.frame_count; ++frame_index) {
                    Frame &frame = group.frames[frame_index];
                    if (frame.identity.instance_id != info.identity.instance_id ||
                        frame.identity.frame_id != info.identity.frame_id ||
                        frame.identity.config_generation != info.identity.config_generation)
                        continue;
                    const unsigned int stride = std::max(1u, info.stride);
                    const unsigned int tile_width = std::min(kRawTileSide,
                        (group.roi_width + stride - 1) / stride);
                    const unsigned int tile_height = std::min(kRawTileSide,
                        (group.roi_height + stride - 1) / stride);
                    const std::size_t plane_values = static_cast<std::size_t>(
                        info.width) * info.height * 4u;
                    const unsigned int sx = std::min(info.width,
                        group.roi_x / stride);
                    const unsigned int sy = std::min(info.height,
                        group.roi_y / stride);
                    const float *raw_end = mapped != nullptr ? mapped +
                        std::min<std::size_t>(bytes / sizeof(float),
                            plane_values * 4u) : nullptr;
                    bool copied = raw_end != nullptr && info.width != 0 &&
                        info.height != 0 && sx < info.width && sy < info.height;
                    for (unsigned int plane = 0; copied && plane < 4; ++plane) {
                        float *destination = RawPlane(*g_session, i,
                            frame_index, plane);
                        const float *source = mapped + plane * plane_values;
                        for (unsigned int y = 0; y < tile_height && copied; ++y) {
                            const unsigned int source_y = sy + y;
                            if (source_y >= info.height) { copied = false; break; }
                            for (unsigned int x = 0; x < tile_width; ++x) {
                                const unsigned int source_x = sx + x;
                                if (source_x >= info.width) { copied = false; break; }
                                const float *pixel = source +
                                    (static_cast<std::size_t>(source_y) * info.width + source_x) * 4u;
                                std::memcpy(destination +
                                    (static_cast<std::size_t>(y) * tile_width + x) * 4u,
                                    pixel, 4u * sizeof(float));
                            }
                        }
                    }
                    if (copied) {
                        frame.raw_width = tile_width;
                        frame.raw_height = tile_height;
                        frame.raw_stride = stride;
                        frame.raw_available = true;
                    }
                    break;
                }
            }
            group.stage_request = 0;
            if (!ordinary_stage_export)
                std::strcpy(group.stage_reason, "raw ROI four-plane tile embedded in sequence.bin");
            g_raw_stage_ready.store(true, std::memory_order_release);
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
}

void Screenshot(reshade::api::effect_runtime *runtime, const char *path) {
    if (runtime == nullptr || path == nullptr) return;
    AcquireSRWLockExclusive(&g_lock);
    if (g_session != nullptr && g_session->runtime_identity == runtime &&
        g_session->group < kGroupCount) {
        Group &group = g_session->groups[g_session->group];
        if (group.screenshot_requested && !group.screenshot_received) {
            const char *base = path;
            for (const char *p = path; *p; ++p)
                if (*p == '/' || *p == '\\') base = p + 1;
            char tag[96] = {};
            std::snprintf(tag, sizeof(tag), "DLSS5_F8_%u", g_session->group);
            if (std::strstr(base, tag) != nullptr &&
                MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1,
                    group.screenshot, static_cast<int>(std::size(group.screenshot))) != 0) {
                wchar_t canonical[kMaxPath] = {};
                const DWORD n = GetFullPathNameW(group.screenshot, kMaxPath,
                    canonical, nullptr);
                if (n != 0 && n < kMaxPath) {
                    std::wcscpy(group.screenshot, canonical);
                    group.screenshot_received = true;
                }
            }
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
}

void AppendFrameMetadata(Session &session, Group &group) {
    Append(session, "group.%u.mode=%s\n", session.group, ModeName(group.mode));
    Append(session, "group.%u.sequenceFrames=%u\n", session.group, group.frame_count);
    Append(session, "group.%u.sequenceComplete=%d\n", session.group,
        group.sequence_complete ? 1 : 0);
    Append(session, "group.%u.segmentCount=%u\n", session.group, group.segment_count);
    Append(session, "group.%u.roi=%u,%u,%u,%u;domain=original-pixel\n", session.group,
        group.roi_x, group.roi_y, group.roi_width, group.roi_height);
    Append(session, "group.%u.partial=%d\n", session.group, group.partial ? 1 : 0);
    Append(session, "group.%u.breakReason=%s\n", session.group,
        group.break_reason[0] ? group.break_reason : "none");
    for (unsigned int i = 0; i < group.frame_count; ++i) {
        const Frame &frame = group.frames[i];
        Append(session, "frame,%u,%llu,%llu,%u,%ux%u,%u,%.9g,%.9g,%.9g,%.9g,%.9g,%d,%s,planes=%.9g/%.9g/%.9g/%.9g\n",
            i, frame.identity.frame_id, frame.identity.instance_id,
            frame.identity.config_generation, frame.width, frame.height,
            frame.reset, frame.jitter_x, frame.jitter_y, frame.mv_x, frame.mv_y,
            frame.exposure_scale, frame.exposure_valid ? 1 : 0,
            frame.params_known ? "known" : "unknown", frame.plane_mean[0],
            frame.plane_mean[1], frame.plane_mean[2], frame.plane_mean[3]);
        Append(session, "frame.%u.rawROI=%d,%ux%u,stride=%u\n", i,
            frame.raw_available ? 1 : 0, frame.raw_width, frame.raw_height,
            frame.raw_stride);
    }
}

bool WriteSequenceBytes(HANDLE file, const void *data, std::size_t bytes) {
    if (bytes == 0) return true;
    if (data == nullptr || bytes > static_cast<std::size_t>(0xffffffffull)) return false;
    DWORD written = 0;
    return WriteFile(file, data, static_cast<DWORD>(bytes), &written, nullptr) != FALSE &&
        written == static_cast<DWORD>(bytes);
}

bool WriteSequenceU32(HANDLE file, std::uint32_t value) {
    unsigned char bytes[4] = {
        static_cast<unsigned char>(value & 0xffu),
        static_cast<unsigned char>((value >> 8) & 0xffu),
        static_cast<unsigned char>((value >> 16) & 0xffu),
        static_cast<unsigned char>((value >> 24) & 0xffu)};
    return WriteSequenceBytes(file, bytes, sizeof(bytes));
}

bool WriteSequenceU64(HANDLE file, std::uint64_t value) {
    unsigned char bytes[8] = {};
    for (unsigned int i = 0; i < 8; ++i)
        bytes[i] = static_cast<unsigned char>((value >> (i * 8u)) & 0xffu);
    return WriteSequenceBytes(file, bytes, sizeof(bytes));
}

bool WriteSequenceF32(HANDLE file, float value) {
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "unexpected float size");
    std::memcpy(&bits, &value, sizeof(bits));
    return WriteSequenceU32(file, bits);
}

// Fixed on-disk sequence format. Every field is written explicitly so the
// output never depends on Frame's padding, bool representation, or compiler.
bool WriteSequenceFrame(HANDLE file, const Frame &frame, const Session &session,
                        unsigned int group_index, unsigned int frame_index) {
    constexpr std::uint32_t kFrameVersion = 1;
    constexpr std::uint32_t kParamsKnown = 1u << 0;
    constexpr std::uint32_t kResetKnown = 1u << 1;
    constexpr std::uint32_t kExposureValid = 1u << 2;
    constexpr std::uint32_t kRawAvailable = 1u << 3;
    const std::uint32_t flags = (frame.params_known ? kParamsKnown : 0u) |
        (frame.reset_known ? kResetKnown : 0u) |
        (frame.exposure_valid ? kExposureValid : 0u) |
        (frame.raw_available ? kRawAvailable : 0u);
    bool ok = WriteSequenceU32(file, kFrameVersion) &&
        WriteSequenceU32(file, frame_index) &&
        WriteSequenceU64(file, frame.identity.frame_id) &&
        WriteSequenceU64(file, frame.identity.instance_id) &&
        WriteSequenceU32(file, frame.identity.config_generation) &&
        WriteSequenceU32(file, frame.width) && WriteSequenceU32(file, frame.height) &&
        WriteSequenceU32(file, frame.reset) && WriteSequenceU32(file, flags) &&
        WriteSequenceF32(file, frame.jitter_x) && WriteSequenceF32(file, frame.jitter_y) &&
        WriteSequenceF32(file, frame.mv_x) && WriteSequenceF32(file, frame.mv_y) &&
        WriteSequenceF32(file, frame.exposure_scale);
    for (float value : frame.plane_mean) ok = ok && WriteSequenceF32(file, value);
    ok = ok && WriteSequenceU32(file, frame.raw_width) &&
        WriteSequenceU32(file, frame.raw_height) &&
        WriteSequenceU32(file, frame.raw_stride);
    const std::size_t raw_bytes = frame.raw_available ?
        static_cast<std::size_t>(frame.raw_width) * frame.raw_height * 4u * sizeof(float) : 0u;
    ok = ok && raw_bytes <= static_cast<std::size_t>(0xffffffffull) &&
        WriteSequenceU32(file, static_cast<std::uint32_t>(raw_bytes));
    if (!ok || raw_bytes == 0) return ok;
    if (session.raw_planes == nullptr || frame.raw_width == 0 || frame.raw_height == 0)
        return false;
    for (unsigned int plane = 0; plane < 4; ++plane) {
        const float *values = RawPlane(session, group_index, frame_index, plane);
        if (values == nullptr || !WriteSequenceBytes(file, values, raw_bytes)) return false;
    }
    return true;
}

bool WriteSequence(const wchar_t *directory, unsigned int index,
                   const Session &session, const Group &group) {
    wchar_t path[kMaxPath] = {};
    if (swprintf_s(path, L"%ls\\sequence-%u.bin", directory, index) < 0) return false;
    HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const char magic[8] = {'F', '8', 'S', 'E', 'Q', 0, 0, 2};
    bool ok = WriteSequenceBytes(file, magic, sizeof(magic)) &&
        WriteSequenceU32(file, 2) && // format version
        WriteSequenceU32(file, static_cast<std::uint32_t>(group.mode)) &&
        WriteSequenceU32(file, group.roi_x) && WriteSequenceU32(file, group.roi_y) &&
        WriteSequenceU32(file, group.roi_width) && WriteSequenceU32(file, group.roi_height) &&
        WriteSequenceU32(file, group.frame_count) && WriteSequenceU32(file, 4) &&
        WriteSequenceU32(file, 1); // ROI coordinates are original pixels.
    for (unsigned int i = 0; ok && i < group.frame_count; ++i) {
        const Frame &frame = group.frames[i];
        ok = WriteSequenceFrame(file, frame, session,
            static_cast<unsigned int>(&group - session.groups), i);
    }
    CloseHandle(file);
    if (!ok) DeleteFileW(path);
    return ok;
}

bool AddStageFiles(nr_bundle::Job &job, const Group &group,
                   unsigned int index) {
    if (!group.stage_available || !group.stage_directory[0]) return false;
    for (const wchar_t *leaf : {L"original.pfm", L"proxy.pfm", L"neural.pfm",
                                L"composited.pfm", L"metadata.txt", L"stages.rgba32f"}) {
        wchar_t source[kMaxPath] = {};
        if (swprintf_s(source, L"%ls\\%ls", group.stage_directory, leaf) < 0) return false;
        char leaf8[64] = {};
        WideToUtf8(leaf, leaf8, sizeof(leaf8));
        char name[160] = {};
        std::snprintf(name, sizeof(name), "groups/%u/stages/%s", index, leaf8);
        nr_bundle::AddFile(job, source, name, nr_bundle::FileKind::Copy, false);
    }
    return true;
}

bool WriteOverview(const wchar_t *directory, const Session &session) {
    wchar_t path[kMaxPath] = {};
    if (swprintf_s(path, L"%ls\\overview.txt", directory) < 0) return false;
    HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    char text[4096] = {};
    int used = std::snprintf(text, sizeof(text),
        "format=DLSS5-F8-overview-v1\n"
        "identity=three independent Present screenshots; no same-frame final SR claim\n"
        "nativeSrOutput=unknown\n");
    for (unsigned int i = 0; used > 0 && static_cast<std::size_t>(used) < sizeof(text) &&
         i < kGroupCount; ++i) {
        const Group &group = session.groups[i];
        const int n = std::snprintf(text + used, sizeof(text) - used,
            "group.%u.mode=%s\nscreenshot=%s\nsequenceFrames=%u\nrawROIFrames=%u\n",
            i, ModeName(group.mode), group.screenshot_received ? "present" : "missing",
            group.frame_count, group.raw_stage_count);
        if (n <= 0) break;
        used += n;
    }
    DWORD written = 0;
    const bool ok = used > 0 && static_cast<std::size_t>(used) < sizeof(text) &&
        WriteFile(file, text, static_cast<DWORD>(used), &written, nullptr) != FALSE &&
        written == static_cast<DWORD>(used);
    CloseHandle(file);
    return ok;
}

DWORD WINAPI Worker(void *parameter) {
    Session *session = static_cast<Session *>(parameter);
    nr_bundle::Result result = {};
    result.status = nr_bundle::Status::Failed;
    wchar_t directory[kMaxPath] = {};
    if (FeedbackCreateDirectory(directory)) {
        auto *job = nr_bundle::CreateJob(directory, session->report);
        if (job != nullptr) {
            nr_bundle::AddFile(*job, g_log_path, "logs/nr-current.log",
                nr_bundle::FileKind::LogTail, true);
            nr_bundle::AddFile(*job, g_ini_path, "settings/nr_before_sr.ini",
                nr_bundle::FileKind::Copy, false);
            nr_bundle::AddFile(*job, g_reshade_ini_path, "settings/ReShade.ini",
                nr_bundle::FileKind::Copy, false);
            FeedbackSession module_metadata = {};
            FeedbackAddModules(*job, module_metadata);
            for (unsigned int i = 0; i < kGroupCount; ++i) {
                const Group &group = session->groups[i];
                char name[128] = {};
                const wchar_t *extension = wcsrchr(group.screenshot, L'.');
                char ext[16] = {};
                if (extension != nullptr) WideToUtf8(extension, ext, sizeof(ext));
                std::snprintf(name, sizeof(name), "groups/%u/display%s", i, ext);
                if (group.screenshot_received)
                    nr_bundle::AddFile(*job, group.screenshot, name,
                        nr_bundle::FileKind::Copy, true, 128ull * 1024 * 1024);
                else nr_bundle::AddMissing(*job, name,
                    "ReShade did not deliver this group's display screenshot", true);
                wchar_t seq[kMaxPath] = {};
                swprintf_s(seq, L"%ls\\sequence-%u.bin", directory, i);
                if (!WriteSequence(directory, i, *session, group)) {
                    char seq_name[128] = {};
                    std::snprintf(seq_name, sizeof(seq_name), "groups/%u/sequence.bin", i);
                    nr_bundle::AddMissing(*job, seq_name, "sequence writer failed", group.mode != GroupMode::NativeSr);
                } else if (group.mode != GroupMode::NativeSr && !group.sequence_complete) {
                    char seq_name[128] = {};
                    std::snprintf(seq_name, sizeof(seq_name), "groups/%u/sequence.bin", i);
                    nr_bundle::AddMissing(*job, seq_name,
                        group.break_reason[0] ? group.break_reason :
                        "fewer than 16 consecutive successful samples", true);
                } else {
                    char seq_name[128] = {};
                    std::snprintf(seq_name, sizeof(seq_name), "groups/%u/sequence.bin", i);
                    nr_bundle::AddFile(*job, seq, seq_name, nr_bundle::FileKind::Copy, false,
                        kMaxSequenceBytes);
                }
                if (!AddStageFiles(*job, group, i)) {
                    char missing_name[96] = {};
                    std::snprintf(missing_name, sizeof(missing_name),
                        "groups/%u/stages", i);
                    nr_bundle::AddMissing(*job, missing_name,
                        group.stage_reason[0] ? group.stage_reason :
                        "four-plane GPU stage readback unavailable", false);
                }
            }
            wchar_t overview[kMaxPath] = {};
            swprintf_s(overview, L"%ls\\overview.txt", directory);
            if (WriteOverview(directory, *session))
                nr_bundle::AddFile(*job, overview, "overview.txt",
                    nr_bundle::FileKind::Copy, false);
            else nr_bundle::AddMissing(*job, "overview.txt",
                "overview writer failed", false);
            nr_bundle::AddMissing(*job, "native-sr-same-frame",
                "native SR output state cannot be proven or read; display screenshots are separate Present events", false);
            result = nr_bundle::Run(*job);
            nr_bundle::DestroyJob(job);
        }
    }
    AcquireSRWLockExclusive(&g_lock);
    if (result.zip_path[0]) std::wcscpy(g_last_zip, result.zip_path);
    g_session = nullptr;
    g_status.store(result.status == nr_bundle::Status::Failed ? Status::Failed :
        result.required_complete ? Status::Complete : Status::Partial,
        std::memory_order_release);
    ReleaseSRWLockExclusive(&g_lock);
    delete[] session->raw_planes;
    delete session;
    return 0;
}

void StartGroup(Session &session, unsigned int index) {
    g_group_transition.store(true, std::memory_order_release);
    Group &group = session.groups[index];
    group = {};
    group.mode = index == 0 ? GroupMode::NativeSr :
        index == 1 ? GroupMode::NrToSr : GroupMode::SrToNr;
    group.segment_count = 1;
    group.requested = GroupConfig(session.original, group.mode, session.options);
    group.started = GetTickCount64();
    group.deadline = group.started + kGroupTimeoutMs;
    ApplyConfig(group.requested, "F8 comparison group", false);
    group.config_generation = g_config_generation.load(std::memory_order_acquire);
    group.roi_width = 256;
    group.roi_height = 256;
    if (session.options.custom_roi) {
        group.roi_x = session.options.roi_x;
        group.roi_y = session.options.roi_y;
    } else {
        group.roi_x = g_last_nr_width.load(std::memory_order_relaxed) > 256
            ? (g_last_nr_width.load(std::memory_order_relaxed) - 256) / 2 : 0;
        group.roi_y = g_last_nr_height.load(std::memory_order_relaxed) > 256
            ? (g_last_nr_height.load(std::memory_order_relaxed) - 256) / 2 : 0;
    }
    g_active_roi_x.store(group.roi_x, std::memory_order_release);
    g_active_roi_y.store(group.roi_y, std::memory_order_release);
    g_active_roi_width.store(group.roi_width, std::memory_order_release);
    g_active_roi_height.store(group.roi_height, std::memory_order_release);
    if (group.mode != GroupMode::NativeSr) {
        const char *blocked = FeedbackStageBlockReason(group.requested);
        if (blocked != nullptr) {
            std::strncpy(group.stage_reason, blocked, sizeof(group.stage_reason) - 1);
        } else if (RequestStageCapture()) {
            group.stage_request = g_stage_capture_request_id.load(
                std::memory_order_acquire);
            group.raw_stage_required = true;
        } else {
            std::strcpy(group.stage_reason,
                "another stage request is already in progress");
        }
    }
    session.group_started = group.started;
    Log("[F8] group=%u mode=%s started; config is transient and will be restored",
        index, ModeName(group.mode));
    g_group_transition.store(false, std::memory_order_release);
}

void Finish(Session &session) {
    for (unsigned int i = 0; i < kGroupCount; ++i)
        session.partial = session.partial || session.groups[i].partial;
    session.partial = session.partial || session.report_truncated;
    for (unsigned int i = 0; i < kGroupCount; ++i) {
        session.group = i;
        AppendFrameMetadata(session, session.groups[i]);
    }
    Append(session, "capture.format=DLSS5-F8-v2\nstatus=%s\nissue=%s\n"
        "intensity=%.9g\nregionProtection.requested=%d\n"
        "regionProtection.effective=%s\nNRjitterExperiment.requested=%d\n"
        "NRjitterExperiment.effective=unknown\nroi=%s\n"
        "nativeSrOutput=unknown\ndisplayScreenshotIdentity=Present-only; not same-frame final SR\n"
        "config.restored=%d\nconfig.writesToIni=0\nmetadata.truncated=%d\n",
        session.partial ? "partial" : "complete", IssueName(session.options.issue),
        static_cast<double>(session.options.intensity), session.options.region_protection ? 1 : 0,
#if NR_HAS_DETAIL_STABILITY
        session.options.region_protection ? "applied" : "off",
#else
        session.options.region_protection ? "unknown-baseline-no-protection-field" : "off",
#endif
        session.options.nr_jitter_experiment ? 1 : 0,
        session.options.custom_roi ? "custom" : "center-256x256-original-pixel", session.restore_verified ? 1 : 0,
        session.report_truncated ? 1 : 0);
}

void FinalizeSession(Session *session) {
    if (session == nullptr) return;
    // Chain and filesystem operations stay outside g_lock.
    g_group_transition.store(true, std::memory_order_release);
    ApplyConfig(session->original, "F8 restore", false);
    session->restore_issued = true;
    session->restore_verified = SameConfig(SnapshotConfig(), session->original);
    SetConfigHold(false);
    Finish(*session);
    AcquireSRWLockExclusive(&g_lock);
    if (g_session == session) {
        g_session = nullptr;
        g_status.store(Status::Packaging, std::memory_order_release);
    }
    ReleaseSRWLockExclusive(&g_lock);
    g_group_transition.store(false, std::memory_order_release);
    if (!QueueUserWorkItem(&Worker, session, WT_EXECUTEDEFAULT)) {
        delete[] session->raw_planes;
        delete session;
        g_status.store(Status::Failed, std::memory_order_release);
    }
    g_cancel.store(false, std::memory_order_release);
}

DWORD WINAPI F8Watchdog(void *parameter) {
    auto *session = static_cast<Session *>(parameter);
    for (;;) {
        Sleep(250);
        bool finalize = false;
        AcquireSRWLockExclusive(&g_lock);
        if (g_session != session ||
            g_status.load(std::memory_order_relaxed) != Status::Running) {
            ReleaseSRWLockExclusive(&g_lock);
            return 0;
        }
        if (!g_group_transition.load(std::memory_order_acquire)) {
            const unsigned int index = session->group;
            const bool cancelled = g_cancel.load(std::memory_order_acquire);
            const bool expired = index >= kGroupCount ||
                GetTickCount64() >= session->groups[index].deadline;
            if (cancelled || expired) {
                if (index < kGroupCount) {
                    Group &group = session->groups[index];
                    group.partial = true;
                    if (!group.break_reason[0]) std::strcpy(group.break_reason,
                        cancelled ? "cancelled by user" : "group timed out");
                }
                session->partial = true;
                session->group = kGroupCount;
                g_group_transition.store(true, std::memory_order_release);
                finalize = true;
            }
        }
        ReleaseSRWLockExclusive(&g_lock);
        if (finalize) {
            FinalizeSession(session);
            return 0;
        }
    }
}

void Present(reshade::api::effect_runtime *runtime) {
    if (runtime == nullptr) return;
    bool consume_request = false;
    AcquireSRWLockExclusive(&g_lock);
    if (g_request.load(std::memory_order_relaxed) &&
        g_status.load(std::memory_order_relaxed) == Status::Requested) {
        g_request.store(false, std::memory_order_release);
        g_status.store(Status::Running, std::memory_order_release);
        consume_request = true;
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (consume_request) {
            auto *session = new (std::nothrow) Session{};
            if (session == nullptr) {
                g_status.store(Status::Failed, std::memory_order_release);
            } else {
                session->started = GetTickCount64();
                session->runtime_identity = runtime;
                const std::size_t raw_values = static_cast<std::size_t>(
                    kGroupCount) * kFrameCount * 4u * kRawTileValues;
                session->raw_planes = new (std::nothrow) float[raw_values];
                if (session->raw_planes == nullptr) {
                    delete session;
                    g_status.store(Status::Failed, std::memory_order_release);
                    return;
                }
                std::fill(session->raw_planes, session->raw_planes + raw_values, NAN);
                session->original = SnapshotConfig();
                AcquireSRWLockExclusive(&g_lock);
                session->options = g_pending_options;
                g_pending_options = {};
                g_session = session;
                ReleaseSRWLockExclusive(&g_lock);
                SetConfigHold(true);
                StartGroup(*session, 0);
                if (!QueueUserWorkItem(&F8Watchdog, session, WT_EXECUTEDEFAULT))
                    Log("F8 watchdog could not start; Present timeout recovery remains active.");
            }
    }
    // Stage exports retire on a worker. Re-arm the next bounded sample from
    // Present after that exact fence has completed; no config generation or
    // NR context rebuild is involved.
    if (g_raw_stage_ready.exchange(false, std::memory_order_acq_rel)) {
        bool retry = false;
        AcquireSRWLockShared(&g_lock);
        if (g_session != nullptr && g_session->group < kGroupCount) {
            const Group &group = g_session->groups[g_session->group];
            retry = group.mode != GroupMode::NativeSr &&
                group.raw_stage_count < kFrameCount && group.stage_request == 0;
        }
        ReleaseSRWLockShared(&g_lock);
        if (retry && !RequestStageCapture())
            g_raw_stage_ready.store(true, std::memory_order_release);
        else if (retry) {
            AcquireSRWLockExclusive(&g_lock);
            if (g_session != nullptr && g_session->group < kGroupCount)
                g_session->groups[g_session->group].stage_request =
                    g_stage_capture_request_id.load(std::memory_order_acquire);
            ReleaseSRWLockExclusive(&g_lock);
        }
    }
    Session *session = nullptr;
    unsigned int next_group = kGroupCount;
    bool request_screenshot = false;
    bool finalize = false;
    {
        AcquireSRWLockExclusive(&g_lock);
        session = g_session;
        if (session == nullptr) {
            ReleaseSRWLockExclusive(&g_lock);
            return;
        }
        const unsigned int index = session->group;
        if (index >= kGroupCount) {
            finalize = true;
        } else {
            Group &group = session->groups[index];
            const ULONGLONG now = GetTickCount64();
            const bool cancelled = g_cancel.load(std::memory_order_acquire);
            if (cancelled || now >= group.deadline) {
                group.partial = true;
                if (!group.break_reason[0]) std::strcpy(group.break_reason,
                    cancelled ? "cancelled by user" : "group timed out");
                session->partial = true;
                session->group = kGroupCount;
                finalize = true;
                g_group_transition.store(true, std::memory_order_release);
            } else if (!group.screenshot_requested &&
                       now - group.started >= kCountdownMs) {
                group.screenshot_requested = true;
                request_screenshot = true;
            }
            const bool ready =
                (group.mode == GroupMode::NativeSr && group.screenshot_received) ||
                (group.sequence_complete && (!group.raw_stage_required ||
                    group.raw_stage_count >= kFrameCount));
            if (!finalize && ready && now - group.started >= kCountdownMs) {
                if (index + 1 < kGroupCount) {
                    ++session->group;
                    next_group = session->group;
                    g_group_transition.store(true, std::memory_order_release);
                } else {
                    session->group = kGroupCount;
                    finalize = true;
                    g_group_transition.store(true, std::memory_order_release);
                }
            }
        }
        ReleaseSRWLockExclusive(&g_lock);
    }
    if (request_screenshot) {
        char tag[96] = {};
        std::snprintf(tag, sizeof(tag), "DLSS5_F8_%u", session->group);
        // Screenshot is a ReShade call and must never run under g_lock.
        runtime->save_screenshot(tag);
    }
    if (next_group < kGroupCount) {
        StartGroup(*session, next_group);
        return;
    }
    if (!finalize) return;

    FinalizeSession(session);
}

void DrawControls() {
    const Status state = g_status.load(std::memory_order_acquire);
    if (!IsActive() && state != Status::Packaging &&
        !g_window_open.load(std::memory_order_acquire)) return;
    if (!IsActive() && state != Status::Packaging) {
        static int issue = 0;
        static int intensity = 0;
        static bool protection = false;
        static bool jitter = false;
        ImGui::SeparatorText(kChineseUi ? "F8 标准对比采集" : "F8 standard comparison capture");
        ImGui::Combo(kChineseUi ? "当前问题" : "Current issue", &issue,
            kChineseUi ? "全部\0细节\0光晕\0闪烁\0颜色\0" : "All\0Detail\0Halo\0Flicker\0Colour\0");
        const char *intensities = "1.0\0 1.2\0 1.5\0";
        ImGui::Combo(kChineseUi ? "强度" : "Intensity", &intensity, intensities);
        ImGui::Checkbox(kChineseUi ? "区域保护（当前版本能力未知时标 unknown）" :
            "Region protection (unknown when baseline has no field)", &protection);
        ImGui::Checkbox(kChineseUi ? "NR jitter 传递实验" : "NR jitter pass experiment", &jitter);
        if (ImGui::Button(kChineseUi ? "开始 F8 三组采集" : "Start F8 three-group capture")) {
            Options options = {};
            options.issue = static_cast<unsigned int>(issue);
            options.intensity = intensity == 0 ? 1.0f : intensity == 1 ? 1.2f : 1.5f;
            options.intensity_override = intensity != 0;
            options.region_protection = protection;
            options.nr_jitter_experiment = jitter;
            RequestWithOptions(options);
        }
    } else {
        ImGui::TextWrapped("%s", kChineseUi ? "F8 正在按原生 SR、Mode2 NR→SR、Mode1 SR→NR 顺序采集；每组先倒计时 3 秒并等待实际应用与连续样本。" :
            "F8 is collecting native SR, Mode 2 NR-to-SR and Mode 1 SR-to-NR; each group counts down 3 seconds and waits for applied continuous samples.");
        if (ImGui::Button(kChineseUi ? "取消 F8 采集" : "Cancel F8 capture")) Cancel();
    }
    ImGui::TextDisabled("%s: %s", kChineseUi ? "状态" : "Status",
        StatusName(state));
    if (state == Status::Complete || state == Status::Partial) {
        char path[4096] = {};
        AcquireSRWLockShared(&g_lock); WideToUtf8(g_last_zip, path, sizeof(path)); ReleaseSRWLockShared(&g_lock);
        ImGui::TextWrapped("%s", path[0] ? path : (kChineseUi ? "反馈包已完成。" : "Feedback package completed."));
    }
}

// Dedicated titled overlay. The F8 hotkey requests ReShade to make its
// overlay surface visible, so this window can appear even when the settings
// panel was closed; it does not depend on the settings overlay registration.
void DrawOverlay(reshade::api::effect_runtime *runtime) {
    if (runtime == nullptr) return;
    const Status state = g_status.load(std::memory_order_acquire);
    if (!g_window_open.load(std::memory_order_acquire) && !IsActive() &&
        state != Status::Packaging) return;
    bool open = true;
    if (ImGui::Begin(kChineseUi ? "F8 标准对比采集" : "F8 standard comparison capture",
                     &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        DrawControls();
    }
    ImGui::End();
    if (!open) Cancel();
}

} // namespace nr_f8
