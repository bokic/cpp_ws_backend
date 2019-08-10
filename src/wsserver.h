#pragma once

#include "wsregex.h"

#include <iostream>
#include <string>
#include <list>
#include <map>


namespace backend {

enum methodType
{
    GET    = 1 << 0,
    PUT    = 1 << 1,
    PATCH  = 1 << 2,
    POST   = 1 << 3,
    DELETE = 1 << 4,
};

class wsserver
{
public:
    wsserver();
    virtual ~wsserver();
    void init(const char *socket_name, int backlog);
    int run();

private:
    std::streambuf *m_cin_streambuf = nullptr;
    std::streambuf *m_cout_streambuf = nullptr;
    std::streambuf *m_cerr_streambuf = nullptr;
    int m_sock_fd = 0;
};

struct route {
    wsregex uri;
    int method;
    void (* function)(std::map<std::string, std::string>, std::list<std::string>);
};

std::map<std::string, std::string> parse_args(const std::string &params);

};
