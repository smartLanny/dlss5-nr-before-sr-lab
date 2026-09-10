#pragma once
#include "nr_face_detector.h"
#include "nr_region_inputs.h"
#include <array>
#include <memory>

namespace nr_face {
constexpr unsigned kMaxPixels=512*512;
struct Reference {
    nr_region::Frame frame{};
    unsigned width=0,height=0;
    // A fixed contiguous BGR8 image. The renderer never resizes this storage.
    std::array<std::uint8_t,kMaxPixels*3> bgr{};
};
struct Result {
    nr_region::Frame frame{};
    std::array<Face,16> faces{};
    unsigned count=0;
    nr_region::Status status=nr_region::Status::Waiting;
    double cpu_ms=0;
};
class Worker {
public:
    Worker();
    ~Worker();
    Worker(const Worker&)=delete;
    Worker& operator=(const Worker&)=delete;
    // Start queues initialization; model and DLL I/O occur only on the worker.
    bool Start(const std::wstring& runtime,const std::wstring& model);
    // Try-lock operations: a busy worker drops the request, never blocks a frame.
    bool Submit(const Reference& reference);
    bool Take(Result& result);
    bool Accepting() const;
    void Stop(); // Signals cancellation; never joins/waits on the rendering thread.
private:
    struct State;
    std::shared_ptr<State> state_;
};
}
