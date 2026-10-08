#include <cstdint>
#include <memory>
#include <thread>

#include "check.hpp"
#include "spsc_queue.hpp"

using namespace book;

int main() {
    constexpr std::uint64_t kCount = 5'000'000;
    auto q = std::make_unique<SpscQueue<std::uint64_t, 1024>> ();

    std::thread producer ([&] {
        for(std::uint64_t i{0}; i < kCount; i++)
            while(!q->push(i)) {}
    });

    std::uint64_t expected = 0, v = 0;
    while(expected < kCount) {
        if(q->pop(v)) {
            CHECK(v == expected);
            expected++;
        }
    }

    producer.join();
    CHECK(!q->pop(v));
    std::puts("test_spsc: OK");
}