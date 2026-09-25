// Offline analysis only. No model, GPU, capture or network access.
#include "nr_skin_protection.h"
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
using namespace nr_skin_protection;

static std::string Token(std::istream& in) {
    std::string token;
    while (in >> std::ws && in.peek() == '#') in.ignore(std::numeric_limits<std::streamsize>::max(),'\n');
    in >> token; return token;
}
static unsigned Dimension(const std::string& token) {
    if (token.empty() || token.find_first_not_of("0123456789") != std::string::npos) throw 0;
    auto value = std::stoul(token);
    if (value == 0 || value > 8192) throw 0;
    return static_cast<unsigned>(value);
}
int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--samples") {
        struct Sample { const char* name; NrSkinRgb rgb; };
        Sample samples[] = {
            {"synthetic_warm_skin",{.76f,.49f,.36f}}, {"synthetic_dark_skin",{.30f,.17f,.11f}},
            {"warm_wood_same_colour_false_positive",{.46f,.28f,.19f}},
            {"low_saturation_skin_miss",{.85f,.84f,.83f}},
            {"blue_light_skin_miss",{.2f,.3f,.8f}}, {"red_light",{1,0,0}},
            {"green_material",{.2f,.8f,.1f}}, {"near_black",{.02f,.01f,.005f}}, {"grey",{.5f,.5f,.5f}}
        };
        std::cout << "sample,r,g,b,confidence,valid\n";
        for (const auto& s : samples) {
            auto m = NrSkinClassify(s.rgb,true);
            std::cout << s.name << ',' << s.rgb.r << ',' << s.rgb.g << ',' << s.rgb.b << ','
                      << m.confidence << ',' << m.valid << '\n';
        }
        return 0;
    }
    if (argc != 5 || std::string(argv[1]) != "--original-srgb709-ppm" ||
        std::string(argv[3]) != "--mask-pgm") {
        std::cerr << "Usage: fixture --samples\n"
                  << "  fixture --original-srgb709-ppm input.ppm --mask-pgm new-mask.pgm\n"
                  << "Input must be authorized, unmodified SDR sRGB/BT.709 original reference.\n";
        return 2;
    }
    // Output is a derived confidence DATA plane, not a beautified/edited source.
    // Refuse existing output so analysis cannot accidentally replace evidence.
    if (std::ifstream(argv[4],std::ios::binary).good()) {
        std::cerr << "Output already exists; choose a new mask path.\n"; return 2;
    }
    std::ifstream in(argv[2],std::ios::binary);
    if (!in || Token(in) != "P6") { std::cerr << "Require binary P6 PPM.\n"; return 2; }
    unsigned width = 0, height = 0;
    try {
        width = Dimension(Token(in));
        height = Dimension(Token(in));
        if (Token(in) != "255") throw 0;
    } catch (...) { std::cerr << "Require dimensions and maxval=255.\n"; return 2; }
    if (!width || !height || width > 8192 || height > 8192 ||
        static_cast<unsigned long long>(width)*height > 33554432) {
        std::cerr << "Image exceeds the offline 32M pixel/8192 dimension budget.\n"; return 2;
    }
    char delimiter = 0; in.get(delimiter);
    if (delimiter != '\n' && delimiter != '\r' && delimiter != ' ' && delimiter != '\t') return 2;
    if (delimiter == '\r' && in.peek() == '\n') in.get();
    const size_t pixels = static_cast<size_t>(width)*height;
    std::vector<unsigned char> rgb(pixels*3), mask(pixels);
    in.read(reinterpret_cast<char*>(rgb.data()),static_cast<std::streamsize>(rgb.size()));
    if (in.gcount() != static_cast<std::streamsize>(rgb.size()) || in.peek() != std::char_traits<char>::eof()) {
        std::cerr << "Require exactly one complete PPM frame.\n"; return 2;
    }
    double sum = 0; size_t selected = 0;
    for (size_t i = 0; i < pixels; ++i) {
        auto m = NrSkinClassify(NrSkinRgbValue(rgb[3*i]/255.f,rgb[3*i+1]/255.f,rgb[3*i+2]/255.f),true);
        mask[i] = static_cast<unsigned char>(m.confidence*255.f+.5f);
        sum += m.confidence; selected += m.confidence > .5f;
    }
    std::ofstream out(argv[4],std::ios::binary);
    out << "P5\n" << width << ' ' << height << "\n255\n";
    out.write(reinterpret_cast<const char*>(mask.data()),static_cast<std::streamsize>(mask.size()));
    if (!out) { std::cerr << "Mask write failed.\n"; return 1; }
    std::cout << "pixels=" << pixels << " mean_confidence=" << sum/pixels
              << " confidence_above_half=" << selected << " (colour selection, not faces)\n";
}
