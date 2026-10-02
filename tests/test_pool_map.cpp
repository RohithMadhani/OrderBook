#include <random>
#include <unordered_map>

#include "check.hpp"
#include "flat_map.hpp"
#include "pool.hpp"

using namespace book;

struct Node {
    std::uint64_t a;
    std::uint64_t b;
};

static void test_pool() {
    Pool<Node> pool(4);
    Node* p[4];
    for(auto &x: p) {
        x = pool.allocate(1ULL, 2ULL);
        CHECK(x != nullptr);
    }
    CHECK(pool.allocate(3ULL, 4ULL) == nullptr); // Pool Exhausted
    CHECK(pool.in_use() == 4);
    pool.deallocate(p[2]);
    Node* again = pool.allocate(5ULL, 6ULL);
    CHECK(again == p[2]); // Reuse of the free slot
    CHECK(again->a == 5 && again->b == 6);
}

static void test_flat_map() {
    FlatMap<int> fm(1 << 14);
    std::unordered_map<std::uint64_t, int> ref;
    std::mt19937_64 rng(123);
    for(int i {0}; i < 2000000; i++) {
        std::uint64_t k = rng() % 5000;
        int op = static_cast<int>(rng() % 3);
        if(op == 0) {
            bool a = fm.insert(k, i);
            bool b = ref.emplace(k, i).second;
            CHECK(a == b);
        }
        else if (op == 1) 
            CHECK(fm.erase(k) == (ref.erase(k) != 0));
        else {
            int *v = fm.find(k);
            auto it = ref.find(k);
            CHECK((v != nullptr) == (it != ref.end()));
            if(v)
                CHECK(*v == it->second);
        }
        CHECK(fm.size() == ref.size());
    }
}

int main() {
    test_pool();
    test_flat_map();
    std::puts("test_pool_map: OK");
}