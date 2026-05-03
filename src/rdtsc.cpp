#include "vertexmatch/rdtsc.hpp"

#include <chrono>
#include <thread>

#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#endif

namespace vm {

std::uint64_t monotonic_ns() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

std::uint64_t rdtsc_ticks() {
#if defined(_MSC_VER)
  return __rdtsc();
#elif defined(__x86_64__) || defined(__i386__)
  return __rdtsc();
#else
  return monotonic_ns();
#endif
}

double rdtsc_ticks_per_ns() {
#if defined(__x86_64__) || defined(__i386__) || defined(_MSC_VER)
  static const double cached = [] {
    const auto ns0 = monotonic_ns();
    const auto t0 = rdtsc_ticks();
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    const auto t1 = rdtsc_ticks();
    const auto ns1 = monotonic_ns();
    const auto dns = static_cast<double>(ns1 - ns0);
    return dns > 0.0 ? static_cast<double>(t1 - t0) / dns : 1.0;
  }();
  return cached;
#else
  return 1.0;
#endif
}

}  // namespace vm
