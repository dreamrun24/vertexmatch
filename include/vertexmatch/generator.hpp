#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "vertexmatch/types.hpp"

namespace vm {

enum class Scenario { Balanced, Burst, Skewed, HeavyCancel, Edge, MarketLike };

Scenario parse_scenario(const std::string& name);
std::vector<Order> generate_orders(std::size_t count,
                                   Scenario scenario,
                                   std::uint64_t seed,
                                   Price mid_price,
                                   Price spread_ticks);

}  // namespace vm
