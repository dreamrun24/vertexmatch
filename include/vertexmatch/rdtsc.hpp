#pragma once

#include <cstdint>

namespace vm {

std::uint64_t monotonic_ns();
std::uint64_t rdtsc_ticks();
double rdtsc_ticks_per_ns();

}  // namespace vm
