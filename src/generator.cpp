#include "vertexmatch/generator.hpp"

#include <algorithm>
#include <random>
#include <stdexcept>
#include <unordered_set>

namespace vm {

Scenario parse_scenario(const std::string& name) {
  if (name == "balanced") return Scenario::Balanced;
  if (name == "burst") return Scenario::Burst;
  if (name == "skewed") return Scenario::Skewed;
  if (name == "heavy-cancel") return Scenario::HeavyCancel;
  if (name == "edge") return Scenario::Edge;
  if (name == "market-like") return Scenario::MarketLike;
  throw std::runtime_error("unknown scenario: " + name);
}

std::vector<Order> generate_orders(std::size_t count,
                                   Scenario scenario,
                                   std::uint64_t seed,
                                   Price mid_price,
                                   Price spread_ticks) {
  std::mt19937_64 rng(seed);
  std::uniform_int_distribution<int> side_dist(0, 1);
  std::uniform_int_distribution<int> qty_dist(1, 200);
  std::normal_distribution<double> px_dist(0.0, scenario == Scenario::MarketLike ? 55.0 : 12.0);
  std::uniform_int_distribution<int> jump_dist(-125, 125);
  std::vector<Order> orders;
  orders.reserve(count);
  std::vector<OrderId> live;
  live.reserve(count / 2 + 1);
  OrderId next_id = 1;

  for (std::size_t i = 0; i < count; ++i) {
    Order o;
    o.timestamp_ns = static_cast<std::uint64_t>(i * 100);
    if (scenario == Scenario::MarketLike) {
      if (i % 25000 == 0 && i != 0) mid_price += jump_dist(rng);
      if (i % 3000 == 0) spread_ticks = 2 + static_cast<Price>(rng() % 10);
    }
    const bool burst = (scenario == Scenario::Burst && (i % 10000) < 1000) ||
                       (scenario == Scenario::MarketLike && (i % 12000) < 1600);
    const bool seed_initial_book = scenario == Scenario::MarketLike && i < (count * 30 / 100);
    const int cancel_threshold = scenario == Scenario::HeavyCancel ? 58 :
                                 scenario == Scenario::MarketLike ? (burst ? 92 : 88) : 12;
    const int market_threshold = scenario == Scenario::Skewed ? 25 :
                                 scenario == Scenario::MarketLike ? (burst ? 45 : 35) : 10;
    const int dice = static_cast<int>(rng() % 100);

    if (!seed_initial_book && (scenario == Scenario::MarketLike || !live.empty()) &&
        dice < cancel_threshold) {
      o.kind = OrderKind::Cancel;
      o.side = side_dist(rng) == 0 ? Side::Buy : Side::Sell;
      const auto pos = live.empty() ? 0 : static_cast<std::size_t>(rng() % live.size());
      o.id = live.empty() ? (next_id > 1 ? 1 + (rng() % (next_id - 1)) : 1) : live[pos];
      o.price = 0;
      o.quantity = 0;
      if (!live.empty()) {
        live[pos] = live.back();
        live.pop_back();
      }
      orders.push_back(o);
      continue;
    }

    const int non_cancel_dice = static_cast<int>(rng() % 100);
    if (seed_initial_book || (scenario == Scenario::MarketLike && live.empty())) {
      o.kind = OrderKind::Limit;
    } else {
      o.kind = non_cancel_dice < market_threshold ? OrderKind::Market : OrderKind::Limit;
    }
    if (scenario == Scenario::MarketLike) {
      const int buy_probability = ((i / 50000) % 2 == 0) ? 58 : 42;
      o.side = (rng() % 100) < static_cast<std::uint64_t>(buy_probability) ? Side::Buy : Side::Sell;
    } else {
      const bool skew_buy = scenario == Scenario::Skewed && (rng() % 100) < 70;
      o.side = (skew_buy || side_dist(rng) == 0) ? Side::Buy : Side::Sell;
    }
    o.id = next_id++;
    const auto offset = static_cast<Price>(px_dist(rng));
    const Price half = spread_ticks / 2;
    o.price = o.side == Side::Buy ? mid_price - half + offset : mid_price + half + offset;
    if (burst) o.price += o.side == Side::Buy ? 4 : -4;
    if (scenario == Scenario::MarketLike && i % 997 == 0) {
      o.price += o.side == Side::Buy ? 180 : -180;
    }
    o.price = std::max<Price>(1, o.price);
    o.quantity = static_cast<Quantity>(qty_dist(rng));

    if (scenario == Scenario::Edge) {
      if (i % 97 == 0) o.kind = OrderKind::Market;
      if (i % 131 == 0) o.price = mid_price + (o.side == Side::Buy ? 100 : -100);
      if (i % 173 == 0) o.quantity = 1;
    }

    if (o.kind == OrderKind::Limit) live.push_back(o.id);
    orders.push_back(o);
  }
  return orders;
}

}  // namespace vm
