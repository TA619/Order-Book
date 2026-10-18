#include <gtest/gtest.h>

#include "map_order_book.hpp"

#include <cstddef>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string printed(const MapOrderBook& book, std::size_t depth = 5) {
    std::ostringstream os;
    book.printBook(os, depth);
    return os.str();
}

// Ids of the orders queued at one level, front (first to fill) first.
std::vector<OrderId> queueIds(const MapOrderBook& book, Side s, Price px) {
    std::vector<OrderId> ids;
    for (const Order& o : book.levelOrders(s, px)) {
        ids.push_back(o.id);
    }
    return ids;
}

}  // namespace

// ---------------------------------------------------------------------------
// Insertion
// ---------------------------------------------------------------------------

TEST(MapOrderBook, EmptyBook) {
    MapOrderBook book;
    EXPECT_EQ(book.orderCount(), 0u);
    EXPECT_EQ(book.levelCount(Side::Buy), 0u);
    EXPECT_EQ(book.levelCount(Side::Sell), 0u);
}

TEST(MapOrderBook, AddLimitInsertsOrders) {
    MapOrderBook book;
    book.addLimit({.id = 1, .price = 10000, .qty = 10, .side = Side::Buy});
    book.addLimit({.id = 2, .price = 10100, .qty = 5, .side = Side::Sell});
    EXPECT_EQ(book.orderCount(), 2u);
    EXPECT_EQ(book.levelCount(Side::Buy), 1u);
    EXPECT_EQ(book.levelCount(Side::Sell), 1u);
}

TEST(MapOrderBook, SamePriceSharesALevel) {
    MapOrderBook book;
    book.addLimit({.id = 1, .price = 10000, .qty = 10, .side = Side::Buy});
    book.addLimit({.id = 2, .price = 10000, .qty = 20, .side = Side::Buy});
    book.addLimit({.id = 3, .price = 9900, .qty = 30, .side = Side::Buy});
    EXPECT_EQ(book.orderCount(), 3u);
    EXPECT_EQ(book.levelCount(Side::Buy), 2u);
}

TEST(MapOrderBook, StoredOrderKeepsFields) {
    MapOrderBook book;
    book.addLimit({.id = 42, .price = 10100, .qty = 17, .side = Side::Sell});

    const std::vector<Order> level = book.levelOrders(Side::Sell, 10100);
    ASSERT_EQ(level.size(), 1u);
    EXPECT_EQ(level[0].id, 42u);
    EXPECT_EQ(level[0].price, 10100);
    EXPECT_EQ(level[0].qty, 17);
    EXPECT_EQ(level[0].side, Side::Sell);
}

TEST(MapOrderBook, ExtremeAndNegativePrices) {
    constexpr Price kMin = std::numeric_limits<Price>::min();
    constexpr Price kMax = std::numeric_limits<Price>::max();

    MapOrderBook book;
    book.addLimit({.id = 1, .price = kMin, .qty = 1, .side = Side::Buy});
    book.addLimit({.id = 2, .price = -5, .qty = 1, .side = Side::Buy});
    book.addLimit({.id = 3, .price = 0, .qty = 1, .side = Side::Buy});
    book.addLimit({.id = 4, .price = kMax, .qty = 1, .side = Side::Sell});

    EXPECT_EQ(book.bestBid(), 0);
    EXPECT_EQ(book.bestAsk(), kMax);
    EXPECT_EQ(book.levelCount(Side::Buy), 3u);
    EXPECT_EQ(book.levelCount(Side::Sell), 1u);
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

TEST(MapOrderBook, RejectsDuplicateIdAndBadQty) {
    MapOrderBook book;
    book.addLimit({.id = 1, .price = 10000, .qty = 10, .side = Side::Buy});
    book.addLimit({.id = 1, .price = 10100, .qty = 10, .side = Side::Buy});  // dup id
    book.addLimit({.id = 2, .price = 10000, .qty = 0, .side = Side::Buy});   // qty 0
    book.addLimit({.id = 3, .price = 10000, .qty = -5, .side = Side::Buy});  // negative
    EXPECT_EQ(book.orderCount(), 1u);
    EXPECT_EQ(book.levelCount(Side::Buy), 1u);
}

TEST(MapOrderBook, DuplicateIdRejectedAcrossSides) {
    // Ids are unique across the whole book, not per side.
    MapOrderBook book;
    book.addLimit({.id = 5, .price = 10000, .qty = 10, .side = Side::Buy});
    book.addLimit({.id = 5, .price = 10100, .qty = 10, .side = Side::Sell});
    EXPECT_EQ(book.orderCount(), 1u);
    EXPECT_EQ(book.levelCount(Side::Sell), 0u);
    EXPECT_EQ(book.bestAsk(), std::nullopt);
}

TEST(MapOrderBook, RejectedOrdersDontConsumeTimestamps) {
    MapOrderBook book;
    book.addLimit({.id = 1, .price = 10000, .qty = 10, .side = Side::Buy});
    book.addLimit({.id = 1, .price = 10000, .qty = 10, .side = Side::Buy});  // dup id
    book.addLimit({.id = 2, .price = 10000, .qty = 0, .side = Side::Buy});   // qty 0
    book.addLimit({.id = 3, .price = 10000, .qty = 10, .side = Side::Buy});

    const std::vector<Order> level = book.levelOrders(Side::Buy, 10000);
    ASSERT_EQ(level.size(), 2u);
    EXPECT_EQ(level[0].ts, 0u);
    EXPECT_EQ(level[1].id, 3u);
    EXPECT_EQ(level[1].ts, 1u);
}

// ---------------------------------------------------------------------------
// Time priority (FIFO within a level)
// ---------------------------------------------------------------------------

TEST(MapOrderBook, LevelOrdersReturnsQueue) {
    MapOrderBook book;
    book.addLimit({.id = 7, .price = 10000, .qty = 10, .side = Side::Buy});
    book.addLimit({.id = 3, .price = 10000, .qty = 20, .side = Side::Buy});

    const std::vector<Order> expected = {
        {.id = 7, .price = 10000, .qty = 10, .ts = 0, .side = Side::Buy},
        {.id = 3, .price = 10000, .qty = 20, .ts = 1, .side = Side::Buy},
    };
    EXPECT_EQ(book.levelOrders(Side::Buy, 10000), expected);
    EXPECT_TRUE(book.levelOrders(Side::Sell, 10000).empty());
    EXPECT_TRUE(book.levelOrders(Side::Buy, 9999).empty());
    EXPECT_EQ(book.levelCount(Side::Buy), 1u);  // lookups created no levels
    EXPECT_EQ(book.levelCount(Side::Sell), 0u);
}

TEST(MapOrderBook, FifoWithinLevel) {
    MapOrderBook book;
    book.addLimit({.id = 1, .price = 10000, .qty = 10, .side = Side::Buy});
    book.addLimit({.id = 2, .price = 10000, .qty = 20, .side = Side::Buy});
    book.addLimit({.id = 3, .price = 10000, .qty = 30, .side = Side::Buy});

    EXPECT_EQ(queueIds(book, Side::Buy, 10000), (std::vector<OrderId>{1, 2, 3}));

    const std::vector<Order> level = book.levelOrders(Side::Buy, 10000);
    ASSERT_EQ(level.size(), 3u);
    EXPECT_LT(level[0].ts, level[1].ts);
    EXPECT_LT(level[1].ts, level[2].ts);
}

TEST(MapOrderBook, FifoAcrossInterleavedLevels) {
    MapOrderBook book;
    book.addLimit({.id = 1, .price = 101, .qty = 1, .side = Side::Sell});
    book.addLimit({.id = 2, .price = 102, .qty = 1, .side = Side::Sell});
    book.addLimit({.id = 3, .price = 101, .qty = 1, .side = Side::Sell});

    EXPECT_EQ(queueIds(book, Side::Sell, 101), (std::vector<OrderId>{1, 3}));
    EXPECT_EQ(queueIds(book, Side::Sell, 102), (std::vector<OrderId>{2}));
}

// ---------------------------------------------------------------------------
// Best prices
// ---------------------------------------------------------------------------

TEST(MapOrderBook, BestPricesEmptyBook) {
    MapOrderBook book;
    EXPECT_EQ(book.bestBid(), std::nullopt);
    EXPECT_EQ(book.bestAsk(), std::nullopt);
}

TEST(MapOrderBook, BestPricesBothSides) {
    MapOrderBook book;
    book.addLimit({.id = 1, .price = 9900, .qty = 10, .side = Side::Buy});
    book.addLimit({.id = 2, .price = 10000, .qty = 10, .side = Side::Buy});
    book.addLimit({.id = 3, .price = 10200, .qty = 5, .side = Side::Sell});
    book.addLimit({.id = 4, .price = 10100, .qty = 5, .side = Side::Sell});
    EXPECT_EQ(book.bestBid(), 10000);
    EXPECT_EQ(book.bestAsk(), 10100);
}

TEST(MapOrderBook, BestPricesOneSide) {
    MapOrderBook book;
    book.addLimit({.id = 1, .price = 10100, .qty = 5, .side = Side::Sell});
    EXPECT_EQ(book.bestBid(), std::nullopt);
    EXPECT_EQ(book.bestAsk(), 10100);
}

TEST(MapOrderBook, BestPricesTrackNewOrders) {
    MapOrderBook book;

    // Bids: a higher bid improves the best, a lower one doesn't.
    book.addLimit({.id = 1, .price = 100, .qty = 1, .side = Side::Buy});
    EXPECT_EQ(book.bestBid(), 100);
    book.addLimit({.id = 2, .price = 105, .qty = 1, .side = Side::Buy});
    EXPECT_EQ(book.bestBid(), 105);
    book.addLimit({.id = 3, .price = 102, .qty = 1, .side = Side::Buy});
    EXPECT_EQ(book.bestBid(), 105);

    // Asks: a lower ask improves the best, a higher one doesn't.
    book.addLimit({.id = 4, .price = 120, .qty = 1, .side = Side::Sell});
    EXPECT_EQ(book.bestAsk(), 120);
    book.addLimit({.id = 5, .price = 110, .qty = 1, .side = Side::Sell});
    EXPECT_EQ(book.bestAsk(), 110);
    book.addLimit({.id = 6, .price = 115, .qty = 1, .side = Side::Sell});
    EXPECT_EQ(book.bestAsk(), 110);

    EXPECT_EQ(book.bestBid(), 105);  // asks never move the bid
}

// ---------------------------------------------------------------------------
// printBook
// ---------------------------------------------------------------------------

TEST(MapOrderBook, PrintEmptyBook) {
    MapOrderBook book;
    EXPECT_EQ(printed(book), "---\n");
}

TEST(MapOrderBook, PrintBookAggregatesLevels) {
    MapOrderBook book;
    book.addLimit({.id = 1, .price = 10000, .qty = 10, .side = Side::Buy});
    book.addLimit({.id = 2, .price = 10000, .qty = 20, .side = Side::Buy});
    book.addLimit({.id = 3, .price = 9900, .qty = 30, .side = Side::Buy});
    book.addLimit({.id = 4, .price = 10100, .qty = 5, .side = Side::Sell});
    book.addLimit({.id = 5, .price = 10200, .qty = 7, .side = Side::Sell});
    EXPECT_EQ(printed(book),
              "ASK 10200 qty=7 orders=1\n"
              "ASK 10100 qty=5 orders=1\n"
              "---\n"
              "BID 10000 qty=30 orders=2\n"
              "BID 9900 qty=30 orders=1\n");
}

TEST(MapOrderBook, PrintBookRespectsDepth) {
    MapOrderBook book;
    OrderId id = 1;
    for (Price px : {101, 102, 103, 104}) {
        book.addLimit({.id = id++, .price = px, .qty = 1, .side = Side::Sell});
    }
    for (Price px : {99, 98, 97}) {
        book.addLimit({.id = id++, .price = px, .qty = 1, .side = Side::Buy});
    }
    EXPECT_EQ(printed(book, 2),
              "ASK 102 qty=1 orders=1\n"
              "ASK 101 qty=1 orders=1\n"
              "---\n"
              "BID 99 qty=1 orders=1\n"
              "BID 98 qty=1 orders=1\n");
    EXPECT_EQ(printed(book, 0), "---\n");
}
