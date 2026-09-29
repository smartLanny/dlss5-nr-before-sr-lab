#include "../experiments/final_residual/amd_of_producer.hpp"
#include <cstdio>
#include <cstdlib>
using namespace nr::final_residual;

static unsigned checks=0;
static void Check(bool value,const char* name){
    ++checks;if(!value){std::printf("FAIL %s\n",name);std::exit(1);}
}
int main(){
    Frame previous{};
    previous.source={7,9,100};previous.source_time=100000;
    previous.contract.device_generation=2;previous.contract.config_generation=3;
    previous.contract.color_domain=kResidualLinearSdr;
    previous.contract.width=640;previous.contract.height=360;
    previous.contract.backend=Backend::AmdOpticalFlow;
    previous.contract.scope=Scope::WholeChain;
    Plan plan{};plan.action=Action::Continue;plan.token=4;
    plan.history_generation=5;plan.reference=previous.source;
    auto next=previous;next.source.sequence=101;next.source_time=116000;
    Check(AmdOwnedSourceContinues(next,previous,plan,5),"consecutive source needs no pair hint");
    next.previous_source=previous.source;
    Check(AmdOwnedSourceContinues(next,previous,plan,5),"consecutive source accepts exact pair hint");
    next.previous_source={7,9,99};
    Check(!AmdOwnedSourceContinues(next,previous,plan,5),"consecutive source rejects contradictory pair hint");
    next.source.sequence=103;next.previous_source=previous.source;
    Check(AmdOwnedSourceContinues(next,previous,plan,5),"gapped application content pairs exact owned source");
    next.previous_source={7,9,99};
    Check(!AmdOwnedSourceContinues(next,previous,plan,5),"gapped source cannot name an older snapshot");
    next.previous_source={};
    Check(!AmdOwnedSourceContinues(next,previous,plan,5),"gapped source without a pair is refused");
    next.previous_source=previous.source;next.source.stream=8;
    Check(!AmdOwnedSourceContinues(next,previous,plan,5),"different stream refused despite pair hint");
    next.source.stream=7;next.source.epoch=10;
    Check(!AmdOwnedSourceContinues(next,previous,plan,5),"different epoch refused despite pair hint");
    next.source.epoch=9;next.source.sequence=99;
    Check(!AmdOwnedSourceContinues(next,previous,plan,5),"reverse sequence refused despite pair hint");
    next.source.sequence=100;
    Check(!AmdOwnedSourceContinues(next,previous,plan,5),"duplicate sequence refused despite pair hint");
    next.source.sequence=103;next.source_time=previous.source_time;
    Check(!AmdOwnedSourceContinues(next,previous,plan,5),"non-increasing source time refused");
    next.source_time=116000;next.contract.color_domain=kResidualLinearScene;
    Check(!AmdOwnedSourceContinues(next,previous,plan,5),"colour domain change refused");
    next.contract=previous.contract;next.contract.device_generation=4;
    Check(!AmdOwnedSourceContinues(next,previous,plan,5),"device generation change refused");
    next.contract=previous.contract;next.contract.config_generation=4;
    Check(!AmdOwnedSourceContinues(next,previous,plan,5),"config generation change refused");
    next.contract=previous.contract;
    Check(!AmdOwnedSourceContinues(next,previous,plan,6),"history generation change refused");
    auto wrong_plan=plan;wrong_plan.reference={7,9,99};
    Check(!AmdOwnedSourceContinues(next,previous,wrong_plan,5),"plan reference must match owned snapshot");
    next.scene_reset=true;
    Check(!AmdOwnedSourceContinues(next,previous,plan,5),"scene reset cannot continue history");
    std::printf("PASS AMD owned-source continuity checks=%u; CPU contract, actual Present consumer tested separately\n",checks);
}
