// usage.cpp
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include "mpmc.cpp"  // wherever your MPMC class lives

int main() {
    MPMC<int> queue(8);  // small capacity to exercise the blocking paths

    constexpr int items_per_producer = 1000;
    constexpr int num_producers = 2;
    constexpr int num_consumers = 2;
    constexpr int total_items = items_per_producer * num_producers;

    std::atomic<int> consumed_count{0};
    std::atomic<long long> consumed_sum{0};

    // Producers: each pushes items_per_producer integers
    std::vector<std::thread> producers;
    for (int p = 0; p < num_producers; ++p) {
        producers.emplace_back([&queue, p] {
            for (int i = 0; i < items_per_producer; ++i) {
                int value = p * items_per_producer + i;  // unique values across producers
                queue.push(value);
            }
            std::cout << "Producer " << p << " done\n";
        });
    }

    // Consumers: pop until they've collectively drained total_items
    std::vector<std::thread> consumers;
    for (int c = 0; c < num_consumers; ++c) {
        consumers.emplace_back([&, c] {
            int local_count = 0;
            while (true) {
                int idx = consumed_count.fetch_add(1, std::memory_order_relaxed);
                if (idx >= total_items) {
                    consumed_count.fetch_sub(1, std::memory_order_relaxed);
                    break;  // someone else will drain the rest
                }
                int v = queue.pop();
                consumed_sum.fetch_add(v, std::memory_order_relaxed);
                ++local_count;
            }
            std::cout << "Consumer " << c << " consumed " << local_count << " items\n";
        });
    }

    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();

    // Verify: sum of 0..(total_items-1) should equal total_items*(total_items-1)/2
    long long expected = static_cast<long long>(total_items) * (total_items - 1) / 2;
    std::cout << "Consumed sum: " << consumed_sum.load() << "\n";
    std::cout << "Expected:     " << expected << "\n";
    std::cout << (consumed_sum.load() == expected ? "PASS\n" : "FAIL\n");

    return 0;
}