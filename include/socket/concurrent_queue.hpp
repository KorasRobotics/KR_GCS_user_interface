/**
 * @file concurrent_queue.hpp
 * @author Inhwan Yoon (inhwan94@korea.ac.kr)
 * @brief  A class for temporarily storing data to be processed simultaneously
 * @details This class uses a queue, an STL container, to process communication data
 * accumulated in real time in the form of FIFO (First in, First Out).
 * @version 1.0
 * @date 2022-12-29
 *
 * @copyright Copyright (c) 2022
 *
 */

#ifndef CONCURRENT_QUEUE_HPP
#define CONCURRENT_QUEUE_HPP

#include <queue>

using namespace std;

namespace tcp_communication {

template<typename Data>
class ConcurrentQueue {
public:
    ConcurrentQueue() {}
    ConcurrentQueue(const ConcurrentQueue &&rhs) {}

public:
    void push(Data const &data) {
        queue_.push(data);
    }

    bool empty() const {
        return queue_.empty();
    }

    bool tryPop(Data &value) {
        if (queue_.empty()) {
            return false;
        }

        value = queue_.front();
        queue_.pop();
        return true;
    }

    bool clear() {
        std::queue<Data> empty_queue;
        queue_ = empty_queue;
        if (queue_.empty()) {
            return true;
        } else {
            return false;
        }
    }

private:
    queue<Data> queue_;
};
} // namespace tcp_comm
#endif
