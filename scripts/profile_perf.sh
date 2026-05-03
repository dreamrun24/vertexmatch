#!/usr/bin/env bash
set -euo pipefail

ENGINE="${1:-optimized}"
INPUT="${2:-data/orders.csv}"
BUILD_DIR="${3:-build}"
OUT_DIR="${4:-profiles}"

mkdir -p "${OUT_DIR}"

perf stat -d -d -d "${BUILD_DIR}/vertexmatch_cli" run \
  --engine "${ENGINE}" \
  --input "${INPUT}" \
  --metrics "${OUT_DIR}/${ENGINE}_metrics.json"

perf record -F 999 -g -o "${OUT_DIR}/${ENGINE}.perf.data" -- \
  "${BUILD_DIR}/vertexmatch_cli" run \
  --engine "${ENGINE}" \
  --input "${INPUT}" \
  --metrics "${OUT_DIR}/${ENGINE}_record_metrics.json"

perf report -i "${OUT_DIR}/${ENGINE}.perf.data" --stdio > "${OUT_DIR}/${ENGINE}_perf_report.txt"

if command -v stackcollapse-perf.pl >/dev/null 2>&1 && command -v flamegraph.pl >/dev/null 2>&1; then
  perf script -i "${OUT_DIR}/${ENGINE}.perf.data" \
    | stackcollapse-perf.pl \
    | flamegraph.pl > "${OUT_DIR}/${ENGINE}_flamegraph.svg"
fi
