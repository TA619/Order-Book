#include "replayer.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <vector>

using lobster::EventType;
using lobster::Message;

// `have` = the order's current qty, which every caller already knows, so
// this needs no extra index lookup.
void Replayer::reduce(OrderId id, Qty have, Qty by) {
    if (by >= have) {
        book_.cancel(id);
        suspect_.erase(id);
    } else {
        book_.modify(id, have - by);
    }
}

// A delete's Size is the order's remaining size on the exchange. If ours
// differs, an earlier reduceLevel() guessed the wrong order; the level total
// was right, so move the difference onto the rest of the level.
void Replayer::deleteOrder(const Message& m, Qty ours) {
    book_.cancel(m.id);
    suspect_.erase(m.id);
    if (ours == m.size) {
        return;
    }
    ++stats_.hiddenCorrections;
    if (ours > m.size) {
        addSynthetic(m.side, m.price, ours - m.size);  // we removed too much
    } else {
        reduceLevel(m.side, m.price, m.size - ours);   // too little
    }
}

void Replayer::addSynthetic(Side s, Price px, Qty qty) {
    OrderId& syn = synthetic_[levelKey(s, px)];
    if (const auto o = syn != 0 ? book_.find(syn) : std::nullopt) {
        book_.modify(syn, o->qty + qty);
    } else {
        syn = nextSynthetic_++;
        book_.addLimit({.id = syn, .price = px, .qty = qty, .side = s});
    }
}

Qty Replayer::reduceLevel(Side s, Price px, Qty by) {
    Qty left = by;
    auto take = [&](OrderId id, Qty qty) {
        const Qty cut = std::min(left, qty);
        reduce(id, qty, cut);
        left -= cut;
    };

    // Preference, best guess first:
    //  1. the level's synthetic (unattributed) quantity;
    //  2. an order that has been out of view (so may be stale) whose size is
    //     exactly what's missing -- a whole order cancelled unseen;
    //  3. other out-of-view orders, newest first (most orders are cancelled
    //     within seconds, so young ones are the likeliest to be gone);
    //  4. orders that were always visible, newest first.
    const OrderId syn = syntheticAt(s, px);
    if (const auto o = syn != 0 ? book_.find(syn) : std::nullopt) {
        take(syn, o->qty);
    }
    const std::vector<Order> orders = book_.levelOrders(s, px);
    for (int pass = 0; pass < 3; ++pass) {
        for (auto it = orders.rbegin(); it != orders.rend() && left > 0; ++it) {
            const auto o = book_.find(it->id);  // may have shrunk or gone this call
            if (!o || o->id == syn || suspect_.contains(o->id) != (pass < 2) ||
                (pass == 0 && o->qty != left)) {
                continue;
            }
            take(o->id, o->qty);
        }
    }
    return by - left;
}

OrderId Replayer::syntheticAt(Side s, Price px) const {
    auto it = synthetic_.find(levelKey(s, px));
    return it == synthetic_.end() ? 0 : it->second;
}

void Replayer::execute(const Message& m, Qty ours) {
    const Qty fill = std::min(m.size, ours);

    // Only a fill of the order at the very front of the queue can be
    // reproduced by a market order; anything else would hit someone else.
    const auto front = book_.frontOrder(m.side);
    if (front->id != m.id) {
        ++(isSynthetic(front->id) ? stats_.seedReorders : stats_.priorityMismatches);
        reduce(m.id, ours, fill);
    } else {
        const std::vector<Trade> trades = book_.addMarket(opposite(m.side), fill, nextTaker_++);
        stats_.engineTrades += trades.size();
    }
    coverShortfall(m, ours);
}

void Replayer::partialCancel(const Message& m, Qty ours) {
    reduce(m.id, ours, std::min(m.size, ours));
    coverShortfall(m, ours);
}

// The exchange removed more from this order than we have left: an earlier
// reduceLevel() took shares from it that belonged to another order at the
// same price. Take the difference from the rest of the level.
void Replayer::coverShortfall(const Message& m, Qty ours) {
    if (m.size > ours) {
        ++stats_.hiddenCorrections;
        reduceLevel(m.side, m.price, m.size - ours);
    }
}

void Replayer::apply(const Message& m) {
    ++stats_.messages;

    switch (m.type) {
        case EventType::Submit: {
            ++stats_.submits;
            const auto trades =
                book_.addLimit({.id = m.id, .price = m.price, .qty = m.size, .side = m.side});
            if (!trades.empty()) {
                ++stats_.unexpectedTrades;
            }
            return;
        }
        case EventType::Cancel:
        case EventType::Delete:
        case EventType::Execute: {
            if (m.type == EventType::Execute) {
                ++stats_.executions;
                stats_.volume += m.size;
                stats_.notional += static_cast<double>(m.price) * static_cast<double>(m.size);
            } else {
                ++(m.type == EventType::Cancel ? stats_.cancels : stats_.deletes);
            }

            const auto known = book_.find(m.id);  // the one index lookup
            if (!known) {
                // An order we never saw: resting before 9:30 or submitted out
                // of view. Its quantity is part of the level's total (reconcile
                // made sure of that), so take it from the level.
                ++(reduceLevel(m.side, m.price, m.size) > 0 ? stats_.unknownOrders
                                                            : stats_.droppedMessages);
            } else if (m.type == EventType::Execute) {
                execute(m, known->qty);
            } else if (m.type == EventType::Delete) {
                deleteOrder(m, known->qty);
            } else {
                partialCancel(m, known->qty);
            }
            return;
        }
        case EventType::ExecuteHidden:
            ++stats_.hiddenExecutions;
            stats_.hiddenVolume += m.size;
            stats_.notional += static_cast<double>(m.price) * static_cast<double>(m.size);
            return;
        case EventType::Cross:
            ++stats_.crosses;
            return;
        case EventType::Halt:
            ++stats_.halts;
            return;
    }
}

// ---------------------------------------------------------------------------
// Reconciling levels the message file can't see
// ---------------------------------------------------------------------------

namespace {

// True if price a is further from the spread than b on side s.
bool worse(Side s, Price a, Price b) {
    return s == Side::Buy ? a < b : a > b;
}

const std::vector<LevelInfo>& levelsOf(const lobster::Snapshot& snap, Side s) {
    return s == Side::Buy ? snap.bids : snap.asks;
}

}  // namespace

void Replayer::setLevelQty(Side s, Price px, Qty want) {
    const std::vector<Order> orders = book_.levelOrders(s, px);
    Qty have = 0;
    for (const Order& o : orders) {
        have += o.qty;
    }
    if (have == want) {
        return;
    }
    ++stats_.hiddenCorrections;

    if (want > have) {
        addSynthetic(s, px, want - have);  // unattributed quantity
        return;
    }

    // Too much: some orders here were cancelled while out of view.
    reduceLevel(s, px, have - want);
}

void Replayer::reconcileSide(Side s, const lobster::Snapshot* prev, const lobster::Snapshot& cur,
                             bool growPass) {
    // Hidden region: prices worse than the last level the previous row
    // showed. If that row didn't fill all N levels, the whole side was
    // visible and nothing could have changed unseen.
    std::optional<Price> hiddenAfter;  // nullopt + prev == nullptr: all hidden
    if (prev) {
        const auto& p = levelsOf(*prev, s);
        if (p.size() < prev->levels) {
            return;
        }
        hiddenAfter = p.back().price;
    }
    auto hidden = [&](Price px) { return !hiddenAfter || worse(s, px, *hiddenAfter); };

    // Current window: everything the current row shows (the whole side if
    // it shows fewer than N levels).
    const auto& want = levelsOf(cur, s);
    const bool curFull = want.size() >= cur.levels;
    auto inWindow = [&](Price px) { return !curFull || !worse(s, px, want.back().price); };

    // Our levels inside the window. Fetch more until we pass its edge.
    std::vector<LevelInfo> ours;
    for (std::size_t n = want.size() + 4;; n *= 2) {
        ours = book_.depth(s, n);
        if (ours.size() < n || !inWindow(ours.back().price)) {
            break;
        }
    }

    for (const LevelInfo& lvl : ours) {
        if (!inWindow(lvl.price)) {
            break;
        }
        if (!growPass && hidden(lvl.price)) {
            // Shrink pass: levels that should be smaller or gone.
            auto w = std::find_if(want.begin(), want.end(),
                                  [&](const LevelInfo& x) { return x.price == lvl.price; });
            const Qty target = w == want.end() ? 0 : w->qty;
            if (target < lvl.qty) {
                setLevelQty(s, lvl.price, target);
            }
        }
    }
    if (growPass) {
        for (const LevelInfo& w : want) {
            if (hidden(w.price)) {
                auto o = std::find_if(ours.begin(), ours.end(),
                                      [&](const LevelInfo& x) { return x.price == w.price; });
                if (o == ours.end() || o->qty < w.qty) {
                    setLevelQty(s, w.price, w.qty);
                }
            }
        }
    }
}

void Replayer::markNewlyHidden(Side s, const lobster::Snapshot& prev,
                               const lobster::Snapshot& cur) {
    // Levels now past the current window's edge but not past the previous
    // one just went out of view. (Window edge = last level shown, or none
    // if the row shows fewer than N levels, i.e. the whole side.)
    const auto& c = levelsOf(cur, s);
    if (c.size() < cur.levels) {
        return;  // whole side visible: nothing hidden now
    }
    const Price curEdge = c.back().price;
    const auto& p = levelsOf(prev, s);
    const std::optional<Price> prevEdge =
        p.size() < prev.levels ? std::nullopt : std::optional{p.back().price};
    if (prevEdge && !worse(s, *prevEdge, curEdge)) {
        return;  // window didn't shrink
    }

    // Our levels up to the previous edge (or all of them).
    std::vector<LevelInfo> ours;
    for (std::size_t n = c.size() + 4;; n *= 2) {
        ours = book_.depth(s, n);
        if (ours.size() < n || (prevEdge && worse(s, ours.back().price, *prevEdge))) {
            break;
        }
    }
    for (const LevelInfo& lvl : ours) {
        if (prevEdge && worse(s, lvl.price, *prevEdge)) {
            break;
        }
        if (worse(s, lvl.price, curEdge)) {
            for (const Order& o : book_.levelOrders(s, lvl.price)) {
                suspect_.insert(o.id);
            }
        }
    }
}

void Replayer::reconcile(const lobster::Snapshot* prev, const lobster::Snapshot& cur) {
    if (prev) {
        markNewlyHidden(Side::Buy, *prev, cur);
        markNewlyHidden(Side::Sell, *prev, cur);
    }
    // Shrink both sides before growing either, so a stale level on one side
    // can never make a new synthetic order on the other side cross it.
    reconcileSide(Side::Buy, prev, cur, false);
    reconcileSide(Side::Sell, prev, cur, false);
    reconcileSide(Side::Buy, prev, cur, true);
    reconcileSide(Side::Sell, prev, cur, true);
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

BookMatch compareBook(const IOrderBook& book, const lobster::Snapshot& snap) {
    auto sideMatches = [&book, &snap](Side s, const std::vector<LevelInfo>& want, bool& topOk) {
        // Ask one level more than the snapshot has: if the snapshot shows an
        // empty level, our book must not have anything there either.
        const std::vector<LevelInfo> got = book.depth(s, want.size() + 1);
        const bool snapshotFull = want.size() >= snap.levels;

        topOk = want.empty() ? got.empty()
                             : !got.empty() && got[0].price == want[0].price &&
                                   got[0].qty == want[0].qty;

        if (got.size() < want.size() || (!snapshotFull && got.size() > want.size())) {
            return false;
        }
        for (std::size_t i = 0; i < want.size(); ++i) {
            if (got[i].price != want[i].price || got[i].qty != want[i].qty) {
                return false;
            }
        }
        return true;
    };

    bool askTop = false;
    bool bidTop = false;
    const bool asks = sideMatches(Side::Sell, snap.asks, askTop);
    const bool bids = sideMatches(Side::Buy, snap.bids, bidTop);
    return {.top = askTop && bidTop, .all = asks && bids};
}
