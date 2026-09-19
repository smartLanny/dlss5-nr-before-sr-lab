#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace nr_edit_chain {
struct Source {
    std::uint64_t stream=0,epoch=0,sequence=0;
    explicit operator bool() const noexcept{return stream&&epoch&&sequence;}
    bool operator==(const Source&) const = default;
};
struct Layer {
    std::uint64_t instance=0,history=0;
    std::uint32_t width=0,height=0;
    bool reset_requested=false;
    bool operator==(const Layer& b) const noexcept{
        return instance==b.instance&&history==b.history&&width==b.width&&height==b.height;
    }
    bool Valid() const noexcept{return instance&&width&&height;}
};
template<std::size_t Max> struct Applied {
    std::array<Layer,Max> layers{};
    std::uint64_t configuration=0;
    unsigned requested=0,used=0;
    bool Valid() const noexcept{
        if(!configuration||!requested||requested>Max||!used||used>requested)return false;
        for(unsigned i=0;i<used;++i){
            if(!layers[i].Valid())return false;
            for(unsigned j=0;j<i;++j)if(layers[i].instance==layers[j].instance)return false;
        }
        return true;
    }
    bool AnyModelReset() const noexcept{
        if(used>Max)return true;
        for(unsigned i=0;i<used;++i)if(layers[i].reset_requested)return true;
        return false;
    }
    bool operator==(const Applied& b) const noexcept{
        if(used>Max||b.used>Max||configuration!=b.configuration||requested!=b.requested||used!=b.used)return false;
        for(unsigned i=0;i<used;++i)if(!(layers[i]==b.layers[i]))return false;
        return true;
    }
};
enum class Reason{Stable,Cold,InvalidSource,InvalidChain,SourceReference,AppliedChainChanged,NativeReset,DuplicateSource};
inline const char* Name(Reason r) noexcept{
    switch(r){
    case Reason::Stable:return "same-applied-chain";
    case Reason::Cold:return "cold-edit-history";
    case Reason::InvalidSource:return "invalid-edit-source";
    case Reason::InvalidChain:return "unproved-applied-chain";
    case Reason::SourceReference:return "edit-reference-gap";
    case Reason::AppliedChainChanged:return "applied-NR-chain-changed";
    case Reason::NativeReset:return "native-or-model-reset";
    case Reason::DuplicateSource:return "duplicate-or-old-edit-source";
    }
    return "unknown";
}
struct Decision{bool admitted=false,reset=true;Reason reason=Reason::Cold;};
template<std::size_t Max> class History {
    Applied<Max> accepted_{};Source source_{};bool valid_=false;
public:
    Decision Check(const Applied<Max>& now,Source source,Source reference,bool reset) const noexcept{
        if(!source)return {false,true,Reason::InvalidSource};
        if(!now.Valid())return {false,true,Reason::InvalidChain};
        if(valid_&&source.stream==source_.stream&&source.epoch==source_.epoch&&source.sequence<=source_.sequence)
            return {false,true,Reason::DuplicateSource};
        if(reset||now.AnyModelReset())return {true,true,Reason::NativeReset};
        if(!valid_)return {true,true,Reason::Cold};
        if(source.stream!=source_.stream||source.epoch!=source_.epoch||!(reference==source_))
            return {true,true,Reason::SourceReference};
        if(!(now==accepted_))return {true,true,Reason::AppliedChainChanged};
        return {true,false,Reason::Stable};
    }
    bool Commit(const Applied<Max>& now,Source source) noexcept{
        if(!now.Valid()||!source){Invalidate();return false;}
        accepted_=now;source_=source;valid_=true;return true;
    }
    void Invalidate() noexcept{valid_=false;accepted_={};source_={};}
};
}
