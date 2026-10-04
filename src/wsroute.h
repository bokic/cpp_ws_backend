#pragma once

#include "wsworker.h"
#include "wsregex.h"
#include <list>


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

};
