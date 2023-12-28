/**
 * @file message_manager.hpp
 * @author Inhwan Yoon (inhwan94@korea.ac.kr)
 * @brief : Processes messages sent/received through socket communication.
 * @version 1.0
 * @date 2022-12-29
 *
 * @copyright Copyright (c) 2022
 *
 */

#ifndef MESSAGE_MANAGER_HPP
#define MESSAGE_MANAGER_HPP

#include <vector>
#include <algorithm>
#include <unordered_map>

#include "concurrent_queue.hpp"

using namespace std;

namespace tcp_communication {

template<typename Data>
class MessageHandler {
public:
    bool createClientQueue(uint32_t id) {
        if (to_client_queue_map_.find(id) != to_client_queue_map_.end()) {
            return false;
        }

        to_client_queue_map_.insert(make_pair(id, ConcurrentQueue<Data>()));

        return true;
    }

    bool deleteClientQueue(uint32_t id) {
        auto itr = to_client_queue_map_.find(id);

        if (itr == to_client_queue_map_.end()) {
            return false;
        }

        to_client_queue_map_.erase(itr);

        return true;
    }

    void pushToWorkerQueue(Data const &data) {
        to_worker_queue_.push(data);
    }

    bool tryPopFromWokerQueue(Data &data) {
        if (to_worker_queue_.empty()) {
            return false;
        }
        return to_worker_queue_.tryPop(data);
    }

    void pushToAllClientQueue(Data const &data) {
        auto ids = getAllClientId();

        for (auto id : ids) {
            pushToClientQueue(id, data);
        }
    }

    bool pushToClientQueue(uint32_t id, Data const &data) {
        auto itr = to_client_queue_map_.find(id);

        if (itr == to_client_queue_map_.end()) {
            return false;
        }

        // original
         itr->second.push(data);

        // modified
//        if (itr->second.empty()) {
//            itr->second.push(data);
//        } else {
//            itr->second.clear();
//            itr->second.push(data);
//        }

        return true;
    }

    bool tryPopFromClientQueue(uint32_t id, Data &data) {
        auto itr = to_client_queue_map_.find(id);

        if (itr == to_client_queue_map_.end() || itr->second.empty()) {
            return false;
        }

        return itr->second.tryPop(data);
    }

    vector<uint32_t> getAllClientId() {
        vector<uint32_t> ids;
        for (auto itr = to_client_queue_map_.begin(); itr != to_client_queue_map_.end(); itr++) {
            ids.push_back(itr->first);
        }
        return ids;
    }

private:
    ConcurrentQueue<Data> to_worker_queue_;
    unordered_map<uint32_t, ConcurrentQueue<Data>> to_client_queue_map_;
};

template<typename Data>
class MessageManager : public MessageHandler<Data> {
private:
    MessageManager() {}
public:
    static MessageManager &getInstance() {
        static MessageManager *_instance = nullptr;
        if ( _instance == nullptr ) {
            _instance = new MessageManager();
        }
        return *_instance;
    }
};
} // namespace tcp_comm
#endif
