#pragma once

#include "types.hpp"

#include <ostream>
#include <type_traits>

// A resting or incoming limit order. Kept as a plain aggregate (no
// constructors) so it can be built with Order{id, side, price, qty, ts} and
// stays trivially copyable for the vector book / object pool later.
//
// Field order matters for padding: the four 8-byte fields come first and the
// 1-byte Side last, so the struct is 4*8 + 1 = 33 bytes, padded to 40.
struct Order {
    OrderId id;
    Price price;
    Qty qty;  // remaining quantity
    Timestamp ts;
    Side side;

    friend bool operator==(const Order&, const Order&) = default;
};

static_assert(sizeof(Order) == 40, "Order layout changed; re-check padding");
static_assert(std::is_trivially_copyable_v<Order>);
static_assert(std::is_aggregate_v<Order>);

inline std::ostream& operator<<(std::ostream& os, const Order& o) {
    return os << "Order{id=" << o.id << ", " << o.side << ", px=" << o.price
              << ", qty=" << o.qty << ", ts=" << o.ts << "}";
}
