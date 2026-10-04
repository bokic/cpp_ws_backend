#include "wsserver.h"
#include "wsworker.h"

#include <fcgio.h>

#include <unistd.h>


backend::wsserver::wsserver()
    : m_router(std::make_shared<router>())
{
    FCGX_Init();
}

backend::wsserver::~wsserver()
{
    shutdown();
}

void backend::wsserver::init(const char *socket_name, int backlog, int workers)
{
    m_sock_fd = FCGX_OpenSocket(socket_name, backlog);
    m_thread_pool.setWorkers(workers);
}

void backend::wsserver::shutdown()
{
    if (m_sock_fd)
    {
        close(m_sock_fd);
    }
}

void backend::wsserver::add_route(const route &r)
{
    m_router->add_route(r);
}

void backend::wsserver::add_routes(const route *routes, size_t count)
{
    m_router->add_routes(routes, count);
}

void backend::wsserver::add_routes(std::initializer_list<route> routes)
{
    m_router->add_routes(routes);
}

std::shared_ptr<const backend::router> backend::wsserver::get_router() const
{
    return m_router;
}

int backend::wsserver::run()
{
    int ret = 0;

    m_thread_pool.setRouter(m_router);

    if (m_thread_pool.count() == 0)
    {
        while(1)
        {
            auto request = std::make_shared<FCGX_Request>();
            backend::wsworker worker(m_router);

            if (FCGX_InitRequest(request.get(), m_sock_fd, 0))
            {
                ret = 1;
                break;
            }

            if (FCGX_Accept_r(request.get()))
                break;

            worker.process(request);
        }
    }
    else
    {
        m_thread_pool.start();

        while(1)
        {
            auto request = std::make_shared<FCGX_Request>();

            if (FCGX_InitRequest(request.get(), m_sock_fd, 0))
            {
                ret = 1;
                break;
            }

            if (FCGX_Accept_r(request.get()))
                break;

            m_thread_pool.addWork(request);
        }
    }

    return ret;
}
