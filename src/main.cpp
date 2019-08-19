#include "wsserver.h"
#include <string>
#include <cstdio>
#include <unistd.h>


using namespace std;

static constexpr auto DEFAULT_SOCKET_NAME = ":9000";
static constexpr auto DEFAULT_BACKLOG = 100;

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

    return server.run();
}
