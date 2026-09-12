#include "geometry.hpp"
#include "shader_source.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <algorithm>
using namespace nr::fullres;
static unsigned checks=0;
static void check(bool yes,const char* what) {
    ++checks;if(!yes){std::cerr<<"FAIL: "<<what<<'\n';std::exit(1);}
}
static bool near(double a,double b,double tol=1e-6){return std::abs(a-b)<=tol*std::max(1.0,std::abs(b));}
int main(){
    check(valid_scale(FlowScale::full)&&valid_scale(FlowScale::half),"100 and 50 allowed");
    for(auto v:{0u,1u,25u,75u,101u,UINT32_MAX})
        check(!geometry({3840,2160},static_cast<FlowScale>(v)).valid(),"other scales rejected");
    check(!geometry({0,2160},FlowScale::half).valid(),"zero source rejected");
    for(auto source:{Extent{3840,2160},Extent{1921,1081},Extent{3440,1440},Extent{129,131},Extent{128,128}}){
        for(auto scale:{FlowScale::full,FlowScale::half}){
            const auto g=geometry(source,scale);
            check(g.valid()&&g.source==source,"source unchanged");
            const auto divisor=scale==FlowScale::full?1u:2u;
            check(g.flow.width==source.width/divisor+(source.width%divisor!=0),"actual flow width");
            check(g.flow.height==source.height/divisor+(source.height%divisor!=0),"actual flow height");
            for(auto b:{Backend::amd,Backend::nvidia}){
                const double unit=b==Backend::amd?1.0:1.0/32.0;
                for(auto grid:{1u,2u,4u,8u}){
                    auto c=shader_constants(g,b,grid,false);
                    check(c.source_width==source.width&&c.flow_width==g.flow.width,"root extents");
                    check(c.grid==grid&&!c.reset,"grid not input scale");
                    check(near(c.unit_x,unit*double(source.width)/g.flow.width),"pixel unit X restored");
                    check(near(c.unit_y,unit*double(source.height)/g.flow.height),"pixel unit Y restored");
                    const auto reset=shader_constants(g,b,grid,true);check(reset.reset==1,"reset explicitly marked");
                    // Arbitrary synthetic translations must recover source-pixel magnitude.
                    for(double vx:{-16.0,0.0,5.5,24.0}){
                        const auto flow_vx=vx*g.flow.width/source.width/unit;
                        check(near(flow_vx*c.unit_x,vx),"source translation recovered");
                    }
                }
            }
            // Independent area-overlap oracle for every output x footprint. No
            // skipped source texels; odd extents get fractional boundary weights.
            for(unsigned p=0;p<g.flow.width;++p){
                const double lo=double(p)*source.width/g.flow.width;
                const double hi=double(p+1)*source.width/g.flow.width;
                double total=0,constant=0;
                for(unsigned x=unsigned(std::floor(lo));x<std::min(source.width,unsigned(std::ceil(hi)));++x){
                    const double w=std::max(0.0,std::min(hi,double(x+1))-std::max(lo,double(x)));
                    total+=w;constant+=w*0.375;
                }
                check(near(total,hi-lo),"area coverage without sampling holes");
                check(near(constant/total,0.375),"constant preserved by reference filter");
            }
        }
    }
    const auto odd=geometry({1921,1081},FlowScale::half);
    check(odd.flow==Extent{961,541},"odd dimensions round up");
    check(!near(vector_scale(odd,Backend::amd).x,2.0),"odd extent does not assume exactly 2x");
    check(geometry({UINT32_MAX,UINT32_MAX},FlowScale::half).flow==Extent{2147483648u,2147483648u},"ceil without overflow");
    const auto invalid=vector_scale({},Backend::amd);check(!invalid.x&&!invalid.y,"invalid geometry has no vector scale");
    const auto full=geometry({3840,2160},FlowScale::full);
    check(vector_scale(full,Backend::amd).x==1.0f,"100 AMD identity");
    check(vector_scale(full,Backend::nvidia).x==1.0f/32.0f,"100 NVOF retains S10.5");
    check(sizeof(guide_shader)>1000,"actual shader source included (NOT GPU compiled)");
    std::cout<<"PASS "<<checks<<" geometry/root-constant/CPU-oracle assertions; shader and GPU not executed\n";
}
