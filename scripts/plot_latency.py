#!/usr/bin/env python3
import csv
import sys
from pathlib import Path

import matplotlib.pyplot as plt


def load(path):
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        return [int(row["latency_ns"]) for row in reader]


def main():
    if len(sys.argv) < 3:
        print("usage: plot_latency.py output.png latency_a.csv [latency_b.csv ...]")
        return 2
    out = sys.argv[1]
    for path in sys.argv[2:]:
        data = load(path)
        label = Path(path).stem
        plt.hist(data, bins=120, alpha=0.45, label=label)
    plt.xlabel("latency (ns)")
    plt.ylabel("orders")
    plt.legend()
    plt.tight_layout()
    plt.savefig(out, dpi=160)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
