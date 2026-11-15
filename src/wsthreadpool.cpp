#include "wsthreadpool.h"
#include "wsworker.h"
#include <thread>

#include <fcgio.h>


// https://stackoverflow.com/questions/15752659/thread-pooling-in-c11

const int MAX_WORKERS = 512;

void backend::wsthreadpool::stop()
{
    std::vector<std::thread> threads;
    {
        std::unique_lock<std::mutex> lock(m_lock);
        if (!m_started)
            return;
        m_started = false;
        m_finished = true;
        threads = std::move(m_thread_pool);
    }

    m_data_condition.notify_all();

    for (auto &t : threads)
    {
        if (t.joinable())
        {
            t.join();
        }
    }
}

backend::wsthreadpool::~wsthreadpool()
{
    stop();
}

void backend::wsthreadpool::setWorkers(int workers)
{
    if (workers < 0)
    {
        workers = 0;
    }
    else if (workers > MAX_WORKERS)
    {
        workers = MAX_WORKERS;
    }

    m_workers.store(static_cast<unsigned int>(workers), std::memory_order_relaxed);
}

void backend::wsthreadpool::start()
{
    std::unique_lock<std::mutex> lock(m_lock);
    if (m_started)
        return;

    unsigned int workers = m_workers.load(std::memory_order_relaxed);
    if (workers > MAX_WORKERS)
    {
        workers = MAX_WORKERS;
        m_workers.store(workers, std::memory_order_relaxed);
    }

    m_finished = false;
    m_started = true;

    for (unsigned int i = 0; i < workers; i++)
    {
        m_thread_pool.push_back(std::thread(&wsthreadpool::worker, this));
    }
}

void backend::wsthreadpool::setRouter(std::shared_ptr<const router> router)
{
    std::unique_lock<std::mutex> lock(m_lock);
    m_router = std::move(router);
}

size_t backend::wsthreadpool::queueSize() const
{
    std::unique_lock<std::mutex> lock(m_lock);
    return m_work_queue.size();
}

void backend::wsthreadpool::addWork(std::shared_ptr<FCGX_Request> request)
{
    {
        std::unique_lock<std::mutex> lock(m_lock);
        if (m_finished)
            return;

        size_t max_q = m_max_queue_size.load(std::memory_order_relaxed);
        if (max_q > 0 && m_work_queue.size() >= max_q)
        {
            lock.unlock();
            if (request && request->out)
            {
                FCGX_FPrintF(request->out, "Status: 503 Service Unavailable\r\nRetry-After: 1\r\n\r\nServer Busy");
                FCGX_Finish_r(request.get());
            }
            return;
        }

        m_work_queue.push(request);
    }

    m_data_condition.notify_one();
}

void backend::wsthreadpool::worker()
{
    std::shared_ptr<const router> router_ref;
    {
        std::unique_lock<std::mutex> lock(m_lock);
        router_ref = m_router;
    }
    backend::wsworker worker(std::move(router_ref));

    while (true)
    {
        std::shared_ptr<FCGX_Request> request;

        {
            std::unique_lock<std::mutex> lock(m_lock);
            m_data_condition.wait(lock, [this] {
                return m_finished || !m_work_queue.empty();
            });

            if ((m_finished)&&(m_work_queue.empty()))
                break;

            request = m_work_queue.front();
            m_work_queue.pop();
        }

        worker.process(request);
    }
}

unsigned int backend::wsthreadpool::count() const
{
    return m_workers.load(std::memory_order_relaxed);
}
