#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "common.hpp"

namespace book {
    // Convert big-endian to little-endian
    inline std::uint16_t be16(const std::uint8_t *p) {
        std::uint16_t v;
        std::memcpy(&v, p, 2);
        return __builtin_bswap16(v);
    }

    inline std::uint32_t be32(const std::uint8_t *p) {
        std::uint32_t v;
        std::memcpy(&v, p, 4);
        return __builtin_bswap32(v);
    }

    inline std::uint64_t be48(const std::uint8_t *p) {
        return static_cast<std::uint64_t>(be16(p) << 32) | be32(p + 2); // Get the first 2 bytes and then the last 4 bytes
    }

    inline std::uint64_t be64(const std::uint8_t *p) {
        std::uint64_t v;
        std::memcpy(&v, p, 8);
        return __builtin_bswap64(v);
    }

    // ITCH messages to five normalized events
    enum class EvType : std::uint8_t { Add, Exec, Cancel, Delete, Replace };

    // Objects must start aligned at 64 bytes to avoid false sharing
    struct alignas(kCacheLine) Event {
        std::uint64_t t_in = 0;
        OrderId ref = 0;
        OrderId new_ref = 0;
        Qty qty = 0;
        Price price = 0;
        EvType type = EvType::Add;
        Side side = Side::Buy;
    };
    static_assert(sizeof(Event) == 64, "Event should be one cache line");

    struct ItchStats {
        std::uint64_t total = 0; // Messages in file
        std::uint64_t emitted = 0; // Events for our symbol
        std::uint64_t by_type[256] = {}; // Messages per type
    };

    class ItchParser {
    private:
        char sym_[8];
        std::uint16_t locate_ = 0;
        bool located_ = false;
        ItchStats stats_;

        bool mine(std::uint16_t loc) {
            return located_ && loc == locate_;
        }

        template<class Sink>
        void emit(Sink &&sink, Event &ev) {
            ++stats_.emitted;
            sink(ev);
        }
    public:
        explicit ItchParser(const char* symbol) {
            std::memset(sym_, ' ', sizeof sym_);
            std::memcpy(sym_, symbol, std::min<std::size_t>(std::strlen(symbol), 8));
        }

        template <class Sink>
        void parse(const std::uint8_t *p, std::size_t n, Sink &&sink) {
            const std::uint8_t *end = p + n;
            Event ev;

            // ITCH message contains, length(2 bytes), type(1 byte), location(2 bytes), tracking(2 bytes), timestamp(6 bytes).
            while(p + 2 <= end) {
                const std::size_t len = be16(p);
                const std::uint8_t *m = p + 2;
                if(len == 0 || m + len > end)
                    break; 
                
                p = m + len;
                ++stats_.total;
                ++stats_.by_type[m[0]];
                const std::uint16_t loc = be16(m + 1); // since m[0] is type
                

                switch(m[0]) {
                    case 'R':
                        // 'R' type matches the sym_ we have within the message stream and creates sets flags so only that specific sym_ gets orderBook created
                        if(!located_ && std::memcmp(m + 11, sym_, 8) == 0) {
                            locate_ = loc;
                            located_ = true;
                        }
                        break;
                    case 'A':
                    case 'F': // Add Order
                        if(!mine(loc)) 
                            break;
                        ev.type = EvType::Add;
                        ev.ref = be64(m + 11); // OrderId
                        ev.side = m[19] == 'B' ? Side::Buy : Side::Sell; // Side
                        ev.qty = be32(m + 20); // Quantity
                        ev.price = be32(m + 32) / 100; // Price in cents
                        emit(sink, ev);
                        break;
                    case 'E':
                    case 'C': // Reduce Order
                        if(!mine(loc))
                            break;
                        ev.type = EvType::Exec;
                        ev.ref = be64(m + 11);
                        ev.qty = be32(m + 19);
                        emit(sink, ev);
                        break;
                    case 'X': // Cancel Order
                        if(!mine(loc))
                            break;
                        ev.type = EvType::Cancel;
                        ev.ref = be64(m + 11);
                        ev.qty = be32(m + 19);
                        emit(sink, ev);
                        break;
                    case 'D': // Delete Order
                        if(!mine(loc))
                            break;
                        ev.type = EvType::Delete;
                        ev.ref = be64(m + 11);
                        emit(sink, ev);
                        break;
                    case 'U': // Replace Order
                        if(!mine(loc))
                            break;
                        ev.type = EvType::Replace;
                        ev.ref = be64(m + 11);
                        ev.new_ref = be64(m + 19);
                        ev.qty = be32(m + 27);
                        ev.price = be32(m + 31) / 100;
                        emit(sink, ev);
                        break;
                    default:
                        break;
                }
            }
        }
        bool located() const { return located_; }
        std::uint16_t locate() const { return locate_; }
        const ItchStats& stats() const { return stats_; }
    };
}