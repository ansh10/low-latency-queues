#include <atomic>
#include <bit>
#include <cstddef>
#include <stdexcept>
#include <thread>
#include <vector>

template <typename T>
class alignas(64) SPSCQueueV6 {
public:
    SPSCQueueV6(size_t size)
        : queue(validateAndRound(size)),
          slots(queue.size()),
          mask(slots - 1) {}

    void push(T item) {
        const size_t h = head.load(std::memory_order_relaxed);
        const size_t next = (h + 1) & mask;

        if (next == cachedTail) {
            cachedTail = tail.load(std::memory_order_acquire);
            while (next == cachedTail) {
                std::this_thread::yield();
                cachedTail = tail.load(std::memory_order_acquire);
            }
        }

        queue[h] = std::move(item);
        head.store(next, std::memory_order_release);
    }

    T pop() {
        const size_t t = tail.load(std::memory_order_relaxed);

        if (t == cachedHead) {
            cachedHead = head.load(std::memory_order_acquire);
            while (t == cachedHead) {
                std::this_thread::yield();
                cachedHead = head.load(std::memory_order_acquire);
            }
        }

        T item = std::move(queue[t]);
        tail.store((t + 1) & mask, std::memory_order_release);
        return item;
    }

private:
    static size_t validateAndRound(size_t s) {
        if (s == 0) throw std::invalid_argument("size must be > 0");
        return std::bit_ceil(s + 1);  // +1 because masked indices waste one slot
    }

    std::vector<T> queue;
    const size_t slots;
    const size_t mask;

    alignas(64) std::atomic<size_t> head{0};
    size_t cachedTail = 0;

    alignas(64) std::atomic<size_t> tail{0};
    size_t cachedHead = 0;

    char pad[64 - ((sizeof(std::atomic<size_t>) + sizeof(size_t)) % 64)];
};