
#include <cstddef>
#include <vector>
#include <mutex>
#include <condition_variable>

template <typename T>
class SPSCQueue {
public:
    SPSCQueue(size_t size): queue(size) {
        if (size == 0) throw std::invalid_argument("size must be > 0");
    };
    void push(T item) {
        std::unique_lock<std::mutex> lock(mtx);

        auto isNotFull = [&] { return numItem < queue.size(); };
        notFull.wait(lock, isNotFull);

        queue[head] = item;
        numItem += 1;
        head = (head + 1) % queue.size();

        lock.unlock();
        notEmpty.notify_all();
    }

    T pop() {
        std::unique_lock<std::mutex> lock(mtx);
        auto isNotEmpty = [&] { return numItem > 0; };
        notEmpty.wait(lock, isNotEmpty);

        numItem -= 1;
        auto item = queue[tail];
        tail = (tail + 1) % queue.size();

        lock.unlock();
        notFull.notify_all();
        return item;
    }

private:
    // synchronization primatives
    std::mutex mtx;
    std::condition_variable notFull;
    std::condition_variable notEmpty;


    std::vector<T> queue;
    size_t numItem {0};
    size_t head {0};
    size_t tail {0};
};


