#include "wsserver.h"
#include <string>
#include <cstdio>
#include <list>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <memory.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>


using namespace std;

static constexpr int MIN_WORKERS = 1;
static constexpr int MAX_WORKERS = 32;
static constexpr auto DEFAULT_SOCKET_NAME = ":9000";
static constexpr auto DEFAULT_BACKLOG = 100;

static backend::wsserver *g_server = nullptr;
static bool g_terminated = false;

void signal_handler(int sig)
{
    if (sig == SIGINT)
    {
        g_terminated = true;

        if (g_server)
        {
            g_server->shutdown();
        }
    }
}

pid_t fork_wsserver(backend::wsserver& server, std::string& socket_name, int backlog)
{
    pid_t pid = fork();
    if (pid == 0)
    {
        server.init(socket_name.c_str(), backlog, 1);

        g_server = &server;

        exit(server.run());
    }

    return pid;
}

int get_cpu_threads()
{
    int ret = 0;

    DIR* dir = opendir("/sys/class/cpuid");
    if (dir == nullptr)
    {
        return 1;
    }

    struct dirent *entry;

    while(entry = readdir(dir))
    {
        if (memcmp(entry->d_name, "cpu", 3) == 0)
            ret++;
    }

    closedir(dir);

    if (ret < 1) ret = 1; // Just in case

    return ret;
}

int main(int argc, char *argv[])
{
    list<pid_t> childs;
    backend::wsserver server;
    string socket_name = DEFAULT_SOCKET_NAME;
    int backlog = DEFAULT_BACKLOG;
    int worker_num = get_cpu_threads() + 1; // Ideally we want to use all CPU threads plus one for IO
    int opt = 0;

    signal(SIGINT, signal_handler);

    while((opt = getopt(argc, argv, "hn:bw:")) != -1)
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
            if (worker_num < MIN_WORKERS)  worker_num = MIN_WORKERS;
            if (worker_num > MAX_WORKERS) worker_num = MAX_WORKERS;
            break;
        case 'h':
            cerr << "Usage: " << argv[0] << " [-n socket name] [-b backlog] [-w number of workers]" << endl;
            return 0;
        }
    }

    do {
        while(childs.size() < (unsigned)worker_num)
        {
            pid_t pid = fork_wsserver(server, socket_name, backlog);
            childs.push_back(pid);
        }

        pid_t pid = wait(nullptr);
        childs.remove(pid);
    } while(!g_terminated);


    while(!childs.empty())
    {
        pid_t pid = wait(nullptr);
        childs.remove(pid);
    }

    return 0;
}
