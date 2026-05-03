#include <benchmark/benchmark.h>

#include <vector>

#include "vertexmatch/book.hpp"
#include "vertexmatch/generator.hpp"

namespace {

template <typename Book>
void bench_book(benchmark::State& state) {
  const auto orders = vm::generate_orders(static_cast<std::size_t>(state.range(0)),
                                          vm::Scenario::Balanced, 7, 100000, 4);
  for (auto _ : state) {
    Book book;
    std::vector<vm::Trade> trades;
    trades.reserve(orders.size() / 4);
    for (const auto& order : orders) book.process(order, trades);
    benchmark::DoNotOptimize(trades.size());
  }
  state.SetItemsProcessed(state.iterations() * state.range(0));
}

void bench_optimized(benchmark::State& state) {
  const auto orders = vm::generate_orders(static_cast<std::size_t>(state.range(0)),
                                          vm::Scenario::Balanced, 7, 100000, 4);
  for (auto _ : state) {
    vm::OptimizedBook book({1, 200000, static_cast<std::size_t>(state.range(0) + 1024)});
    std::vector<vm::Trade> trades;
    trades.reserve(orders.size() / 4);
    for (const auto& order : orders) book.process(order, trades);
    benchmark::DoNotOptimize(trades.size());
  }
  state.SetItemsProcessed(state.iterations() * state.range(0));
}

}  // namespace

BENCHMARK_TEMPLATE(bench_book, vm::BaselineBook)->Arg(10000)->Arg(100000);
BENCHMARK(bench_optimized)->Arg(10000)->Arg(100000);
BENCHMARK_MAIN();
