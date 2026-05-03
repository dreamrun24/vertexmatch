#pragma once

#include <vector>

#include "vertexmatch/types.hpp"

namespace vm {

class IBook {
 public:
  virtual ~IBook() = default;
  virtual void process(const Order& order, std::vector<Trade>& trades) = 0;
  virtual BookSnapshot snapshot() const = 0;
};

class BaselineBook final : public IBook {
 public:
  struct Impl;
  ~BaselineBook() override;
  void process(const Order& order, std::vector<Trade>& trades) override;
  BookSnapshot snapshot() const override;

 private:
  Impl* impl_{nullptr};
  Impl& impl();
  const Impl& impl() const;
};

struct OptimizedConfig {
  Price min_price{1};
  Price max_price{2000000};
  std::size_t max_orders{4000000};
};

class OptimizedBook final : public IBook {
 public:
  struct Impl;
  explicit OptimizedBook(OptimizedConfig config = {});
  ~OptimizedBook() override;
  OptimizedBook(const OptimizedBook&) = delete;
  OptimizedBook& operator=(const OptimizedBook&) = delete;

  void process(const Order& order, std::vector<Trade>& trades) override;
  BookSnapshot snapshot() const override;

 private:
  Impl* impl_{nullptr};
};

}  // namespace vm
