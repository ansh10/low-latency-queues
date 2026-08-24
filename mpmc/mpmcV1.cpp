#include <cstddef>
#include <vector>

// empty when head == tail
// full when head + 1 == tail

template <typename T>
class MPMCV1 {
public:
    MPMCV1(size_t size): cap(size + 1), queue(size + 1) {};

    void push(T item) {
        std::unique_lock<std::mutex> lock(mtx); 
        notFullCV.wait(lock, [this] {return !isFull(); });

        queue[head] = std::move(item);
        head = (head + 1) % cap;

        lock.unlock();
        notEmptyCV.notify_one();
    };

    T pop() {
        std::unique_lock<std::mutex> lock(mtx);
        notEmptyCV.wait(lock, [this] { return !isEmpty(); });
        
        T item = std::move(queue[tail]);
        tail = (tail + 1) % cap;
        
        lock.unlock();
        notFullCV.notify_one();
        return item;
    }

private: 
    size_t cap;
    std::vector<T> queue;
    size_t head {0};
    size_t tail {0};

    // sync prim
    std::mutex mtx;
    std::condition_variable notFullCV;
    std::condition_variable notEmptyCV;


    bool isFull() { return (head + 1) % cap == tail; };
    bool isEmpty() { return head == tail; };
};