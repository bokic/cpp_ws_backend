#pragma once

#include "wsthreadpool.h"


namespace backend {

class wsserver
{
public:
    wsserver();
    void init(const char *socket_name, int backlog, int workers);
    int run();

private:
    backend::wsthreadpool m_thread_pool;
    int m_sock_fd = 0;
};

};
