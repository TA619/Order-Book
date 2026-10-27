#include <gtest/gtest.h>

#include "lobster.hpp"

#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>

using lobster::EventType;
using lobster::Message;
using lobster::Snapshot;

// ---------------------------------------------------------------------------
// parseMessage
// ---------------------------------------------------------------------------

TEST(ParseMessage, SubmitBuy) {
    const auto m = lobster::parseMessage("34200.18960767,1,11885113,21,2238100,1");
    ASSERT_TRUE(m);
    EXPECT_EQ(m->timeNs, 34200'189607670);  // padded to 9 decimals
    EXPECT_EQ(m->type, EventType::Submit);
    EXPECT_EQ(m->id, 11885113u);
    EXPECT_EQ(m->size, 21);
    EXPECT_EQ(m->price, 2238100);
    EXPECT_EQ(m->side, Side::Buy);
}

TEST(ParseMessage, SellAndOtherTypes) {
    const auto m = lobster::parseMessage("57599.999999999,4,7,100,5859400,-1");
    ASSERT_TRUE(m);
    EXPECT_EQ(m->timeNs, 57599'999999999);
    EXPECT_EQ(m->type, EventType::Execute);
    EXPECT_EQ(m->side, Side::Sell);

    EXPECT_EQ(lobster::parseMessage("34200,5,0,1,2238200,-1")->type, EventType::ExecuteHidden);
    EXPECT_EQ(lobster::parseMessage("34200,7,0,0,-1,-1")->price, -1);  // halt row
}

TEST(ParseMessage, IntegerSecondsAndWindowsLineEnding) {
    const auto m = lobster::parseMessage("34200,3,5,10,100,1\r\n");
    ASSERT_TRUE(m);
    EXPECT_EQ(m->timeNs, 34200'000000000);
    EXPECT_EQ(m->type, EventType::Delete);
}

TEST(ParseMessage, RejectsMalformedLines) {
    EXPECT_FALSE(lobster::parseMessage(""));
    EXPECT_FALSE(lobster::parseMessage("34200.1,1,5,10,100"));       // missing direction
    EXPECT_FALSE(lobster::parseMessage("34200.1,9,5,10,100,1"));     // unknown type
    EXPECT_FALSE(lobster::parseMessage("34200.1,1,5,10,100,0"));     // bad direction
    EXPECT_FALSE(lobster::parseMessage("34200.1,1,x,10,100,1"));     // not a number
    EXPECT_FALSE(lobster::parseMessage("34200.1,1,5,10,100,1,99"));  // extra field
}

// ---------------------------------------------------------------------------
// parseSnapshot
// ---------------------------------------------------------------------------

TEST(ParseSnapshot, TwoLevels) {
    Snapshot s;
    ASSERT_TRUE(lobster::parseSnapshot("2239500,100,2231800,200,2239900,50,2230700,70", s));
    EXPECT_EQ(s.levels, 2u);
    ASSERT_EQ(s.asks.size(), 2u);
    ASSERT_EQ(s.bids.size(), 2u);
    EXPECT_EQ(s.asks[0].price, 2239500);
    EXPECT_EQ(s.asks[0].qty, 100);
    EXPECT_EQ(s.asks[1].price, 2239900);
    EXPECT_EQ(s.bids[0].price, 2231800);
    EXPECT_EQ(s.bids[1].qty, 70);
}

TEST(ParseSnapshot, DropsEmptyLevels) {
    Snapshot s;
    ASSERT_TRUE(lobster::parseSnapshot("105,10,100,20,9999999999,0,-9999999999,0", s));
    EXPECT_EQ(s.levels, 2u);
    EXPECT_EQ(s.asks.size(), 1u);
    EXPECT_EQ(s.bids.size(), 1u);
}

TEST(ParseSnapshot, RejectsIncompleteLevel) {
    Snapshot s;
    EXPECT_FALSE(lobster::parseSnapshot("105,10,100", s));
}

// ---------------------------------------------------------------------------
// loadMessages
// ---------------------------------------------------------------------------

TEST(LoadMessages, ReadsFileAndReportsBadLine) {
    const std::string path = testing::TempDir() + "lobster_test_messages.csv";
    {
        std::ofstream out(path);
        out << "34200.1,1,1,10,100,1\n34200.2,3,1,10,100,1\n";
    }
    const auto msgs = lobster::loadMessages(path);
    ASSERT_EQ(msgs.size(), 2u);
    EXPECT_EQ(msgs[1].type, EventType::Delete);

    {
        std::ofstream out(path, std::ios::app);
        out << "garbage\n";
    }
    EXPECT_THROW((void)lobster::loadMessages(path), std::runtime_error);
    std::remove(path.c_str());

    EXPECT_THROW((void)lobster::loadMessages("does_not_exist.csv"), std::runtime_error);
}
