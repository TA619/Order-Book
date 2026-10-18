#include <gtest/gtest.h>

#include "map_order_book.hpp"

#include <vector>

// Week 2 Day 1: matching loop. These FAIL until matchAgainst is implemented.
// Each test mirrors an example in the matchAgainst problem statement.

namespace {

std::vector<OrderId> queueIds(const MapOrderBook& book, Side s, Price px) {
    std::vector<OrderId> ids;
    for (const Order& o : book.levelOrders(s, px)) {
        ids.push_back(o.id);
    }
    return ids;
}

}  // namespace

TEST(Matching, FullFillOneRestingOrder) {
    MapOrderBook book;
    book.addLimit({.id = 1, .price = 101, .qty = 5, .side = Side::Sell});

    const auto trades = book.addLimit({.id = 9, .price = 101, .qty = 5, .side = Side::Buy});

    EXPECT_EQ(trades, (std::vector<Trade>{{.buyId = 9, .sellId = 1, .price = 101, .qty = 5}}));
    EXPECT_EQ(book.orderCount(), 0u);  // both gone: resting filled, taker never rested
    EXPECT_EQ(book.levelCount(Side::Sell), 0u);
    EXPECT_EQ(book.levelCount(Side::Buy), 0u);
}

TEST(Matching, TradesAtRestingPrice) {
    MapOrderBook book;
    book.addLimit({.id = 1, .price = 101, .qty = 5, .side = Side::Sell});

    const auto trades = book.addLimit({.id = 9, .price = 105, .qty = 5, .side = Side::Buy});

    EXPECT_EQ(trades, (std::vector<Trade>{{.buyId = 9, .sellId = 1, .price = 101, .qty = 5}}));
}

TEST(Matching, SweepsLevelsAndRestsRemainder) {
    MapOrderBook book;
    book.addLimit({.id = 1, .price = 101, .qty = 3, .side = Side::Sell});
    book.addLimit({.id = 2, .price = 102, .qty = 4, .side = Side::Sell});

    const auto trades = book.addLimit({.id = 9, .price = 102, .qty = 10, .side = Side::Buy});

    const std::vector<Trade> expected = {
        {.buyId = 9, .sellId = 1, .price = 101, .qty = 3},
        {.buyId = 9, .sellId = 2, .price = 102, .qty = 4},
    };
    EXPECT_EQ(trades, expected);
    EXPECT_EQ(book.levelCount(Side::Sell), 0u);
    EXPECT_EQ(book.bestBid(), 102);
    const auto rest = book.levelOrders(Side::Buy, 102);
    ASSERT_EQ(rest.size(), 1u);
    EXPECT_EQ(rest[0].id, 9u);
    EXPECT_EQ(rest[0].qty, 3);
}

TEST(Matching, FifoAndPartialFillOfRestingOrder) {
    MapOrderBook book;
    book.addLimit({.id = 1, .price = 101, .qty = 3, .side = Side::Sell});
    book.addLimit({.id = 2, .price = 101, .qty = 3, .side = Side::Sell});

    const auto trades = book.addLimit({.id = 9, .price = 101, .qty = 4, .side = Side::Buy});

    const std::vector<Trade> expected = {
        {.buyId = 9, .sellId = 1, .price = 101, .qty = 3},
        {.buyId = 9, .sellId = 2, .price = 101, .qty = 1},
    };
    EXPECT_EQ(trades, expected);
    const auto rest = book.levelOrders(Side::Sell, 101);
    ASSERT_EQ(rest.size(), 1u);
    EXPECT_EQ(rest[0].id, 2u);
    EXPECT_EQ(rest[0].qty, 2);
    EXPECT_EQ(book.orderCount(), 1u);  // #1 removed from the index too
}

TEST(Matching, NoCrossRestsBothSides) {
    MapOrderBook book;
    book.addLimit({.id = 1, .price = 101, .qty = 5, .side = Side::Sell});

    const auto trades = book.addLimit({.id = 9, .price = 100, .qty = 5, .side = Side::Buy});

    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book.bestAsk(), 101);
    EXPECT_EQ(book.bestBid(), 100);
    EXPECT_EQ(book.orderCount(), 2u);
}

TEST(Matching, SellTakerHitsBidsHighestFirst) {
    // Mirror image: an incoming sell matches the HIGHEST bids first.
    MapOrderBook book;
    book.addLimit({.id = 1, .price = 99, .qty = 2, .side = Side::Buy});
    book.addLimit({.id = 2, .price = 100, .qty = 2, .side = Side::Buy});
    book.addLimit({.id = 3, .price = 98, .qty = 2, .side = Side::Buy});

    const auto trades = book.addLimit({.id = 9, .price = 99, .qty = 5, .side = Side::Sell});

    const std::vector<Trade> expected = {
        {.buyId = 2, .sellId = 9, .price = 100, .qty = 2},
        {.buyId = 1, .sellId = 9, .price = 99, .qty = 2},
    };
    EXPECT_EQ(trades, expected);
    EXPECT_EQ(book.bestBid(), 98);  // 98 doesn't cross a sell at 99
    EXPECT_EQ(book.bestAsk(), 99);  // remaining 1 rests at the taker's limit
    EXPECT_EQ(queueIds(book, Side::Sell, 99), (std::vector<OrderId>{9}));
}
