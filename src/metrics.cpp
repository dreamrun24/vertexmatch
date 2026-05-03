#include "vertexmatch/metrics.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace vm {

static double percentile(const std::vector<std::uint64_t>& v, double p) {
  if (v.empty()) return 0.0;
  const auto idx = static_cast<std::size_t>((p * static_cast<double>(v.size() - 1)) + 0.5);
  return static_cast<double>(v[std::min(idx, v.size() - 1)]);
}

RunMetrics summarize_latencies(const std::string& engine,
                               const std::string& mode,
                               const std::string& label,
                               std::vector<std::uint64_t>& latencies_ns,
                               std::uint64_t trades,
                               double elapsed_seconds) {
  std::sort(latencies_ns.begin(), latencies_ns.end());
  RunMetrics m;
  m.engine = engine;
  m.mode = mode;
  m.label = label;
  m.orders = latencies_ns.size();
  m.trades = trades;
  m.elapsed_seconds = elapsed_seconds;
  m.throughput = elapsed_seconds > 0.0 ? static_cast<double>(m.orders) / elapsed_seconds : 0.0;
  if (!latencies_ns.empty()) {
    long double sum = 0.0;
    for (const auto ns : latencies_ns) sum += static_cast<long double>(ns);
    m.avg_ns = static_cast<double>(sum / static_cast<long double>(latencies_ns.size()));
  }
  m.p50_ns = percentile(latencies_ns, 0.50);
  m.p90_ns = percentile(latencies_ns, 0.90);
  m.p99_ns = percentile(latencies_ns, 0.99);
  m.max_ns = latencies_ns.empty() ? 0.0 : static_cast<double>(latencies_ns.back());
  return m;
}

void write_metrics_csv(const std::string& path, const RunMetrics& m) {
  std::ofstream out(path);
  if (!out) throw std::runtime_error("failed to open metrics csv: " + path);
  out << "engine,mode,label,orders,trades,elapsed_seconds,throughput,avg_ns,p50_ns,p90_ns,p99_ns,max_ns\n";
  out << m.engine << ',' << m.mode << ',' << m.label << ',' << m.orders << ',' << m.trades << ','
      << std::setprecision(12) << m.elapsed_seconds << ',' << m.throughput << ','
      << m.avg_ns << ',' << m.p50_ns << ',' << m.p90_ns << ',' << m.p99_ns << ',' << m.max_ns << '\n';
}

void write_metrics_json(const std::string& path, const RunMetrics& m) {
  std::ofstream out(path);
  if (!out) throw std::runtime_error("failed to open metrics json: " + path);
  out << "{\n"
      << "  \"engine\": \"" << m.engine << "\",\n"
      << "  \"mode\": \"" << m.mode << "\",\n"
      << "  \"label\": \"" << m.label << "\",\n"
      << "  \"orders\": " << m.orders << ",\n"
      << "  \"trades\": " << m.trades << ",\n"
      << "  \"elapsed_seconds\": " << std::setprecision(12) << m.elapsed_seconds << ",\n"
      << "  \"throughput\": " << m.throughput << ",\n"
      << "  \"avg_ns\": " << m.avg_ns << ",\n"
      << "  \"p50_ns\": " << m.p50_ns << ",\n"
      << "  \"p90_ns\": " << m.p90_ns << ",\n"
      << "  \"p99_ns\": " << m.p99_ns << ",\n"
      << "  \"max_ns\": " << m.max_ns << "\n"
      << "}\n";
}

void write_latency_csv(const std::string& path,
                       const std::vector<std::uint64_t>& latencies_ns) {
  std::ofstream out(path);
  if (!out) throw std::runtime_error("failed to open latency csv: " + path);
  out << "index,latency_ns\n";
  for (std::size_t i = 0; i < latencies_ns.size(); ++i) {
    out << i << ',' << latencies_ns[i] << '\n';
  }
}

}  // namespace vm
