#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace book {
    // Build once ITCH message and appends to the byte vector with a 2-byte length prefix
    class ItchWriter {
    private:
        std::uint8_t b_[64];
        std::size_t n_ = 0;

        void put(std::uint64_t v, int bytes) {
            for(int i = bytes - 1; i >= 0; i--) 
                b_[n_++] = static_cast<std::uint8_t>(v >> (8 * i)); // Store every byte in an index
        }
    public:
        void u8(std::uint8_t v) { b_[n_++] = v; }
        void u16(std::uint16_t v) { put(v, 2); }
        void u32(std::uint32_t v) { put(v, 4); }
        void u48(std::uint64_t v) { put(v, 6); }
        void u64(std::uint64_t v) { put(v, 8); }
        
        void begin(char type, std::uint16_t locate, std::uint64_t ts_ns) {
            n_ = 0;
            u8(static_cast<std::uint8_t>(type));
            u16(locate);
            u16(0);
            u48(ts_ns);
        }

        // Space padding 8 chars
        void sym(const char* s) {
            for(int i{0}; i < 8; i++)
                b_[n_++] = i < static_cast<int>(std::strlen(s)) ? s[i] : ' ';
        }

        // Padding 0 bytes
        void pad(std::size_t bytes) {
            while(bytes--)
                b_[n_++] = 0;
        }


        // Adding message to the output vector
        void finish(std::vector<std::uint8_t> &out) {
            out.push_back(static_cast<std::uint8_t>(n_ >> 8)); // Get the upper byte
            out.push_back(static_cast<std::uint8_t>(n_ & 0xff)); // Get the lower byte
            out.insert(out.end(), b_, b_ + n_); // appending the message
        }
    };
}