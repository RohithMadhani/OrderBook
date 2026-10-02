#include <algorithm>
#include <deque>
#include <iterator>
#include <map>
#include <random>
#include <unordered_map>
#include <vector>

#include "check.hpp"
#include "order_book.hpp"

using namespace book;

// Defining the same OrderBook but using std::map and deque to check our implementation
struct RefBook {
    struct O {
        OrderId id;
        Qty qty;
    };
    std::map<Price, std::deque<O>> bids, asks; // ascending by price
    std::unordered_map<OrderId, std::pair<Side, Price>> loc;

    auto &side_map(Side s) { return s == Side::Buy ? bids : asks; }

    bool add(OrderId id, Side s, Price px, Qty q) {
        if(q == 0 || loc.count(id))
            return false;
        side_map(s)[px].push_back({id, q});
        loc[id] = {s, px};
        return true;
    }

    bool cancel(OrderId id) {
        auto it1 = loc.find(id);
        if(it1 == loc.end())
            return false;
        auto &m = side_map(it1->second.first);
        auto it2 = m.find(it1->second.second);
        auto &dq = it2->second;
        dq.erase(std::find_if(dq.begin(), dq.end(), [&](const O &o) { return o.id == id; }));
        if(dq.empty())
            m.erase(it2);
        loc.erase(it1);
        return true;
    }

    bool reduce(OrderId id, Qty by) {
        auto it1 = loc.find(id);
        if(it1 == loc.end())
            return false;
        auto &dq = side_map(it1->second.first)[it1->second.second];
        auto it2 = std::find_if(dq.begin(), dq.end(), [&](const O &o) { return o.id == id; });
        if(by >= it2->qty)
            return cancel(id);
        it2->qty -= by;
        return true;
    }

    bool replace(OrderId old_id, OrderId new_id, Price px, Qty q) {
        auto it = loc.find(old_id);
        if(it == loc.end())
            return false;
        Side s = it->second.first;
        cancel(old_id);
        return add(new_id, s, px, q);
    }

    Qty submit(OrderId id, Side s, Price px, Qty q, bool rest) {
        Qty rem = q;
        auto &opp = side_map(s == Side::Buy ? Side::Sell : Side::Buy);
        while(rem && !opp.empty()) {
            auto it = (s == Side::Buy) ? opp.begin() : std::prev(opp.end());
            if(s == Side::Buy ? it->first > px : it->first < px)
                break;
            auto &dq = it->second;
            while(rem && !dq.empty()) {
                Qty take = std::min(rem, dq.front().qty);
                dq.front().qty -= take;
                rem -= take;
                if(dq.front().qty == 0) {
                    loc.erase(dq.front().id);
                    dq.pop_front();
                }
            }
            if(dq.empty())
                opp.erase(it);
        }
        if(rem && rest)
            add(id, s, px, rem);
        return q - rem;
    }

    Price best_bid() { return bids.empty() ? OrderBook::kNone : bids.rbegin()->first; }
    Price best_ask() { return asks.empty() ? OrderBook::kNone : asks.begin()->first; }
    
    std::uint64_t qty_at(Side s, Price px) {
        auto &m = side_map(s);
        auto it = m.find(px);
        if(it == m.end())
            return 0;
        std::uint64_t sum = 0;
        for(auto &o : it->second)
            sum += o.qty;
        return sum;
    }
};

static void test_basic() {
    OrderBook b(16);
    CHECK(b.add(1, Side::Buy, 100, 10));
    CHECK(b.add(2, Side::Buy, 101, 5));
    CHECK(b.add(3, Side::Sell, 103, 7));
    CHECK(b.best_bid() == 101 && b.best_ask() == 103);
    CHECK(b.cancel(2));
    CHECK(b.best_bid() == 100);
    CHECK(b.submit(4, Side::Buy, 103, 10) == 7); // takes all 7 and rests 3
    CHECK(b.best_ask() == OrderBook::kNone);
    CHECK(b.best_bid() == 103 && b.qty_at(Side::Buy, 103) == 3);
    CHECK(!b.add(1, Side::Buy, 50, 1)); // duplicate id gets rejected
    CHECK(b.reduce(4, 1) && b.qty_at(Side::Buy, 103) == 2);
    CHECK(b.replace(4, 9, 99, 5));
    CHECK(b.qty_at(Side::Buy, 103) == 0 && b.qty_at(Side::Buy, 99) == 5);
}

static void test_random() {
    OrderBook book(1 << 16);
    RefBook ref;
    std::mt19937 rng(40);
    std::vector<OrderId> ids;
    OrderId next_id = 1;
    auto rnd = [&](std::uint32_t lo, std::uint32_t hi) {
        return lo + static_cast<std::uint32_t>(rng() % (hi - lo + 1));
    };
    
    for(int i{0}; i < 300000; i++) {
        int op = static_cast<int> (rng() % 100);
        Side s = rng() & 1 ? Side::Buy : Side::Sell;
        if(op < 40) {
            OrderId id = next_id++;
            ids.push_back(id);
            Price px = rnd(95, 105);
            Qty q = rnd(1, 50);
            CHECK(book.submit(id, s, px, q, true) == ref.submit(id, s, px, q, true));
        }
        else if(op < 45) {
            OrderId id = next_id++;
            Qty q = rnd(1, 200);
            Price px = s == Side::Buy ? 1000000 : 0; // market order
            CHECK(book.submit(id, s, px, q, false) == ref.submit(id, s, px, q, false));
        }
        else if(op < 70 && !ids.empty()) {
            OrderId id = ids[rng() % ids.size()];
            CHECK(book.cancel(id) == ref.cancel(id));
        }
        else if(op < 85 && !ids.empty()) {
            OrderId id = ids[rng() % ids.size()];
            Qty by = rnd(1, 30);
            CHECK(book.reduce(id, by) == ref.reduce(id, by));
        }
        else if (!ids.empty()) {
            OrderId old_id = ids[rng() % ids.size()];
            OrderId id = next_id++; 
            ids.push_back(id);
            Price px = rnd(95, 105); 
            Qty q = rnd(1, 50);
            CHECK(book.replace(old_id, id, px, q) == ref.replace(old_id, id, px, q));
        }
        CHECK(book.best_bid() == ref.best_bid());
        CHECK(book.best_ask() == ref.best_ask());
        CHECK(book.order_count() == ref.loc.size());
        if(i % 500 == 0) {
            for(Price p = 90; p <= 110; p++) {
                CHECK(book.qty_at(Side::Buy, p) == ref.qty_at(Side::Buy, p));
                CHECK(book.qty_at(Side::Sell, p) == ref.qty_at(Side::Sell, p)); 
            }
        }
    }
}

int main() {
    test_basic();
    test_random();
    std::puts("test_book : OK");
}