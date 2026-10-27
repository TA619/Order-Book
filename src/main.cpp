#include "map_order_book.hpp"

#include <iostream>
#include <string_view>
#include <vector>

// Demo: a scripted sequence of orders, printing every trade and the book
// after each step.

namespace {

void report(std::string_view action, const std::vector<Trade>& trades, const MapOrderBook& book) {
    std::cout << "\n> " << action << '\n';
    if (trades.empty()) {
        std::cout << "  (no trades)\n";
    }
    for (const Trade& t : trades) {
        std::cout << "  " << t << '\n';
    }
    book.printBook(std::cout);
}

void report(std::string_view action, bool ok, const MapOrderBook& book) {
    std::cout << "\n> " << action << (ok ? "  [ok]" : "  [rejected]") << '\n';
    book.printBook(std::cout);
}

}  // namespace

int main() {
    MapOrderBook book;

    // Build a two-sided book: no prices cross, so nothing trades.
    book.addLimit({.id = 1, .price = 10000, .qty = 10, .side = Side::Buy});
    book.addLimit({.id = 2, .price = 10000, .qty = 20, .side = Side::Buy});
    book.addLimit({.id = 3, .price = 9900, .qty = 30, .side = Side::Buy});
    book.addLimit({.id = 4, .price = 10100, .qty = 5, .side = Side::Sell});
    book.addLimit({.id = 5, .price = 10200, .qty = 7, .side = Side::Sell});
    report("initial book", std::vector<Trade>{}, book);

    // Aggressive limit buy: sweeps 10100 and 10200, rests the remainder.
    report("BUY 15 @ 10200 (id 6)",
           book.addLimit({.id = 6, .price = 10200, .qty = 15, .side = Side::Buy}), book);

    // Market sell: hits the best bids in price-time order (6, then 1, then 2).
    report("SELL 15 MARKET (id 7)", book.addMarket(Side::Sell, 15, 7), book);

    // Passive limit sell: below the old asks but above the best bid, so it rests.
    report("SELL 10 @ 10150 (id 8)",
           book.addLimit({.id = 8, .price = 10150, .qty = 10, .side = Side::Sell}), book);

    // Order management.
    report("CANCEL id 3", book.cancel(3), book);
    report("MODIFY id 2 -> qty 8 (keeps priority)", book.modify(2, 8), book);
    report("REPLACE id 2 -> px 10050 qty 8 (loses priority)",
           book.replace(2, 10050, 8), book);
    report("CANCEL id 99 (unknown)", book.cancel(99), book);

    if (const auto bid = book.bestBid(), ask = book.bestAsk(); bid && ask) {
        std::cout << "\nspread = " << (*ask - *bid) << " ticks\n";
    }
    return 0;
}
