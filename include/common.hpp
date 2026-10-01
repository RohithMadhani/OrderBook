#pragma once
#include <cstddef>
#include <cstdint>

namespace book {
    using OrderId = std::uint64_t;
    using Price = std::uint32_t; // Price is in ticks (1 tick = 1 cent)
    using Qty = std::uint32_t;

    enum class Side : std::uint8_t {Buy = 0, Sell = 1};

    inline constexpr std::size_t kCacheLine = 64;
}