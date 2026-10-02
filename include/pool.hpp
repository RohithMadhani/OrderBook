#pragma once
#include <sys/mman.h>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <utility>

namespace book {
    template <class T>
    class Pool {
        struct FreeNode {
            FreeNode* next;
        };
        static_assert (
            sizeof(T) >= sizeof(FreeNode), 
            "T too small for free list"
        );
    private:
        std::size_t capacity_;
        std::size_t bytes_ = 0;
        std::size_t in_use_ = 0;
        unsigned char* mem_ = nullptr;
        FreeNode* free_ = nullptr;
    public:
        explicit Pool(std::size_t capacity) : capacity_(capacity) {
            constexpr std::size_t kHuge = 2u << 20;
            bytes_ = (capacity * sizeof(T) + kHuge - 1) / kHuge * kHuge;
            mem_ = static_cast<unsigned char*>(std::aligned_alloc(kHuge, bytes_));

            if(!mem_)
                throw std::bad_alloc();

            (void) madvise(mem_, bytes_, MADV_HUGEPAGE); // Request for huge pages
            
            // Create a free list to use free slots
            for(std::size_t i = capacity; i-- > 0;)
                free_ = new (mem_ + i * sizeof(T)) FreeNode{free_};
        }

        ~Pool() {
            std::free(mem_);
        }
        Pool(const Pool&) = delete;
        Pool &operator=(const Pool&) = delete;

        // Assign to current slot and move to next free slot
        template<class... Args>
        T* allocate(Args&&... args) {
            if(!free_)
                return nullptr;
            FreeNode* n = free_;
            free_ = n->next;
            ++in_use_;
            return new (n) T{std::forward<Args>(args)...};
        }

        void deallocate(T* p) {
            p->~T();
            free_ = new (p) FreeNode{free_};
            --in_use_;
        }

        std::size_t in_use() const { return in_use_;}
        std::size_t capacity() const { return capacity_;}
    };
}