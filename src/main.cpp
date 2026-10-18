#include "map_order_book.hpp"

#include <iostream>

// Small demo: build a book by hand and print it.
int main() {
    MapOrderBook book;
    book.addLimit({.id = 1, .price = 10000, .qty = 10, .side = Side::Buy});
    book.addLimit({.id = 2, .price = 10000, .qty = 20, .side = Side::Buy});
    book.addLimit({.id = 3, .price = 9900, .qty = 30, .side = Side::Buy});
    book.addLimit({.id = 4, .price = 10100, .qty = 5, .side = Side::Sell});
    book.addLimit({.id = 5, .price = 10200, .qty = 7, .side = Side::Sell});

    book.printBook(std::cout);

    const auto bid = book.bestBid();
    const auto ask = book.bestAsk();
    if (bid && ask) {
        std::cout << "spread = " << (*ask - *bid) << " ticks\n";
    }
    return 0;
}
