#include "vertexmatch/selftest.hpp"

#include <iostream>
#include <vector>

#include "vertexmatch/book.hpp"
#include "vertexmatch/generator.hpp"

namespace vm {

static bool same_trades(const std::vector<Trade>& a, const std::vector<Trade>& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i].aggressor_id != b[i].aggressor_id || a[i].resting_id != b[i].resting_id ||
        a[i].price != b[i].price || a[i].quantity != b[i].quantity ||
        a[i].aggressor_side != b[i].aggressor_side) {
      return false;
    }
  }
  return true;
}

bool run_selftest() {
  std::vector<Order> orders = {
      {1, OrderKind::Limit, Side::Sell, 1, 101, 10},
      {2, OrderKind::Limit, Side::Sell, 2, 101, 5},
      {3, OrderKind::Limit, Side::Buy, 3, 101, 12},
      {4, OrderKind::Cancel, Side::Buy, 2, 0, 0},
      {5, OrderKind::Market, Side::Buy, 4, 0, 20},
      {6, OrderKind::Limit, Side::Buy, 5, 99, 7},
      {7, OrderKind::Limit, Side::Sell, 6, 98, 3},
  };
  auto generated = generate_orders(25000, Scenario::Edge, 42, 100000, 4);
  orders.insert(orders.end(), generated.begin(), generated.end());

  BaselineBook baseline;
  OptimizedBook optimized({1, 200000, 100000});
  std::vector<Trade> bt;
  std::vector<Trade> ot;
  for (const auto& o : orders) {
    baseline.process(o, bt);
    optimized.process(o, ot);
  }
  const auto bs = baseline.snapshot();
  const auto os = optimized.snapshot();
  const bool ok = same_trades(bt, ot) && bs.best_bid == os.best_bid && bs.best_ask == os.best_ask &&
                  bs.best_bid_qty == os.best_bid_qty && bs.best_ask_qty == os.best_ask_qty;
  std::cout << (ok ? "selftest: PASS\n" : "selftest: FAIL\n");
  if (!ok) {
    std::cout << "baseline trades=" << bt.size() << " optimized trades=" << ot.size() << "\n";
    std::cout << "baseline best_bid=" << bs.best_bid << " best_ask=" << bs.best_ask << "\n";
    std::cout << "optimized best_bid=" << os.best_bid << " best_ask=" << os.best_ask << "\n";
  }
  return ok;
}

}  // namespace vm
