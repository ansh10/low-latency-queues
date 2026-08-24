#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <thread>
#include <vector>

// Lock-free single-producer / single-consumer ring buffer. Producer owns
// `head`, consumer owns `tail`; the cross-thread handoff goes through
// release/acquire on those atomics. The ring uses one extra slot (slots =
// size + 1) so that `head == tail` means empty and `(head+1) % slots == tail`
// means full — that wastes one slot internally but keeps the public capacity
// identical to SPSCQueue.
template <typename T>
class SPSCQueueV3 {
public:
    SPSCQueueV3(size_t size): queue(size + 1), slots(size + 1) {
        if (size == 0) throw std::invalid_argument("size must be > 0");
    }

    void push(T item) {
        const size_t h = head.load();
        const size_t next = (h + 1) % slots;
        while (next == tail.load()) {
            std::this_thread::yield();
        }
        queue[h] = std::move(item);
        head.store(next);
    }

    T pop() {
        const size_t t = tail.load();
        while (t == head.load()) {
            std::this_thread::yield();
        }
        T item = std::move(queue[t]);
        tail.store((t + 1) % slots);
        return item;
    }

private:
    std::vector<T> queue;
    size_t slots;
    std::atomic<size_t> head{0};
    std::atomic<size_t> tail{0};
};
