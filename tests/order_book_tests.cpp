#include <gtest/gtest.h>

#include "map_order_book.hpp"

#include <sstream>
#include <string>

// Day 5 tests: addLimit (insert only).

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

TEST(MapOrderBook, RejectsDuplicateIdAndBadQty) {
    MapOrderBook book;
    book.addLimit({.id = 1, .price = 10000, .qty = 10, .side = Side::Buy});
    book.addLimit({.id = 1, .price = 10100, .qty = 10, .side = Side::Buy});  // dup id
    book.addLimit({.id = 2, .price = 10000, .qty = 0, .side = Side::Buy});   // qty 0
    book.addLimit({.id = 3, .price = 10000, .qty = -5, .side = Side::Buy});  // negative
    EXPECT_EQ(book.orderCount(), 1u);
    EXPECT_EQ(book.levelCount(Side::Buy), 1u);
}

// ---------------------------------------------------------------------------
// Day 6 tests: these FAIL until bestBid/bestAsk/printBook are implemented.
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

static std::string printed(const MapOrderBook& book, std::size_t depth = 5) {
    std::ostringstream os;
    book.printBook(os, depth);
    return os.str();
}

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
