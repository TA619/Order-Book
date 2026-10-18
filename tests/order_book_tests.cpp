#include <gtest/gtest.h>

#include "map_order_book.hpp"

// Day 5 tests: these FAIL until addLimit is implemented. Make them pass.

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
