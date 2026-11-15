#include "wsserver.h"
#include "wsworker.h"

#include <fcgio.h>

#include <unistd.h>
#include <sys/socket.h>
#include <system_error>
#include <cerrno>
#include <csignal>


backend::wsserver::wsserver()
    : m_router(std::make_shared<router>())
{
    std::signal(SIGPIPE, SIG_IGN);
    FCGX_Init();
}

backend::wsserver::~wsserver()
{
    shutdown();
}

void backend::wsserver::init(const char *socket_name, int backlog, int workers)
{
    {
        std::lock_guard<std::mutex> lock(m_hooks_mutex);
        m_socket_path = socket_name ? socket_name : "";
    }
    int fd = FCGX_OpenSocket(socket_name, backlog);
    if (fd < 0)
    {
        throw std::system_error(errno, std::generic_category(),
            std::string("Failed to open FastCGI socket: ") + (socket_name ? socket_name : "(null)"));
    }
    m_sock_fd = fd;
    m_shutdown_requested.store(false, std::memory_order_release);
    m_thread_pool.setWorkers(workers);
}

void backend::wsserver::shutdown()
{
    m_shutdown_requested.store(true, std::memory_order_release);
    FCGX_ShutdownPending();

    int current_fd = m_sock_fd.load(std::memory_order_acquire);
    if (current_fd >= 0)
    {
        ::shutdown(current_fd, SHUT_RDWR);
    }

    {
        std::unique_lock<std::mutex> lock(m_hooks_mutex);
        if (m_running && m_run_thread_id != std::this_thread::get_id())
        {
            m_run_cv.wait(lock, [this]() { return !m_running; });
        }
    }

    int fd = m_sock_fd.exchange(-1);
    if (fd >= 0)
    {
        close(fd);
    }

    std::string socket_path;
    {
        std::lock_guard<std::mutex> lock(m_hooks_mutex);
        socket_path = std::move(m_socket_path);
        m_socket_path.clear();
    }
    if (!socket_path.empty())
    {
        if (socket_path.find('/') != std::string::npos || socket_path.find(':') == std::string::npos)
        {
            unlink(socket_path.c_str());
        }
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
    int sock_fd = m_sock_fd.load(std::memory_order_acquire);
    if (sock_fd < 0)
    {
        return 1;
    }

    {
        std::lock_guard<std::mutex> lock(m_hooks_mutex);
        m_running = true;
        m_run_thread_id = std::this_thread::get_id();
    }

    struct run_guard {
        wsserver *self;
        ~run_guard() {
            {
                std::lock_guard<std::mutex> lock(self->m_hooks_mutex);
                self->m_running = false;
                self->m_run_thread_id = std::thread::id{};
            }
            self->m_run_cv.notify_all();
        }
    } guard{this};

    int ret = 0;

    m_thread_pool.setRouter(m_router);

    if (m_thread_pool.count() == 0)
    {
        while (!m_shutdown_requested.load(std::memory_order_acquire))
        {
            auto request = make_fcgx_request();
            backend::wsworker worker(m_router);

            if (FCGX_InitRequest(request.get(), sock_fd, FCGI_FAIL_ACCEPT_ON_INTR))
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

        while (!m_shutdown_requested.load(std::memory_order_acquire))
        {
            auto request = make_fcgx_request();

            if (FCGX_InitRequest(request.get(), sock_fd, FCGI_FAIL_ACCEPT_ON_INTR))
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
