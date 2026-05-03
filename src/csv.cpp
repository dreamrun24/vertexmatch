#include "vertexmatch/csv.hpp"

#include <charconv>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace vm {

static bool parse_u64(std::string_view s, std::uint64_t& out) {
  const auto* first = s.data();
  const auto* last = s.data() + s.size();
  auto [ptr, ec] = std::from_chars(first, last, out);
  return ec == std::errc{} && ptr == last;
}

static bool parse_i64(std::string_view s, std::int64_t& out) {
  const auto* first = s.data();
  const auto* last = s.data() + s.size();
  auto [ptr, ec] = std::from_chars(first, last, out);
  return ec == std::errc{} && ptr == last;
}

static Order parse_line(std::string_view line) {
  std::string_view fields[6];
  std::size_t start = 0;
  std::size_t n = 0;
  for (std::size_t i = 0; i <= line.size() && n < 6; ++i) {
    if (i == line.size() || line[i] == ',') {
      fields[n++] = line.substr(start, i - start);
      start = i + 1;
    }
  }
  if (n != 6) throw std::runtime_error("invalid CSV line");

  Order o;
  std::uint64_t tmp = 0;
  if (!parse_u64(fields[0], o.timestamp_ns)) throw std::runtime_error("bad timestamp");
  if (fields[1] == "L" || fields[1] == "LIMIT") o.kind = OrderKind::Limit;
  else if (fields[1] == "M" || fields[1] == "MARKET") o.kind = OrderKind::Market;
  else if (fields[1] == "C" || fields[1] == "CANCEL") o.kind = OrderKind::Cancel;
  else if (fields[1] == "E" || fields[1] == "EXECUTE") o.kind = OrderKind::Execute;
  else throw std::runtime_error("bad order kind");
  if (fields[2] == "B" || fields[2] == "BUY") o.side = Side::Buy;
  else if (fields[2] == "S" || fields[2] == "SELL") o.side = Side::Sell;
  else throw std::runtime_error("bad side");
  if (!parse_u64(fields[3], o.id)) throw std::runtime_error("bad id");
  if (!parse_i64(fields[4], o.price)) throw std::runtime_error("bad price");
  if (!parse_u64(fields[5], tmp)) throw std::runtime_error("bad quantity");
  o.quantity = static_cast<Quantity>(tmp);
  return o;
}

static void parse_buffer(std::string_view data, std::vector<Order>& out) {
  std::size_t pos = 0;
  bool first_line = true;
  while (pos < data.size()) {
    std::size_t end = data.find('\n', pos);
    if (end == std::string_view::npos) end = data.size();
    auto line = data.substr(pos, end - pos);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (!line.empty()) {
      if (first_line && line.find("timestamp") != std::string_view::npos) {
        first_line = false;
      } else {
        out.push_back(parse_line(line));
        first_line = false;
      }
    }
    pos = end + 1;
  }
}

std::vector<Order> read_orders_csv(const std::string& path) {
  std::vector<Order> orders;
#if defined(__unix__) || defined(__APPLE__)
  const int fd = open(path.c_str(), O_RDONLY);
  if (fd < 0) throw std::runtime_error("failed to open csv: " + path);
  struct stat st {};
  if (fstat(fd, &st) != 0) {
    close(fd);
    throw std::runtime_error("failed to stat csv: " + path);
  }
  if (st.st_size == 0) {
    close(fd);
    return orders;
  }
  void* mem = mmap(nullptr, static_cast<std::size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
  if (mem == MAP_FAILED) {
    close(fd);
    throw std::runtime_error("mmap failed: " + path);
  }
  orders.reserve(static_cast<std::size_t>(st.st_size / 32));
  parse_buffer({static_cast<const char*>(mem), static_cast<std::size_t>(st.st_size)}, orders);
  munmap(mem, static_cast<std::size_t>(st.st_size));
  close(fd);
#else
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("failed to open csv: " + path);
  std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  orders.reserve(data.size() / 32);
  parse_buffer(data, orders);
#endif
  return orders;
}

void write_orders_csv(const std::string& path, const std::vector<Order>& orders) {
  std::ofstream out(path);
  if (!out) throw std::runtime_error("failed to write orders csv: " + path);
  out << "timestamp_ns,kind,side,order_id,price,quantity\n";
  for (const auto& o : orders) {
    out << o.timestamp_ns << ',' << to_string(o.kind) << ',' << to_string(o.side)
        << ',' << o.id << ',' << o.price << ',' << o.quantity << '\n';
  }
}

void write_trades_csv(const std::string& path, const std::vector<Trade>& trades) {
  std::ofstream out(path);
  if (!out) throw std::runtime_error("failed to write trades csv: " + path);
  out << "timestamp_ns,aggressor_id,resting_id,price,quantity,aggressor_side\n";
  for (const auto& t : trades) {
    out << t.timestamp_ns << ',' << t.aggressor_id << ',' << t.resting_id << ','
        << t.price << ',' << t.quantity << ',' << to_string(t.aggressor_side) << '\n';
  }
}

}  // namespace vm
