#pragma once
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

// Little-endian, fixed-width transport shared by the in-game recorder and the
// independent replay tool. No process pointers, native structure dumps or paths.
// Payloads contain complete mip-0 typed shader values, not a screenshot/ROI.
namespace nr_replay {
constexpr std::uint32_t kVersion = 1;
constexpr std::uint32_t kPlanes = 7;
constexpr std::uint32_t kParameters = 64;
constexpr std::uint32_t kFrames = 16;
constexpr std::uint64_t kFrameLimit = 512ull << 20;
constexpr std::uint64_t kResidentLimit = 1536ull << 20;
constexpr std::uint64_t kSequenceLimit = 8ull << 30;
enum class Role : std::uint32_t { Source, Proxy, Neural, Depth, Motion, Exposure, Composite };
enum class Encoding : std::uint32_t { Float32 = 1, Float16 = 2 };
enum class ScalarType : std::uint32_t { UInt32 = 1, Int32 = 2, Float32 = 3 };
enum class Completion : std::uint32_t { Incomplete, Complete, Invalidated };
struct Plane {
    std::uint32_t role = 0, width = 0, height = 0, channels = 0;
    std::uint32_t storage_format = 0, view_format = 0, encoding = 0, reserved = 0;
    std::uint32_t x = 0, y = 0, resource_width = 0, resource_height = 0;
    std::uint64_t offset = 0, bytes = 0;
};
struct Scalar {
    char name[64]{};
    std::uint32_t type = 0;
    std::uint32_t present = 0;
    std::uint32_t value = 0;
    std::uint32_t reserved = 0;
};
struct Header {
    char magic[8] = {'N','R','R','P','L','Y','1',0};
    std::uint32_t version = kVersion, header_bytes = 0;
    std::uint64_t sequence = 0, instance = 0, frame = 0;
    std::uint32_t index = 0, target_frames = kFrames, completion = 0, mode = 0;
    std::uint32_t config = 0, evaluation = 0, since_reset = 0, reset = 0;
    std::uint32_t source_x = 0, source_y = 0, source_width = 0, source_height = 0;
    std::uint32_t work_width = 0, work_height = 0, codec_flags = 0, color_mode = 0;
    std::uint32_t parameter_count = 0, plane_count = kPlanes;
    // Presence observations of our NR block: 0 unknown, 1 null, 2 non-null.
    std::uint32_t control_mask_before = 0, control_mask_after = 0;
    std::uint32_t auto_mask_before = 0, auto_mask_after = 0;
    float pre_exposure = 1, exposure_scale = 1, proxy_scale = 1;
    std::uint32_t source_dirty = 0;
    char source_commit[48]{}, build[48]{};
    Plane planes[kPlanes]{};
    Scalar parameters[kParameters]{};
    std::uint32_t codec_constant_count = 0, codec_pass = 0;
    std::uint32_t codec_constants[64]{};
    std::uint64_t payload_bytes = 0;
};
static_assert(std::is_trivially_copyable_v<Header>);
static_assert(sizeof(Plane) == 64 && sizeof(Scalar) == 80);
static_assert(sizeof(Header) == 6080, "Replay file ABI changed; version the reader too");
inline bool PlaneBytes(std::uint32_t width, std::uint32_t height,
                       std::uint32_t channels, Encoding encoding,
                       std::uint64_t &bytes) {
    bytes = 0;
    if (!width || !height || width > 16384 || height > 16384 ||
        (channels != 1 && channels != 2 && channels != 4) ||
        (encoding != Encoding::Float32 && encoding != Encoding::Float16)) return false;
    const auto size = std::uint64_t(width) * height * channels *
        (encoding == Encoding::Float16 ? 2u : 4u);
    // Each dispatch writes complete uint words. Odd scalar half planes are
    // padded, with padding initialized by the shader rather than left undefined.
    bytes = (size + 3u) & ~std::uint64_t(3u);
    return bytes <= kFrameLimit;
}
inline bool AppendPlane(Header &header, Role role, std::uint32_t width,
                        std::uint32_t height, std::uint32_t channels,
                        std::uint32_t storage, std::uint32_t view, Encoding encoding) {
    const auto index = static_cast<std::uint32_t>(role);
    if (index >= kPlanes || header.planes[index].bytes) return false;
    std::uint64_t bytes = 0;
    if (!PlaneBytes(width,height,channels,encoding,bytes)) return false;
    const auto offset = (header.payload_bytes + 15u) & ~std::uint64_t(15u);
    if (offset > kFrameLimit || bytes > kFrameLimit - offset) return false;
    header.planes[index] = {index,width,height,channels,storage,view,
        static_cast<std::uint32_t>(encoding),0,0,0,width,height,offset,bytes};
    header.payload_bytes = offset + bytes;
    header.header_bytes = sizeof(Header);
    return true;
}
inline bool ValidHeader(const Header &header) {
    const Header reference{};
    if (std::memcmp(header.magic,reference.magic,8) || header.version != kVersion ||
        header.header_bytes != sizeof(Header) || header.plane_count != kPlanes ||
        header.parameter_count > kParameters || !header.sequence ||
        header.codec_constant_count > 64 || header.codec_pass > 2 || header.source_dirty > 1 ||
        header.target_frames != kFrames || header.index >= kFrames ||
        !header.payload_bytes || header.payload_bytes > kFrameLimit ||
        (header.mode != 1 && header.mode != 2) ||
        !header.work_width || !header.work_height ||
        !header.source_width || !header.source_height) return false;
    std::uint64_t end = 0;
    for (std::uint32_t i=0; i<kPlanes; ++i) {
        const auto &plane = header.planes[i];
        if (!plane.bytes) {
            if (i != static_cast<std::uint32_t>(Role::Exposure)) return false;
            continue;
        }
        std::uint64_t expected = 0;
        if (plane.role != i || !PlaneBytes(plane.width,plane.height,plane.channels,
                static_cast<Encoding>(plane.encoding),expected) || plane.bytes != expected ||
            !plane.resource_width || !plane.resource_height ||
            plane.x>plane.resource_width || plane.width>plane.resource_width-plane.x ||
            plane.y>plane.resource_height || plane.height>plane.resource_height-plane.y ||
            plane.offset < end || plane.offset % 16 ||
            plane.offset > header.payload_bytes || plane.bytes > header.payload_bytes-plane.offset)
            return false;
        end = plane.offset + plane.bytes;
    }
    if (end != header.payload_bytes) return false;
    const auto &proxy=header.planes[static_cast<unsigned>(Role::Proxy)];
    const auto &neural=header.planes[static_cast<unsigned>(Role::Neural)];
    if(proxy.width!=header.work_width || proxy.height!=header.work_height ||
       neural.width!=proxy.width || neural.height!=proxy.height ||
       header.planes[0].width!=header.source_width || header.planes[0].height!=header.source_height ||
       header.planes[0].x!=header.source_x || header.planes[0].y!=header.source_y)
        return false;
    for (std::uint32_t i=0;i<header.parameter_count;++i) {
        const auto &parameter = header.parameters[i];
        if (!std::memchr(parameter.name,0,sizeof(parameter.name)) || !parameter.name[0] ||
            parameter.type < 1 || parameter.type > 3 || parameter.present > 1) return false;
        for(std::uint32_t j=0;j<i;++j)if(std::strcmp(parameter.name,header.parameters[j].name)==0)return false;
    }
    return true;
}
// Sequence assembly never silently drops a frame. A complete replay starts at
// the first successful evaluation of a fresh/reset instance and stays coherent.
struct Sequence {
    std::uint64_t id=0, instance=0, bytes=0;
    std::uint32_t next=0, config=0, mode=0;
    bool failed=false;
    bool Accept(const Header &header) {
        if (failed || !ValidHeader(header) || header.completion !=
            static_cast<std::uint32_t>(Completion::Complete) || header.index != next ||
            header.payload_bytes > kSequenceLimit-bytes) return failed=true,false;
        if (!next) {
            if (!header.reset || header.since_reset != 1 || header.evaluation != 1)
                return failed=true,false;
            id=header.sequence;instance=header.instance;config=header.config;mode=header.mode;
        } else if(header.sequence!=id || header.instance!=instance || header.config!=config ||
                  header.mode!=mode || header.evaluation!=next+1 || header.since_reset!=next+1 ||
                  header.reset) return failed=true,false;
        bytes+=header.payload_bytes;++next;return true;
    }
    bool Complete() const { return !failed && next==kFrames; }
};
} // namespace nr_replay
