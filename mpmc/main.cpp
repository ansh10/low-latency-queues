#include "MPMC.cpp"
// Add more MPMC versions here as you create them, e.g.:
// #include "MPMCV2.cpp"
// #include "MPMCV3.cpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Single-threaded sanity tests
// ---------------------------------------------------------------------------

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
static void test_string_payload() {
    Q<std::string> q(4);
    for (int i = 0; i < 50; ++i) {
        q.push("payload-" + std::to_string(i));
        std::string got = q.pop();
        assert(got == "payload-" + std::to_string(i));
    }
    std::puts("[OK]    test_string_payload");
}

// ---------------------------------------------------------------------------
// MPMC concurrency helper: runs P producers and C consumers, each producer
// pushes `per_producer` items with values that are globally unique, and
// consumers cooperatively drain exactly P*per_producer items. Verifies that
// every value 0..P*per_producer-1 was consumed exactly once.
// ---------------------------------------------------------------------------
template <template <typename> class Q>
static void run_mpmc_workload(size_t cap,
                              int num_producers,
                              int num_consumers,
                              size_t per_producer,
                              const char* label) {
    const size_t total = per_producer * static_cast<size_t>(num_producers);
    Q<size_t> q(cap);

    std::atomic<size_t> claimed{0};
    std::vector<std::vector<size_t>> consumed(num_consumers);

    std::vector<std::thread> producers;
    producers.reserve(num_producers);
    for (int p = 0; p < num_producers; ++p) {
        producers.emplace_back([&, p] {
            const size_t base = static_cast<size_t>(p) * per_producer;
            for (size_t i = 0; i < per_producer; ++i) {
                q.push(base + i);
            }
        });
    }

    std::vector<std::thread> consumers;
    consumers.reserve(num_consumers);
    for (int c = 0; c < num_consumers; ++c) {
        consumers.emplace_back([&, c] {
            auto& local = consumed[c];
            local.reserve(total / static_cast<size_t>(num_consumers) + 1);
            while (true) {
                size_t slot = claimed.fetch_add(1, std::memory_order_relaxed);
                if (slot >= total) break;
                local.push_back(q.pop());
            }
        });
    }

    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();

    std::vector<size_t> all;
    all.reserve(total);
    for (auto& v : consumed) {
        all.insert(all.end(), v.begin(), v.end());
    }
    assert(all.size() == total);

    std::sort(all.begin(), all.end());
    for (size_t i = 0; i < total; ++i) {
        assert(all[i] == i);
    }

    std::printf("[OK]    %s (cap=%zu, P=%d, C=%d, N=%zu)\n",
                label, cap, num_producers, num_consumers, total);
}

template <template <typename> class Q>
static void test_mpmc_balanced() {
    run_mpmc_workload<Q>(64, 4, 4, 25'000, "test_mpmc_balanced");
}

template <template <typename> class Q>
static void test_mpmc_capacities() {
    for (size_t cap : {size_t(1), size_t(2), size_t(7), size_t(64), size_t(1024)}) {
        run_mpmc_workload<Q>(cap, 3, 3, 10'000, "test_mpmc_capacities");
    }
}

template <template <typename> class Q>
static void test_spmc() {
    run_mpmc_workload<Q>(32, 1, 8, 50'000, "test_spmc");
}

template <template <typename> class Q>
static void test_mpsc() {
    run_mpmc_workload<Q>(32, 8, 1, 50'000, "test_mpsc");
}

template <template <typename> class Q>
static void test_many_threads() {
    run_mpmc_workload<Q>(16, 8, 8, 20'000, "test_many_threads");
}

template <template <typename> class Q>
static void test_more_producers_than_consumers() {
    run_mpmc_workload<Q>(16, 8, 2, 10'000, "test_more_producers_than_consumers");
}

template <template <typename> class Q>
static void test_more_consumers_than_producers() {
    run_mpmc_workload<Q>(16, 2, 8, 10'000, "test_more_consumers_than_producers");
}

template <template <typename> class Q>
static void test_tiny_capacity_high_contention() {
    run_mpmc_workload<Q>(2, 8, 8, 5'000, "test_tiny_capacity_high_contention");
}

// ---------------------------------------------------------------------------
// Tests with artificial jitter, to exercise blocking paths on full/empty.
// ---------------------------------------------------------------------------

template <template <typename> class Q>
static void test_slow_producers() {
    constexpr size_t per_producer = 200;
    constexpr int P = 4;
    constexpr int C = 4;
    constexpr size_t total = per_producer * P;

    Q<size_t> q(8);
    std::atomic<size_t> claimed{0};
    std::atomic<long long> sum{0};

    std::vector<std::thread> producers;
    for (int p = 0; p < P; ++p) {
        producers.emplace_back([&, p] {
            const size_t base = static_cast<size_t>(p) * per_producer;
            for (size_t i = 0; i < per_producer; ++i) {
                q.push(base + i);
                std::this_thread::sleep_for(std::chrono::microseconds(20));
            }
        });
    }

    std::vector<std::thread> consumers;
    for (int c = 0; c < C; ++c) {
        consumers.emplace_back([&] {
            while (true) {
                size_t slot = claimed.fetch_add(1, std::memory_order_relaxed);
                if (slot >= total) break;
                sum.fetch_add(static_cast<long long>(q.pop()),
                              std::memory_order_relaxed);
            }
        });
    }

    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();

    long long expected =
        static_cast<long long>(total) * (static_cast<long long>(total) - 1) / 2;
    assert(sum.load() == expected);
    std::puts("[OK]    test_slow_producers");
}

template <template <typename> class Q>
static void test_slow_consumers() {
    constexpr size_t per_producer = 200;
    constexpr int P = 4;
    constexpr int C = 4;
    constexpr size_t total = per_producer * P;

    Q<size_t> q(8);
    std::atomic<size_t> claimed{0};
    std::atomic<long long> sum{0};

    std::vector<std::thread> producers;
    for (int p = 0; p < P; ++p) {
        producers.emplace_back([&, p] {
            const size_t base = static_cast<size_t>(p) * per_producer;
            for (size_t i = 0; i < per_producer; ++i) q.push(base + i);
        });
    }

    std::vector<std::thread> consumers;
    for (int c = 0; c < C; ++c) {
        consumers.emplace_back([&] {
            while (true) {
                size_t slot = claimed.fetch_add(1, std::memory_order_relaxed);
                if (slot >= total) break;
                sum.fetch_add(static_cast<long long>(q.pop()),
                              std::memory_order_relaxed);
                std::this_thread::sleep_for(std::chrono::microseconds(20));
            }
        });
    }

    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();

    long long expected =
        static_cast<long long>(total) * (static_cast<long long>(total) - 1) / 2;
    assert(sum.load() == expected);
    std::puts("[OK]    test_slow_consumers");
}

template <template <typename> class Q>
static void test_random_jitter() {
    constexpr size_t per_producer = 1'000;
    constexpr int P = 3;
    constexpr int C = 3;
    constexpr size_t total = per_producer * P;

    Q<size_t> q(16);
    std::atomic<size_t> claimed{0};
    std::atomic<long long> sum{0};

    std::vector<std::thread> producers;
    for (int p = 0; p < P; ++p) {
        producers.emplace_back([&, p] {
            std::mt19937 rng(0xC0FFEE ^ p);
            std::uniform_int_distribution<int> dist(0, 20);
            const size_t base = static_cast<size_t>(p) * per_producer;
            for (size_t i = 0; i < per_producer; ++i) {
                q.push(base + i);
                if ((i & 0x3F) == 0)
                    std::this_thread::sleep_for(std::chrono::microseconds(dist(rng)));
            }
        });
    }

    std::vector<std::thread> consumers;
    for (int c = 0; c < C; ++c) {
        consumers.emplace_back([&, c] {
            std::mt19937 rng(0xDEADBEEF ^ c);
            std::uniform_int_distribution<int> dist(0, 20);
            size_t local = 0;
            while (true) {
                size_t slot = claimed.fetch_add(1, std::memory_order_relaxed);
                if (slot >= total) break;
                sum.fetch_add(static_cast<long long>(q.pop()),
                              std::memory_order_relaxed);
                if ((local++ & 0x3F) == 0)
                    std::this_thread::sleep_for(std::chrono::microseconds(dist(rng)));
            }
        });
    }

    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();

    long long expected =
        static_cast<long long>(total) * (static_cast<long long>(total) - 1) / 2;
    assert(sum.load() == expected);
    std::puts("[OK]    test_random_jitter");
}

template <template <typename> class Q>
static void test_stress_repeated() {
    constexpr int iterations = 30;
    constexpr int P = 4;
    constexpr int C = 4;
    constexpr size_t per_producer = 5'000;
    for (int it = 0; it < iterations; ++it) {
        run_mpmc_workload<Q>(64, P, C, per_producer,
                             it == iterations - 1 ? "test_stress_repeated[last]"
                                                  : "test_stress_repeated[..]");
    }
    std::printf("[OK]    test_stress_repeated (%d iters)\n", iterations);
}

// ---------------------------------------------------------------------------
// Test driver
// ---------------------------------------------------------------------------

template <template <typename> class Q>
static void run_all_tests(const char* label) {
    std::printf("\n=== %s ===\n", label);
    test_basic_push_pop<Q>();
    test_interleaved_push_pop<Q>();
    test_fill_then_drain<Q>();
    test_wraparound<Q>();
    test_capacity_one<Q>();
    test_string_payload<Q>();
    test_mpmc_balanced<Q>();
    test_mpmc_capacities<Q>();
    test_spmc<Q>();
    test_mpsc<Q>();
    test_many_threads<Q>();
    test_more_producers_than_consumers<Q>();
    test_more_consumers_than_producers<Q>();
    test_tiny_capacity_high_contention<Q>();
    test_slow_producers<Q>();
    test_slow_consumers<Q>();
    test_random_jitter<Q>();
    test_stress_repeated<Q>();
}

// ---------------------------------------------------------------------------
// Benchmark: multi-producer / multi-consumer throughput.
// ---------------------------------------------------------------------------
template <template <typename> class Q>
static void bench_mpmc(const char* label,
                       int num_producers,
                       int num_consumers,
                       size_t total_items) {
    const size_t per_producer =
        total_items / static_cast<size_t>(num_producers);
    const size_t total = per_producer * static_cast<size_t>(num_producers);

    Q<size_t> q(1024);
    std::atomic<size_t> claimed{0};
    std::atomic<long long> sink{0};

    auto start = std::chrono::steady_clock::now();

    std::vector<std::thread> producers;
    producers.reserve(num_producers);
    for (int p = 0; p < num_producers; ++p) {
        producers.emplace_back([&, p] {
            const size_t base = static_cast<size_t>(p) * per_producer;
            for (size_t i = 0; i < per_producer; ++i) q.push(base + i);
        });
    }

    std::vector<std::thread> consumers;
    consumers.reserve(num_consumers);
    for (int c = 0; c < num_consumers; ++c) {
        consumers.emplace_back([&] {
            long long local = 0;
            while (true) {
                size_t slot = claimed.fetch_add(1, std::memory_order_relaxed);
                if (slot >= total) break;
                local += static_cast<long long>(q.pop());
            }
            sink.fetch_add(local, std::memory_order_relaxed);
        });
    }

    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();

    auto elapsed = std::chrono::steady_clock::now() - start;
    double secs = std::chrono::duration<double>(elapsed).count();
    double mops = (static_cast<double>(total) / secs) / 1e6;
    double ns_per_op = (secs * 1e9) / static_cast<double>(total);

    long long observed = sink.load();
    asm volatile("" : : "r"(observed) : "memory");

    std::printf(
        "[BENCH %-12s P=%d C=%d] %zu items in %.3f s -> %.2f M ops/s (%.1f ns/op)\n",
        label, num_producers, num_consumers, total, secs, mops, ns_per_op);
}

template <template <typename> class Q>
static void run_all_benches(const char* label) {
    constexpr size_t N = 2'000'000;
    bench_mpmc<Q>(label, 1, 1, N);
    bench_mpmc<Q>(label, 1, 4, N);
    bench_mpmc<Q>(label, 4, 1, N);
    bench_mpmc<Q>(label, 2, 2, N);
    bench_mpmc<Q>(label, 4, 4, N);
    bench_mpmc<Q>(label, 8, 8, N);
}

int main() {
    run_all_tests<MPMC>("MPMC");
    // run_all_tests<MPMCV2>("MPMCV2");
    // run_all_tests<MPMCV3>("MPMCV3");

    std::puts("\n=== Benchmarks ===");
    run_all_benches<MPMC>("MPMC");
    // run_all_benches<MPMCV2>("MPMCV2");
    // run_all_benches<MPMCV3>("MPMCV3");

    std::puts("\nAll tests passed.");
    return 0;
}
