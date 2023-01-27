#include "wsserver.h"
#include <string>
#include <cstdio>
#include <unistd.h>
#include <signal.h>


using namespace std;

static constexpr auto DEFAULT_SOCKET_NAME = ":9000";
static constexpr auto DEFAULT_BACKLOG = 100;

static backend::wsserver *g_server = nullptr;

int main(int argc, char *argv[])
{
    backend::wsserver server;
    string socket_name = DEFAULT_SOCKET_NAME;
    int backlog = DEFAULT_BACKLOG;
    int worker_num = 0;
    int opt = 0;

    while((opt = getopt(argc, argv, "hn:b:")) != -1)
    {
        switch(opt)
        {
        case 'n':
            socket_name = optarg;
            break;
        case 'b':
            backlog = atoi(optarg);
            break;
        case 'w':
            worker_num = atoi(optarg);
            break;
        case 'h':
            cerr << "Usage: " << argv[0] << " [-n socket name] [-b backlog] [-w number of workers]" << endl;
            return 0;
        }
    }

    server.init(socket_name.c_str(), backlog, worker_num);

    g_server = &server;

    return server.run();
}
