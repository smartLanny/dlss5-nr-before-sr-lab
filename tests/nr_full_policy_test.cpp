#include "../src/nr_full_pass.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
static unsigned checks=0;
void Check(bool yes,const char* why){++checks;if(!yes)throw std::runtime_error(why);}
struct Backend {
    std::vector<unsigned> calls;
    unsigned stop=99;nr_full::Step stopped{};bool adopted=false;
    nr_full::Step Layer(unsigned i){calls.push_back(i);return i==stop?stopped:nr_full::Step{0,true,true};}
    void Adopt(unsigned last){Check(last+1==calls.size(),"adopt last evaluated layer only");adopted=true;}
};
int main(){try {
    Check(!nr_full::MakePlan({}, {1920,1080}),"full reference defaults off");
    Check(nr_full::Settings{}.passes==2,"default pass count two");
    for(unsigned n=0;n<7;++n) {
        auto p=nr_full::MakePlan({true,n},{1920,1080});
        Check(bool(p)==(n>=2&&n<=4),"only complete two to four layers");
        if(!p)continue;
        for(unsigned i=0;i<n-1;++i) {
            Check(p.layers[i].path==nr_dual::Path::DirectSecond,"full layers never use residual resize");
            Check(p.layers[i].secondary==nr_dual::Extent{1920,1080},"actual matching raster");
        }
        Backend good;auto r=nr_full::Run(n,n-1,good);
        Check(r.adopted&&good.adopted&&r.evaluated==n-1,"adopt complete chain once");
        for(unsigned at=0;at<n-1;++at)for(unsigned fail=0;fail<3;++fail) {
            Backend b;b.stop=at;
            b.stopped=fail==0?nr_full::Step{0xBAD00005u,false,false}:
                fail==1?nr_full::Step{0,false,false}:nr_full::Step{0,true,false};
            auto out=nr_full::Run(n,n-1,b);
            Check(!out.adopted&&!b.adopted,"no partial full-reference claim");
            Check(b.calls.size()==at+1,"no later calls after creation/warmup/fatal");
            Check((out.fatal!=0)==(fail==0),"opaque failure stays fatal");
        }
        for(unsigned available=0;available<n-1;++available) {
            Backend b;auto partial=nr_full::Run(n,available,b);
            Check(!partial.adopted&&!b.adopted,"preparing subset is not complete reference");
        }
    }
    Check(!nr_full::MakePlan({true,4},{0,1080}),"invalid raster refused");
    std::array<nr_dual::AuxHistory,3> histories;
    for(unsigned layer=1;layer<=3;++layer) {
        nr_dual::LayerKey key{layer,100,4,200+layer,{1920,1080}};
        Check(histories[layer-1].NeedsReset(key,10,false),"independent cold history");
        Check(histories[layer-1].CommitSuccess(key,10),"each auxiliary owns history");
        Check(!histories[layer-1].NeedsReset(key,11,false),"continuous root evaluation");
        Check(histories[layer-1].NeedsReset(key,12,false),"skipped source resets");
        for(unsigned other=1;other<=3;++other)if(other!=layer)
            Check(histories[other-1].NeedsReset(key,11,false),"history cannot be borrowed from sibling");
    }
    nr_dual::AuxHistory history;
    Check(!history.CommitSuccess({4,1,1,1,{1,1}},1),"reject fifth layer");
    Check(nr_dual::MakePlan({2,{1,2}},{1920,1080},{}).path==nr_dual::Path::MatchedResidual,"legacy reduced path unchanged");
    std::cout<<"PASS "<<checks<<" CPU full-chain checks; synthetic backend, not GPU/NGX\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
