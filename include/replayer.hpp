#pragma once

#include "i_order_book.hpp"
#include "lobster.hpp"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

// Counters collected while replaying a LOBSTER message stream.
struct ReplayStats {
    std::uint64_t messages = 0;
    std::uint64_t submits = 0;
    std::uint64_t cancels = 0;  // partial cancels (type 2)
    std::uint64_t deletes = 0;  // full cancels (type 3)
    std::uint64_t executions = 0;
    std::uint64_t hiddenExecutions = 0;
    std::uint64_t crosses = 0;
    std::uint64_t halts = 0;

    std::uint64_t engineTrades = 0;  // trades our matching engine produced
    Qty volume = 0;                  // visible executed quantity
    Qty hiddenVolume = 0;
    double notional = 0;  // sum(px * qty) over all executions, in ticks; for VWAP

    // Health checks: the first three should stay ~0 if the book is right.
    std::uint64_t droppedMessages = 0;     // message for a price level we don't have
    std::uint64_t priorityMismatches = 0;  // exchange filled an order our FIFO had behind another
    std::uint64_t unexpectedTrades = 0;    // a plain submit crossed the spread in our book
    std::uint64_t seedReorders = 0;  // FIFO check involving a synthetic order (expected)
    std::uint64_t hiddenCorrections = 0;  // levels fixed by reconcile() (expected)
    std::uint64_t unknownOrders = 0;  // never-seen id, applied to its level (expected)
};

// Drives an IOrderBook from LOBSTER messages:
//
//   Submit (1)      -> addLimit
//   Cancel (2)      -> modify down by Size (or cancel if nothing is left)
//   Delete (3)      -> cancel
//   Execute (4)     -> if the executed order is first in our queue, an
//                      opposite-side MARKET order for Size, so the fill goes
//                      through our own price-time matching. If it isn't, our
//                      FIFO disagrees with the exchange: count it and reduce
//                      the order directly so the book stays in sync.
//   5 / 6 / 7       -> counted only (hidden or auction trades never touch the
//                      visible book)
//
// Two things in the data are invisible to the message file:
//  1. Orders resting before 9:30, which were never submitted in the file.
//  2. Anything deeper than the file's N levels: a level-10 message file only
//     contains events INSIDE the top 10 levels, so an order that drifts to
//     level 11 can be cancelled without a message.
// reconcile() handles both. It looks only at price levels that were outside
// the previous snapshot's visible window (so could have changed unseen) and
// makes our book agree with the current snapshot there, using "synthetic"
// orders for quantity it can't attribute to a known id. Levels that WERE
// visible are never touched, so they still test the engine honestly.
class Replayer {
public:
    explicit Replayer(IOrderBook& book) : book_(book) {}

    // Loads the first snapshot (everything in it counts as unseen).
    void seed(const lobster::Snapshot& snap) { reconcile(nullptr, snap); }

    void apply(const lobster::Message& m);

    // Call after apply(message k) with rows k-1 and k of the orderbook file.
    void reconcile(const lobster::Snapshot* prev, const lobster::Snapshot& cur);

    [[nodiscard]] const ReplayStats& stats() const noexcept { return stats_; }

private:
    // Ids far above anything LOBSTER uses (those are < 2^32).
    static constexpr OrderId kSyntheticBase = OrderId{1} << 62;
    static constexpr OrderId kTakerBase = OrderId{1} << 63;

    [[nodiscard]] static bool isSynthetic(OrderId id) noexcept {
        return id >= kSyntheticBase && id < kTakerBase;
    }

    [[nodiscard]] static std::int64_t levelKey(Side s, Price px) noexcept {
        return px * 2 + (s == Side::Buy ? 1 : 0);
    }

    [[nodiscard]] OrderId syntheticAt(Side s, Price px) const;  // 0 if none

    void reduce(OrderId id, Qty have, Qty by);  // partial or full cancel of one order
    // Removes `by` shares from a level whose orders we can't tell apart.
    // Returns how much was actually removed.
    Qty reduceLevel(Side s, Price px, Qty by);
    void addSynthetic(Side s, Price px, Qty qty);
    void deleteOrder(const lobster::Message& m, Qty ours);
    void execute(const lobster::Message& m, Qty ours);
    void partialCancel(const lobster::Message& m, Qty ours);
    void coverShortfall(const lobster::Message& m, Qty ours);

    // Makes our level at (s, px) hold exactly `want` shares.
    void setLevelQty(Side s, Price px, Qty want);
    void markNewlyHidden(Side s, const lobster::Snapshot& prev, const lobster::Snapshot& cur);
    void reconcileSide(Side s, const lobster::Snapshot* prev, const lobster::Snapshot& cur,
                       bool growPass);

    IOrderBook& book_;
    ReplayStats stats_;
    std::unordered_map<std::int64_t, OrderId> synthetic_;  // levelKey -> synthetic id
    std::unordered_set<OrderId> suspect_;  // orders that have been out of view
    OrderId nextSynthetic_ = kSyntheticBase;
    OrderId nextTaker_ = kTakerBase;
};

// Result of comparing our book against one orderbook-file row.
struct BookMatch {
    bool top;  // best bid and best ask: same price AND size
    bool all;  // every level in the snapshot matches
};

[[nodiscard]] BookMatch compareBook(const IOrderBook& book, const lobster::Snapshot& snap);
