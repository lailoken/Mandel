#pragma once

#include "worker_base.hpp"

#include <atomic>
#include <vector>

namespace mandel
{

// Dummy worker for testing UI without Mandelbrot rendering
class DummyWorker : public WorkerBase
{
public:
    DummyWorker(int canvas_width, int canvas_height,
                std::atomic<unsigned int>& current_generation,
                long double canvas_x_min,
                long double canvas_x_max,
                long double canvas_y_min,
                long double canvas_y_max);
    ~DummyWorker() = default;

    // WorkerBase interface
    void start_render() override;
    bool try_complete_render() override;

private:
    void generate_dummy_image();
};

}  // namespace mandel
