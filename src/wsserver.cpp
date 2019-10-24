#include "wsserver.h"

#include <sstream>
#include <string>
#include <list>
#include <map>

#include <fcgio.h>

#include <unistd.h>


using namespace std;

backend::wsserver::wsserver()
{
    FCGX_Init();
}

backend::wsserver::~wsserver()
{
    shutdown();
}

void backend::wsserver::init(const char *socket_name, int backlog, int workers)
{
    m_sock_fd = FCGX_OpenSocket(socket_name, backlog);
    m_thread_pool.setWorkers(workers);
}

void backend::wsserver::shutdown()
{
    if (m_sock_fd)
    {
        close(m_sock_fd);
    }
}

int backend::wsserver::run()
{
    int ret = 0;

    //m_db_connection_pool.start();
    m_thread_pool.start();

    while(1)
    {
        auto request = make_shared<FCGX_Request>();

        if (FCGX_InitRequest(request.get(), m_sock_fd, 0))
        {
            ret = 1;
            break;
        }

        if (FCGX_Accept_r(request.get()))
            break;

        m_thread_pool.addWork(request);
    }

    return ret;
}
