#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace nr_face {
struct Face {
    float x, y, width, height; // Normalized reference-image coordinates.
    float landmarks[10];
    float confidence;
};
// CPU-only, worker-owned detector. No graphics API, settings or render-thread waits.
// Input is an already resized/padded BGR8 reference derived BEFORE NR.
class Detector {
public:
    Detector();
    ~Detector();
    Detector(const Detector&) = delete;
    Detector& operator=(const Detector&) = delete;
    bool Open(const wchar_t* runtime_path, const void* model, std::size_t model_size,
              const char* model_sha256, std::string& error);
    bool Detect(const std::uint8_t* bgr, unsigned width, unsigned height,
                std::size_t row_bytes, std::vector<Face>& faces, std::string& error);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
