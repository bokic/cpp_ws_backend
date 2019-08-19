#include "wsthreadpool.h"
#include "wsworker.h"
#include <thread>

#include <fcgio.h>


// https://stackoverflow.com/questions/15752659/thread-pooling-in-c11

const int MAX_WORKERS = 512;

using namespace std;

backend::wsthreadpool::~wsthreadpool()
{
    if (m_started)
    {
        {
            std::unique_lock<std::mutex> lock(m_lock);
            m_finished = true;
        }

        m_data_condition.notify_all();
    }
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

    if (m_workers == 0)
    {
        m_workers = std::thread::hardware_concurrency();
    }
    else if (m_workers > MAX_WORKERS)
    {
        m_workers = MAX_WORKERS;
    }

    for (unsigned int i = 0; i < m_workers; i++)
    {
        m_thread_pool.push_back(std::thread(&wsthreadpool::worker, this));
    }
}

void backend::wsthreadpool::addWork(shared_ptr<FCGX_Request> request)
{
    {
        std::unique_lock<std::mutex> lock(m_lock);
        m_work_queue.push(request);
    }

    m_data_condition.notify_one();
}

void backend::wsthreadpool::worker()
{
    backend::wsworker worker;

    while (true)
    {
        shared_ptr<FCGX_Request> request;

        {
            std::unique_lock<std::mutex> lock(m_lock);
            m_data_condition.wait(lock);

            if ((m_finished)&&(m_work_queue.empty()))
                return;

            request = m_work_queue.front();
            m_work_queue.pop();
        }

        worker.process(request);
    }
}
