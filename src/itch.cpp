#include "vertexmatch/itch.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace vm {

namespace {

struct LiveOrder {
  OrderId id{0};
  Side side{Side::Buy};
  Price price{0};
  Quantity quantity{0};
};

bool parse_u64(std::string_view s, std::uint64_t& out) {
  const auto* first = s.data();
  const auto* last = s.data() + s.size();
  auto [ptr, ec] = std::from_chars(first, last, out);
  return ec == std::errc{} && ptr == last;
}

bool parse_i64(std::string_view s, std::int64_t& out) {
  const auto* first = s.data();
  const auto* last = s.data() + s.size();
  auto [ptr, ec] = std::from_chars(first, last, out);
  return ec == std::errc{} && ptr == last;
}

ItchMessage parse_message(std::string_view s) {
  if (s == "A" || s == "ADD") return ItchMessage::Add;
  if (s == "E" || s == "EXECUTE") return ItchMessage::Execute;
  if (s == "C" || s == "CANCEL") return ItchMessage::Cancel;
  if (s == "D" || s == "DELETE") return ItchMessage::Delete;
  if (s == "R" || s == "REPLACE") return ItchMessage::Replace;
  throw std::runtime_error("bad ITCH message type");
}

Side parse_side(std::string_view s) {
  if (s == "B" || s == "BUY") return Side::Buy;
  if (s == "S" || s == "SELL") return Side::Sell;
  return Side::Buy;
}

ItchEvent parse_line(std::string_view line) {
  std::string_view fields[9];
  std::size_t start = 0;
  std::size_t n = 0;
  for (std::size_t i = 0; i <= line.size() && n < 9; ++i) {
    if (i == line.size() || line[i] == ',') {
      fields[n++] = line.substr(start, i - start);
      start = i + 1;
    }
  }
  if (n != 9) throw std::runtime_error("invalid ITCH CSV line");

  ItchEvent e;
  std::uint64_t tmp = 0;
  if (!parse_u64(fields[0], e.sequence)) throw std::runtime_error("bad sequence");
  if (!parse_u64(fields[1], e.timestamp_ns)) throw std::runtime_error("bad timestamp");
  e.message = parse_message(fields[2]);
  e.symbol = std::string(fields[3]);
  if (!parse_u64(fields[4], e.order_id)) throw std::runtime_error("bad order_id");
  if (!parse_u64(fields[5], e.new_order_id)) throw std::runtime_error("bad new_order_id");
  e.side = parse_side(fields[6]);
  if (!parse_i64(fields[7], e.price)) throw std::runtime_error("bad price");
  if (!parse_u64(fields[8], tmp)) throw std::runtime_error("bad quantity");
  e.quantity = static_cast<Quantity>(tmp);
  return e;
}

}  // namespace

const char* to_string(ItchMessage message) {
  switch (message) {
    case ItchMessage::Add: return "A";
    case ItchMessage::Execute: return "E";
    case ItchMessage::Cancel: return "C";
    case ItchMessage::Delete: return "D";
    case ItchMessage::Replace: return "R";
  }
  return "?";
}

std::vector<ItchEvent> read_itch_csv(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("failed to open ITCH csv: " + path);
  std::vector<ItchEvent> events;
  std::string line;
  bool first = true;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    if (first && line.find("sequence") != std::string::npos) {
      first = false;
      continue;
    }
    first = false;
    events.push_back(parse_line(line));
  }
  std::stable_sort(events.begin(), events.end(), [](const ItchEvent& a, const ItchEvent& b) {
    if (a.sequence != b.sequence) return a.sequence < b.sequence;
    return a.timestamp_ns < b.timestamp_ns;
  });
  return events;
}

void write_itch_csv(const std::string& path, const std::vector<ItchEvent>& events) {
  std::ofstream out(path);
  if (!out) throw std::runtime_error("failed to write ITCH csv: " + path);
  out << "sequence,timestamp_ns,msg,symbol,order_id,new_order_id,side,price,quantity\n";
  for (const auto& e : events) {
    out << e.sequence << ',' << e.timestamp_ns << ',' << to_string(e.message) << ','
        << e.symbol << ',' << e.order_id << ',' << e.new_order_id << ','
        << to_string(e.side) << ',' << e.price << ',' << e.quantity << '\n';
  }
}

std::vector<Order> replay_itch_to_orders(const std::vector<ItchEvent>& events) {
  std::vector<Order> orders;
  orders.reserve(events.size() + events.size() / 20);
  for (const auto& e : events) {
    switch (e.message) {
      case ItchMessage::Add:
        orders.push_back({e.timestamp_ns, OrderKind::Limit, e.side, e.order_id, e.price, e.quantity});
        break;
      case ItchMessage::Execute:
        orders.push_back({e.timestamp_ns, OrderKind::Execute, e.side, e.order_id, 0, e.quantity});
        break;
      case ItchMessage::Cancel:
      case ItchMessage::Delete:
        orders.push_back({e.timestamp_ns, OrderKind::Cancel, e.side, e.order_id, 0, 0});
        break;
      case ItchMessage::Replace: {
        const OrderId new_id = e.new_order_id == 0 ? e.order_id : e.new_order_id;
        orders.push_back({e.timestamp_ns, OrderKind::Cancel, e.side, e.order_id, 0, 0});
        orders.push_back({e.timestamp_ns, OrderKind::Limit, e.side, new_id, e.price, e.quantity});
        break;
      }
    }
  }
  return orders;
}

ItchStats describe_itch(const std::vector<ItchEvent>& events) {
  ItchStats s;
  std::unordered_set<Price> levels;
  for (const auto& e : events) {
    switch (e.message) {
      case ItchMessage::Add: ++s.add; break;
      case ItchMessage::Execute: ++s.execute; break;
      case ItchMessage::Cancel: ++s.cancel; break;
      case ItchMessage::Delete: ++s.delete_order; break;
      case ItchMessage::Replace: ++s.replace; break;
    }
    if (e.price > 0) {
      levels.insert(e.price);
      s.min_price = s.min_price == 0 ? e.price : std::min(s.min_price, e.price);
      s.max_price = std::max(s.max_price, e.price);
    }
  }
  s.price_levels = levels.size();
  return s;
}

std::vector<ItchEvent> generate_itch_events(std::size_t count,
                                            std::uint64_t seed,
                                            Price mid_price,
                                            Price spread_ticks,
                                            const std::string& symbol) {
  std::mt19937_64 rng(seed);
  std::normal_distribution<double> px_dist(0.0, 180.0);
  std::uniform_int_distribution<int> qty_dist(1, 900);
  std::vector<ItchEvent> events;
  events.reserve(count);
  std::vector<LiveOrder> live;
  live.reserve(count / 3 + 1);
  OrderId next_id = count + 1;

  auto choose_touch_pos = [&](Side side) {
    std::size_t best = 0;
    bool found = false;
    Price best_price = side == Side::Buy ? 0 : std::numeric_limits<Price>::max();
    for (std::size_t j = 0; j < live.size(); ++j) {
      if (live[j].side != side) continue;
      if (!found || (side == Side::Buy ? live[j].price > best_price : live[j].price < best_price)) {
        best = j;
        best_price = live[j].price;
        found = true;
      }
    }
    return found ? best : static_cast<std::size_t>(rng() % live.size());
  };

  for (std::size_t i = 0; i < count; ++i) {
    const bool volatility_spike = (i % 200000) < 24000;
    const bool sparse_phase = (i % 180000) > 150000;
    const bool burst = (i % 50000) < (volatility_spike ? 12000 : 6000);
    const bool sweep_phase = (i % 70000) < 2500;

    if (i % 20000 == 0 && i != 0) {
      const int jump = volatility_spike ? static_cast<int>(rng() % 1201) - 600
                                        : static_cast<int>(rng() % 401) - 200;
      mid_price += static_cast<Price>(jump);
    }
    if (i % 4000 == 0) {
      spread_ticks = 2 + static_cast<Price>(rng() % (volatility_spike ? 30 : 14));
    }

    const bool seed_book = i < count * 12 / 100;
    const int dice = static_cast<int>(rng() % 100);
    ItchEvent e;
    e.sequence = static_cast<std::uint64_t>(i + 1);
    e.timestamp_ns = static_cast<std::uint64_t>(i * (burst ? 25 : 250));
    e.symbol = symbol;

    const int cancel_threshold = sparse_phase ? 84 : (burst ? 76 : 70);
    const int execute_threshold = cancel_threshold + (sweep_phase ? 12 : (burst ? 8 : 6));
    const int replace_threshold = execute_threshold + (sparse_phase ? 2 : 6);

    if (!seed_book && dice < cancel_threshold) {
      const bool use_live = !live.empty() && ((rng() % 100) < (sparse_phase ? 92 : 72));
      const auto pos = use_live ? choose_touch_pos((rng() % 2) == 0 ? Side::Buy : Side::Sell) : 0;
      e.message = (rng() % 100) < 85 ? ItchMessage::Cancel : ItchMessage::Delete;
      e.order_id = use_live ? live[pos].id : static_cast<OrderId>(1 + (rng() % count));
      e.side = use_live ? live[pos].side : ((rng() % 2) == 0 ? Side::Buy : Side::Sell);
      e.price = 0;
      e.quantity = 0;
      if (use_live) {
        live[pos] = live.back();
        live.pop_back();
      }
    } else if (!seed_book && !live.empty() && dice < execute_threshold) {
      const auto pos = sweep_phase ? choose_touch_pos((rng() % 2) == 0 ? Side::Buy : Side::Sell)
                                   : static_cast<std::size_t>(rng() % live.size());
      auto& lo = live[pos];
      e.message = ItchMessage::Execute;
      e.order_id = lo.id;
      e.side = lo.side;
      e.quantity = sweep_phase ? lo.quantity
                                : std::min<Quantity>(lo.quantity, static_cast<Quantity>(qty_dist(rng)));
      e.price = 0;
      lo.quantity -= e.quantity;
      if (lo.quantity == 0) {
        live[pos] = live.back();
        live.pop_back();
      }
    } else if (!seed_book && !live.empty() && dice < replace_threshold) {
      const auto pos = static_cast<std::size_t>(rng() % live.size());
      auto old = live[pos];
      e.message = ItchMessage::Replace;
      e.order_id = old.id;
      e.new_order_id = next_id++;
      e.side = old.side;
      e.price = std::max<Price>(1, old.price + static_cast<Price>(static_cast<int>(rng() % 161) - 80));
      e.quantity = static_cast<Quantity>(qty_dist(rng));
      live[pos] = {e.new_order_id, e.side, e.price, e.quantity};
    } else {
      e.message = ItchMessage::Add;
      e.order_id = next_id++;
      const int side_bias = ((i / 60000) % 2 == 0) ? (volatility_spike ? 68 : 56)
                                                   : (volatility_spike ? 32 : 44);
      e.side = (rng() % 100) < static_cast<std::uint64_t>(side_bias) ? Side::Buy : Side::Sell;
      const Price half = spread_ticks / 2;
      e.price = e.side == Side::Buy ? mid_price - half + static_cast<Price>(px_dist(rng))
                                    : mid_price + half + static_cast<Price>(px_dist(rng));
      if (burst) e.price += e.side == Side::Buy ? 18 : -18;
      if (volatility_spike && i % 401 == 0) e.price += e.side == Side::Buy ? 520 : -520;
      if (i % 1201 == 0) e.price += e.side == Side::Buy ? 320 : -320;
      e.price = std::max<Price>(1, e.price);
      e.quantity = static_cast<Quantity>(qty_dist(rng));
      live.push_back({e.order_id, e.side, e.price, e.quantity});
    }
    events.push_back(e);
  }
  return events;
}

}  // namespace vm
