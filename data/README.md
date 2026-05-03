# VertexMatch Data

Generate reproducible CSV feeds with:

```bash
./build/vertexmatch_cli generate --out data/orders.csv --orders 1000000 --scenario market-like --seed 42
```

Generate simplified ITCH-style lifecycle feeds with:

```bash
./build/vertexmatch_cli generate-itch --out data/itch_orders.csv --orders 1000000 --seed 42 --symbol VERT
```

Replay ITCH-style input with:

```bash
./build/vertexmatch_cli bench --input data/itch_orders.csv --input-format itch --mode all --out-prefix data/itch --pressure-mb 64
```

Research-scale stress replay:

```bash
./build/vertexmatch_cli generate-itch --out data/itch_stress_1m.csv --orders 1000000 --seed 42 --symbol STRESS
./build/vertexmatch_cli run --engine optimized --input data/itch_stress_1m.csv --input-format itch --mode full_pipeline_latency --pressure-mb 128 --metrics data/itch_stress_full.json --latency data/itch_stress_full_latency.csv --trades data/itch_stress_full_trades.csv
```

Scenarios:

- `market-like`: cancel-heavy, bursty, moving-midpoint flow with more than 200 target price levels.
- `balanced`: two-sided flow around the midpoint.
- `burst`: short aggressive bursts every 10k events.
- `skewed`: buy-heavy flow with more market orders.
- `heavy-cancel`: cancellation-dominated traffic.
- `edge`: empty-book, crossed-limit, small-quantity, and rapid-price-change cases.
