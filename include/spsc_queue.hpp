#pragma once
#include <atomic>
#include <cstddef>

#include "common.hpp"

namespace book {
    template <class T, std::size_t N>
    class SpscQueue {
        static_assert((N & (N - 1)) == 0, "N must be a power of two");
    private:
        alignas(kCacheLine) std::atomic<std::size_t> head_{0}; 
        alignas(kCacheLine) std::size_t cached_tail_ = 0;
        alignas(kCacheLine) std::atomic<std::size_t> tail_{0};
        alignas(kCacheLine) std::size_t cached_head_ = 0;
        alignas(kCacheLine) T buf_[N];
    public:
        bool push(const T& v) {
            const std::size_t t = tail_.load(std::memory_order_relaxed);
            if(t - cached_head_ == N) {
                cached_head_ = head_.load(std::memory_order_acquire);
                if(t - cached_head_ == N)
                    return false; // Queue is full
            }
            buf_[t & (N - 1)] = v;
            tail_.store(t + 1, std::memory_order_release);
            return true;
        }

        bool pop(T &out) {
            const std::size_t h = head_.load(std::memory_order_relaxed);
            if(h == cached_tail_) {
                cached_tail_ = tail_.load(std::memory_order_acquire);
                if(h == cached_tail_) 
                    return false; // Queue is Empty
            }
            out = buf_[h & (N - 1)];
            head_.store(h + 1, std::memory_order_release);
            return true;
        }
    };
}