#include <pthread.h>
#include <sched.h>
#include <atomic>
#include <chrono> 
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "itch.hpp"
#include "mapped_file.hpp"
#include "order_book.hpp"
#include "spsc_queue.hpp"
#include "timing.hpp"

using namespace book;

// Assign logical CPU to a specific thread (producer/consumer)
static void pin(int cpu) {
    if(cpu < 0)
        return;
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    if(pthread_setaffinity_np(pthread_self(), sizeof set, &set) != 0)
        std::fprintf(stderr, "warning: could not pin to cpu %d \n", cpu);
}

// Convert the Itch Parser output into the OrderBook operations
static inline bool apply(OrderBook &b, const Event &e) {
    switch (e.type) {
        case EvType::Add:
            return b.add(e.ref, e.side, e.price, e.qty);
        case EvType::Exec:
        case EvType::Cancel:
            return b.reduce(e.ref, e.qty);
        case EvType::Delete:
            return b.cancel(e.ref);
        case EvType::Replace:
            return b.replace(e.ref, e.new_ref, e.price, e.qty);
    }

    return false;
}

static constexpr std::size_t kQueueSize = 1 << 16;
using Queue = SpscQueue<Event, kQueueSize>;

int main(int argc, char** argv) {
    if(argc < 2) {
        std::fprintf(stderr, "usage: bench <itch_file> [--symbol S] [--rate R] [--pcpu N] [--ccpu N] [--csv F] [--snapshot F]\n ");
        return 1;
    }

    std::string symbol = "AAPL", csv_path, snap_path;
    double rate = 0;
    int pcpu = -1, ccpu = -1;
    for(int i{2}; i + 1 < argc; i += 2) {
        std::string k = argv[i];
        if(k == "--symbol")
            symbol = argv[i + 1];
        else if (k == "--rate")
            rate = std::atof(argv[i + 1]);
        else if(k == "--pcpu")
            pcpu = std::atoi(argv[i + 1]);
        else if(k == "--ccpu")
            ccpu = std::atoi(argv[i + 1]);
        else if(k == "--csv")
            csv_path = argv[i + 1];
        else if(k == "--snapshot") 
            snap_path = argv[i + 1];
        else {
            std::fprintf(stderr, "unknown option %s\n", k.c_str());
            return 1;
        }    
    }

    MappedFile file(argv[1]);
    std::printf("file: %s (%.1f MB) symbol: %s\n", argv[1], file.size() / 1e6, symbol.c_str());

    // Parser Only
    std::uint64_t emitted = 0;
    {
        ItchParser parser(symbol.c_str());
        std::uint64_t sink_count = 0;
        auto t0 = std::chrono::steady_clock::now();
        parser.parse(file.data(), file.size(), [&](Event&) {
            sink_count++;
        });
        double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        emitted = parser.stats().emitted;
        std::printf("\n[parse only] %lu msgs in %.3f s = %.1f M msgs/s, %.0f MB/s\n", parser.stats().total, sec, parser.stats().total / sec / 1e6, file.size() / sec / 1e6);
        std::printf(" symbol locate=%u events for symbol=%lu\n by type:", parser.locate(), emitted);

        for(int c = 0; c < 256; c++) {
            if(parser.stats().by_type[c])
                std::printf(" %c=%lu", c, parser.stats().by_type[c]);
        }
        std::printf("\n");
        if(!parser.located()) {
            std::fprintf(stderr, "symbol not found in file \n");
            return 1;
        }
    }

    const double ghz = tsc_ghz();
    const std::uint64_t overhead = timer_overhead_cycle();
    std::printf("\nTSC: %.3f GHz    timer overhead: %lu cycles (%.1f ns, subtracted from results)\n", ghz, overhead, overhead / ghz);

    // Full Pipeline
    auto queue = std::make_unique<Queue>();
    auto book = std::make_unique<OrderBook>(1 << 22);
    std::vector<std::uint32_t> apply_cycles, e2e_cycles;
    apply_cycles.reserve(emitted);
    e2e_cycles.reserve(emitted);
    std::atomic<bool> done{false};
    std::uint64_t failed = 0;

    std::thread consumer([&] {
        pin(ccpu);
        Event ev;
        for(;;) {
            bool got = queue->pop(ev);
            if(!got) {
                if(!done.load(std::memory_order_acquire)) {
                    _mm_pause();
                    continue;
                }
                got = queue->pop(ev);
                if(!got)
                    break;
            }
            std::uint64_t t0 = tsc_begin();
            bool ok = apply(*book, ev);
            std::uint64_t t1 = tsc_end();
            if(!ok)
                ++failed;
            apply_cycles.push_back(static_cast<std::uint32_t>(std::min<std::uint64_t>(t1 - t0, UINT32_MAX)));
            e2e_cycles.push_back(static_cast<std::uint32_t>(std::min<std::uint64_t>(t1 - ev.t_in, UINT32_MAX)));
        }
    });

    pin(pcpu);
    ItchParser parser(symbol.c_str());
    const double interval = rate > 0 ? ghz * 1e9 / rate : 0; // TSC ticks b/w msgs
    double next = static_cast<double>(__rdtsc()) + interval;
    auto t_start = std::chrono::steady_clock::now();
    parser.parse(file.data(), file.size(), [&](Event &ev) {
        if(interval > 0) {
            while(static_cast<double>(__rdtsc()) < next)
                _mm_pause();
            ev.t_in = static_cast<std::uint64_t>(next);
            next += interval;
        }
        else {
            ev.t_in = __rdtsc();
        }
        while(!queue->push(ev))
            _mm_pause();
    });

    done.store(true, std::memory_order_release);
    consumer.join();
    double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count();

    std::printf("\n[pipeline] %lu events in %.3f s = %.2f M events/s (rate limit: %s)\n", emitted, sec, emitted/sec/1e6, rate > 0 ? std::to_string(static_cast<long>(rate)).c_str() : "none");
    std::printf(" failed book operations: %lu trades matched: %lu\n\n", failed, book->trades());

    std::vector<double> ns = report("book apply latency", apply_cycles, ghz, overhead);
    report("end-to-end (queue + apply)", e2e_cycles, ghz, overhead);
    std::printf("\napply-latency histogram (log2 buckets):\n");
    histogram(ns);

    if(!csv_path.empty()) {
        std::ofstream csv(csv_path);
        for(double v : ns)
            csv << v << "\n";
        std::printf("\nwrote %s\n", csv_path.c_str());
    }

    std::printf("\nfinal book: \n");
    book->dump(std::cout, 5);
    if(!snap_path.empty()) {
        std::ofstream snap(snap_path);
        book->dump(snap, 10);
        std::printf("wrote snapshot %s\n", snap_path.c_str());
    }
}