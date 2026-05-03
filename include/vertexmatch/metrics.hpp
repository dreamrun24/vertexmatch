#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vm {

struct RunMetrics {
  std::string engine;
  std::string mode;
  std::string label;
  std::uint64_t orders{0};
  std::uint64_t trades{0};
  double elapsed_seconds{0.0};
  double throughput{0.0};
  double avg_ns{0.0};
  double p50_ns{0.0};
  double p90_ns{0.0};
  double p99_ns{0.0};
  double max_ns{0.0};
};

RunMetrics summarize_latencies(const std::string& engine,
                               const std::string& mode,
                               const std::string& label,
                               std::vector<std::uint64_t>& latencies_ns,
                               std::uint64_t trades,
                               double elapsed_seconds);

void write_metrics_csv(const std::string& path, const RunMetrics& metrics);
void write_metrics_json(const std::string& path, const RunMetrics& metrics);
void write_latency_csv(const std::string& path,
                       const std::vector<std::uint64_t>& latencies_ns);

}  // namespace vm
