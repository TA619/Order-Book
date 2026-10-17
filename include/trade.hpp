#pragma once

#include "types.hpp"

#include <ostream>
#include <type_traits>

// One fill between a buy and a sell order. The price is always the resting
// (maker) order's price, not the incoming (taker) order's limit.
struct Trade {
    OrderId buyId;
    OrderId sellId;
    Price price;
    Qty qty;

    friend bool operator==(const Trade&, const Trade&) = default;
};

static_assert(sizeof(Trade) == 32);
static_assert(std::is_trivially_copyable_v<Trade>);

// Lets GoogleTest print readable values when EXPECT_EQ on trades fails.
inline std::ostream& operator<<(std::ostream& os, const Trade& t) {
    return os << "Trade{buy=" << t.buyId << ", sell=" << t.sellId
              << ", px=" << t.price << ", qty=" << t.qty << "}";
}
