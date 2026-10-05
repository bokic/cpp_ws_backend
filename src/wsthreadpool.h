#pragma once

#include <condition_variable>
#include <memory>
#include <atomic>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>

#include <fcgio.h>


namespace backend {

class router;

class wsthreadpool
{
public:
    wsthreadpool() = default;
    ~wsthreadpool();
    wsthreadpool(const wsthreadpool &) = delete;
    wsthreadpool &operator=(const wsthreadpool &) = delete;
    wsthreadpool(wsthreadpool &&) = delete;
    wsthreadpool &operator=(wsthreadpool &&) = delete;
    void setWorkers(int workers);
    void setRouter(std::shared_ptr<const router> router);
    void start();
    void addWork(std::shared_ptr<FCGX_Request> request);
    unsigned int count() const;

private:
    void worker();
    std::shared_ptr<const router> m_router;
    std::vector<std::thread> m_thread_pool;
    std::queue<std::shared_ptr<FCGX_Request>> m_work_queue;
    std::condition_variable m_data_condition;
    std::atomic<bool> m_finished{false};
    std::mutex m_lock;
    unsigned int m_workers = 0;
    bool m_started = false;
};

};
