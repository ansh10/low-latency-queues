#include "SPSCQueue.cpp"
#include "SPSCQueueV2.cpp"
#include "SPSCQueueV3.cpp"
#include "SPSCQueueV4.cpp"
#include "SPSCQueueV5.cpp"
#include "SPSCQueueV6.cpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>

template <template <typename> class Q>
static void test_basic_push_pop() {
    Q<int> q(8);
    q.push(1);
    q.push(2);
    q.push(3);
    assert(q.pop() == 1);
    assert(q.pop() == 2);
    assert(q.pop() == 3);
    std::puts("[OK]    test_basic_push_pop");
}

template <template <typename> class Q>
static void test_interleaved_push_pop() {
    Q<int> q(4);
    for (int i = 0; i < 16; ++i) {
        q.push(i);
        assert(q.pop() == i);
    }
    std::puts("[OK]    test_interleaved_push_pop");
}

template <template <typename> class Q>
static void test_fill_then_drain() {
    constexpr size_t cap = 4;
    Q<int> q(cap);
    for (size_t i = 0; i < cap; ++i) q.push(static_cast<int>(i));
    for (size_t i = 0; i < cap; ++i) assert(q.pop() == static_cast<int>(i));
    std::puts("[OK]    test_fill_then_drain");
}

template <template <typename> class Q>
static void test_wraparound() {
    constexpr size_t cap = 5;
    constexpr int rounds = 7;
    Q<int> q(cap);
    int next_expected = 0;
    int next_to_push = 0;
    for (int r = 0; r < rounds; ++r) {
        for (size_t i = 0; i < cap; ++i) q.push(next_to_push++);
        for (size_t i = 0; i < cap; ++i) {
            int v = q.pop();
            assert(v == next_expected);
            ++next_expected;
        }
    }
    assert(next_expected == rounds * static_cast<int>(cap));
    std::puts("[OK]    test_wraparound");
}

template <template <typename> class Q>
static void test_capacity_one() {
    Q<int> q(1);
    for (int i = 0; i < 32; ++i) {
        q.push(i);
        assert(q.pop() == i);
    }
    std::puts("[OK]    test_capacity_one");
}

template <template <typename> class Q>
static void test_zero_capacity_throws() {
    bool threw = false;
    try {
        Q<int> q(0);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);
    std::puts("[OK]    test_zero_capacity_throws");
}

template <template <typename> class Q>
static void test_string_payload() {
    Q<std::string> q(4);
    for (int i = 0; i < 50; ++i) {
        q.push("payload-" + std::to_string(i));
        std::string got = q.pop();
        assert(got == "payload-" + std::to_string(i));
    }
    std::puts("[OK]    test_string_payload");
}

template <template <typename> class Q>
static void test_two_threads(size_t capacity, size_t N) {
    Q<size_t> q(capacity);

    std::thread producer([&] {
        for (size_t i = 0; i < N; ++i) q.push(i);
    });

    std::atomic<size_t> popped{0};
    std::thread consumer([&] {
        size_t expected = 0;
        for (size_t i = 0; i < N; ++i) {
            size_t v = q.pop();
            assert(v == expected);
            ++expected;
            popped.store(expected, std::memory_order_relaxed);
        }
    });

    producer.join();
    consumer.join();
    assert(popped.load() == N);
    std::printf("[OK]    test_two_threads (cap=%zu, N=%zu)\n", capacity, N);
}

template <template <typename> class Q>
static void test_slow_producer() {
    constexpr size_t N = 500;
    Q<size_t> q(8);

    std::thread producer([&] {
        for (size_t i = 0; i < N; ++i) {
            q.push(i);
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    });

    std::thread consumer([&] {
        for (size_t i = 0; i < N; ++i) {
            size_t v = q.pop();
            assert(v == i);
        }
    });

    producer.join();
    consumer.join();
    std::puts("[OK]    test_slow_producer");
}

template <template <typename> class Q>
static void test_slow_consumer() {
    constexpr size_t N = 500;
    Q<size_t> q(8);

    std::thread producer([&] {
        for (size_t i = 0; i < N; ++i) q.push(i);
    });

    std::thread consumer([&] {
        for (size_t i = 0; i < N; ++i) {
            size_t v = q.pop();
            assert(v == i);
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    });

    producer.join();
    consumer.join();
    std::puts("[OK]    test_slow_consumer");
}

template <template <typename> class Q>
static void test_random_jitter() {
    constexpr size_t N = 5'000;
    Q<size_t> q(16);

    std::thread producer([&] {
        std::mt19937 rng(0xC0FFEE);
        std::uniform_int_distribution<int> dist(0, 20);
        for (size_t i = 0; i < N; ++i) {
            q.push(i);
            if ((i & 0x3F) == 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(dist(rng)));
            }
        }
    });

    std::thread consumer([&] {
        std::mt19937 rng(0xDEADBEEF);
        std::uniform_int_distribution<int> dist(0, 20);
        for (size_t i = 0; i < N; ++i) {
            size_t v = q.pop();
            assert(v == i);
            if ((i & 0x3F) == 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(dist(rng)));
            }
        }
    });

    producer.join();
    consumer.join();
    std::puts("[OK]    test_random_jitter");
}

template <template <typename> class Q>
static void test_stress_repeated() {
    constexpr int iterations = 50;
    constexpr size_t N = 20'000;
    for (int it = 0; it < iterations; ++it) {
        Q<size_t> q(64);
        std::thread producer([&] {
            for (size_t i = 0; i < N; ++i) q.push(i);
        });
        std::thread consumer([&] {
            for (size_t i = 0; i < N; ++i) {
                size_t v = q.pop();
                assert(v == i);
            }
        });
        producer.join();
        consumer.join();
    }
    std::printf("[OK]    test_stress_repeated (%d iters x %zu items)\n",
                iterations, N);
}

template <template <typename> class Q>
static void run_all_tests(const char* label) {
    std::printf("\n=== %s ===\n", label);
    test_basic_push_pop<Q>();
    test_interleaved_push_pop<Q>();
    test_fill_then_drain<Q>();
    test_wraparound<Q>();
    test_capacity_one<Q>();
    test_zero_capacity_throws<Q>();
    test_string_payload<Q>();
    for (size_t cap : {size_t(1), size_t(2), size_t(7), size_t(64), size_t(1024)}) {
        test_two_threads<Q>(cap, 50'000);
    }
    test_slow_producer<Q>();
    test_slow_consumer<Q>();
    test_random_jitter<Q>();
    test_stress_repeated<Q>();
}

template <template <typename> class Q>
static void bench_throughput(const char* label) {
    constexpr size_t N = 10'000'000;
    Q<size_t> q(1024);

    auto start = std::chrono::steady_clock::now();

    std::thread producer([&] {
        for (size_t i = 0; i < N; ++i) q.push(i);
    });
    std::thread consumer([&] {
        size_t sink = 0;
        for (size_t i = 0; i < N; ++i) sink += q.pop();
        asm volatile("" : : "r"(sink) : "memory");
    });

    producer.join();
    consumer.join();

    auto elapsed = std::chrono::steady_clock::now() - start;
    double secs = std::chrono::duration<double>(elapsed).count();
    double mops = (static_cast<double>(N) / secs) / 1e6;
    double ns_per_op = (secs * 1e9) / static_cast<double>(N);

    std::printf("[BENCH %-12s] %zu items in %.3f s -> %.2f M ops/s (%.1f ns/op)\n",
                label, N, secs, mops, ns_per_op);
}

int main() {
    run_all_tests<SPSCQueue>("SPSCQueue");
    run_all_tests<SPSCQueueV2>("SPSCQueueV2");
    run_all_tests<SPSCQueueV3>("SPSCQueueV3");
    run_all_tests<SPSCQueueV4>("SPSCQueueV4");
    run_all_tests<SPSCQueueV5>("SPSCQueueV5");
    run_all_tests<SPSCQueueV6>("SPSCQueueV6");

    std::puts("\n=== Benchmarks ===");
    bench_throughput<SPSCQueue>("SPSCQueue");
    bench_throughput<SPSCQueueV2>("SPSCQueueV2");
    bench_throughput<SPSCQueueV3>("SPSCQueueV3");
    bench_throughput<SPSCQueueV4>("SPSCQueueV4");
    bench_throughput<SPSCQueueV5>("SPSCQueueV5");
    bench_throughput<SPSCQueueV6>("SPSCQueueV6");

    std::puts("\nAll tests passed.");
    return 0;
}
