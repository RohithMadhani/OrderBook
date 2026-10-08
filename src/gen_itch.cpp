#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#include "itch_writer.hpp"

using namespace book;

struct Live {
    std::uint64_t id;
    char side;
    std::uint32_t px;
    std::uint32_t qty;
};

struct Sym {
    const char *name;
    std::uint16_t locate;
    std::int64_t mid; // ticks
    std::vector<Live> live;
};

int main(int argc, char** argv) {
    if(argc < 2) {
        std::fprintf(stderr, "usage: gen_itch out [n] [seed]\n");
        return 1;
    }

    const long n = argc > 2 ? std::atol(argv[2]) : 2000000; // Sets the number of samples
    std::mt19937_64 rng(argc > 3 ? std::atol(argv[3]) : 1); // Sets the seed
    auto rnd = [&](std::uint64_t lo, std::uint64_t hi) {
        return lo + rng() % (hi - lo + 1);
    };

    Sym syms[2] = {{"AAPL", 1, 15000, {}}, {"MSFT", 2, 30000, {}}};
    std::vector<std::uint8_t> out;
    ItchWriter w;
    std::uint64_t ts = 1000, next_id = 1000;
    long counts[256] = {};
    FILE* f = std::fopen(argv[1], "wb");
    if(!f) {
        std::perror("fopen");
        return 1;
    }

    w.begin('S', 0, ts); w.u8('O'); w.finish(out); ++counts['S'];

    for(auto &s : syms) {
        w.begin('R', s.locate, ts); w.sym(s.name); w.pad(20); w.finish(out); ++counts['R'];
    }

    for(long i {0}; i < n; i++) {
        Sym &s = syms[rng() % 10 < 7 ? 0 : 1];
        ts += rnd(50, 500);
        if(i % 100 == 0)
            s.mid += static_cast<std::uint64_t>(rnd(0, 2)) - 1; // random walk

        auto new_order = [&](char side) {
            Live o{next_id++, side, 0, static_cast<std::uint32_t> (rnd(1, 500))};
            o.px = static_cast<std::uint32_t>(side == 'B' ? s.mid - 1 - rnd(0, 19) : s.mid + 1 + rnd(0, 19));
            return o;
        };

        int op = static_cast<int> (rng() % 100);
        // Add Order
        if(s.live.empty() || op < 40 || s.live.size() < 200) {
            Live o = new_order(rng() & 1 ? 'B' : 'S');
            w.begin('A', s.locate, ts); w.u64(o.id); w.u8(o.side); w.u32(o.qty); w.sym(s.name); w.u32(o.px * 100); w.finish(out); ++counts['A'];
            s.live.push_back(o);
            continue;
        }
        std::size_t idx = rng() % s.live.size();
        Live &o = s.live[idx];
        auto remove = [&] {
            s.live[idx] = s.live.back();
            s.live.pop_back();
        };

        // Delete Order
        if(op < 65) {
            w.begin('D', s.locate, ts); w.u64(o.id); w.finish(out);
            ++counts['D'];
            remove();
        }
        // Reduce Order
        else if(op < 80) {
            std::uint32_t q = static_cast<std::uint32_t>(rnd(1, o.qty));
            w.begin('X', s.locate, ts); w.u64(o.id); w.u32(q); w.finish(out);
            ++counts['X'];
            if(q == o.qty)
                remove();
            else
                o.qty -= q;
        }
        // Submit Order (partial or full)
        else if(op < 90) {
            std::uint32_t q = static_cast<std::uint32_t> (rnd(1, o.qty));
            w.begin('E', s.locate, ts); w.u64(o.id); w.u32(q); w.u64(ts); w.finish(out); 
            ++counts['E'];
            if(q == o.qty)
                remove();
            else
                o.qty -= q;
        }
        // Replace order
        else {
            Live nw = new_order(o.side);
            w.begin('U', s.locate, ts); w.u64(o.id); w.u64(nw.id); w.u32(nw.qty); w.u32(nw.px * 100); w.finish(out);
            ++counts['U'];
            s.live[idx] = nw;
        }

        if(out.size() > (1u << 20)) {
            std::fwrite(out.data(), 1, out.size(), f);
            out.clear();
        }
    }
    std::fwrite(out.data(), 1, out.size(), f);
    std::fclose(f);

    std::uint64_t bq = 0, aq = 0;
    for(auto &o : syms[0].live) 
        (o.side == 'B' ? bq : aq) += o.qty;
    std::printf("wrote %s\n", argv[1]);
    std::printf("message counts:");
    for(int c = 0; c < 256; c++) {
        if(counts[c])
            std::printf(" %c=%ld", c, counts[c]);
    }

    std::printf("\n EXPECTED AAPL: live_orders=%zu bid_qty=%lu ask_qty=%lu\n", syms[0].live.size(), bq, aq);
}