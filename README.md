# VertexMatch

VertexMatch is a research-grade C++17 limit order book matching engine focused on deterministic behavior, price-time priority, and measurable low-latency performance. It has no UI; the product surface is a CLI plus CSV/JSON benchmark outputs.

## Architecture

```text
CSV feed / generator
        |
        v
 zero-copy parser on Linux mmap
        |
        v
 +----------------------+      non-blocking SPSC      +----------------+
 | single-thread matcher | --------------------------> | trade log sink |
 +----------------------+                              +----------------+
        |
        v
 metrics: throughput, p50, p90, p99, max latency
```

## Implementations

Baseline uses `std::map<price, deque<order>>` for bids and asks. It is simple and auditable, but every price-level operation pays tree and allocator costs.

Optimized uses contiguous, cache-line-aligned price levels and an intrusive preallocated order pool. Order ids index directly into a slot table, so cancel lookup is O(1). Price levels store FIFO head/tail indexes rather than pointers. No `std::map`, `new`, or `malloc` is used in the matching hot path after initialization.

## Memory Layout

- `alignas(64)` price levels keep hot head/tail/aggregate fields on separate cache lines.
- Resting orders are compact `alignas(32)` records in a contiguous vector.
- Queues are represented by integer indexes, reducing pointer chasing and improving hardware prefetch behavior.
- SPSC ring head and tail atomics are separated by cache-line padding to avoid false sharing.
- The optimized book trades memory for determinism: it preallocates the price grid, order pool, id lookup table, and free stack.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

If Google Benchmark is installed, CMake also builds `vertexmatch_google_bench`.

## CLI

```bash
./build/vertexmatch_cli selftest
./build/vertexmatch_cli generate --out data/orders.csv --orders 1000000 --scenario market-like --seed 42
./build/vertexmatch_cli generate-itch --out data/itch_orders.csv --orders 1000000 --seed 42 --symbol VERT
./build/vertexmatch_cli run --engine optimized --input data/orders.csv --mode hot_path_latency
./build/vertexmatch_cli run --engine optimized --input data/itch_orders.csv --input-format itch --mode engine_loop_latency --pressure-mb 64
./build/vertexmatch_cli bench --input data/itch_orders.csv --input-format itch --mode all --out-prefix results/run1 --pressure-mb 64
```

CSV input format:

```csv
timestamp_ns,kind,side,order_id,price,quantity
0,LIMIT,BUY,1,99999,100
100,LIMIT,SELL,2,100001,100
200,MARKET,BUY,3,0,50
300,CANCEL,BUY,1,0,0
```

## ITCH-Style Replay

VertexMatch supports a simplified NASDAQ TotalView-ITCH-style CSV for event-driven book replay. This is not the full binary ITCH specification; it is a research format that preserves the important lifecycle semantics: sequenced incremental messages mutate the book over time.

ITCH-style CSV format:

```csv
sequence,timestamp_ns,msg,symbol,order_id,new_order_id,side,price,quantity
1,0,A,VERT,1001,0,BUY,99995,200
2,250,E,VERT,1001,0,BUY,0,50
3,500,C,VERT,1001,0,BUY,0,0
4,750,R,VERT,1002,1003,SELL,100010,100
```

Mapping into engine actions:

- `A` Add Order -> `LIMIT` insert
- `E` Order Executed -> `EXECUTE` reduce resting quantity by `order_id`
- `C` Cancel Order -> `CANCEL` remove by `order_id`
- `D` Delete Order -> `CANCEL` remove by `order_id`
- `R` Replace Order -> `CANCEL old_id` followed by `LIMIT new_id`

Replay preserves sequence ordering. The simplified parser sorts by `sequence`, converts lifecycle messages into internal engine actions, and then feeds the same benchmark modes as normal CSV input.

Generate a replayable ITCH-style feed:

```bash
./build/vertexmatch_cli generate-itch --out data/itch_orders.csv --orders 1000000 --seed 42 --symbol VERT
```

Run benchmarks on ITCH-style input:

```bash
./build/vertexmatch_cli bench --input data/itch_orders.csv --input-format itch --mode all --out-prefix results/itch --pressure-mb 64
```

Market-data nuance: a real feed excerpt can contain cancels for orders that existed before the capture window. A fully reconstructable file starting from an empty book needs the corresponding adds or an opening snapshot. VertexMatch currently generates self-contained replay files, so very high cancel ratios require either a larger opening add phase or a future opening-snapshot input mode.

## Benchmark Modes

VertexMatch separates microbenchmark timing from pipeline timing. This distinction matters: a warmed `book->process(order)` call can report tens of nanoseconds, while realistic ingest, logging, serialization, cache pressure, and tail behavior live in microseconds.

- `hot_path_latency`: times only `book->process(order)`. This is a lower-bound microbenchmark for the cache-hot matching core.
- `engine_loop_latency`: times the full in-memory engine loop: memory-pressure touch, matching, SPSC trade publication, and ring drain. It excludes file I/O.
- `ingest_to_match_latency`: includes CSV read, parsing, book execution, and engine-loop latency. It excludes output writing.
- `full_pipeline_latency`: includes ingest, parse, matching, SPSC logging, latency/trade CSV writing, and metrics output.

If p50 is below `0.1 us`, the CLI prints:

```text
warning: Latency likely reflects microbenchmark or unrealistic workload
```

## Benchmark Output

The CLI prints terminal results with explicit mode labels:

```text
=== HOT PATH (MICROBENCHMARK) ===
Engine: optimized
Label: Microbenchmark (cache-optimized hot path)
p50: 0.03 us
p90: 0.07 us
p99: 0.14 us

=== ENGINE LOOP ===
throughput: ...
avg latency: ...

=== INGEST -> MATCH ===
total time: ...
throughput: ...

=== FULL PIPELINE ===
total time: ...
throughput: ...
```

Outputs:

- `*_<engine>_<mode>.json`: throughput and percentile summaries.
- `*_latency.csv`: per-order latency samples.
- `*_trades.csv`: trade log events.

Prefer p99 over average when evaluating improvements. Averages hide sparse price scans, cache misses, allocator effects in the baseline, burst pressure, and logging serialization. In trading systems the bad tail is often where the missed fill lives.

## Data Realism

The default generator is `market-like`:

```bash
./build/vertexmatch_cli generate --out data/orders.csv --orders 1000000 --scenario market-like --seed 42
```

It targets:

- at least `1,000,000` orders for research runs,
- roughly `60-70%` cancels,
- limit, market, and cancel traffic,
- more than `200` nonzero price levels,
- moving midpoint and spread,
- clustered burst windows,
- alternating side skew,
- rapid price jumps and empty-book edge cases.

The CLI prints workload statistics before running a benchmark and warns when the dataset is below these realism targets. `--pressure-mb` adds a configurable memory-pressure buffer to non-hot-path modes so the run is less dominated by a tiny warm working set.

For exchange-style replay, prefer `generate-itch` or a converted ITCH CSV over plain synthetic orders. The ITCH-style path exercises order lifecycle events, id-targeted cancels, id-targeted executions, and replace expansion.

The ITCH stress generator is intentionally harsher than the plain synthetic generator:

- volatility spike windows with larger midpoint jumps and wider spreads,
- clustered bursts with compressed timestamps,
- sparse-book phases that cancel near-touch liquidity,
- sweep-like execution clusters that remove multiple top-of-book orders over short windows,
- external-window cancels/deletes so a captured-feed excerpt can reach realistic `60-70%` cancel/delete ratios,
- thousands of price levels so parsing, replay state, and book access exceed tiny warm-cache behavior.

Reproduce the research-scale stress run:

```bash
./build/vertexmatch_cli generate-itch --out data/itch_stress_1m.csv --orders 1000000 --seed 42 --symbol STRESS
./build/vertexmatch_cli run --engine optimized --input data/itch_stress_1m.csv --input-format itch --mode full_pipeline_latency --pressure-mb 128 --metrics data/itch_stress_full.json --latency data/itch_stress_full_latency.csv --trades data/itch_stress_full_trades.csv
```

## Profiling

Linux perf integration:

```bash
scripts/profile_perf.sh optimized data/orders.csv build profiles
scripts/profile_perf.sh baseline data/orders.csv build profiles
```

This records `perf stat -d -d -d`, call graphs, text reports, and flame graphs when `stackcollapse-perf.pl` and `flamegraph.pl` are available.

Key counters to inspect:

- `cycles` and `instructions`: instruction pipeline efficiency.
- `branches` and `branch-misses`: price-crossing and queue-empty branch predictability.
- `cache-misses`, `L1-dcache-load-misses`, LLC misses: locality of order pool and price levels.
- Context switches and migrations: benchmark noise.

## Expected Performance Reasoning

The optimized engine should improve p99 latency primarily by removing tree rebalancing, allocator variability, and pointer-heavy traversal from the hot path. Direct price indexing makes best-level access predictable. Intrusive FIFO queues keep matching work close in memory, while preallocation removes allocator jitter during bursts and heavy cancellation phases.

Baseline remains valuable as a correctness oracle and as a comparison point for profiling. If optimized p50 improves but p99 does not, inspect ring buffer drops, branch misses around empty levels, memory-pressure sensitivity, and long scans when price ranges are sparse.

## Hardware Specs

Record these with every benchmark run:

```bash
lscpu
numactl --hardware
free -h
uname -a
```

For reproducibility, pin the process and use the performance governor when permitted:

```bash
taskset -c 2 ./build/vertexmatch_cli bench --input data/orders.csv --out-prefix results/pinned
```

## Correctness

The self-test runs hand-authored FIFO/partial-fill/cancel cases plus generated edge flow through both engines and compares trades and top-of-book state:

```bash
./build/vertexmatch_cli selftest
```
## Disclaimer

This project is for research and educational purposes only. Not intended for live trading.
