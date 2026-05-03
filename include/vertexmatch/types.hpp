#pragma once

#include <cstdint>
#include <string>

namespace vm {

using OrderId = std::uint64_t;
using Price = std::int64_t;
using Quantity = std::uint32_t;

enum class Side : std::uint8_t { Buy = 0, Sell = 1 };
enum class OrderKind : std::uint8_t { Limit = 0, Market = 1, Cancel = 2, Execute = 3 };
enum class EngineKind : std::uint8_t { Baseline = 0, Optimized = 1 };

struct Order {
  std::uint64_t timestamp_ns{0};
  OrderKind kind{OrderKind::Limit};
  Side side{Side::Buy};
  OrderId id{0};
  Price price{0};
  Quantity quantity{0};
};

struct Trade {
  std::uint64_t timestamp_ns{0};
  OrderId aggressor_id{0};
  OrderId resting_id{0};
  Price price{0};
  Quantity quantity{0};
  Side aggressor_side{Side::Buy};
};

struct OrderUpdate {
  OrderId id{0};
  Quantity remaining{0};
  bool accepted{false};
  bool canceled{false};
};

struct BookSnapshot {
  Price best_bid{0};
  Price best_ask{0};
  Quantity best_bid_qty{0};
  Quantity best_ask_qty{0};
  std::uint64_t live_orders{0};
};

inline const char* to_string(Side side) {
  return side == Side::Buy ? "BUY" : "SELL";
}

inline const char* to_string(OrderKind kind) {
  switch (kind) {
    case OrderKind::Limit: return "LIMIT";
    case OrderKind::Market: return "MARKET";
    case OrderKind::Cancel: return "CANCEL";
    case OrderKind::Execute: return "EXECUTE";
  }
  return "UNKNOWN";
}

}  // namespace vm
