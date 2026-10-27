#include <gtest/gtest.h>

#include "map_order_book.hpp"
#include "replayer.hpp"

#include <vector>

using lobster::EventType;
using lobster::Message;
using lobster::Snapshot;

namespace {

Message msg(EventType type, OrderId id, Qty size, Price px, Side side) {
    return {.timeNs = 0, .type = type, .id = id, .size = size, .price = px, .side = side};
}

Snapshot snapshot(std::vector<LevelInfo> asks, std::vector<LevelInfo> bids, std::size_t levels) {
    return {.asks = std::move(asks), .bids = std::move(bids), .levels = levels};
}

}  // namespace

// ---------------------------------------------------------------------------
// New book queries: find / depth
// ---------------------------------------------------------------------------

TEST(BookQueries, FindReturnsCurrentState) {
    MapOrderBook book;
    book.addLimit({.id = 1, .price = 100, .qty = 10, .side = Side::Buy});
    book.addLimit({.id = 2, .price = 101, .qty = 4, .side = Side::Sell});
    book.addLimit({.id = 3, .price = 100, .qty = 1, .side = Side::Sell});  // fills 1 of id 1

    const auto o = book.find(1);
    ASSERT_TRUE(o);
    EXPECT_EQ(o->qty, 9);
    EXPECT_EQ(o->side, Side::Buy);
    EXPECT_FALSE(book.find(3));  // fully filled, never rested
    EXPECT_FALSE(book.find(99));
}

TEST(BookQueries, DepthIsBestFirstAndAggregated) {
    MapOrderBook book;
    book.addLimit({.id = 1, .price = 100, .qty = 10, .side = Side::Buy});
    book.addLimit({.id = 2, .price = 100, .qty = 5, .side = Side::Buy});
    book.addLimit({.id = 3, .price = 99, .qty = 7, .side = Side::Buy});
    book.addLimit({.id = 4, .price = 103, .qty = 2, .side = Side::Sell});
    book.addLimit({.id = 5, .price = 102, .qty = 3, .side = Side::Sell});

    EXPECT_EQ(book.depth(Side::Buy, 5),
              (std::vector<LevelInfo>{{100, 15, 2}, {99, 7, 1}}));
    EXPECT_EQ(book.depth(Side::Sell, 1), (std::vector<LevelInfo>{{102, 3, 1}}));
    EXPECT_TRUE(book.depth(Side::Sell, 0).empty());
}

// ---------------------------------------------------------------------------
// Replayer: one message type at a time
// ---------------------------------------------------------------------------

TEST(Replayer, SubmitCancelDelete) {
    MapOrderBook book;
    Replayer r(book);

    r.apply(msg(EventType::Submit, 10, 100, 5000, Side::Buy));
    EXPECT_EQ(book.bestBid(), 5000);

    r.apply(msg(EventType::Cancel, 10, 30, 5000, Side::Buy));  // partial: 30 removed
    EXPECT_EQ(book.find(10)->qty, 70);

    r.apply(msg(EventType::Delete, 10, 70, 5000, Side::Buy));
    EXPECT_FALSE(book.find(10));
    EXPECT_FALSE(book.bestBid());

    EXPECT_EQ(r.stats().submits, 1u);
    EXPECT_EQ(r.stats().cancels, 1u);
    EXPECT_EQ(r.stats().deletes, 1u);
    EXPECT_EQ(r.stats().unknownOrders, 0u);
}

TEST(Replayer, ExecuteGoesThroughMatchingEngine) {
    MapOrderBook book;
    Replayer r(book);
    r.apply(msg(EventType::Submit, 1, 50, 5000, Side::Sell));
    r.apply(msg(EventType::Submit, 2, 50, 5000, Side::Sell));

    // Exchange says the OLDEST ask (id 1) traded 20: FIFO agrees.
    r.apply(msg(EventType::Execute, 1, 20, 5000, Side::Sell));
    EXPECT_EQ(book.find(1)->qty, 30);
    EXPECT_EQ(book.find(2)->qty, 50);
    EXPECT_EQ(r.stats().engineTrades, 1u);
    EXPECT_EQ(r.stats().volume, 20);
    EXPECT_EQ(r.stats().priorityMismatches, 0u);
}

TEST(Replayer, ExecutionOutOfFifoOrderIsCounted) {
    MapOrderBook book;
    Replayer r(book);
    r.apply(msg(EventType::Submit, 1, 50, 5000, Side::Sell));
    r.apply(msg(EventType::Submit, 2, 50, 5000, Side::Sell));

    // Exchange says id 2 traded first -- our FIFO would have picked id 1.
    r.apply(msg(EventType::Execute, 2, 10, 5000, Side::Sell));
    EXPECT_EQ(r.stats().priorityMismatches, 1u);
}

TEST(Replayer, HiddenAndAuctionTradesDontTouchTheBook) {
    MapOrderBook book;
    Replayer r(book);
    r.apply(msg(EventType::Submit, 1, 50, 5000, Side::Sell));
    r.apply(msg(EventType::ExecuteHidden, 0, 7, 5000, Side::Sell));
    r.apply(msg(EventType::Cross, 0, 100, 5000, Side::Buy));
    r.apply(msg(EventType::Halt, 0, 0, -1, Side::Sell));

    EXPECT_EQ(book.find(1)->qty, 50);
    EXPECT_EQ(r.stats().hiddenVolume, 7);
    EXPECT_EQ(r.stats().crosses, 1u);
    EXPECT_EQ(r.stats().halts, 1u);
    EXPECT_EQ(r.stats().messages, 4u);
}

// ---------------------------------------------------------------------------
// Replayer: pre-open orders seeded from the first snapshot
// ---------------------------------------------------------------------------

TEST(Replayer, UnknownIdsHitTheSeededLevel) {
    MapOrderBook book;
    Replayer r(book);
    r.seed(snapshot({{105, 300, 0}}, {{100, 200, 0}}, 1));
    EXPECT_EQ(book.depth(Side::Sell, 1), (std::vector<LevelInfo>{{105, 300, 1}}));

    // A pre-open order (id 777, never submitted) is executed for 100,
    // then another (id 778) is deleted with its 50 remaining shares.
    r.apply(msg(EventType::Execute, 777, 100, 105, Side::Sell));
    r.apply(msg(EventType::Delete, 778, 50, 105, Side::Sell));
    EXPECT_EQ(book.depth(Side::Sell, 1)[0].qty, 150);

    // An unknown id at a price we never saw cannot be applied.
    r.apply(msg(EventType::Delete, 779, 10, 90, Side::Buy));
    EXPECT_EQ(r.stats().unknownOrders, 2u);
    EXPECT_EQ(r.stats().droppedMessages, 1u);
    EXPECT_EQ(r.stats().priorityMismatches, 0u);
}

TEST(Replayer, NewOrdersQueueBehindSeededOrders) {
    MapOrderBook book;
    Replayer r(book);
    r.seed(snapshot({}, {{100, 200, 0}}, 1));
    r.apply(msg(EventType::Submit, 5, 10, 100, Side::Buy));

    // Pre-open orders are older, so a 200-share execution consumes exactly
    // the seeded quantity and leaves the new order untouched.
    r.apply(msg(EventType::Execute, 999, 200, 100, Side::Buy));
    EXPECT_EQ(book.depth(Side::Buy, 1), (std::vector<LevelInfo>{{100, 10, 1}}));
    EXPECT_EQ(book.find(5)->qty, 10);
}

// ---------------------------------------------------------------------------
// compareBook
// ---------------------------------------------------------------------------

TEST(CompareBook, MatchAndMismatch) {
    MapOrderBook book;
    book.addLimit({.id = 1, .price = 100, .qty = 10, .side = Side::Buy});
    book.addLimit({.id = 2, .price = 99, .qty = 5, .side = Side::Buy});
    book.addLimit({.id = 3, .price = 101, .qty = 8, .side = Side::Sell});

    auto m = compareBook(book, snapshot({{101, 8, 0}}, {{100, 10, 0}, {99, 5, 0}}, 2));
    EXPECT_TRUE(m.top);
    EXPECT_TRUE(m.all);

    // Second bid level differs: top still matches, full book does not.
    m = compareBook(book, snapshot({{101, 8, 0}}, {{100, 10, 0}, {99, 6, 0}}, 2));
    EXPECT_TRUE(m.top);
    EXPECT_FALSE(m.all);

    // Best ask size differs.
    m = compareBook(book, snapshot({{101, 9, 0}}, {{100, 10, 0}, {99, 5, 0}}, 2));
    EXPECT_FALSE(m.top);
}

TEST(CompareBook, ExtraLevelBeyondFullSnapshotIsFine) {
    MapOrderBook book;
    book.addLimit({.id = 1, .price = 100, .qty = 10, .side = Side::Buy});
    book.addLimit({.id = 2, .price = 99, .qty = 5, .side = Side::Buy});

    // 1-level file: our deeper level 99 is simply not shown.
    EXPECT_TRUE(compareBook(book, snapshot({}, {{100, 10, 0}}, 1)).all);
    // 2-level file whose 2nd bid level is empty: our 99 should not exist.
    EXPECT_FALSE(compareBook(book, snapshot({}, {{100, 10, 0}}, 2)).all);
}

// ---------------------------------------------------------------------------
// Replayer: levels that change out of view (reconcile)
// ---------------------------------------------------------------------------

TEST(Reconcile, FixesLevelsThatWereOutOfView) {
    MapOrderBook book;
    Replayer r(book);
    // 1-level file: only the best bid is visible.
    const Snapshot prev = snapshot({}, {{100, 10, 0}}, 1);
    r.apply(msg(EventType::Submit, 1, 10, 100, Side::Buy));
    r.apply(msg(EventType::Submit, 2, 5, 99, Side::Buy));  // in view then (side not full yet)

    // Best bid deleted. The new row shows 98 as best: so 99 (hidden behind
    // 100) was cancelled without a message, and 98 arrived unseen.
    r.apply(msg(EventType::Delete, 1, 10, 100, Side::Buy));
    r.reconcile(&prev, snapshot({}, {{98, 7, 0}}, 1));

    EXPECT_EQ(book.depth(Side::Buy, 5), (std::vector<LevelInfo>{{98, 7, 1}}));
    EXPECT_FALSE(book.find(2));
    EXPECT_EQ(r.stats().hiddenCorrections, 2u);  // 99 removed, 98 added
}

TEST(Reconcile, NeverTouchesLevelsThatWereVisible) {
    MapOrderBook book;
    Replayer r(book);
    r.apply(msg(EventType::Submit, 1, 10, 100, Side::Buy));

    // 100 was visible, so any change there must come from a message. A row
    // that disagrees is a real mismatch and must stay one.
    const Snapshot prev = snapshot({}, {{100, 10, 0}}, 1);
    const Snapshot cur = snapshot({}, {{100, 7, 0}}, 1);
    r.reconcile(&prev, cur);
    EXPECT_EQ(book.find(1)->qty, 10);
    EXPECT_FALSE(compareBook(book, cur).top);
}

TEST(Reconcile, UnseenCancelTakesFromOutOfViewOrderOfThatSize) {
    MapOrderBook book;
    Replayer r(book);
    // Level 100 has a 1300-lot (id 1) and a 100-lot (id 2), then drops out
    // of view behind a better bid at 101.
    r.apply(msg(EventType::Submit, 1, 1300, 100, Side::Buy));
    r.apply(msg(EventType::Submit, 2, 100, 100, Side::Buy));
    r.apply(msg(EventType::Submit, 3, 50, 101, Side::Buy));
    const Snapshot before = snapshot({}, {{100, 1400, 0}}, 1);
    r.reconcile(&before, snapshot({}, {{101, 50, 0}}, 1));

    // 101 leaves; 100 is back in view with 1300: the 100-lot is the one
    // that was cancelled unseen, not 100 shares of the big order.
    const Snapshot prev = snapshot({}, {{101, 50, 0}}, 1);
    r.apply(msg(EventType::Delete, 3, 50, 101, Side::Buy));
    r.reconcile(&prev, snapshot({}, {{100, 1300, 0}}, 1));
    EXPECT_EQ(book.find(1)->qty, 1300);
    EXPECT_FALSE(book.find(2));
}

TEST(Reconcile, DeleteWithDifferentSizeRepairsTheLevel) {
    MapOrderBook book;
    Replayer r(book);
    r.seed(snapshot({}, {{100, 100, 0}}, 1));             // synthetic 100
    r.apply(msg(EventType::Submit, 1, 50, 100, Side::Buy));  // level = 150

    // Say an earlier guess left our id 1 with 50 while the exchange says it
    // still has 80 when deleted: the extra 30 must come off the level.
    r.apply(msg(EventType::Delete, 1, 80, 100, Side::Buy));
    EXPECT_EQ(book.depth(Side::Buy, 1), (std::vector<LevelInfo>{{100, 70, 1}}));
}
