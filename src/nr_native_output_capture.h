#pragma once
#include <cstdint>
#include <cstring>
#include <type_traits>

// Optional sidecar; the existing four-stage payload and NRF v1 stay unchanged.
// Raw native storage is not a claim about gamma, primaries or scene linearity.
namespace nr_native_capture {
enum class Status : std::uint32_t { Missing, Armed, Recorded, Complete };
enum class Reason : std::uint32_t {
    NotRequested, None, NotPreSr, NativeFailed, IdentityUnknown, StateUnknown,
    OutputMissing, UnsupportedStorage, InvalidRect, Budget, Allocation,
    HooksIncomplete, RecordingChanged, CompletionUnknown, ExportFailed,
    CommandStateUnknown, SourceChanged
};
constexpr std::uint64_t kReadbackLimit=128ull<<20;
struct Header {
    char magic[8]={'N','R','O','C','A','P','1',0};
    std::uint32_t version=1,header_bytes=320;
    Status status=Status::Missing;Reason reason=Reason::NotRequested;
    std::uint64_t request=0,instance=0,frame=0,feature=0,recording=0,reset=0,queue=0,fence=0;
    std::uint64_t source_stream=0,source_epoch=0,source_sequence=0;
    std::uint32_t config=0,evaluation=0,width=0,height=0,x=0,y=0,resource_width=0,resource_height=0;
    std::uint32_t storage_format=0,view_format=0,bytes_per_pixel=0,row_bytes=0;
    std::uint32_t state=0,create_flags=0,native_result=0,domain=0;
    float pre_exposure=1,exposure_scale=1;
    std::uint64_t payload_bytes=0;
    char source_commit[48]{},build[48]{};
    std::uint32_t reserved[8]{};
};
static_assert(sizeof(Header)==320 && std::is_trivially_copyable_v<Header>);
inline unsigned BytesPerPixel(unsigned format) {
    switch(format){case 2:return 16;case 10:return 8;
        case 24:case 26:case 28:case 29:case 87:case 91:return 4;default:return 0;}
}
inline bool ValidPixels(const Header& h) {
    const Header reference{};
    return !std::memcmp(h.magic,reference.magic,8)&&h.version==1&&h.header_bytes==sizeof(Header)&&
        h.status==Status::Complete&&h.reason==Reason::None&&h.request&&h.instance&&h.frame&&h.feature&&
        h.recording&&h.queue&&h.fence&&h.source_stream&&h.source_epoch&&h.source_sequence&&
        h.native_result==1&&h.domain==0&&h.width&&h.height&&h.width<=16384&&h.height<=16384&&
        h.resource_width<=16384&&h.resource_height<=16384&&h.x<=h.resource_width&&h.y<=h.resource_height&&
        h.width<=h.resource_width-h.x&&h.height<=h.resource_height-h.y&&
        h.bytes_per_pixel&&h.bytes_per_pixel==BytesPerPixel(h.storage_format)&&h.view_format==h.storage_format&&
        h.row_bytes==h.width*h.bytes_per_pixel&&h.payload_bytes==std::uint64_t(h.row_bytes)*h.height&&
        h.payload_bytes<=kReadbackLimit;
}
inline const char* ReasonName(Reason reason) {
    switch(reason){case Reason::None:return "none";case Reason::NotPreSr:return "not-pre-SR";
    case Reason::NativeFailed:return "native-evaluate-failed";case Reason::IdentityUnknown:return "identity-unknown";
    case Reason::StateUnknown:return "state-unknown";case Reason::OutputMissing:return "output-missing";
    case Reason::UnsupportedStorage:return "unsupported-storage";case Reason::InvalidRect:return "invalid-rect";
    case Reason::Budget:return "readback-budget";case Reason::Allocation:return "allocation-failed";
    case Reason::HooksIncomplete:return "barrier-hooks-incomplete";case Reason::RecordingChanged:return "recording-changed";
    case Reason::CompletionUnknown:return "completion-unknown";case Reason::ExportFailed:return "export-failed";
    case Reason::CommandStateUnknown:return "command-state-unknown";case Reason::SourceChanged:return "source-changed";
    default:return "not-requested";}
}
}
