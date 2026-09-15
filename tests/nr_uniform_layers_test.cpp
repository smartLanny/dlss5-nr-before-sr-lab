#include "../src/nr_layer_config.h"
#include "../src/nr_full_pass.hpp"
#include <iostream>
#include <stdexcept>
#include <limits>
struct Config : nr_layers::Model { nr_layers::Auxiliary nr_layers{}; };
static unsigned checks=0;
void Check(bool condition,const char* reason){++checks;if(!condition)throw std::runtime_error(reason);}
int main(){try{
    Config original;original.intensity=1.75f;original.local_tone=.4f;original.style=2;
    for(unsigned count=1;count<=5;++count){
        Config c=original;nr_layers::Migrate(c.nr_layers,count,nr_layers::Primary(c));
        Check(nr_layers::Active(c.nr_layers)==count,"migration preserves active prefix");
        for(unsigned i=1;i<count;++i)Check(nr_layers::Read(c,i)==nr_layers::Primary(c),"active legacy layers preserve model controls");
        for(unsigned i=count;i<5;++i)Check(nr_layers::Read(c,i)==nr_layers::Model{},"unused new layers use fixed D21 defaults");
        auto previous=c;
        if(count>1){
            c.nr_layers[0].local_tone=1.9f;
            Check(nr_layers::FirstChanged(c,previous)==1,"second-layer edit leaves primary history alone");
            Check(nr_layers::Primary(c)==nr_layers::Primary(previous),"auxiliary tuning cannot overwrite primary");
        }
    }
    Config five;nr_layers::Migrate(five.nr_layers,5,nr_layers::Primary(five));
    five.nr_layers[2].style=2;auto saved=five;
    five.nr_layers[0].enabled=false;
    Check(nr_layers::Active(five.nr_layers)==1,"disabled middle stops all downstream processing");
    Check(five.nr_layers[2].style==2&&five.nr_layers[3].enabled,"downstream values and requested enables persist");
    five.nr_layers[0].enabled=true;
    Check(five.nr_layers==saved.nr_layers&&nr_layers::Active(five.nr_layers)==5,"re-enable restores chain without erasing edits");
    five.nr_layers[3].intensity=.25f;
    Check(nr_layers::FirstChanged(five,saved)==4,"last-layer change does not invalidate earlier layers");
    nr_layers::Model invalid;invalid.intensity=std::numeric_limits<float>::quiet_NaN();invalid.local_tone=10;invalid.style=99;
    invalid.auto_mask=true;invalid.skin_structure=-1;nr_layers::Normalize(invalid);
    Check(invalid.intensity==1.2f&&invalid.local_tone==2&&invalid.style==2&&invalid.skin_structure==0,"invalid per-layer controls normalized consistently");
    for(unsigned count=2;count<=5;++count)for(unsigned percent:{100u,75u,50u}){
        const nr_dual::Extent grid{1920*percent/100,1080*percent/100};
        const auto plan=nr_full::MakePlan({true,count},grid);
        Check(bool(plan)&&!plan.reduced,"whole-chain work scale never adds a second reduction");
        for(unsigned i=0;i<count-1;++i)Check(plan.layers[i].primary==grid&&plan.layers[i].secondary==grid,"all layers retain SR-input-derived raster");
    }
    std::cout<<"PASS "<<checks<<" uniform-chain configuration/history/raster checks\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
