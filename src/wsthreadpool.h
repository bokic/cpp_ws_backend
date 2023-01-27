#pragma once

#include <condition_variable>
#include <functional>
#include <memory>
#include <atomic>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>

#include <fcgio.h>


namespace backend {

class wsthreadpool
{
public:
    ~wsthreadpool();
    void setWorkers(int workers);
    void start();
    void addWork(std::shared_ptr<FCGX_Request> request);
    unsigned int count() const;

private:
    void worker();
    std::vector<std::thread> m_thread_pool;
    std::queue<std::shared_ptr<FCGX_Request>> m_work_queue;
    std::condition_variable m_data_condition;
    std::atomic<bool> m_finished{false};
    std::mutex m_lock;
    unsigned int m_workers = 0;
    bool m_started = false;
};

};
