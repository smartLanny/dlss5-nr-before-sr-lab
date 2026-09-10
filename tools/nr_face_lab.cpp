#include "../src/nr_face_detector.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>
namespace fs=std::filesystem;
std::vector<unsigned char> Read(const fs::path& path) {
    const auto n=fs::file_size(path);
    if(!n||n>32*1024*1024)throw std::runtime_error("Invalid input length");
    std::ifstream file(path,std::ios::binary);std::vector<unsigned char> data(n);
    if(!file.read(reinterpret_cast<char*>(data.data()),n))throw std::runtime_error("Input read failed");
    return data;
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc!=8)throw std::runtime_error("Usage: nr-face-lab runtime.dll model.onnx model-sha input.bgr width height output-new.json");
        const auto model=Read(argv[2]),bgr=Read(argv[4]);const std::wstring whash=argv[3];
        const std::string hash(whash.begin(),whash.end());
        const auto width=std::stoul(argv[5]),height=std::stoul(argv[6]);
        if(width>512||height>512||bgr.size()!=width*height*3||fs::exists(argv[7]))throw std::runtime_error("Invalid dimensions or output exists");
        nr_face::Detector detector;std::string error;
        if(!detector.Open(argv[1],model.data(),model.size(),hash.c_str(),error))throw std::runtime_error(error);
        std::vector<nr_face::Face> faces;std::vector<double> times;
        for(unsigned i=0;i<36;++i) {
            const auto start=std::chrono::steady_clock::now();
            if(!detector.Detect(bgr.data(),static_cast<unsigned>(width),static_cast<unsigned>(height),width*3,faces,error))throw std::runtime_error(error);
            const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            if(i>=4)times.push_back(ms);
        }
        std::sort(times.begin(),times.end());
        std::ofstream out(fs::path(argv[7]),std::ios::binary);out<<std::setprecision(9);
        out<<"{\"scope\":\"native CPU only; no game or GPU performance claim\",\"median_ms\":"<<(times[15]+times[16])*.5<<",\"p95_ms\":"<<times[30]<<",\"faces\":[";
        for(std::size_t i=0;i<faces.size();++i) {
            if(i)out<<',';const auto& f=faces[i];out<<'['<<f.x<<','<<f.y<<','<<f.width<<','<<f.height;
            for(float v:f.landmarks)out<<','<<v;out<<','<<f.confidence<<']';
        }
        out<<"]}\n";if(!out)throw std::runtime_error("Output write failed");
        std::cout<<"Faces="<<faces.size()<<" median_ms="<<(times[15]+times[16])*.5<<"\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
