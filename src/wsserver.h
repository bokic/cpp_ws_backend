#pragma once

#include "wsdbconnectionpool.h"
#include "wsthreadpool.h"


namespace backend {

class wsserver
{
public:
    wsserver();
    virtual ~wsserver();
    void init(const char *socket_name, int backlog, int workers);
    void shutdown();
    int run();

private:
    backend::wsdbconnectionpool m_db_connection_pool;
    backend::wsthreadpool m_thread_pool;
    int m_sock_fd = 0;
};

};
