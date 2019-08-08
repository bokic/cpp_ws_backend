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
        case 'h':
            cerr << "Usage: " << argv[0] << " [-n socket_name] [-b backlog]" << endl;
            return 0;
        }
    }

    server.init(socket_name.c_str(), backlog);

    return server.run();
}
