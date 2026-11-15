#include "wsthreadpool.h"
#include "wsworker.h"
#include <thread>

#include <fcgio.h>


// https://stackoverflow.com/questions/15752659/thread-pooling-in-c11

const int MAX_WORKERS = 512;

void backend::wsthreadpool::stop()
{
    if (m_started)
    {
        {
            std::unique_lock<std::mutex> lock(m_lock);
            m_finished = true;
        }

        m_data_condition.notify_all();

        for (auto &t : m_thread_pool)
        {
            if (t.joinable())
            {
                t.join();
            }
        }
        m_thread_pool.clear();
        m_started = false;
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

    m_workers = static_cast<unsigned int>(workers);
}

void backend::wsthreadpool::start()
{
    if (m_started)
        return;

    if (m_workers > MAX_WORKERS)
    {
        m_workers = MAX_WORKERS;
    }

    m_started = true;

    for (unsigned int i = 0; i < m_workers; i++)
    {
        m_thread_pool.push_back(std::thread(&wsthreadpool::worker, this));
    }
}

void backend::wsthreadpool::setRouter(std::shared_ptr<const router> router)
{
    m_router = std::move(router);
}

size_t backend::wsthreadpool::queueSize() const
{
    std::unique_lock<std::mutex> lock(const_cast<std::mutex&>(m_lock));
    return m_work_queue.size();
}

void backend::wsthreadpool::addWork(std::shared_ptr<FCGX_Request> request)
{
    {
        std::unique_lock<std::mutex> lock(m_lock);
        if (m_finished)
            return;

        if (m_max_queue_size > 0 && m_work_queue.size() >= m_max_queue_size)
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
    backend::wsworker worker(m_router);

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
    return m_workers;
}
