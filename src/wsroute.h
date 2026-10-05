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

struct route {
    std::regex uri;
    int method;
    route_handler function;

    route(std::string_view pattern, int method, route_handler handler,
          std::regex_constants::syntax_option_type flags = std::regex_constants::ECMAScript)
        : uri(pattern.data(), pattern.size(), flags), method(method), function(std::move(handler)) {}

    route(std::regex uri, int method, route_handler handler)
        : uri(std::move(uri)), method(method), function(std::move(handler)) {}

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
    router(const router &) = default;
    router &operator=(const router &) = default;
    router(router &&) noexcept = default;
    router &operator=(router &&) noexcept = default;

    void add_route(route r) {
        m_routes.push_back(std::move(r));
    }

    void add_routes(const route *routes, size_t count) {
        if (routes && count > 0) {
            m_routes.insert(m_routes.end(), routes, routes + count);
        }
    }

    template <size_t N>
    void add_routes(const route (&routes)[N]) {
        add_routes(routes, N);
    }

    void add_routes(std::initializer_list<route> routes) {
        m_routes.insert(m_routes.end(), routes.begin(), routes.end());
    }

    const std::vector<route>& routes() const {
        return m_routes;
    }

private:
    std::vector<route> m_routes;
};

};
