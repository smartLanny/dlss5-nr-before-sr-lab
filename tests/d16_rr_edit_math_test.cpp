#include "../shaders/experiments/nr_edit_scalar.hlsli"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
using namespace nr_edit_math;
static bool close(float a,float b,float e=1e-5f){return std::fabs(a-b)<=e;}
int main() {
    const float nan=std::numeric_limits<float>::quiet_NaN();
    assert(NrEditDepthWeight(10,10,1,.01f,.01f)==1);
    assert(NrEditDepthWeight(10,20,1,.01f,.01f)==0); // disocclusion
    // Camera moves: compare expected PREVIOUS depth, not current camera depth.
    assert(NrEditDepthWeight(12,12,1,.01f,.01f)==1);
    assert(NrEditDepthWeight(10,12,1,.01f,.01f)==0);
    assert(NrEditDepthWeight(nan,12,1,.01f,.01f)==0);
    assert(NrEditDepthWeight(0,0,1,.01f,.01f)==0);
    assert(NrEditDepthWeight(10,10,nan,.01f,.01f)==0);
    assert(NrEditDepthWeight(10,10,1,-1,.01f)==0);
    assert(NrEditDepthWeight(1e38f,1e38f,1,0,1e38f)==0);
    assert(close(NrEditAccumulate(-.4f,-.2f,1,.25f,-1,1),-.25f));
    assert(close(NrEditAccumulate(.4f,10,1,.1f,.2f,.6f),.58f)); // neighborhood clamp
    assert(NrEditAccumulate(.4f,10,0,.1f,.2f,.6f)==.4f);
    assert(NrEditAccumulate(.4f,nan,1,.1f,.2f,.6f)==.4f);
    assert(NrEditAccumulate(nan,0,1,.1f,-1,1)==0);
    assert(close(NrEditViewZ(.9f,1,-1),10,1e-4f)); // forward, infinite far
    assert(close(NrEditViewZ(.1f,0,1),10)); // reversed Z
    assert(NrEditViewZ(1,1,-1)==0); // infinite far is not usable history depth
    assert(NrEditViewZ(nan,0,1)==0);
    std::mt19937 random(16);
    std::uniform_real_distribution<float> sample(-10,10),confidence(0,1);
    for(unsigned i=0;i<20000;++i){
        const float c=sample(random),h=sample(random),w=confidence(random),a=confidence(random);
        const float out=NrEditAccumulate(c,h,w,a,c-1,c+1);
        assert(std::isfinite(out)&&out>=c-1.00001f&&out<=c+1.00001f);
        assert(close(NrEditAccumulate(c,h,0,a,c-1,c+1),c));
    }
    std::cout<<"RR edit shared scalar math: depth/sign/finite cases and 20000 randomized cases passed\n";
}
