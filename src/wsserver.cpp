#include "wsserver.h"
#include "wsworker.h"

#include <fcgio.h>

#include <unistd.h>
#include <system_error>
#include <cerrno>


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
    if (socket_name)
    {
        m_socket_path = socket_name;
    }
    int fd = FCGX_OpenSocket(socket_name, backlog);
    if (fd < 0)
    {
        throw std::system_error(errno, std::generic_category(),
            std::string("Failed to open FastCGI socket: ") + (socket_name ? socket_name : "(null)"));
    }
    m_sock_fd = fd;
    m_thread_pool.setWorkers(workers);
}

void backend::wsserver::shutdown()
{
    FCGX_ShutdownPending();
    int fd = m_sock_fd.exchange(0);
    if (fd > 0)
    {
        close(fd);
    }

    if (!m_socket_path.empty())
    {
        if (m_socket_path.find('/') != std::string::npos || m_socket_path.find(':') == std::string::npos)
        {
            unlink(m_socket_path.c_str());
        }
        m_socket_path.clear();
    }

    m_thread_pool.stop();

    std::vector<std::function<void()>> hooks;
    {
        std::lock_guard<std::mutex> lock(m_hooks_mutex);
        hooks = std::move(m_shutdown_hooks);
    }
    for (auto &hook : hooks)
    {
        if (hook)
        {
            try
            {
                hook();
            }
            catch (...)
            {
            }
        }
    }
}

void backend::wsserver::on_shutdown(std::function<void()> hook)
{
    if (hook)
    {
        std::lock_guard<std::mutex> lock(m_hooks_mutex);
        m_shutdown_hooks.push_back(std::move(hook));
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

void backend::wsserver::use(middleware_handler mw)
{
    m_router->use(std::move(mw));
}

std::shared_ptr<const backend::router> backend::wsserver::get_router() const
{
    return m_router;
}

static std::shared_ptr<FCGX_Request> make_fcgx_request()
{
    return std::shared_ptr<FCGX_Request>(new FCGX_Request{}, [](FCGX_Request *r) {
        if (r) {
            FCGX_Free(r, 1);
            delete r;
        }
    });
}

int backend::wsserver::run()
{
    int ret = 0;

    m_thread_pool.setRouter(m_router);

    if (m_thread_pool.count() == 0)
    {
        while(1)
        {
            auto request = make_fcgx_request();
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
            auto request = make_fcgx_request();

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
