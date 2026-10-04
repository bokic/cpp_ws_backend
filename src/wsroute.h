#pragma once

#include "wsworker.h"
#include "wsregex.h"
#include <list>
#include <vector>
#include <initializer_list>
#include <cstddef>


namespace backend {

enum methodType
{
    GET    = 1 << 0,
    PUT    = 1 << 1,
    PATCH  = 1 << 2,
    POST   = 1 << 3,
    DELETE = 1 << 4,
};

struct route {
    wsregex uri;
    int method;
    void (* function)(wsworker *worker, const std::map<std::string, std::string> &header, const std::list<std::string> &uri_params);
};

class router {
public:
    router() = default;

    void add_route(const route &r) {
        m_routes.push_back(r);
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
