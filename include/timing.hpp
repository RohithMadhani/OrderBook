#pragma once
#include <x86intrin.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace book {
    inline std::uint64_t tsc_begin() {
        _mm_lfence(); // Stop instructions before from crossing over the boundary being measured
        return __rdtsc();
    }

    inline std::uint64_t tsc_end() {
        unsigned aux;
        std::uint64_t t = __rdtscp(&aux); // Waits for earlier instructions to finish
        _mm_lfence();
        return t;
    }

    // TSC ticks per ns
    inline double tsc_ghz() {
        using clk = std::chrono::steady_clock;
        auto t0 = clk::now();

        std::uint64_t c0 = tsc_begin();
        while(clk::now() - t0 < std::chrono::milliseconds(200)) {}
        std::uint64_t c1 = tsc_end();

        auto t1 = clk::now();
        double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();

        return static_cast<double>(c1 - c0) / ns;
    }

    // Time taken by an empty tsc_begin()/tsc_end() pair, in cycles. We return the median
    inline std::uint64_t timer_overhead_cycle() {
        std::vector<std::uint64_t> v(100000);
        for(auto &x : v) {
            std::uint64_t a = tsc_begin();
            std::uint64_t b = tsc_end();
            x = b - a;
        }
        std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
        return v[v.size() / 2];
    }

    // Benchmark the 50th, 90th, 99th, 99.9th percentile elements based on cycles
    inline std::vector<double> report(const char* name, std::vector<std::uint32_t> &cycles, double ghz, std::uint64_t overhead) {
        std::vector<double> ns;
        if(cycles.empty()) {
            std::printf("%s: no samples\n", name);
            return ns;
        }
        std::sort(cycles.begin(), cycles.end());
        ns.reserve(cycles.size());
        double sum = 0;
        for(std::uint32_t c : cycles) {
            double v = (c > overhead ? c - overhead : 0) / ghz;
            ns.push_back(v);
            sum += v;
        }
        auto pct = [&](double p) {
            return ns[std::min(ns.size() - 1, static_cast<std::size_t>(p * ns.size()))];
        };
        
        std::printf("%-22s n=%zu mean=%.0f p50=%.0f p90=%.0f p99=%.0f p99.9=%.0f max=%.0f (ns) \n", name, ns.size(), sum / ns.size(), pct(0.50), pct(0.90), pct(0.99), pct(0.999), ns.back());
        return ns;
    }

    // Log2-bucket histogram sorted latencies in ns
    inline void histogram(const std::vector<double> &ns) {
        if(ns.empty())
            return;
        int buckets[40] = {};
        for(double v : ns) {
            int b = v < 1 ? 0 : static_cast<int>(std::log2(v));
            ++buckets[std::min(b, 39)];
        }
        std::size_t maxc = 0;
        for(int c : buckets)
            maxc = std::max<std::size_t>(maxc, c);
        for(int b = 0; b < 40; b++) {
            if(!buckets[b])
                continue;
            int bar = static_cast<int>(50.0 * buckets[b] / maxc);
            std::printf(" %8.0f - %-8.0f ns | %-50.*s %d\n", std::pow(2.0, b), std::pow(2.0, b + 1), bar, "##################################################", buckets[b]);
        }
    }
}