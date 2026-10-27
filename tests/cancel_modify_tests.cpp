#include <gtest/gtest.h>

#include "map_order_book.hpp"

#include <optional>
#include <vector>

namespace {

std::vector<OrderId> queueIds(const MapOrderBook& book, Side s, Price px) {
    std::vector<OrderId> ids;
    for (const Order& o : book.levelOrders(s, px)) {
        ids.push_back(o.id);
    }
    return ids;
}

// Fixture: three bids queued at 100 (ids 1, 2, 3) and one ask at 105 (id 4).
class BookWithQueue : public ::testing::Test {
protected:
    void SetUp() override {
        book.addLimit({.id = 1, .price = 100, .qty = 10, .side = Side::Buy});
        book.addLimit({.id = 2, .price = 100, .qty = 10, .side = Side::Buy});
        book.addLimit({.id = 3, .price = 100, .qty = 10, .side = Side::Buy});
        book.addLimit({.id = 4, .price = 105, .qty = 10, .side = Side::Sell});
    }

    MapOrderBook book;
};

}  // namespace

// ---------------------------------------------------------------------------
// cancel
// ---------------------------------------------------------------------------

TEST(Cancel, UnknownIdReturnsFalse) {
    MapOrderBook book;
    EXPECT_FALSE(book.cancel(42));
}

TEST_F(BookWithQueue, CancelRemovesOrderAndKeepsOthersInOrder) {
    EXPECT_TRUE(book.cancel(2));  // middle of the queue
    EXPECT_EQ(queueIds(book, Side::Buy, 100), (std::vector<OrderId>{1, 3}));
    EXPECT_EQ(book.orderCount(), 3u);
}

TEST_F(BookWithQueue, CancelFrontAndBack) {
    EXPECT_TRUE(book.cancel(1));
    EXPECT_TRUE(book.cancel(3));
    EXPECT_EQ(queueIds(book, Side::Buy, 100), (std::vector<OrderId>{2}));
}

TEST_F(BookWithQueue, CancelTwiceFailsSecondTime) {
    EXPECT_TRUE(book.cancel(1));
    EXPECT_FALSE(book.cancel(1));
}

TEST_F(BookWithQueue, CancellingLastOrderRemovesLevel) {
    EXPECT_TRUE(book.cancel(4));
    EXPECT_EQ(book.levelCount(Side::Sell), 0u);
    EXPECT_EQ(book.bestAsk(), std::nullopt);
}

TEST_F(BookWithQueue, CancelledOrderIsNotMatched) {
    book.cancel(1);
    const auto trades = book.addLimit({.id = 9, .price = 100, .qty = 10, .side = Side::Sell});
    EXPECT_EQ(trades, (std::vector<Trade>{{.buyId = 2, .sellId = 9, .price = 100, .qty = 10}}));
}

TEST_F(BookWithQueue, CancelledIdCanBeReused) {
    book.cancel(1);
    book.addLimit({.id = 1, .price = 99, .qty = 5, .side = Side::Buy});
    EXPECT_EQ(queueIds(book, Side::Buy, 99), (std::vector<OrderId>{1}));
}

TEST_F(BookWithQueue, IteratorsStayValidAcrossOtherChanges) {
    // Inserts, fills and cancels around order 3 must not break its index_
    // entry (std::list iterator stability).
    book.addLimit({.id = 5, .price = 100, .qty = 10, .side = Side::Buy});
    book.cancel(2);
    book.addMarket(Side::Sell, 10, 9);  // fills order 1
    EXPECT_TRUE(book.cancel(3));
    EXPECT_EQ(queueIds(book, Side::Buy, 100), (std::vector<OrderId>{5}));
}

TEST(Cancel, FilledOrderCannotBeCancelled) {
    MapOrderBook book;
    book.addLimit({.id = 1, .price = 100, .qty = 5, .side = Side::Sell});
    book.addLimit({.id = 2, .price = 100, .qty = 5, .side = Side::Buy});
    EXPECT_FALSE(book.cancel(1));
    EXPECT_FALSE(book.cancel(2));  // fully filled taker never rested
}

// ---------------------------------------------------------------------------
// modify (quantity only)
// ---------------------------------------------------------------------------

TEST_F(BookWithQueue, ReduceKeepsQueuePosition) {
    EXPECT_TRUE(book.modify(1, 4));
    EXPECT_EQ(queueIds(book, Side::Buy, 100), (std::vector<OrderId>{1, 2, 3}));
    EXPECT_EQ(book.levelOrders(Side::Buy, 100).at(0).qty, 4);
}

TEST_F(BookWithQueue, IncreaseLosesQueuePosition) {
    EXPECT_TRUE(book.modify(1, 25));
    EXPECT_EQ(queueIds(book, Side::Buy, 100), (std::vector<OrderId>{2, 3, 1}));
    const auto level = book.levelOrders(Side::Buy, 100);
    EXPECT_EQ(level.back().qty, 25);
    EXPECT_GT(level.back().ts, level[1].ts);  // fresh timestamp
}

TEST_F(BookWithQueue, SameQtyIsANoOp) {
    EXPECT_TRUE(book.modify(2, 10));
    EXPECT_EQ(queueIds(book, Side::Buy, 100), (std::vector<OrderId>{1, 2, 3}));
}

TEST_F(BookWithQueue, ModifyRejectsBadInput) {
    EXPECT_FALSE(book.modify(42, 5));  // unknown id
    EXPECT_FALSE(book.modify(1, 0));   // use cancel instead
    EXPECT_FALSE(book.modify(1, -3));
    EXPECT_EQ(book.levelOrders(Side::Buy, 100).at(0).qty, 10);  // untouched
}

TEST_F(BookWithQueue, MovedOrderCanStillBeCancelled) {
    // splice() must keep the index_ iterator valid.
    book.modify(1, 25);
    EXPECT_TRUE(book.cancel(1));
    EXPECT_EQ(queueIds(book, Side::Buy, 100), (std::vector<OrderId>{2, 3}));
}

TEST_F(BookWithQueue, MatchingUsesModifiedQty) {
    book.modify(1, 3);
    const auto trades = book.addMarket(Side::Sell, 5, 9);
    const std::vector<Trade> expected = {
        {.buyId = 1, .sellId = 9, .price = 100, .qty = 3},
        {.buyId = 2, .sellId = 9, .price = 100, .qty = 2},
    };
    EXPECT_EQ(trades, expected);
}

// ---------------------------------------------------------------------------
// replace (price and/or quantity: cancel + re-add)
// ---------------------------------------------------------------------------

TEST_F(BookWithQueue, ReplaceMovesToNewPrice) {
    EXPECT_TRUE(book.replace(1, 101, 10).empty());
    EXPECT_EQ(book.bestBid(), 101);
    EXPECT_EQ(queueIds(book, Side::Buy, 100), (std::vector<OrderId>{2, 3}));
    EXPECT_EQ(book.orderCount(), 4u);
}

TEST_F(BookWithQueue, ReplaceAtSamePriceLosesPriority) {
    book.replace(1, 100, 10);
    EXPECT_EQ(queueIds(book, Side::Buy, 100), (std::vector<OrderId>{2, 3, 1}));
}

TEST_F(BookWithQueue, ReplaceAcrossSpreadTrades) {
    // Bid 1 re-priced to 106 crosses the ask at 105: trades 10 at 105.
    const auto trades = book.replace(1, 106, 15);
    EXPECT_EQ(trades, (std::vector<Trade>{{.buyId = 1, .sellId = 4, .price = 105, .qty = 10}}));
    EXPECT_EQ(book.bestAsk(), std::nullopt);
    EXPECT_EQ(book.bestBid(), 106);  // remaining 5 rests at the new price
    EXPECT_EQ(book.levelOrders(Side::Buy, 106).at(0).qty, 5);
}

TEST_F(BookWithQueue, ReplaceRejectsBadInputWithoutChanges) {
    EXPECT_TRUE(book.replace(42, 101, 10).empty());  // unknown id
    EXPECT_TRUE(book.replace(1, 101, 0).empty());    // bad qty
    EXPECT_EQ(queueIds(book, Side::Buy, 100), (std::vector<OrderId>{1, 2, 3}));
    EXPECT_EQ(book.bestBid(), 100);
}

// ---------------------------------------------------------------------------
// Empty book edge cases
// ---------------------------------------------------------------------------

TEST(EmptyBook, EveryOperationIsSafe) {
    MapOrderBook book;
    EXPECT_TRUE(book.addMarket(Side::Buy, 5, 1).empty());
    EXPECT_FALSE(book.cancel(1));
    EXPECT_FALSE(book.modify(1, 5));
    EXPECT_TRUE(book.replace(1, 100, 5).empty());
    EXPECT_EQ(book.bestBid(), std::nullopt);
    EXPECT_EQ(book.bestAsk(), std::nullopt);
    EXPECT_EQ(book.orderCount(), 0u);
}
