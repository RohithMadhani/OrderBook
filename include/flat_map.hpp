#pragma once 
#include <cstddef>
#include <cstdint>
#include <vector>

namespace book {
    template <class V>
    class FlatMap {
        struct Slot {
            std::uint64_t key;
            V val;
        };
    private:
        std::size_t index(std::uint64_t key) const {
            return (key * 0x9E3779B97F4A7C15ULL) >> shift_;
        }

        std::vector<Slot> slots_;
        std::size_t mask_ = 0;
        int shift_ = 0;
        std::size_t size_ = 0;
    public:
        static constexpr std::uint64_t kEmpty = ~0ULL; // UNIT64_MAX

        explicit FlatMap(std::size_t min_capacity) {
            std::size_t cap = 2;
            int lg = 1;
            while(cap < min_capacity) {
                cap <<= 1;
                ++lg;
            }
            mask_ = cap - 1;
            shift_ = 64 - lg;
            slots_.assign(cap, Slot{kEmpty, V{}});
        }

        V* find(std::uint64_t key) {
            std::size_t i = index(key);
            for(;;) {
                if(slots_[i].key == key)
                    return &slots_[i].val;
                if(slots_[i].key == kEmpty)
                    return nullptr;
                i = (i + 1) & mask_;
            }
        }

        // True if inserted, False if already exists or the hash table is more than 7/8 full.
        bool insert(std::uint64_t key, V val) {
            if(size_ + 1 > (mask_ + 1) / 8 * 7)
                return false;
            std::size_t i = index(key);
            for(;;) {
                if(slots_[i].key == key)
                    return false;
                if(slots_[i].key == kEmpty) {
                    slots_[i] = Slot{key, val};
                    ++size_;
                    return true;
                }
                i = (i + 1) & mask_;
            }
        }

        bool erase(std::uint64_t key) {
            std::size_t i = index(key);
            for(;;) {
                if(slots_[i].key == key)
                    break;
                if(slots_[i].key == kEmpty)
                    return false;
                i = (i + 1) & mask_;
            }

            std::size_t j = i;
            for(;;) {
                j = (j + 1) & mask_;
                if(slots_[j].key == kEmpty)
                    break;
                std::size_t ideal = index(slots_[j].key);
                if(((j - ideal) & mask_) >= ((j - i) & mask_)) {
                    slots_[i] = slots_[j];
                    i = j;
                }
            }
            slots_[i].key = kEmpty;
            --size_;
            return true;
        }

        std::size_t size() const { return size_; }
    };
}