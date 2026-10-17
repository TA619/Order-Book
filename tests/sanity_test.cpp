#include <gtest/gtest.h>

#include <cstdint>

// Checks that GoogleTest is wired up. Real order book tests start on Day 7.
TEST(Sanity, OnePlusOne) {
    EXPECT_EQ(1 + 1, 2);
}

// Prices are stored as integer ticks; make sure int64_t is the size we expect.
TEST(Sanity, TickTypeIs64Bit) {
    EXPECT_EQ(sizeof(std::int64_t), 8u);
}
