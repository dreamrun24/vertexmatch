#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "vertexmatch/types.hpp"

namespace vm {

enum class ItchMessage : std::uint8_t {
  Add,
  Execute,
  Cancel,
  Delete,
  Replace
};

struct ItchEvent {
  std::uint64_t sequence{0};
  std::uint64_t timestamp_ns{0};
  ItchMessage message{ItchMessage::Add};
  std::string symbol{"VERT"};
  OrderId order_id{0};
  OrderId new_order_id{0};
  Side side{Side::Buy};
  Price price{0};
  Quantity quantity{0};
};

struct ItchStats {
  std::uint64_t add{0};
  std::uint64_t execute{0};
  std::uint64_t cancel{0};
  std::uint64_t delete_order{0};
  std::uint64_t replace{0};
  std::size_t price_levels{0};
  Price min_price{0};
  Price max_price{0};
};

std::vector<ItchEvent> read_itch_csv(const std::string& path);
void write_itch_csv(const std::string& path, const std::vector<ItchEvent>& events);
std::vector<Order> replay_itch_to_orders(const std::vector<ItchEvent>& events);
ItchStats describe_itch(const std::vector<ItchEvent>& events);
std::vector<ItchEvent> generate_itch_events(std::size_t count,
                                            std::uint64_t seed,
                                            Price mid_price,
                                            Price spread_ticks,
                                            const std::string& symbol = "VERT");
const char* to_string(ItchMessage message);

}  // namespace vm
