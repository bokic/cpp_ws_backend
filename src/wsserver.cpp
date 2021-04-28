#include "wsserver.h"

#include <sstream>
#include <string>
#include <list>
#include <map>

#include <fcgio.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
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
    reuseport(socket_name);

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

void backend::wsserver::reuseport(const char *socket_name)
{
    //reuseport
    if ((socket_name)&&(strlen(socket_name) > 0))
    {
        int enable = 1;
        int err = 0;
        int fd = 0;

        if(socket_name[0] == ':')
        {
            // use local TCP port
            fd = socket(AF_INET, SOCK_STREAM, 0);
            if (fd <= 0)
            {
                fprintf(stderr, "reuseport sock creation failed!");
                return;
            }

            err = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
            if (err)
            {
                fprintf(stderr, "reuseport sock setsockopt failed!");
                close(fd);
                return;
            }

            struct sockaddr_in serv_addr;
            serv_addr.sin_family = AF_INET;
            serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
            serv_addr.sin_port = htons(9000);
            err = bind(fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
            if (err)
            {
                fprintf(stderr, "reuseport sock bind failed!");
                close(fd);
                return;
            }
        }
        else
        {
            // use UNIX domain socket

        }
    }
}
