#include "vertexmatch/book.hpp"

#include <deque>
#include <map>
#include <memory>
#include <unordered_map>

namespace vm {

struct BaselineBook::Impl {
  struct Resting {
    OrderId id;
    Price price;
    Quantity qty;
    Side side;
  };

  using Level = std::deque<Resting>;
  std::map<Price, Level, std::greater<Price>> bids;
  std::map<Price, Level, std::less<Price>> asks;
  std::unordered_map<OrderId, std::pair<Side, Price>> index;
  std::uint64_t live{0};
};

BaselineBook::~BaselineBook() {
  delete impl_;
}

BaselineBook::Impl& BaselineBook::impl() {
  if (!impl_) impl_ = new Impl();
  return *impl_;
}

const BaselineBook::Impl& BaselineBook::impl() const {
  return *impl_;
}

static void erase_from_level(BaselineBook::Impl::Level& level, OrderId id) {
  for (auto it = level.begin(); it != level.end(); ++it) {
    if (it->id == id) {
      level.erase(it);
      return;
    }
  }
}

static bool reduce_from_level(BaselineBook::Impl::Level& level, OrderId id, Quantity qty) {
  for (auto it = level.begin(); it != level.end(); ++it) {
    if (it->id == id) {
      if (qty >= it->qty) {
        level.erase(it);
      } else {
        it->qty -= qty;
      }
      return true;
    }
  }
  return false;
}

void BaselineBook::process(const Order& order, std::vector<Trade>& trades) {
  Impl& b = impl();
  if (order.kind == OrderKind::Cancel) {
    auto it = b.index.find(order.id);
    if (it == b.index.end()) return;
    if (it->second.first == Side::Buy) {
      auto lvl = b.bids.find(it->second.second);
      if (lvl != b.bids.end()) {
        erase_from_level(lvl->second, order.id);
        if (lvl->second.empty()) b.bids.erase(lvl);
      }
    } else {
      auto lvl = b.asks.find(it->second.second);
      if (lvl != b.asks.end()) {
        erase_from_level(lvl->second, order.id);
        if (lvl->second.empty()) b.asks.erase(lvl);
      }
    }
    b.index.erase(it);
    --b.live;
    return;
  }
  if (order.kind == OrderKind::Execute) {
    auto it = b.index.find(order.id);
    if (it == b.index.end()) return;
    bool removed = false;
    if (it->second.first == Side::Buy) {
      auto lvl = b.bids.find(it->second.second);
      if (lvl != b.bids.end()) {
        const auto before = lvl->second.size();
        reduce_from_level(lvl->second, order.id, order.quantity);
        removed = lvl->second.size() != before;
        if (lvl->second.empty()) b.bids.erase(lvl);
      }
    } else {
      auto lvl = b.asks.find(it->second.second);
      if (lvl != b.asks.end()) {
        const auto before = lvl->second.size();
        reduce_from_level(lvl->second, order.id, order.quantity);
        removed = lvl->second.size() != before;
        if (lvl->second.empty()) b.asks.erase(lvl);
      }
    }
    if (removed) {
      b.index.erase(it);
      --b.live;
    }
    return;
  }
  if (order.kind == OrderKind::Limit && b.index.find(order.id) != b.index.end()) return;

  Quantity remaining = order.quantity;
  if (order.side == Side::Buy) {
    while (remaining && !b.asks.empty()) {
      auto best = b.asks.begin();
      if (order.kind == OrderKind::Limit && best->first > order.price) break;
      auto& q = best->second;
      while (remaining && !q.empty()) {
        auto& resting = q.front();
        const Quantity fill = remaining < resting.qty ? remaining : resting.qty;
        trades.push_back({order.timestamp_ns, order.id, resting.id, resting.price, fill, order.side});
        remaining -= fill;
        resting.qty -= fill;
        if (resting.qty == 0) {
          b.index.erase(resting.id);
          q.pop_front();
          --b.live;
        }
      }
      if (q.empty()) b.asks.erase(best);
      else break;
    }
    if (order.kind == OrderKind::Limit && remaining) {
      b.bids[order.price].push_back({order.id, order.price, remaining, order.side});
      b.index[order.id] = {order.side, order.price};
      ++b.live;
    }
  } else {
    while (remaining && !b.bids.empty()) {
      auto best = b.bids.begin();
      if (order.kind == OrderKind::Limit && best->first < order.price) break;
      auto& q = best->second;
      while (remaining && !q.empty()) {
        auto& resting = q.front();
        const Quantity fill = remaining < resting.qty ? remaining : resting.qty;
        trades.push_back({order.timestamp_ns, order.id, resting.id, resting.price, fill, order.side});
        remaining -= fill;
        resting.qty -= fill;
        if (resting.qty == 0) {
          b.index.erase(resting.id);
          q.pop_front();
          --b.live;
        }
      }
      if (q.empty()) b.bids.erase(best);
      else break;
    }
    if (order.kind == OrderKind::Limit && remaining) {
      b.asks[order.price].push_back({order.id, order.price, remaining, order.side});
      b.index[order.id] = {order.side, order.price};
      ++b.live;
    }
  }
}

BookSnapshot BaselineBook::snapshot() const {
  if (!impl_) return {};
  const Impl& b = impl();
  BookSnapshot s;
  s.live_orders = b.live;
  if (!b.bids.empty()) {
    s.best_bid = b.bids.begin()->first;
    for (const auto& o : b.bids.begin()->second) s.best_bid_qty += o.qty;
  }
  if (!b.asks.empty()) {
    s.best_ask = b.asks.begin()->first;
    for (const auto& o : b.asks.begin()->second) s.best_ask_qty += o.qty;
  }
  return s;
}

}  // namespace vm
