#pragma once

#include "wsthreadpool.h"
#include "wsroute.h"
#include <memory>
#include <initializer_list>
#include <cstddef>
#include <atomic>
#include <functional>
#include <vector>
#include <mutex>


namespace backend {

class wsserver
{
public:
    wsserver();
    virtual ~wsserver();
    wsserver(const wsserver &) = delete;
    wsserver &operator=(const wsserver &) = delete;
    wsserver(wsserver &&) = delete;
    wsserver &operator=(wsserver &&) = delete;
    void init(const char *socket_name, int backlog, int workers);
    void shutdown();
    int run();

    void add_route(const route &r);
    void add_routes(const route *routes, size_t count);
    template <size_t N>
    void add_routes(const route (&routes)[N]) {
        add_routes(routes, N);
    }
    void add_routes(std::initializer_list<route> routes);

    void use(middleware_handler mw);

    std::shared_ptr<const router> get_router() const;

    void on_shutdown(std::function<void()> hook);

    template <typename Pool>
    void register_connection_pool(std::shared_ptr<Pool> pool) {
        if (pool) {
            on_shutdown([pool]() {
                pool->shutdown();
            });
        }
    }

private:
    std::shared_ptr<router> m_router;
    backend::wsthreadpool m_thread_pool;
    std::atomic<int> m_sock_fd{0};
    mutable std::mutex m_hooks_mutex;
    std::vector<std::function<void()>> m_shutdown_hooks;
};

};
