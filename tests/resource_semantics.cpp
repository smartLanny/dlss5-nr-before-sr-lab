// Exercises the actual production resolver. The probe supplies independent
// device-query evidence; it does not reimplement candidate selection.
#include "nr_resource_semantics.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace {
using namespace nr_resource_semantics;
namespace format=nr_format_access;
unsigned checks=0,failures=0;
void Check(bool condition,const char *name) {
    ++checks;if(!condition){++failures;std::printf("FAIL %s\n",name);}
}
Capability Supported(bool load=true,bool store=true) {return {true,0,true,load,store};}
Capability Unsupported() {return {true,0,false,false,false};}
struct Device {
    std::array<Capability,128> evidence;
    std::vector<std::pair<Format,Format>> calls;
    Device(){for(auto &value:evidence)value=Supported();}
    Capability operator()(Format view,Format write) {
        calls.emplace_back(view,write);
        return view<evidence.size()?evidence[view]:Unsupported();
    }
};
Request Typeless(Format storage=format::Rgba8Typeless,Role role=Role::ColorInput) {
    Request request;request.storage=storage;request.role=role;
    request.lifecycle_known=true;request.contract=Contract::LdrEncoded;request.contract_known=true;
    request.allow_write=role==Role::Output;return request;
}
const Candidate *Find(const Decision &decision,Format view) {
    for(unsigned i=0;i<decision.candidate_count;++i)if(decision.candidates[i].view==view)return &decision.candidates[i];
    return nullptr;
}
void RequireCandidate(const Decision &d,Format view,bool implemented) {
    const auto *candidate=Find(d,view);
    Check(candidate!=nullptr,"bounded diagnostics retain every legal R8 interpretation");
    if(candidate) {
        Check(candidate->layout_legal&&candidate->contract_possible,"declared LDR does not discard a legal numeric interpretation");
        Check(candidate->codec_implemented==implemented,"codec availability is diagnostic, independent of semantic possibility");
    }
}
void AmbiguityOracle() {
    for(const auto role:{Role::ColorInput,Role::Output}) {
        auto r=Typeless(format::Rgba8Typeless,role);Device device;
        const auto d=ResourceSemanticResolver::Resolve(r,device);
        Check(d.verdict==Verdict::Ambiguous&&d.source==Source::None&&d.remaining_count==5&&!static_cast<bool>(d),
              "SEMANTIC_AMBIGUITY_ORACLE: five supported R8 meanings cannot become unique because only UNORM has a codec");
        Check(d.candidate_count==5&&device.calls.size()==5,"all five possible R8 views receive actual capability evidence");
        RequireCandidate(d,format::Rgba8Unorm,true);RequireCandidate(d,format::Rgba8Srgb,false);
        RequireCandidate(d,format::Rgba8Uint,false);RequireCandidate(d,format::Rgba8Snorm,false);RequireCandidate(d,format::Rgba8Sint,false);
        const auto *srgb=Find(d,format::Rgba8Srgb);
        Check(srgb&&srgb->write_view==(role==Role::Output?format::Rgba8Unorm:0),"sRGB interpretation retains its distinct meaning even with a shared legal UNORM write view");
    }
}
void ExactEvidence() {
    Request r;r.storage=format::Rgba16Float;Device device;
    auto d=ResourceSemanticResolver::Resolve(r,device);
    Check(static_cast<bool>(d)&&d.source==Source::TypedResource&&d.access.srv==format::Rgba16Float,
          "exact typed physical resource resolves without a guessed contract or observed view");
    Check(device.calls.size()==1&&device.calls[0]==std::pair<Format,Format>{format::Rgba16Float,0},"exact typed input queries only its actual read format");
    Check(d.access.provenance==format::Provenance::ExactTyped,"access plan preserves exact typed provenance");
    r=Typeless(format::Rgba16Typeless);r.contract=Contract::SceneFloat;r.observed_view=format::Rgba16Unorm;device={};
    d=ResourceSemanticResolver::Resolve(r,device);
    Check(d.source==Source::ObservedNativeView&&d.access.srv==format::Rgba16Unorm&&d.verdict==Verdict::Unsupported,
          "exact observed UNORM wins over SceneFloat inference even when its codec is missing");
    Check(!static_cast<bool>(d)&&d.access.srv!=format::Rgba16Float,"codec rejection never substitutes the inferable float sibling");
    r=Typeless();r.observed_view=format::Rgba8Unorm;r.contract_known=false;device={};
    d=ResourceSemanticResolver::Resolve(r,device);
    Check(static_cast<bool>(d)&&d.source==Source::ObservedNativeView&&d.access.srv==format::Rgba8Unorm,
          "current native typed view takes priority over unavailable generic color-contract inference");
    Check(device.calls.size()==1&&d.access.provenance==format::Provenance::ObservedView,"observed route probes only the chosen view and preserves provenance");
    for(const auto view:{format::Rgba8Uint,format::Rgba8Snorm,format::Rgba8Srgb}) {
        r=Typeless(format::Rgba8Typeless,Role::Output);r.observed_view=view;device={};
        d=ResourceSemanticResolver::Resolve(r,device);
        Check(d.verdict==Verdict::Unsupported&&d.source==Source::ObservedNativeView&&d.access.srv==view,
              "observed UINT/SNORM/sRGB stays selected when the codec refuses it");
        Check(d.source!=Source::UniqueContract&&!static_cast<bool>(d),"missing observed codec does not authorize a different interpretation");
    }
    r={};r.storage=format::Rgba8Unorm;r.observed_view=format::Rgba8Srgb;r.lifecycle_known=true;device={};
    d=ResourceSemanticResolver::Resolve(r,device);
    Check(!static_cast<bool>(d)&&d.source!=Source::UniqueContract&&device.calls.empty(),"illegal view over fully typed storage cannot trigger family fallback");
}
void ObservationGuards() {
    for(const bool exact:{false,true}) {
        auto r=Typeless(format::Rgba16Typeless);r.contract=Contract::SceneFloat;
        if(exact)r.observed_view=format::Rgba16Float;
        Device device;r.observation_conflict=true;
        auto d=ResourceSemanticResolver::Resolve(r,device);
        Check(d.verdict==Verdict::Conflict&&!static_cast<bool>(d)&&d.source==Source::None&&device.calls.empty(),
              "conflicting observation blocks exact and unique paths before capability inference");
        r.observation_conflict=false;r.observation_stale=true;device={};d=ResourceSemanticResolver::Resolve(r,device);
        Check(d.verdict==Verdict::Stale&&!static_cast<bool>(d)&&d.source==Source::None&&device.calls.empty(),
              "stale observation cannot fall through to a unique SceneFloat choice");
        r.observation_stale=false;r.lifecycle_known=false;device={};d=ResourceSemanticResolver::Resolve(r,device);
        Check(d.verdict==Verdict::Stale&&!static_cast<bool>(d)&&device.calls.empty(),"unknown resource lifetime blocks observed/inferred typeless admission");
        r.lifecycle_known=true;device={};d=ResourceSemanticResolver::Resolve(r,device);
        Check(static_cast<bool>(d),"fresh inputs can recover in the same process without a sticky resolver failure");
    }
}
void UniqueContractAndCapabilities() {
    for(const auto storage:{format::Rgba16Typeless,format::Rgba32Typeless}) {
        auto r=Typeless(storage);r.contract=Contract::SceneFloat;Device device;
        const auto expected=storage==format::Rgba16Typeless?format::Rgba16Float:format::Rgba32Float;
        auto d=ResourceSemanticResolver::Resolve(r,device);
        Check(static_cast<bool>(d)&&d.source==Source::UniqueContract&&d.access.srv==expected&&d.remaining_count==1,
              "declared SceneFloat contract yields the unique float interpretation");
        Check(device.calls.size()==1&&device.calls[0].first==expected,"SceneFloat contract eliminates incompatible numeric meanings without fabricated device results");
        for(unsigned i=0;i<d.candidate_count;++i)if(d.candidates[i].view!=expected)
            Check(!d.candidates[i].contract_possible&&!d.candidates[i].capability.queried,"contract-eliminated alternatives remain visible as unqueried diagnostics");
        r.contract_known=false;device={};d=ResourceSemanticResolver::Resolve(r,device);
        Check(!static_cast<bool>(d)&&d.source!=Source::UniqueContract&&d.verdict==Verdict::MissingContract,
              "an undeclared SceneFloat hint cannot authorize Unique");
    }
    for(const auto role:{Role::ColorInput,Role::Output}) {
        auto r=Typeless(format::Rgba8Typeless,role);Device device;
        for(auto &cap:device.evidence)cap=Unsupported();device.evidence[format::Rgba8Unorm]=Supported();
        auto d=ResourceSemanticResolver::Resolve(r,device);
        Check(static_cast<bool>(d)&&d.source==Source::UniqueContract&&d.access.srv==format::Rgba8Unorm&&d.remaining_count==1,
              "successful device queries can leave one legal contract interpretation");
        Check(device.calls.size()==5,"device-derived uniqueness queries every possible R8 interpretation");
        r.contract_known=false;device.calls.clear();d=ResourceSemanticResolver::Resolve(r,device);
        Check(!static_cast<bool>(d)&&d.source!=Source::UniqueContract&&d.verdict==Verdict::MissingContract,
              "device capability uniqueness alone cannot invent an undeclared color contract");
        r.contract_known=true;r.contract=Contract::None;device.calls.clear();d=ResourceSemanticResolver::Resolve(r,device);
        Check(!static_cast<bool>(d)&&d.source!=Source::UniqueContract&&d.verdict==Verdict::MissingContract,
              "MISSING_CONTRACT_ORACLE: unknown contract enum cannot become Unique merely because the known flag is true");
    }
    for(const auto contract:{Contract::LdrEncoded,Contract::HdrDeclared,Contract::ExposureScale}) {
        auto r=Typeless(format::Rgba16Typeless);r.contract=contract;Device device;
        const auto d=ResourceSemanticResolver::Resolve(r,device);
        Check(d.verdict==Verdict::Ambiguous&&d.remaining_count==5,
              "generic LDR/HDR/exposure declaration does not imply the numeric FLOAT interpretation");
    }
    auto output=Typeless(format::Rgba16Typeless,Role::Output);output.contract=Contract::HdrDeclared;
    Device output_device;for(auto &cap:output_device.evidence)cap=Supported(true,false);
    output_device.evidence[format::Rgba16Float]=Supported();
    auto route=ResourceSemanticResolver::Resolve(output,output_device);
    Check(static_cast<bool>(route)&&route.source==Source::UniqueContract&&route.access.srv==format::Rgba16Float&&output_device.calls.size()==5,
          "complete per-view store capabilities may narrow Output to one interpretation");
    output.role=Role::ColorInput;output.allow_write=false;output_device.calls.clear();route=ResourceSemanticResolver::Resolve(output,output_device);
    Check(route.verdict==Verdict::Ambiguous&&route.remaining_count==5,"pre path cannot use irrelevant source-store limitations to manufacture uniqueness");
    for(const auto selected:{format::Rgba8Uint,format::Rgba8Snorm,format::Rgba8Srgb}) {
        auto r=Typeless();Device device;for(auto &cap:device.evidence)cap=Unsupported();device.evidence[selected]=Supported();
        const auto d=ResourceSemanticResolver::Resolve(r,device);
        Check(d.source==Source::UniqueContract&&d.remaining_count==1&&d.access.srv==selected&&d.verdict==Verdict::Unsupported,
              "uniqueness may identify an unimplemented UINT/SNORM/sRGB codec but cannot reinterpret it as UNORM");
    }
}
void IncompleteQueries() {
    for(const bool attempted:{false,true}) {
        auto r=Typeless();Device device;for(auto &cap:device.evidence)cap=Unsupported();
        device.evidence[format::Rgba8Unorm]=Supported();
        device.evidence[format::Rgba8Srgb]={attempted,attempted?static_cast<std::int32_t>(0x80004005u):0,true,true,true};
        const auto d=ResourceSemanticResolver::Resolve(r,device);
        Check(d.verdict==Verdict::CapabilityUnknown&&d.remaining_count==2&&d.source==Source::None&&!d.definitive,
              "failed or unattempted query remains a possible sibling and prevents wrong Unique");
        const auto *unknown=Find(d,format::Rgba8Srgb);
        Check(unknown&&unknown->capability.queried==attempted&&!unknown->capability.known(),"candidate diagnostics preserve failed-versus-unattempted query evidence");
    }
    auto r=Typeless();Device device;
    device.evidence[format::Rgba8Srgb]={true,static_cast<std::int32_t>(0x80004005u),true,true,true};
    device.evidence[format::Rgba8Snorm]=Unsupported();device.evidence[format::Rgba8Sint]={false,0,true,true,true};
    auto d=ResourceSemanticResolver::Resolve(r,device);
    Check(d.candidate_count==5&&d.remaining_count==4&&device.calls.size()==5&&d.verdict==Verdict::CapabilityUnknown,
          "bounded diagnostics retain supported, failed, unsupported and unattempted candidates together");
    Check(Find(d,format::Rgba8Snorm)&&Find(d,format::Rgba8Snorm)->capability.known()&&!Find(d,format::Rgba8Snorm)->capability.texture2d,
          "successful unsupported capability result is distinguishable from failed query");
    for(const auto view:{format::Rgba8Srgb,format::Rgba8Sint})device.evidence[view]=Unsupported();
    device.calls.clear();d=ResourceSemanticResolver::Resolve(r,device);
    Check(d.verdict==Verdict::Ambiguous&&d.remaining_count==2,"later successful queries recover capability uncertainty without erasing another supported integer meaning");
    device.evidence[format::Rgba8Uint]=Unsupported();device.calls.clear();d=ResourceSemanticResolver::Resolve(r,device);
    Check(static_cast<bool>(d)&&d.source==Source::UniqueContract,"same-process complete new capability evidence can resolve remaining ambiguity");
    r.observed_view=format::Rgba8Unorm;device.evidence[format::Rgba8Unorm]={true,-1,false,false,false};device.calls.clear();
    d=ResourceSemanticResolver::Resolve(r,device);
    Check(d.verdict==Verdict::CapabilityUnknown&&d.source==Source::ObservedNativeView&&device.calls.size()==1,
          "failed exact selected-view query stays unknown rather than unsupported or inferred");
}
void ResourceRoutesAndCopy() {
    Request r;r.storage=format::Rgba16Float;Device device;device.evidence[format::Rgba16Float]=Supported(true,false);
    auto d=ResourceSemanticResolver::Resolve(r,device);
    Check(static_cast<bool>(d)&&d.access.uav==0,"pre Color requires readable source, not source UAV storage");
    r.role=Role::Output;device.calls.clear();d=ResourceSemanticResolver::Resolve(r,device);
    Check(d.verdict==Verdict::Unsupported&&d.access.status==format::Status::WritebackUnavailable&&device.calls.empty(),
          "Output with no UAV route refuses before capability probing");
    r.allow_write=true;device.calls.clear();d=ResourceSemanticResolver::Resolve(r,device);
    Check(d.verdict==Verdict::Unsupported&&!static_cast<bool>(d),"output UAV declaration does not replace missing typed-store capability");
    device={};r.shape_valid=false;r.exact_copy_snapshot=true;d=ResourceSemanticResolver::Resolve(r,device);
    Check(d.verdict==Verdict::Unsupported&&device.calls.empty(),"invalid shape is never bypassed by exact copy evidence");
    r.shape_valid=true;r.exact_copy_snapshot=false;r.deny_read=true;device={};d=ResourceSemanticResolver::Resolve(r,device);
    Check(d.verdict==Verdict::Unsupported&&d.access.status==format::Status::DeniedRead&&device.calls.empty(),"shader-read denial blocks ordinary codec path");
    r.deny_read=false;r.exact_copy_snapshot=true;device={};device.evidence[format::Rgba16Float]=Supported(false,true);
    d=ResourceSemanticResolver::Resolve(r,device);
    Check(static_cast<bool>(d)&&d.source==Source::TypedResource&&d.access.srv==format::Rgba16Float,
          "COPY_EXCEPTION_ORACLE: proven typed FP16 output copy snapshot does not require unused shader-load support");
    r.exact_copy_snapshot=false;device.calls.clear();d=ResourceSemanticResolver::Resolve(r,device);
    Check(!static_cast<bool>(d),"ordinary typed FP16 output still needs actual shader-load support");
    r.exact_copy_snapshot=true;r.deny_read=true;device.calls.clear();d=ResourceSemanticResolver::Resolve(r,device);
    Check(static_cast<bool>(d),"proven typed FP16 Output copy bypasses shader-read denial without requiring a shader view");
    r.role=Role::ColorInput;device={};d=ResourceSemanticResolver::Resolve(r,device);
    Check(d.verdict==Verdict::Unsupported&&d.access.status==format::Status::DeniedRead,
          "copy exception cannot waive denied shader read for pre Color");
    r.role=Role::Output;r.deny_read=false;device.evidence[format::Rgba16Float]=Supported(false,true);
    r.exact_copy_snapshot=true;r.storage=format::Rgba16Typeless;r.observed_view=format::Rgba16Float;r.lifecycle_known=true;device.calls.clear();
    d=ResourceSemanticResolver::Resolve(r,device);
    Check(!static_cast<bool>(d),"typed FP16 copy exception is not extended to typeless physical storage by an observed view");
    for(const auto storage:{format::Rgba8Unorm,format::Rgba8Typeless}) {
        r=Typeless(storage,Role::Output);r.observed_view=format::Rgba8Unorm;r.deny_read=true;r.exact_copy_snapshot=true;device={};
        d=ResourceSemanticResolver::Resolve(r,device);
        Check(d.verdict==Verdict::Unsupported&&d.access.status==format::Status::DeniedRead&&!static_cast<bool>(d),
              "COPY_SCOPE_ORACLE: generic exact-copy flag cannot waive shader denial on R8 storage");
    }
    r={};r.storage=format::Rgba16Float;r.role=Role::Output;r.allow_write=true;r.exact_copy_snapshot=true;device={};
    device.evidence[format::Rgba16Float]={true,0,false,false,true};d=ResourceSemanticResolver::Resolve(r,device);
    Check(!static_cast<bool>(d),"copy exception still requires known Texture2D support");
    device.evidence[format::Rgba16Float]=Supported(false,false);d=ResourceSemanticResolver::Resolve(r,device);
    Check(!static_cast<bool>(d),"copy snapshot does not waive typed output store support");
    r.allow_write=false;device={};d=ResourceSemanticResolver::Resolve(r,device);
    Check(!static_cast<bool>(d)&&device.calls.empty(),"copy snapshot is not an alternative writeback route without UAV permission");
}
void BoundedDiagnostics() {
    Device device;auto r=Typeless();const auto d=ResourceSemanticResolver::Resolve(r,device);
    Check(d.candidate_count<=d.candidates.size(),"candidate diagnostics remain within fixed array");
    for(unsigned i=d.candidate_count;i<d.candidates.size();++i)Check(d.candidates[i].view==0&&!d.candidates[i].capability.queried,"unused diagnostic candidate slots remain unobserved");
    for(auto storage:{format::Rgba32Typeless,format::Rgba16Typeless,format::Rgb10a2Typeless,format::Bgra8Typeless,format::Bgrx8Typeless}) {
        r=Typeless(storage);device={};const auto family=ResourceSemanticResolver::Resolve(r,device);
        Check(family.candidate_count>0&&family.candidate_count<=family.candidates.size()&&device.calls.size()==family.candidate_count,
              "each supported typeless family has complete bounded query diagnostics");
    }
    r=Typeless(0);device={};const auto missing=ResourceSemanticResolver::Resolve(r,device);
    Check(!static_cast<bool>(missing)&&missing.source==Source::None&&missing.candidate_count==0&&device.calls.empty(),"unknown format cannot synthesize candidates or a Unique choice");
    Check(std::strcmp(SourceName(Source::UniqueContract),"unique-contract")==0&&std::strcmp(VerdictName(Verdict::CapabilityUnknown),"capability-unknown")==0,
          "resolver verdict and source labels distinguish inference from incomplete device evidence");
}
}
int main(int argc,char **argv) {
    if(argc==2&&std::strcmp(argv[1],"--ambiguity-oracle")==0) {
        AmbiguityOracle();std::printf("resource semantic ambiguity oracle: %u checks, %u failures\n",checks,failures);
        return failures?41:0;
    }
    AmbiguityOracle();ExactEvidence();ObservationGuards();UniqueContractAndCapabilities();
    IncompleteQueries();ResourceRoutesAndCopy();BoundedDiagnostics();
    std::printf("resource semantic resolver: %u checks, %u failures; actual pure policy, no GPU\n",checks,failures);
    return failures?1:0;
}
