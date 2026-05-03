#pragma once

#include <string>
#include <vector>

#include "vertexmatch/types.hpp"

namespace vm {

std::vector<Order> read_orders_csv(const std::string& path);
void write_orders_csv(const std::string& path, const std::vector<Order>& orders);
void write_trades_csv(const std::string& path, const std::vector<Trade>& trades);

}  // namespace vm
