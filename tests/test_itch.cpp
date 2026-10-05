#include <vector>

#include "check.hpp"
#include "itch.hpp"
#include "itch_writer.hpp"
#include "order_book.hpp"

using namespace book;

int main() {
    std::vector<std::uint8_t> buf;
    ItchWriter w;

    // Stock directory
    w.begin('R', 7, 1);
    w.sym("TEST");
    w.pad(20);
    w.finish(buf);

    w.begin('R', 9, 1);
    w.sym("OTHR");
    w.pad(20);
    w.finish(buf);

    // Add: OrderId 42, Buy, qty 100, TEST, price = 123.45 -> 12345 ticks
    w.begin('A', 7, 2); w.u64(42); w.u8('B'); w.u32(100); w.sym("TEST"); w.u32(1234500); w.finish(buf);
    // Add order for the other symbol (will be ignored by the parser)
    w.begin('A', 9, 3); w.u64(43); w.u8('S'); w.u32(50); w.sym("OTHR"); w.u32(1000000); w.finish(buf);
    // Execute 30 shares of 42
    w.begin('E', 7, 4); w.u64(42); w.u32(30); w.u64(999); w.finish(buf);
    // Replace OrderId, qty and price
    w.begin('U', 7, 5); w.u64(42); w.u64(44); w.u32(80); w.u32(1234600); w.finish(buf);
    // Delete OrderId
    w.begin('D', 7, 6); w.u64(44); w.finish(buf);

    ItchParser parser("TEST");
    std::vector<Event> events;
    parser.parse(buf.data(), buf.size(), [&](Event &e) {
        events.push_back(e);
    });

    // Check parser condition
    CHECK(parser.located() && parser.locate() == 7);
    CHECK(parser.stats().total == 7);
    CHECK(parser.stats().by_type['A'] == 2);
    CHECK(events.size() == 4);
    CHECK(events[0].type == EvType::Add && events[0].ref == 42 && events[0].side == Side::Buy);
    CHECK(events[0].qty == 100 && events[0].price == 12345);
    CHECK(events[1].type == EvType::Exec && events[1].qty == 30);
    CHECK(events[2].type == EvType::Replace && events[2].new_ref == 44 && events[2].price == 12346);
    CHECK(events[3].type == EvType::Delete && events[3].ref == 44);

    // Feed a book and check state
    OrderBook b(16);
    CHECK(b.add(events[0].ref, events[0].side, events[0].price, events[0].qty));
    CHECK(b.reduce(events[1].ref, events[1].qty) && b.qty_at(Side::Buy, 12345) == 70);
    CHECK(b.replace(events[2].ref, events[2].new_ref, events[2].price, events[2].qty));
    CHECK(b.qty_at(Side::Buy, 12345) == 0 && b.qty_at(Side::Buy, 12346) == 80);
    CHECK(b.cancel(events[3].ref) && b.order_count() == 0);

    // Check for a cut-off message
    ItchParser p2("TEST");
    p2.parse(buf.data(), buf.size() - 5, [&](Event &) {});
    CHECK(p2.stats().total == 6);

    std::puts("test_itch: OK");
}