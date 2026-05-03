#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include "vertexmatch/book.hpp"
#include "vertexmatch/csv.hpp"
#include "vertexmatch/generator.hpp"
#include "vertexmatch/itch.hpp"
#include "vertexmatch/metrics.hpp"
#include "vertexmatch/rdtsc.hpp"
#include "vertexmatch/selftest.hpp"
#include "vertexmatch/spsc_ring.hpp"

namespace {

std::string arg_value(int argc, char** argv, const std::string& name, const std::string& fallback = "") {
  for (int i = 2; i + 1 < argc; ++i) {
    if (argv[i] == name) return argv[i + 1];
  }
  return fallback;
}

std::size_t arg_size(int argc, char** argv, const std::string& name, std::size_t fallback) {
  const auto v = arg_value(argc, argv, name);
  return v.empty() ? fallback : static_cast<std::size_t>(std::stoull(v));
}

void usage() {
  std::cout
      << "VertexMatch CLI\n"
      << "  selftest\n"
      << "  generate --out orders.csv --orders 1000000 --scenario market-like|balanced|burst|skewed|heavy-cancel|edge [--seed 42]\n"
      << "  generate-itch --out itch_orders.csv --orders 1000000 [--seed 42] [--symbol VERT]\n"
      << "  run --engine baseline|optimized --input orders.csv --input-format auto|csv|itch [--mode hot_path_latency|engine_loop_latency|ingest_to_match_latency|full_pipeline_latency]\n"
      << "  bench --input orders.csv --input-format auto|csv|itch [--mode all|hot_path_latency|engine_loop_latency|ingest_to_match_latency|full_pipeline_latency] [--out-prefix results] [--pressure-mb 64]\n";
}

enum class BenchMode { HotPath, EngineLoop, IngestToMatch, FullPipeline };

struct WorkloadStats {
  std::uint64_t limit{0};
  std::uint64_t market{0};
  std::uint64_t cancel{0};
  std::uint64_t execute{0};
  std::uint64_t buy{0};
  std::uint64_t sell{0};
  std::size_t price_levels{0};
  vm::Price min_price{0};
  vm::Price max_price{0};
};

struct LoadedFeed {
  std::vector<vm::Order> orders;
  std::string source_format{"csv"};
  vm::ItchStats itch_stats{};
  bool has_itch_stats{false};
};

BenchMode parse_mode(const std::string& mode) {
  if (mode == "hot_path_latency") return BenchMode::HotPath;
  if (mode == "engine_loop_latency") return BenchMode::EngineLoop;
  if (mode == "ingest_to_match_latency") return BenchMode::IngestToMatch;
  if (mode == "full_pipeline_latency") return BenchMode::FullPipeline;
  throw std::runtime_error("unknown mode: " + mode);
}

const char* mode_name(BenchMode mode) {
  switch (mode) {
    case BenchMode::HotPath: return "hot_path_latency";
    case BenchMode::EngineLoop: return "engine_loop_latency";
    case BenchMode::IngestToMatch: return "ingest_to_match_latency";
    case BenchMode::FullPipeline: return "full_pipeline_latency";
  }
  return "unknown";
}

const char* mode_label(BenchMode mode) {
  switch (mode) {
    case BenchMode::HotPath: return "Microbenchmark (cache-optimized hot path)";
    case BenchMode::EngineLoop: return "Engine loop (matching + SPSC logging, no file I/O)";
    case BenchMode::IngestToMatch: return "Ingest to match (file read + parse + matching)";
    case BenchMode::FullPipeline: return "Full pipeline (ingest + match + logging + output writing)";
  }
  return "unknown";
}

WorkloadStats describe_workload(const std::vector<vm::Order>& orders) {
  WorkloadStats s;
  std::unordered_set<vm::Price> levels;
  for (const auto& o : orders) {
    if (o.kind == vm::OrderKind::Limit) ++s.limit;
    else if (o.kind == vm::OrderKind::Market) ++s.market;
    else if (o.kind == vm::OrderKind::Cancel) ++s.cancel;
    else ++s.execute;
    if (o.side == vm::Side::Buy) ++s.buy;
    else ++s.sell;
    if (o.price > 0) {
      levels.insert(o.price);
      s.min_price = s.min_price == 0 ? o.price : std::min(s.min_price, o.price);
      s.max_price = std::max(s.max_price, o.price);
    }
  }
  s.price_levels = levels.size();
  return s;
}

void print_workload_stats(const std::vector<vm::Order>& orders) {
  const auto s = describe_workload(orders);
  std::cout << "\nWorkload\n"
            << "Orders: " << orders.size() << "\n"
            << "Limit: " << s.limit << "  Market: " << s.market
            << "  Cancel: " << s.cancel << "  Execute: " << s.execute << "\n"
            << "Buy: " << s.buy << "  Sell: " << s.sell << "\n"
            << "Unique nonzero price levels: " << s.price_levels << "\n"
            << "Price range: " << s.min_price << "..." << s.max_price << "\n";
  if (orders.size() < 1000000 || s.price_levels < 200 || s.cancel < orders.size() * 6 / 10) {
    std::cout << "warning: workload is below research-grade realism targets "
              << "(>=1M orders, >=200 price levels, 60-70% cancels).\n";
  }
}

bool looks_like_itch_csv(const std::string& path) {
  std::ifstream in(path);
  std::string line;
  if (!std::getline(in, line)) return false;
  return line.find("sequence") != std::string::npos && line.find("msg") != std::string::npos;
}

LoadedFeed load_feed(const std::string& input, const std::string& input_format) {
  const bool itch = input_format == "itch" || (input_format == "auto" && looks_like_itch_csv(input));
  if (input_format != "auto" && input_format != "csv" && input_format != "itch") {
    throw std::runtime_error("unknown --input-format: " + input_format);
  }
  LoadedFeed feed;
  if (itch) {
    auto events = vm::read_itch_csv(input);
    feed.itch_stats = vm::describe_itch(events);
    feed.has_itch_stats = true;
    feed.source_format = "itch";
    feed.orders = vm::replay_itch_to_orders(events);
  } else {
    feed.source_format = "csv";
    feed.orders = vm::read_orders_csv(input);
  }
  return feed;
}

void print_itch_stats(const vm::ItchStats& s) {
  std::cout << "\nITCH Event Stream\n"
            << "Add: " << s.add << "  Execute: " << s.execute
            << "  Cancel: " << s.cancel << "  Delete: " << s.delete_order
            << "  Replace: " << s.replace << "\n"
            << "ITCH price levels: " << s.price_levels << "\n"
            << "ITCH price range: " << s.min_price << "..." << s.max_price << "\n";
  const auto total = s.add + s.execute + s.cancel + s.delete_order + s.replace;
  const auto cancels = s.cancel + s.delete_order;
  if (total < 1000000 || s.price_levels < 200 || cancels < total * 6 / 10 || cancels > total * 8 / 10) {
    std::cout << "warning: ITCH stream is below target realism "
              << "(>=1M events, >=200 price levels, 60-80% cancels/deletes).\n";
  }
}

void print_feed_stats(const LoadedFeed& feed) {
  std::cout << "\nInput format: " << feed.source_format << "\n";
  if (feed.has_itch_stats) print_itch_stats(feed.itch_stats);
  print_workload_stats(feed.orders);
}

std::unique_ptr<vm::IBook> make_book(const std::string& engine, const std::vector<vm::Order>& orders) {
  if (engine == "baseline") return std::make_unique<vm::BaselineBook>();
  if (engine != "optimized") throw std::runtime_error("unknown engine: " + engine);
  vm::Price minp = 1;
  vm::Price maxp = 1;
  vm::OrderId maxid = 1;
  for (const auto& o : orders) {
    if (o.price > 0) {
      minp = std::min(minp == 1 ? o.price : minp, o.price);
      maxp = std::max(maxp, o.price);
    }
    maxid = std::max(maxid, o.id);
  }
  return std::make_unique<vm::OptimizedBook>(
      vm::OptimizedConfig{std::max<vm::Price>(1, minp - 1024), maxp + 1024,
                          static_cast<std::size_t>(maxid + 1024)});
}

volatile std::uint64_t g_pressure_sink = 0;

void touch_memory_pressure(std::vector<std::uint64_t>& pressure, std::uint64_t key) {
  if (pressure.empty()) return;
  const auto idx = (key * 11400714819323198485ull) & (pressure.size() - 1);
  pressure[idx] += key | 1u;
  g_pressure_sink ^= pressure[idx];
}

vm::RunMetrics run_engine_loaded(const std::string& engine,
                                 BenchMode mode,
                                 const std::vector<vm::Order>& orders,
                                 std::vector<vm::Trade>& trade_log,
                                 std::vector<std::uint64_t>& latencies,
                                 std::size_t pressure_mb) {
  auto book = make_book(engine, orders);
  vm::SpscRing<vm::Trade> trade_ring(1 << 20);
  std::uint64_t dropped_logs = 0;
  latencies.clear();
  latencies.reserve(orders.size());
  std::vector<vm::Trade> batch;
  batch.reserve(16);
  const double ticks_per_ns = vm::rdtsc_ticks_per_ns();
  const std::size_t pressure_items = pressure_mb == 0 ? 0 : (std::size_t{1} << 20) * pressure_mb / sizeof(std::uint64_t);
  std::vector<std::uint64_t> pressure;
  if (pressure_items) {
    std::size_t pow2 = 1;
    while (pow2 < pressure_items) pow2 <<= 1;
    pressure.assign(pow2, 0x9e3779b97f4a7c15ull);
  }

  const auto start = std::chrono::steady_clock::now();
  for (const auto& order : orders) {
    if (mode == BenchMode::HotPath) {
      batch.clear();
      const auto t0 = vm::rdtsc_ticks();
      book->process(order, batch);
      const auto t1 = vm::rdtsc_ticks();
      latencies.push_back(static_cast<std::uint64_t>(
          static_cast<double>(t1 - t0) / ticks_per_ns + 0.5));
    } else {
      const auto t0 = vm::rdtsc_ticks();
      touch_memory_pressure(pressure, order.id);
      batch.clear();
      book->process(order, batch);
      for (const auto& tr : batch) {
        if (!trade_ring.push(tr)) ++dropped_logs;
      }
      vm::Trade drained;
      while (trade_ring.pop(drained)) trade_log.push_back(drained);
      const auto t1 = vm::rdtsc_ticks();
      latencies.push_back(static_cast<std::uint64_t>(
          static_cast<double>(t1 - t0) / ticks_per_ns + 0.5));
    }
    if (mode == BenchMode::HotPath) {
      for (const auto& tr : batch) {
        if (!trade_ring.push(tr)) ++dropped_logs;
      }
      vm::Trade drained;
      while (trade_ring.pop(drained)) trade_log.push_back(drained);
    }
  }
  vm::Trade drained;
  while (trade_ring.pop(drained)) trade_log.push_back(drained);
  const auto end = std::chrono::steady_clock::now();
  const double elapsed = std::chrono::duration<double>(end - start).count();
  auto sorted = latencies;
  auto metrics = vm::summarize_latencies(engine, mode_name(mode), mode_label(mode), sorted,
                                         trade_log.size() + dropped_logs, elapsed);
  if (dropped_logs) std::cerr << "warning: dropped " << dropped_logs << " trade log events\n";
  return metrics;
}

void print_metrics(const vm::RunMetrics& m) {
  if (m.mode == "hot_path_latency") std::cout << "\n=== HOT PATH (MICROBENCHMARK) ===\n";
  else if (m.mode == "engine_loop_latency") std::cout << "\n=== ENGINE LOOP ===\n";
  else if (m.mode == "ingest_to_match_latency") std::cout << "\n=== INGEST -> MATCH ===\n";
  else if (m.mode == "full_pipeline_latency") std::cout << "\n=== FULL PIPELINE ===\n";
  else std::cout << "\n=== BENCHMARK ===\n";
  std::cout << "Engine: " << m.engine << "\n"
            << "Label: " << m.label << "\n"
            << "Orders: " << m.orders << "  Trades: " << m.trades << "\n"
            << "Total time: " << std::fixed << std::setprecision(6) << m.elapsed_seconds << " sec\n"
            << "Throughput: " << std::setprecision(2) << m.throughput << " orders/sec\n"
            << "avg latency: " << m.avg_ns / 1000.0 << " us\n"
            << "p50: " << m.p50_ns / 1000.0 << " us\n"
            << "p90: " << m.p90_ns / 1000.0 << " us\n"
            << "p99: " << m.p99_ns / 1000.0 << " us\n";
  if (m.p50_ns < 100.0) {
    std::cout << "warning: Latency likely reflects microbenchmark or unrealistic workload\n";
  }
}

void print_analysis() {
  std::cout << "\nAnalysis Summary\n"
            << "hot_path_latency times the matching call in a warm in-memory book and is useful only as a lower-bound microbenchmark.\n"
            << "engine_loop_latency includes matching, SPSC publication, ring draining, and memory-pressure touches, but still excludes file I/O.\n"
            << "ingest_to_match_latency adds file read, CSV/ITCH parsing, replay conversion, and matching.\n"
            << "full_pipeline_latency adds output serialization, which usually dominates once trade and latency CSVs are written.\n"
            << "ITCH-style replay exercises id-targeted add/execute/cancel/replace lifecycles instead of only submitted order flow.\n"
            << "p99 is the headline number: cache misses, sparse price scans, bursty cancels, and output pressure show up in the tail before they move averages.\n";
}

void write_outputs(const std::string& prefix,
                   const std::string& engine,
                   BenchMode mode,
                   const vm::RunMetrics& metrics,
                   const std::vector<std::uint64_t>& latencies,
                   const std::vector<vm::Trade>& trades,
                   bool include_metrics = true) {
  const std::string stem = prefix + "_" + engine + "_" + mode_name(mode);
  if (include_metrics) {
    vm::write_metrics_json(stem + ".json", metrics);
    vm::write_metrics_csv(stem + ".csv", metrics);
  }
  vm::write_latency_csv(stem + "_latency.csv", latencies);
  vm::write_trades_csv(stem + "_trades.csv", trades);
}

void override_total_time(vm::RunMetrics& metrics, double elapsed_seconds) {
  metrics.elapsed_seconds = elapsed_seconds;
  metrics.throughput = elapsed_seconds > 0.0 ? static_cast<double>(metrics.orders) / elapsed_seconds : 0.0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc < 2) {
      usage();
      return 1;
    }
    const std::string cmd = argv[1];
    if (cmd == "selftest") return vm::run_selftest() ? 0 : 2;

    if (cmd == "generate") {
      const auto out = arg_value(argc, argv, "--out", "orders.csv");
      const auto count = arg_size(argc, argv, "--orders", 1000000);
      const auto scenario = vm::parse_scenario(arg_value(argc, argv, "--scenario", "market-like"));
      const auto seed = static_cast<std::uint64_t>(arg_size(argc, argv, "--seed", 42));
      auto orders = vm::generate_orders(count, scenario, seed, 100000, 8);
      vm::write_orders_csv(out, orders);
      std::cout << "wrote " << orders.size() << " orders to " << out << "\n";
      print_workload_stats(orders);
      return 0;
    }

    if (cmd == "generate-itch") {
      const auto out = arg_value(argc, argv, "--out", "itch_orders.csv");
      const auto count = arg_size(argc, argv, "--orders", 1000000);
      const auto seed = static_cast<std::uint64_t>(arg_size(argc, argv, "--seed", 42));
      const auto symbol = arg_value(argc, argv, "--symbol", "VERT");
      auto events = vm::generate_itch_events(count, seed, 100000, 8, symbol);
      vm::write_itch_csv(out, events);
      std::cout << "wrote " << events.size() << " ITCH-style events to " << out << "\n";
      LoadedFeed feed;
      feed.source_format = "itch";
      feed.has_itch_stats = true;
      feed.itch_stats = vm::describe_itch(events);
      feed.orders = vm::replay_itch_to_orders(events);
      print_feed_stats(feed);
      return 0;
    }

    if (cmd == "run") {
      const auto engine = arg_value(argc, argv, "--engine", "optimized");
      const auto input = arg_value(argc, argv, "--input");
      const auto input_format = arg_value(argc, argv, "--input-format", "auto");
      const auto mode = parse_mode(arg_value(argc, argv, "--mode", "hot_path_latency"));
      const auto pressure_mb = arg_size(argc, argv, "--pressure-mb", 64);
      if (input.empty()) throw std::runtime_error("--input is required");
      const auto total_start = std::chrono::steady_clock::now();
      auto feed = load_feed(input, input_format);
      print_feed_stats(feed);
      std::vector<vm::Trade> trades;
      std::vector<std::uint64_t> latencies;
      auto metrics = run_engine_loaded(engine, mode, feed.orders, trades, latencies, pressure_mb);
      const auto metrics_path = arg_value(argc, argv, "--metrics");
      const auto latency_path = arg_value(argc, argv, "--latency");
      const auto trades_path = arg_value(argc, argv, "--trades");
      if (mode == BenchMode::FullPipeline) {
        if (!latency_path.empty()) vm::write_latency_csv(latency_path, latencies);
        if (!trades_path.empty()) vm::write_trades_csv(trades_path, trades);
      }
      if (mode == BenchMode::IngestToMatch || mode == BenchMode::FullPipeline) {
        const auto total_end = std::chrono::steady_clock::now();
        override_total_time(metrics, std::chrono::duration<double>(total_end - total_start).count());
      }
      print_metrics(metrics);
      if (!metrics_path.empty()) {
        if (metrics_path.size() >= 5 && metrics_path.substr(metrics_path.size() - 5) == ".json") {
          vm::write_metrics_json(metrics_path, metrics);
        } else {
          vm::write_metrics_csv(metrics_path, metrics);
        }
      }
      if (mode != BenchMode::FullPipeline && !latency_path.empty()) vm::write_latency_csv(latency_path, latencies);
      if (mode != BenchMode::FullPipeline && !trades_path.empty()) vm::write_trades_csv(trades_path, trades);
      print_analysis();
      return 0;
    }

    if (cmd == "bench") {
      const auto input = arg_value(argc, argv, "--input");
      if (input.empty()) throw std::runtime_error("--input is required");
      const auto input_format = arg_value(argc, argv, "--input-format", "auto");
      const auto prefix = arg_value(argc, argv, "--out-prefix", "results");
      const auto requested_mode = arg_value(argc, argv, "--mode", "all");
      const auto pressure_mb = arg_size(argc, argv, "--pressure-mb", 64);
      auto feed = load_feed(input, input_format);
      print_feed_stats(feed);
      std::vector<BenchMode> modes;
      if (requested_mode == "all") {
        modes = {BenchMode::HotPath, BenchMode::EngineLoop, BenchMode::IngestToMatch, BenchMode::FullPipeline};
      } else {
        modes = {parse_mode(requested_mode)};
      }
      for (const std::string engine : {"baseline", "optimized"}) {
        for (const auto mode : modes) {
          const auto total_start = std::chrono::steady_clock::now();
          LoadedFeed mode_feed;
          const std::vector<vm::Order>* active_orders = &feed.orders;
          if (mode == BenchMode::IngestToMatch || mode == BenchMode::FullPipeline) {
            mode_feed = load_feed(input, input_format);
            active_orders = &mode_feed.orders;
          }
          std::vector<vm::Trade> trades;
          std::vector<std::uint64_t> latencies;
          auto metrics = run_engine_loaded(engine, mode, *active_orders, trades, latencies, pressure_mb);
          if (mode == BenchMode::FullPipeline) {
            write_outputs(prefix, engine, mode, metrics, latencies, trades, false);
          }
          if (mode == BenchMode::IngestToMatch || mode == BenchMode::FullPipeline) {
            const auto total_end = std::chrono::steady_clock::now();
            override_total_time(metrics, std::chrono::duration<double>(total_end - total_start).count());
          }
          print_metrics(metrics);
          if (mode == BenchMode::FullPipeline) {
            const std::string stem = prefix + "_" + engine + "_" + mode_name(mode);
            vm::write_metrics_json(stem + ".json", metrics);
            vm::write_metrics_csv(stem + ".csv", metrics);
          } else {
            write_outputs(prefix, engine, mode, metrics, latencies, trades);
          }
        }
      }
      print_analysis();
      return 0;
    }

    usage();
    return 1;
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 2;
  }
}
