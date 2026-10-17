#include <gtest/gtest.h>

#include "order.hpp"
#include "trade.hpp"
#include "types.hpp"

#include <sstream>

TEST(Types, OppositeSide) {
    EXPECT_EQ(opposite(Side::Buy), Side::Sell);
    EXPECT_EQ(opposite(Side::Sell), Side::Buy);
    static_assert(opposite(opposite(Side::Buy)) == Side::Buy);
}

TEST(Types, OrderAggregateInit) {
    // $101.25 with a $0.01 tick -> 10125 ticks
    Order o{.id = 1, .price = 10125, .qty = 100, .ts = 7, .side = Side::Buy};
    EXPECT_EQ(o.id, 1u);
    EXPECT_EQ(o.price, 10125);
    EXPECT_EQ(o.qty, 100);
    EXPECT_EQ(o.ts, 7u);
    EXPECT_EQ(o.side, Side::Buy);
}

TEST(Types, QtyIsSignedSoUnderflowIsVisible) {
    Qty remaining = 5;
    remaining -= 10;  // a fill bug: overfilled by 5
    EXPECT_LT(remaining, 0);
}

TEST(Types, TradeEquality) {
    Trade a{1, 2, 10000, 50};
    Trade b{1, 2, 10000, 50};
    Trade c{1, 2, 10001, 50};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(Types, Printing) {
    std::ostringstream os;
    os << Trade{1, 2, 10000, 50} << ' ' << Side::Sell;
    EXPECT_EQ(os.str(), "Trade{buy=1, sell=2, px=10000, qty=50} Sell");
}
