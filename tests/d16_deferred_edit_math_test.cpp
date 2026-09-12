#include "../shaders/experiments/nr_edit_scalar.hlsli"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
using namespace nr_edit_math;
int main() {
    assert(NrEditPack(0,4).carrier==.5f&&NrEditPack(0,4).tail==0);
    assert(NrEditUnpack(.5f,0,4)==0);
    assert(NrEditPack(-8,4).carrier==0&&NrEditPack(-8,4).tail==-4);
    assert(NrEditPack(8,4).carrier==1&&NrEditPack(8,4).tail==4);
    assert(NrEditUnpack(1.5f,4,4)==8); // bounded inverse under neural overshoot
    assert(NrEditUnpack(-1.5f,-4,4)==-8);
    const float nan=std::numeric_limits<float>::quiet_NaN();
    assert(!NrEditPack(nan,4).valid&&!NrEditPack(1,nan).valid&&!NrEditPack(1,0).valid);
    assert(NrEditUnpack(nan,0,4)==0);
    std::mt19937 random(16);
    std::uniform_real_distribution<float> residual(-10000,10000),ranges(.01f,100);
    for(unsigned i=0;i<50000;++i) {
        const float d=residual(random),range=ranges(random);
        const auto p=NrEditPack(d,range);
        assert(p.valid&&p.carrier>=0&&p.carrier<=1);
        const float result=NrEditUnpack(p.carrier,p.tail,range);
        assert(std::fabs(result-d)<=.002f);
        // Away from saturation, an injected carrier error has bounded gain 2*range.
        const float epsilon=1e-4f;
        const float error=std::fabs(NrEditUnpack(p.carrier+epsilon,p.tail,range)-result);
        assert(error<=2*range*epsilon+.003f);
    }
    // Exposure normalization is separate from the carrier. No second strength.
    for(float e : {.25f,1.f,8.f}) {
        const float original=3.f*e,edited=1.5f*e;
        const auto p=NrEditPack((edited-original)/e,4);
        const float clean=5.f*e;
        const float final=clean+NrEditUnpack(p.carrier,p.tail,4)*e;
        assert(std::fabs(final-3.5f*e)<1e-5f);
    }
    std::cout<<"Deferred shared scalar math: 50000 signed/HDR/error-bound cases passed\n";
}
