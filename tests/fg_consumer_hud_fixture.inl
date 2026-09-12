// Test-only explicit native HUDless/full/alpha resource contract. Included in
// namespace host_fg after the real upload and observation adapters.
double Linear(unsigned byte){const double v=byte/255.;return v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4);}
unsigned Srgb(double value){value=std::clamp(value,0.,1.);return unsigned(std::round(255*(value<=.0031308?value*12.92:1.055*std::pow(value,1/2.4)-.055)));}
struct HudFixture {
    Io scene;ID3D12Resource* alpha=nullptr;ID3D12Resource* alpha_upload=nullptr;unsigned mode=0;
    void Init(Io& full,unsigned requested){mode=requested;if(!mode)return;scene.Init(full.device,full.queue);scene.observed_execute=ObservedExecute;
        void* upload=nullptr;D3D12_RANGE none{0,0};Hr(ID3D12Resource_Map(full.upload,0,&none,&upload),"explicit full/HUDless upload");
        for(unsigned y=0;y<Io::height;++y)for(unsigned x=0;x<Io::width;++x){const unsigned zone=std::min(2u,x*3/Io::width);const double coverage=zone*.5;
            unsigned pixel=0xff000000;const unsigned hud=0xff40c0e0;
            for(unsigned ch=0;ch<3;++ch){const unsigned shift=8*ch;const double base=Linear((scene.reference[y*Io::width+x]>>shift)&255);
                const double ui=Linear((hud>>shift)&255);pixel|=Srgb(base*(1-coverage)+ui*coverage)<<shift;}
            full.reference[y*Io::width+x]=pixel;std::memcpy(static_cast<char*>(upload)+y*full.footprint.Footprint.RowPitch+x*4,&pixel,4);
        }ID3D12Resource_Unmap(full.upload,0,nullptr);
        if(mode==2){alpha=Guide(full.device,DXGI_FORMAT_R32_FLOAT);scene.Buffer(D3D12_HEAP_TYPE_UPLOAD,&alpha_upload);
            Hr(ID3D12Resource_Map(alpha_upload,0,&none,&upload),"native explicit alpha upload");
            for(unsigned y=0;y<Io::height;++y)for(unsigned x=0;x<Io::width;++x){const float coverage=std::min(2u,x*3/Io::width)*.5f;
                std::memcpy(static_cast<char*>(upload)+y*scene.footprint.Footprint.RowPitch+x*4,&coverage,4);}
            ID3D12Resource_Unmap(alpha_upload,0,nullptr);full.Reset();auto state=D3D12_RESOURCE_STATE_COMMON;Transition(full.list,alpha,state,D3D12_RESOURCE_STATE_COPY_DEST);
            D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=alpha_upload;src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=scene.footprint;src.PlacedFootprint.Footprint.Format=DXGI_FORMAT_R32_FLOAT;
            D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=alpha;dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            ID3D12GraphicsCommandList_CopyTextureRegion(full.list,&dst,0,0,0,&src,nullptr);Transition(full.list,alpha,state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);full.Submit();
        }
    }
    void Bind(NgxParams* params){if(!mode)return;SetResource(params,nr_fg_contract::hudless,scene.backbuffer);SetResource(params,nr_fg_contract::ui_alpha,alpha);}
    void Fill(){if(mode)scene.Fill();}
    void Observe(Io& full){if(!mode)return;auto state=D3D12_RESOURCE_STATE_PRESENT;
        Transition(full.list,scene.backbuffer,state,D3D12_RESOURCE_STATE_COPY_SOURCE);Transition(full.list,scene.backbuffer,state,D3D12_RESOURCE_STATE_PRESENT);
        if(alpha){state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;Transition(full.list,alpha,state,D3D12_RESOURCE_STATE_COMMON);Transition(full.list,alpha,state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);}}
    void CheckAdoption(Io& full){if(!mode)return;const unsigned changed=scene.Read();Check(changed>0,"real NR updated actual HUDless input");
        unsigned pixels_by_zone[3]{},changed_by_zone[3]{},maximum_error=0;
        for(unsigned y=0;y<Io::height;++y)for(unsigned x=0;x<Io::width;++x){const unsigned i=y*Io::width+x,zone=std::min(2u,x*3/Io::width);
            ++pixels_by_zone[zone];changed_by_zone[zone]+=(full.pixels[i]&0xffffff)!=(full.reference[i]&0xffffff);
            if(zone==2||(mode==1&&zone==1)){Check(full.pixels[i]==full.reference[i],"protected native HUD pixels byte-exact");continue;}
            const double coverage=mode==1?0:zone*.5;
            for(unsigned ch=0;ch<3;++ch){const unsigned shift=ch*8;const unsigned before=(full.reference[i]>>shift)&255;
                const double delta=Linear((scene.pixels[i]>>shift)&255)-Linear((scene.reference[i]>>shift)&255);
                const unsigned expected=Srgb(Linear(before)+delta*(1-coverage)),actual=(full.pixels[i]>>shift)&255;
                maximum_error=std::max(maximum_error,actual>expected?actual-expected:expected-actual);}
        }
        Check(changed_by_zone[0]>0,"uncovered scene visibly adopted");if(mode==2)Check(changed_by_zone[1]>0,"half-alpha scene contribution adopted");
        Check(maximum_error<=3,"full/HUDless scene delta within 8-bit quantization tolerance");
        std::printf("actual-HUDless mode=%u changedScene=%u changedZones=%u/%u/%u maximumSrgbCodeError=%u\n",mode,changed,changed_by_zone[0],changed_by_zone[1],changed_by_zone[2],maximum_error);
    }
};
