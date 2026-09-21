// Real GPU special-value regression. Half payloads are uploaded as bits, never
// sanitized by a CPU floating-point conversion before the shader can see them.
void run_finite_pixels(Host& host){
    constexpr unsigned w=64,h=64;HistoryContract history;
    ResidualAccumulator consumer(host.device.Get(),host.queue.Get(),w,h);
    check(consumer.initialized(),consumer.status());
    std::vector<unsigned char> original(w*h*4,128);for(unsigned p=0;p<w*h;++p)original[p*4+3]=255;
    auto base=upload_pixels(host,w,h,DXGI_FORMAT_R8G8B8A8_UNORM,4,original.data());
    std::vector<unsigned char> previous;
    const float b=128.f/255.f;
    for(unsigned seq=1;seq<=2;++seq){
        std::vector<std::uint16_t> enhanced(w*h*4);std::vector<std::uint32_t> flow_bits(w*h*2,0);
        const float delta[3]={seq==1?.2f:.02f,seq==1?-.2f:-.02f,seq==1?.1f:.03f};
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){const unsigned p=y*w+x;
            for(unsigned c=0;c<3;++c)enhanced[p*4+c]=half(b+delta[c]);enhanced[p*4+3]=half(1);
            if(seq==2){switch(x/8){
                case 0:enhanced[p*4]=0x7e01;break; // positive qNaN
                case 1:enhanced[p*4+1]=0x7c00;break; // +Inf
                case 2:enhanced[p*4+2]=0xfc00;break; // -Inf
                case 3:enhanced[p*4]=0xfd01;break; // negative signaling NaN
                case 4:enhanced[p*4]=half(-1);break;
                case 5:enhanced[p*4+1]=half(2);break;
            }
            switch(y/8){
                case 0:flow_bits[p*2]=0x7fc00001;break;
                case 1:flow_bits[p*2+1]=0xffc00001;break;
                case 2:flow_bits[p*2]=0x7f800000;break;
                case 3:flow_bits[p*2+1]=0xff800000;break;
                case 4:flow_bits[p*2]=0xc61c4000;break; // -10000, outside image
                case 5:flow_bits[p*2+1]=0x461c4000;break; // +10000, outside image
                case 6:flow_bits[p*2]=flow_bits[p*2+1]=0xbf800000;break; // legal (-1,-1)
                default:break; // legal zero
            }}
        }
        auto enhanced_texture=upload_pixels(host,w,h,DXGI_FORMAT_R16G16B16A16_FLOAT,8,enhanced.data());
        auto vector_texture=upload_pixels(host,w,h,DXGI_FORMAT_R32G32_FLOAT,8,flow_bits.data());
        auto frame=frame_for(seq,90);frame.contract.width=w;frame.contract.height=h;
        auto plan=history.prepare(frame);auto flow=controlled(host,frame,plan,vector_texture.Get());
        check(history.observe_flow(plan.token,flow.receipt)==(seq==1?FlowDecision::Seed:FlowDecision::Accumulate),"finite probe source contract");
        check(consumer.submit(true,frame,plan,flow,{frame.source,frame.source,base.Get(),enhanced_texture.Get()})==ResidualSubmit::Submitted,"finite probe actual dispatch");
        ResidualOutput out;check(consumer.poll(out),"finite output");wait(out.completion,out.value);
        auto signed_pixels=host.readback(out.signed_history,8),pixels=host.readback(out.pixels,8);
        if(seq==2){save("special-values-residual",100,seq,signed_pixels);save("special-values-final",100,seq,pixels);}
        unsigned original_fallback=0,flow_seed=0,legal_accumulate=0;
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){const unsigned p=y*w+x;
            const bool invalid_enhanced=seq==2&&x<48;
            const bool invalid_flow=seq==2&&y<48;
            if(invalid_enhanced)++original_fallback;else if(invalid_flow)++flow_seed;else if(seq==2)++legal_accumulate;
            for(unsigned c=0;c<3;++c){const float actual=unhalf(signed_pixels.data()+p*8+c*2),final=unhalf(pixels.data()+p*8+c*2);
                check(std::isfinite(actual)&&std::isfinite(final),"NONFINITE_HISTORY_ORACLE");
                float expected=invalid_enhanced?0:unhalf(reinterpret_cast<unsigned char*>(&enhanced[p*4+c]))-b;
                if(seq==2&&!invalid_enhanced&&!invalid_flow)expected=.5f*(expected+unhalf(previous.data()+c*2));
                check(std::abs(actual-expected)<.0012f,invalid_enhanced?"ENHANCED_FALLBACK_ORACLE":invalid_flow?"FLOW_RESEED_ORACLE":"LEGAL_NEGATIVE_FLOW_ORACLE");
                check(std::abs(final-std::clamp(b+expected,0.f,1.f))<.0012f,"special-value final pixel matches guarded history");
            }
        }
        previous=std::move(signed_pixels);
        check(consumer.release_output(plan.token,host.done.Get(),host.value),"finite output retired");
        check(history.finish(plan.token,plan.history_generation,ResidualStatus::SubmittedAdopted)==Completion::Adopted,"finite actual adoption");
        std::printf("finite source=%u original_fallback=%u flow_seed=%u legal_accumulate=%u all_pixels_finite=1\n",seq,original_fallback,flow_seed,legal_accumulate);std::fflush(stdout);
    }
    check(consumer.dispatch_count()==2,"exactly two finite-regression dispatches");
}
