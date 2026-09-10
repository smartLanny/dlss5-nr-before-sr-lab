float RegionLuma(const Float4& p){return .2126f*p.x+.7152f*p.y+.0722f*p.z;}
void ValidateRegionComposition(Gpu& gpu) {
 unsigned scenarios=0;
 for(bool before:{false,true})for(UINT flags:{1u,16u,32u,0u,128u})for(unsigned scale:{100u,75u,50u})for(unsigned group=0;group<5;group++) {
  constexpr UINT w=16,h=12,ow=w+4;UINT mw=w*scale/100,mh=h*scale/100;
  int variant=group==2||group==3?41:40;RegionCase region;region.flags=1;
  region.field.x=group==0?2.f:group==1?1.f:group==2?-2.f:group==3?-1.f:2.f;region.field.y=group==4?1.f:0.f;
  region.field.w=(flags&1)? .2f:std::pow((.2f+.055f)/1.055f,2.4f);
  auto run=[&](RegionCase r){return Run(gpu,w,h,0,1,flags,variant,before,mw,mh,{},0,{},1,{1,1,1,1},true,1,{}, {},r);};
  const auto baseline=run({}),neutral=run(region);region.gains[group*2]=0;auto noLuma=run(region);
  region.gains[group*2]=1;region.gains[group*2+1]=0;auto noColor=run(region);
  region.gains[group*2+1]=1;region.gains[group*2]=2;auto twice=run(region);
  auto wrong=region;wrong.gains[group*2]=1;wrong.gains[((group+1)%5)*2]=0;auto unrelated=run(wrong);
  for(UINT y=0;y<h;y++)for(UINT x=0;x<w;x++){
   auto at=(y+1)*ow+x+2;const auto& ref=baseline.output[at];
   Require(std::memcmp(&neutral.output[at],&ref,16)==0,"REGION_NEUTRAL_EXACT");
   Require(std::memcmp(&unrelated.output[at],&ref,16)==0,"REGION_UNRELATED_GAIN_EXACT");
   if(x>=w/2){Require(std::memcmp(&noLuma.output[at],&ref,16)==0&&std::memcmp(&noColor.output[at],&ref,16)==0&&std::memcmp(&twice.output[at],&ref,16)==0,"REGION_OUTSIDE_EXACT");}
   else if(flags==1){
    Require(std::abs(RegionLuma(noLuma.output[at])-.2f)<.00004f,"REGION_ZERO_LUMA_ENDPOINT");
    const auto& col=noColor.output[at];Require(std::abs(col.x-col.y)<.00003f&&std::abs(col.y-col.z)<.00003f,"REGION_ZERO_COLOR_ENDPOINT");
    float delta=RegionLuma(ref)-.2f;Require(std::abs((RegionLuma(twice.output[at])-.2f)-2*delta)<.00004f,"REGION_LINEAR_EDIT_GAIN");
   }
  }
  // The preview must not change when gains do, and must be identical across
  // the two composition routes for the same fixed region field.
  region.flags=1u|(1u<<1);auto a=run(region);std::fill(region.gains,region.gains+11,4.f);auto b=run(region);
  Require(a.output.size()==b.output.size()&&std::memcmp(a.output.data(),b.output.data(),a.output.size()*sizeof(Float4))==0,"REGION_PREVIEW_GAIN_INVARIANT");
  ++scenarios;
 }
 // A soft reflection boundary shares the edit without duplicating it.
 for(bool before:{false,true}) {
  RegionCase r;r.flags=1;r.field={1.5f,.25f,.4f,.2f};std::fill(r.gains,r.gains+11,0.f);
  auto result=Run(gpu,16,12,0,1,1,40,before,8,6,{},0,{},1,{1,1,1,1},true,1,{}, {},r);
  for(UINT y=0;y<12;y++)for(UINT x=0;x<8;x++){
   const auto& p=result.output[(y+1)*20+x+2];Require(std::abs(p.x-.2f)<.00003f&&std::abs(p.y-.2f)<.00003f&&std::abs(p.z-.2f)<.00003f,"REGION_PARTITION_CONSERVATION");}
  ++scenarios;
 }
 for(bool before:{false,true}) {
  RegionCase r;r.flags=1;std::fill(r.gains,r.gains+11,0.f);
  auto run=[&](RegionCase c){return Run(gpu,16,12,0,1,1,42,before,8,6,{},0,{},1,{1,1,1,1},true,1,{}, {},c);};
  auto base=run({}),controlled=run(r);
  const UINT edge=(3+1)*20+3+2,flat=(2+1)*20+2+2;
  Require(std::memcmp(&base.output[edge],&controlled.output[edge],sizeof(Float4))==0,"REGION_FULL_SOURCE_EDGE_REJECTS_LOW_GRID_BLEED");
  Require(std::abs(controlled.output[flat].x-.2f)<.00003f,"REGION_FULL_SOURCE_EDGE_DOES_NOT_DISABLE_FLAT_NEIGHBOUR");
  ++scenarios;
 }
 std::printf("PASS region composition WARP: %u scenarios, pre/post x five domains x 100/75/50 grids, per-group luma/colour, neutral/outside identity, gain-independent preview and conserved soft boundaries.\n",scenarios);
}
