#pragma once
#include <cstdint>
#include <limits>
#include <ostream>
#include <vector>

#include "common.hpp"
#include "flat_map.hpp"
#include "pool.hpp"
#ifdef USE_STD_MAP
#include <unordered_map>
#endif

namespace book {
    struct Order {
        OrderId id;
        Price price; // ticks
        Qty qty;
        Side side;
        Order* prev; 
        Order* next; // DLL within price level
    };

    struct Level {
        Order* head = nullptr; // Oldest order (First in FIFO)
        Order* tail = nullptr;
        std::uint64_t qty = 0; // Total resting quantity at this size
    };

    #ifdef USE_STD_MAP
    class IdMap {
    private:
        std::unordered_map<OrderId, Order*> m_;
    public:
        explicit IdMap(std::size_t cap) { m_.reserve(cap); }
        Order** find(OrderId k) {
            auto it = m_.find(k);
            return it == m_.end() ? nullptr : &it->second;
        }
        bool insert(OrderId k, Order* v) { return m_.emplace(k, v).second; }
        bool erase(OrderId k) { return m_.erase(k) != 0; }
    };
    #else 
    using IdMap = FlatMap<Order*>;
    #endif

    class OrderBook {
    private:
        Pool<Order> pool_;
        IdMap ids_;
        std::vector<Level> bids_;
        std::vector<Level> asks_;
        Price best_bid_ = kNone;
        Price best_ask_ = kNone;
        std::size_t bid_orders_ = 0;
        std::size_t ask_orders_ = 0;
        std::uint64_t trades_ = 0;
        
        Level &level(Side s, Price px) {
            return s == Side::Buy ? bids_[px] : asks_[px];
        }

        void link(Order* o) {
            Level &L = level(o->side, o->price);
            o->prev = L.tail;
            o->next = nullptr;
            if(L.tail)
                L.tail->next = o;
            else    
                L.head = o;
            L.tail = o;
            L.qty += o->qty;
            if(o->side == Side::Buy) {
                ++bid_orders_;
                if(best_bid_ == kNone || o->price > best_bid_)
                    best_bid_ = o->price;
            }
            else {
                ++ask_orders_;
                if(best_ask_ == kNone || o->price < best_ask_)
                    best_ask_ = o->price;
            }
        }

        void advance_bid() {
            if(bid_orders_ == 0) {
                best_bid_ = kNone;
                return;
            }
            Price p = best_bid_;
            while(bids_[p].head == nullptr) --p;
            best_bid_ = p;
        }

        void advance_ask() {
            if(ask_orders_ == 0) {
                best_ask_ = kNone;
                return;
            }
            Price p = best_ask_;
            while(asks_[p].head == nullptr) ++p;
            best_ask_ = p;
        }

        void unlink(Order* o) {
            Level &L = level(o->side, o->price);
            if(o->prev)
                o->prev->next = o->next;
            else    
                L.head = o->next;
            
            if(o->next)
                o->next->prev = o->prev;
            else
                L.tail = o->prev;
            
            L.qty -= o->qty;
            if(o->side == Side::Buy) {
                --bid_orders_;
                if(!L.head && o->price == best_bid_)
                    advance_bid();
            }
            else {
                --ask_orders_;
                if(!L.head && o->price == best_ask_)
                    advance_ask();
            }
        }

        Qty fill(Level &L, Qty want) {
            Qty done = 0;
            while(want && L.head) {
                Order* o = L.head;
                Qty take = o->qty < want ? o->qty : want;
                o->qty -= take;
                L.qty -= take;
                want -= take;
                done += take;
                ++trades_;
                if(o->qty == 0) {
                    unlink(o);
                    ids_.erase(o->id);
                    pool_.deallocate(o);
                }
            }
            return done;
        }
    public:
        static constexpr std::size_t kLevels = 1u << 20;
        static constexpr Price kNone = std::numeric_limits<Price>::max();

        explicit OrderBook(std::size_t max_orders) : pool_(max_orders), ids_(max_orders * 2), bids_(kLevels), asks_(kLevels) {}

        bool add(OrderId id, Side side, Price px, Qty qty) {
            if(px >= kLevels || qty == 0)
                return false;
            Order* o = pool_.allocate(id, px, qty, side, nullptr, nullptr);
            if(!o)
                return false;
            if(!ids_.insert(id, o)) {
                pool_.deallocate(o);
                return false;
            }
            link(o);
            return true;
        }

        bool cancel(OrderId id) {
            Order** slot = ids_.find(id);
            if(!slot)
                return false;
            Order* o = *slot;
            unlink(o);
            ids_.erase(id);
            pool_.deallocate(o);
            return true;
        }

        bool reduce(OrderId id, Qty by) {
            Order** slot = ids_.find(id);
            if(!slot)
                return false;
            Order* o = *slot;
            if(by >= o->qty)
                return cancel(id);
            o->qty -= by;
            level(o->side, o->price).qty -= by;
            return true;
        }

        bool replace(OrderId old_id, OrderId new_id, Price px, Qty qty) {
            Order** slot = ids_.find(old_id);
            if(!slot)
                return false;
            Side s = (*slot)->side;
            cancel(old_id);
            return add(new_id, s, px, qty);
        }

        Qty submit(OrderId id, Side side, Price px, Qty qty, bool rest = true) {
            Qty remaining = qty;
            if(side == Side::Buy) {
                while(remaining && best_ask_ != kNone && best_ask_ <= px)
                    remaining -= fill(asks_[best_ask_], remaining);
            }
            else {
                while(remaining && best_bid_ != kNone && best_bid_ >= px)
                    remaining -= fill(bids_[best_bid_], remaining);
            }

            if(remaining && rest) 
                add(id, side, px, remaining);
            return qty - remaining;
        }

        Price best_bid() const { return best_bid_; }
        Price best_ask() const { return best_ask_; }
        std::uint64_t qty_at(Side s, Price px) const {
            return px < kLevels ? (s == Side::Buy ? bids_[px].qty : asks_[px].qty) : 0;
        }
        std::size_t order_count() const { return bid_orders_ + ask_orders_; }
        std::uint64_t trades() const { return trades_; }

        // Total Quantity on one side (slow operation)
        std::uint64_t side_qty(Side s) const {
            std::uint64_t sum = 0;
            for(const Level &l : (s == Side::Buy ? bids_ : asks_))
                sum += l.qty;
            return sum;
        }

        // Snapshot
        void dump(std::ostream &os, int depth = 5) const {
            os << "orders bid=" << bid_orders_ << " ask=" << ask_orders_ << "\n";
            os << "total_qty bid=" << side_qty(Side::Buy) << " ask=" << side_qty(Side::Sell) << "\n";
            os << "best_bid=" << (best_bid_ == kNone ? -1 : static_cast<long>(best_bid_)) << " best_ask=" << (best_ask_ == kNone ? -1 : static_cast<long>(best_ask_)) << "\n";
            int n = 0;
            for(long p = (best_bid_ == kNone ? -1 : static_cast<long>(best_bid_)); p >= 0 && n < depth; p--) {
                if(bids_[p].head) {
                    os << "B " << p << " " << bids_[p].qty << "\n"; 
                    n++;
                }
            }
            n = 0;
            if(best_ask_ != kNone) {
                for(std::size_t p = best_ask_; p < kLevels && n < depth; p++) {
                    if(asks_[p].head) {
                        os << "A " << p << " " << asks_[p].qty << "\n";
                        n++;
                    }
                }
            }
        }
    };
}