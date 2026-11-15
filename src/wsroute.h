#pragma once

#include "wsworker.h"
#include <regex>
#include <string_view>
#include <functional>
#include <list>
#include <vector>
#include <initializer_list>
#include <cstddef>
#include <utility>
#include <shared_mutex>
#include <mutex>


namespace backend {

enum methodType
{
    GET    = 1 << 0,
    PUT    = 1 << 1,
    PATCH  = 1 << 2,
    POST   = 1 << 3,
    DELETE = 1 << 4,
};

using route_handler = std::function<void(wsworker *worker, const std::map<std::string, std::string> &header, const std::list<std::string> &uri_params)>;
using middleware_handler = std::function<bool(wsworker *worker, const std::map<std::string, std::string> &header)>;

struct route {
    std::string pattern_str;
    bool is_exact = false;
    std::regex uri;
    int method;
    route_handler function;

    static bool contains_regex_meta(std::string_view s) noexcept {
        for (char c : s) {
            switch (c) {
                case '^': case '$': case '\\': case '.':
                case '*': case '+': case '?':
                case '(': case ')': case '[': case ']':
                case '{': case '}': case '|':
                    return true;
            }
        }
        return false;
    }

    route(std::string_view pattern, int method, route_handler handler,
          std::regex_constants::syntax_option_type flags = std::regex_constants::ECMAScript)
        : pattern_str(pattern),
          is_exact(!contains_regex_meta(pattern)),
          uri(pattern.data(), pattern.size(), flags),
          method(method),
          function(std::move(handler)) {}

    route(std::regex uri, int method, route_handler handler)
        : pattern_str(""),
          is_exact(false),
          uri(std::move(uri)),
          method(method),
          function(std::move(handler)) {}

    ~route() = default;
    route(const route &) = default;
    route &operator=(const route &) = default;
    route(route &&) noexcept = default;
    route &operator=(route &&) noexcept = default;
};

class router {
public:
    router() = default;
    ~router() = default;

    router(const router &other) {
        std::shared_lock<std::shared_mutex> lock(other.m_mutex);
        m_routes = other.m_routes;
        m_middlewares = other.m_middlewares;
    }

    router &operator=(const router &other) {
        if (this != &other) {
            std::unique_lock<std::shared_mutex> lock_this(m_mutex, std::defer_lock);
            std::shared_lock<std::shared_mutex> lock_other(other.m_mutex, std::defer_lock);
            std::lock(lock_this, lock_other);
            m_routes = other.m_routes;
            m_middlewares = other.m_middlewares;
        }
        return *this;
    }

    router(router &&other) noexcept {
        std::unique_lock<std::shared_mutex> lock(other.m_mutex);
        m_routes = std::move(other.m_routes);
        m_middlewares = std::move(other.m_middlewares);
    }

    router &operator=(router &&other) noexcept {
        if (this != &other) {
            std::unique_lock<std::shared_mutex> lock_this(m_mutex, std::defer_lock);
            std::unique_lock<std::shared_mutex> lock_other(other.m_mutex, std::defer_lock);
            std::lock(lock_this, lock_other);
            m_routes = std::move(other.m_routes);
            m_middlewares = std::move(other.m_middlewares);
        }
        return *this;
    }

    void add_route(route r) {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_routes.push_back(std::move(r));
    }

    void add_routes(const route *routes, size_t count) {
        if (routes && count > 0) {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            m_routes.insert(m_routes.end(), routes, routes + count);
        }
    }

    template <size_t N>
    void add_routes(const route (&routes)[N]) {
        add_routes(routes, N);
    }

    void add_routes(std::initializer_list<route> routes) {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_routes.insert(m_routes.end(), routes.begin(), routes.end());
    }

    const std::vector<route>& routes() const {
        return m_routes;
    }

    void use(middleware_handler mw) {
        if (mw) {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            m_middlewares.push_back(std::move(mw));
        }
    }

    const std::vector<middleware_handler>& middlewares() const {
        return m_middlewares;
    }

    std::shared_lock<std::shared_mutex> lock_shared() const {
        return std::shared_lock<std::shared_mutex>(m_mutex);
    }

private:
    mutable std::shared_mutex m_mutex;
    std::vector<route> m_routes;
    std::vector<middleware_handler> m_middlewares;
};

};
