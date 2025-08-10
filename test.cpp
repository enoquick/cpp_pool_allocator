

#include <string>
#include <vector>
#include <list>
#include <map>
#include <iostream>
#include <iomanip>
#include <cassert>
#include <chrono>
#include <numeric>

#include "pool_allocator.h"
//#include "sfl_pool_allocator.h" //another pool allocator - not used

using ck_t = std::chrono::high_resolution_clock::time_point;
static inline ck_t now()noexcept {
    return std::chrono::high_resolution_clock::now();
}

static inline auto diff(const ck_t& t1, const ck_t& t2)noexcept {
   return std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
}

typedef enum enum_type_allocator : std::uint16_t {
    a_pool
    ,a_pool_thread_safe
//    ,a_sfl //another pool allocator - not used
    ,a_std
} type_allocator_t;

using test_values_t = std::map<std::pair<std::size_t,type_allocator_t>, std::vector<decltype(diff(now(), now()))>>;

static inline void init_values(test_values_t& values) {
    const std::vector<type_allocator_t> allocators = { a_pool,a_pool_thread_safe/*,a_sfl*/,a_std };
    const std::vector<std::size_t> list_insert_numbers = {
        1000
        ,2000
        ,4000
        ,8000
        ,16000
        ,32767
        ,65536
       ,4000000
       ,20000000 
       // ,80000000
    };
    for (auto& t : allocators) {
        for (auto& list_insert_number : list_insert_numbers) {
           [[maybe_unused]] auto& v= values[std::make_pair(list_insert_number,t)];
        }
    }
}

static const char* str(type_allocator_t t)noexcept {
    switch (t) {
        using t_t = decltype(t);
        case t_t::a_pool: return "pool";
        case t_t::a_pool_thread_safe: return "pool ts";
		//case t_t::a_sfl: return "sfl"; //another pool allocator - not used
        case t_t::a_std: return "std";
    }
    assert(0);
    return "";
}

typedef enum class enum_test {
    list_insertion
    , allocator
} test_t;

static const char* str(test_t t)noexcept {
    switch (t) {
        using t_t = decltype(t);
        case t_t::list_insertion: return "list insertions";
        case t_t::allocator: return "allocate/deallocate";
    }
    assert(0);
    return "";
}

template <test_t t>
static inline void print_values(const test_values_t& values) {
    std::cout << "test " << str(t) << std::endl;
    std::size_t pred_num{};
    for (auto& [k, v] : values) {
        auto& [list_insert_numbers,type_allocator] = k;
        if (pred_num != list_insert_numbers) {
            pred_num = list_insert_numbers;
            std::cout << std::string(20, '-') << std::endl;
        }
        const auto sum = std::accumulate(v.begin(), v.end(), std::size_t{}) / v.size();
        std::cout << std::setw(20) << "inserts: " << list_insert_numbers << ' ' << str(type_allocator) << ": " << sum << ": average in micros" << std::endl;
    }

}

template <test_t t,typename A>
static inline void test(A a, std::size_t list_insertion) {
	using value_type=typename A::value_type;
    if constexpr (t == test_t::list_insertion) {
        std::list<value_type, A> listp(a);
        for (std::size_t i = 0; i < list_insertion; ++i) {
            listp.push_back(static_cast<value_type>(i));
        }
    }
    else if constexpr (t == test_t::allocator) {
        std::vector<value_type*> v;
        v.reserve(list_insertion);
        for (std::size_t i = 0; i < list_insertion; ++i) {
            v.push_back(a.allocate(1));
        }
        for (auto& p : v) {
            a.deallocate(p,1);
        }
    }
    else {
        assert(0);
    }
}

template <test_t t>
static inline void test() {
    test_values_t values;
    try {
        init_values(values);
        for (std::size_t i = 0; i < 10; ++i) {
            std::cout << "loop " << i << std::endl;
            for (auto& m : values) {
                const auto& [list_insert_number, type_allocator] = m.first;
                auto& list = m.second;
                switch (type_allocator) {
                    using t_t = std::decay_t<decltype(type_allocator)>;
                    case t_t::a_pool: {
                        const auto a = pool_allocator<int, false>(list_insert_number);
                        const auto t1 = now();
                        test<t>(a, list_insert_number);
                        list.push_back(diff(t1, now()));
                        break;
                    }
                    case t_t::a_pool_thread_safe: {
                        const auto a = pool_allocator<int, true>(list_insert_number);
                        const auto t1 = now();
                        test<t>(a, list_insert_number);
                        list.push_back(diff(t1, now()));
                        break;
                    }
#if 0 //another pool allocator
                    case t_t::a_sfl: {
                        const auto a = sfl::pool_allocator<int>{};
                        const auto t1 = now();
                        test<t>(a, list_insert_number);
                        list.push_back(diff(t1, now()));
                        break;
                    }
#endif 
                    case t_t::a_std: {
                        const auto a = std::allocator<int>{};
                        const auto t1 = now();
                        test<t>(a, list_insert_number);
                        list.push_back(diff(t1, now()));
                        break;
                    }
                }
            }
        }
        print_values<t>(values);
    }
    catch (const std::bad_alloc&) {
        std::cerr << "no such memory\n";
    }
    catch (...) {
        std::cerr << "unknow exception catched\n";
    }
}

int main() {
    test<test_t::list_insertion>();
    test<test_t::allocator>();
    return 0;
}
 


