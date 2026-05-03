# VertexMatch

## What is this project?

VertexMatch is a fast C++ system that simulates how a stock exchange matches buy and sell orders.

In financial markets, people place orders like:

* “Buy at this price”
* “Sell at this price”

The exchange keeps track of all these orders and matches them when possible. This system is called a **limit order book**.

VertexMatch builds this system from scratch and focuses on making it:

* fast
* predictable
* measurable

There is no UI — everything runs through the command line and outputs results as CSV/JSON files.

---

## How it works (simple view)

```
Order data (CSV / ITCH)
        ↓
Parser (reads input efficiently)
        ↓
Matching engine (processes orders)
        ↓
Trade log (records trades)
        ↓
Metrics (latency, throughput)
```

---

## Two versions of the engine

### 1. Baseline (simple version)

* Uses standard C++ containers (`std::map`, `deque`)
* Easy to understand
* Slower because of memory and allocation overhead

---

### 2. Optimized (fast version)

* Uses preallocated memory (no `new` or `malloc` during matching)
* Stores data in continuous blocks (better for CPU cache)
* Cancels orders in O(1) time
* Avoids pointer-heavy structures

👉 This version is designed for **low latency**

---

## Why memory layout matters

Modern CPUs are very sensitive to how data is stored.

VertexMatch improves performance by:

* keeping frequently used data close together in memory
* avoiding pointer chasing
* reducing cache misses
* separating shared variables to avoid contention

👉 Result: more predictable and faster execution

---

## Input data

VertexMatch can process two types of data:

### 1. Simple CSV orders

Example:

```
timestamp_ns,kind,side,order_id,price,quantity
0,LIMIT,BUY,1,99999,100
```

---

### 2. ITCH-style data (exchange-like)

This simulates real exchange events:

* Add order
* Execute order
* Cancel order
* Replace order

Example:

```
sequence,timestamp_ns,msg,order_id,price,quantity
1,0,A,1001,99995,200
```

Internally, these are converted into actions like:

* Add → place order
* Execute → reduce quantity
* Cancel/Delete → remove order
* Replace → cancel + new order

---

## Running the project

### Build

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

---

### Run tests

```
./build/vertexmatch_cli selftest
```

---

### Generate test data

```
./build/vertexmatch_cli generate --out data/orders.csv --orders 1000000
```

---

### Generate ITCH-style data

```
./build/vertexmatch_cli generate-itch --out data/itch_orders.csv --orders 1000000
```

---

### Run benchmarks

```
./build/vertexmatch_cli bench --input data/itch_orders.csv --input-format itch --mode all
```

---

## How performance is measured

VertexMatch measures performance in different ways:

### 1. Hot path (fastest case)

* Measures only the matching function
* Very fast (best-case scenario)

---

### 2. Engine loop

* Includes matching + logging
* Still in memory

---

### 3. Ingest → match

* Includes reading and parsing input

---

### 4. Full pipeline

* Includes everything:

  * input
  * matching
  * logging
  * output

---

## Important idea

There are two very different questions:

* “How fast is the core function?”
* “How fast is the whole system?”

VertexMatch measures both.

---

## Why p99 matters

Instead of just looking at average latency, we look at **p99 (tail latency)**.

Why?

Because:

* averages hide slow cases
* real trading systems care about worst-case delays

👉 Missing one trade due to a slow spike can matter more than average speed

---

## Data realism

The system generates realistic workloads with:

* many cancel orders (60–70%)
* changing prices
* bursts of activity
* large datasets (1M+ orders)

This helps simulate real market behavior.

---

## Profiling

You can analyze performance using Linux tools like:

```
perf
```

This helps identify:

* cache misses
* branch prediction issues
* CPU efficiency

---

## Key takeaway

VertexMatch is not just about being fast.

It shows:

* how system design affects performance
* how data patterns change latency
* how to measure performance correctly

---

## Disclaimer

This project is for learning and research only.
It is not intended for real trading use.
