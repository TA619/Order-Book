#include <gtest/gtest.h>

#include "map_order_book.hpp"

#include <vector>

namespace {

std::vector<OrderId> queueIds(const MapOrderBook& book, Side s, Price px) {
    std::vector<OrderId> ids;
    for (const Order& o : book.levelOrders(s, px)) {
        ids.push_back(o.id);
    }
    return ids;
}

}  // namespace

// ---------------------------------------------------------------------------
// Limit orders that cross the spread
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Market orders
// ---------------------------------------------------------------------------

TEST(MarketOrder, SweepsLevelsAtRestingPrices) {
    MapOrderBook book;
    book.addLimit({.id = 1, .price = 101, .qty = 3, .side = Side::Sell});
    book.addLimit({.id = 2, .price = 105, .qty = 4, .side = Side::Sell});

    const auto trades = book.addMarket(Side::Buy, 5, 9);

    const std::vector<Trade> expected = {
        {.buyId = 9, .sellId = 1, .price = 101, .qty = 3},
        {.buyId = 9, .sellId = 2, .price = 105, .qty = 2},
    };
    EXPECT_EQ(trades, expected);
    const auto rest = book.levelOrders(Side::Sell, 105);
    ASSERT_EQ(rest.size(), 1u);
    EXPECT_EQ(rest[0].qty, 2);
    EXPECT_EQ(book.levelCount(Side::Buy), 0u);
}

TEST(MarketOrder, LeftoverIsDroppedNotRested) {
    MapOrderBook book;
    book.addLimit({.id = 1, .price = 101, .qty = 3, .side = Side::Sell});

    const auto trades = book.addMarket(Side::Buy, 10, 9);

    EXPECT_EQ(trades, (std::vector<Trade>{{.buyId = 9, .sellId = 1, .price = 101, .qty = 3}}));
    EXPECT_EQ(book.orderCount(), 0u);
    EXPECT_EQ(book.levelCount(Side::Buy), 0u);
    EXPECT_EQ(book.levelCount(Side::Sell), 0u);
}

TEST(MarketOrder, SellHitsHighestBidsFirst) {
    MapOrderBook book;
    book.addLimit({.id = 1, .price = 99, .qty = 2, .side = Side::Buy});
    book.addLimit({.id = 2, .price = 100, .qty = 2, .side = Side::Buy});

    const auto trades = book.addMarket(Side::Sell, 3, 9);

    const std::vector<Trade> expected = {
        {.buyId = 2, .sellId = 9, .price = 100, .qty = 2},
        {.buyId = 1, .sellId = 9, .price = 99, .qty = 1},
    };
    EXPECT_EQ(trades, expected);
    EXPECT_EQ(book.bestBid(), 99);
}

TEST(MarketOrder, EmptyBookNoTrades) {
    MapOrderBook book;
    EXPECT_TRUE(book.addMarket(Side::Sell, 5, 9).empty());
    EXPECT_EQ(book.orderCount(), 0u);
}

TEST(MarketOrder, RejectsBadQtyAndDuplicateId) {
    MapOrderBook book;
    book.addLimit({.id = 1, .price = 101, .qty = 5, .side = Side::Sell});

    EXPECT_TRUE(book.addMarket(Side::Buy, 0, 9).empty());
    EXPECT_TRUE(book.addMarket(Side::Buy, -3, 9).empty());
    EXPECT_TRUE(book.addMarket(Side::Buy, 5, 1).empty());  // id 1 is resting
    EXPECT_EQ(book.levelOrders(Side::Sell, 101).at(0).qty, 5);  // untouched
}
