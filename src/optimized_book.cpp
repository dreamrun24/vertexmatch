#include "vertexmatch/book.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <vector>

namespace vm {

namespace {
constexpr std::uint32_t kNull = std::numeric_limits<std::uint32_t>::max();
}

struct alignas(64) OptimizedBook::Impl {
  struct alignas(32) Resting {
    OrderId id{0};
    Price price{0};
    Quantity qty{0};
    std::uint32_t next{kNull};
    std::uint32_t prev{kNull};
    std::uint8_t side{0};
    std::uint8_t active{0};
    std::uint16_t pad{0};
  };

  struct alignas(64) Level {
    std::uint32_t head{kNull};
    std::uint32_t tail{kNull};
    Quantity aggregate{0};
    char pad[64 - sizeof(std::uint32_t) * 2 - sizeof(Quantity)]{};
  };

  explicit Impl(OptimizedConfig c)
      : config(c),
        levels(static_cast<std::size_t>(c.max_price - c.min_price + 1)),
        pool(c.max_orders + 1),
        id_to_slot(c.max_orders + 1, kNull),
        free_stack(c.max_orders) {
    if (c.min_price <= 0 || c.max_price < c.min_price || c.max_orders == 0) {
      throw std::invalid_argument("invalid optimized book config");
    }
    for (std::uint32_t i = 0; i < c.max_orders; ++i) {
      free_stack[i] = static_cast<std::uint32_t>(c.max_orders - i);
    }
    free_top = static_cast<std::uint32_t>(c.max_orders);
  }

  OptimizedConfig config;
  std::vector<Level> levels;
  std::vector<Resting> pool;
  std::vector<std::uint32_t> id_to_slot;
  std::vector<std::uint32_t> free_stack;
  std::uint32_t free_top{0};
  Price best_bid{0};
  Price best_ask{0};
  std::uint64_t live{0};

  bool in_range(Price p) const noexcept {
    return p >= config.min_price && p <= config.max_price;
  }

  std::size_t idx(Price p) const noexcept {
    return static_cast<std::size_t>(p - config.min_price);
  }

  std::uint32_t alloc_slot() {
    if (free_top == 0) throw std::runtime_error("optimized order pool exhausted");
    return free_stack[--free_top];
  }

  void release_slot(std::uint32_t slot) noexcept {
    pool[slot] = Resting{};
    free_stack[free_top++] = slot;
  }

  void refresh_best_bid() noexcept {
    while (best_bid >= config.min_price && best_bid != 0 && levels[idx(best_bid)].head == kNull) {
      --best_bid;
    }
    if (best_bid < config.min_price) best_bid = 0;
  }

  void refresh_best_ask() noexcept {
    while (best_ask <= config.max_price && best_ask != 0 && levels[idx(best_ask)].head == kNull) {
      ++best_ask;
    }
    if (best_ask > config.max_price) best_ask = 0;
  }
};

OptimizedBook::OptimizedBook(OptimizedConfig config) : impl_(new Impl(config)) {}

OptimizedBook::~OptimizedBook() {
  delete impl_;
}

static void unlink_slot(OptimizedBook::Impl& b, std::uint32_t slot) noexcept {
  auto& o = b.pool[slot];
  auto& level = b.levels[b.idx(o.price)];
  if (o.prev != kNull) b.pool[o.prev].next = o.next;
  else level.head = o.next;
  if (o.next != kNull) b.pool[o.next].prev = o.prev;
  else level.tail = o.prev;
  level.aggregate -= o.qty;
  if (o.id < b.id_to_slot.size()) b.id_to_slot[o.id] = kNull;
  if (o.side == static_cast<std::uint8_t>(Side::Buy) && o.price == b.best_bid && level.head == kNull) {
    b.refresh_best_bid();
  }
  if (o.side == static_cast<std::uint8_t>(Side::Sell) && o.price == b.best_ask && level.head == kNull) {
    b.refresh_best_ask();
  }
  --b.live;
  b.release_slot(slot);
}

static void add_resting(OptimizedBook::Impl& b, const Order& order, Quantity qty) {
  if (!b.in_range(order.price)) throw std::runtime_error("price outside optimized book range");
  if (order.id >= b.id_to_slot.size()) throw std::runtime_error("order id exceeds optimized id table");
  const auto slot = b.alloc_slot();
  auto& o = b.pool[slot];
  o.id = order.id;
  o.price = order.price;
  o.qty = qty;
  o.side = static_cast<std::uint8_t>(order.side);
  o.active = 1;
  auto& level = b.levels[b.idx(order.price)];
  o.prev = level.tail;
  o.next = kNull;
  if (level.tail != kNull) b.pool[level.tail].next = slot;
  else level.head = slot;
  level.tail = slot;
  level.aggregate += qty;
  b.id_to_slot[order.id] = slot;
  ++b.live;
  if (order.side == Side::Buy) b.best_bid = std::max(b.best_bid, order.price);
  else b.best_ask = b.best_ask == 0 ? order.price : std::min(b.best_ask, order.price);
}

void OptimizedBook::process(const Order& order, std::vector<Trade>& trades) {
  Impl& b = *impl_;
  if (order.kind == OrderKind::Cancel) {
    if (order.id < b.id_to_slot.size()) {
      const auto slot = b.id_to_slot[order.id];
      if (slot != kNull) unlink_slot(b, slot);
    }
    return;
  }
  if (order.kind == OrderKind::Execute) {
    if (order.id < b.id_to_slot.size()) {
      const auto slot = b.id_to_slot[order.id];
      if (slot != kNull) {
        auto& resting = b.pool[slot];
        auto& level = b.levels[b.idx(resting.price)];
        const Quantity reduce = order.quantity < resting.qty ? order.quantity : resting.qty;
        resting.qty -= reduce;
        level.aggregate -= reduce;
        if (resting.qty == 0) unlink_slot(b, slot);
      }
    }
    return;
  }
  if (order.kind == OrderKind::Limit && order.id < b.id_to_slot.size() &&
      b.id_to_slot[order.id] != kNull) {
    return;
  }

  Quantity remaining = order.quantity;
  if (order.side == Side::Buy) {
    while (remaining && b.best_ask && (order.kind == OrderKind::Market || b.best_ask <= order.price)) {
      auto& level = b.levels[b.idx(b.best_ask)];
      const auto slot = level.head;
      auto& resting = b.pool[slot];
      const Quantity fill = remaining < resting.qty ? remaining : resting.qty;
      trades.push_back({order.timestamp_ns, order.id, resting.id, resting.price, fill, order.side});
      remaining -= fill;
      resting.qty -= fill;
      level.aggregate -= fill;
      if (resting.qty == 0) unlink_slot(b, slot);
    }
  } else {
    while (remaining && b.best_bid && (order.kind == OrderKind::Market || b.best_bid >= order.price)) {
      auto& level = b.levels[b.idx(b.best_bid)];
      const auto slot = level.head;
      auto& resting = b.pool[slot];
      const Quantity fill = remaining < resting.qty ? remaining : resting.qty;
      trades.push_back({order.timestamp_ns, order.id, resting.id, resting.price, fill, order.side});
      remaining -= fill;
      resting.qty -= fill;
      level.aggregate -= fill;
      if (resting.qty == 0) unlink_slot(b, slot);
    }
  }

  if (remaining && order.kind == OrderKind::Limit) {
    add_resting(b, order, remaining);
  }
}

BookSnapshot OptimizedBook::snapshot() const {
  const Impl& b = *impl_;
  BookSnapshot s;
  s.best_bid = b.best_bid;
  s.best_ask = b.best_ask;
  s.live_orders = b.live;
  if (b.best_bid) s.best_bid_qty = b.levels[b.idx(b.best_bid)].aggregate;
  if (b.best_ask) s.best_ask_qty = b.levels[b.idx(b.best_ask)].aggregate;
  return s;
}

}  // namespace vm
