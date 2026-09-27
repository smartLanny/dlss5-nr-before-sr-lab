#pragma once
#if __has_include("private_sr/nr_edit_dx12.hpp")
#include "private_sr/nr_edit_dx12.hpp"
#else
#include "../experiments/nr-edit/src/nr_edit_dx12.hpp"
#endif

namespace nr_private_sr_artifacts {
std::array<nr_edit_dx12::Bytecode, static_cast<unsigned>(nr_edit_dx12::Kernel::Count)> Kernels();
}
