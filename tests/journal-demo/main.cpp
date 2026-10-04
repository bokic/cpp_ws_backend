#include "wsserver.h"
#include "wsworker.h"
#include "wsroute.h"

#include <iostream>
#include <map>
#include <list>
#include <string>
#include <cstring>
#include <cstdlib>
#include <stdexcept>
#include <unistd.h>
#include <csignal>
#include <atomic>

#include <fcgio.h>
#include <json-c/json.h>
#include <systemd/sd-journal.h>

static const std::string &get_header(const std::map<std::string, std::string> &header, const std::string &key)
{
    static const std::string empty_str;
    auto it = header.find(key);
    return it != header.end() ? it->second : empty_str;
}

static void request_ws_system_logs(backend::wsworker *worker, const std::map<std::string, std::string> &header, __attribute__((unused)) const std::list<std::string> &uri_params)
{
    sd_journal *journal = nullptr;
    const char *msg = nullptr;
    size_t len = 0;

    int r = sd_journal_open(&journal, SD_JOURNAL_CURRENT_USER);
    if (r < 0 || journal == nullptr) {
        throw std::runtime_error("Can't open journal!");
    }

    auto json = json_object_new_object();
    auto json_data = json_object_new_array();
    json_object_object_add(json, "data", json_data);

    int c = 1;
    while (sd_journal_next(journal) > 0)
    {
        r = sd_journal_get_data(journal, "MESSAGE", reinterpret_cast<const void **>(&msg), &len);
        if (r == 0 && msg != nullptr) {
            // "MESSAGE=" prefix is 8 chars
            if (len >= 8) {
                json_object_array_add(json_data, json_object_new_string(reinterpret_cast<const char *>(msg + 8)));
            } else {
                json_object_array_add(json_data, json_object_new_string(""));
            }
        }

        if (c >= 100) break;
        c++;
    }

    sd_journal_close(journal);
    journal = nullptr;

    const char *json_str = json_object_get_string(json);
    const std::string &proto = get_header(header, "SERVER_PROTOCOL");
    const char *server_protocol = proto.empty() ? "HTTP/1.1" : proto.c_str();

    FCGX_FPrintF(worker->out(), "%s 200 OK\r\nContent-type: application/json\r\nX-Content-Type-Options: nosniff\r\nContent-Length: %d\r\n\r\n%s",
                 server_protocol, static_cast<int>(std::strlen(json_str)), json_str);

    json_object_put(json);
}

static constexpr auto DEFAULT_SOCKET_NAME = ":9001";
static constexpr auto DEFAULT_BACKLOG = 100;

static std::atomic<backend::wsserver*> g_server{nullptr};

static void signal_handler(int sig)
{
    (void)sig;
    backend::wsserver *server = g_server.load();
    if (server)
    {
        server->shutdown();
    }
}

int main(int argc, char *argv[])
{
    backend::wsserver server;
    g_server.store(&server);

    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGUSR1, &sa, nullptr);

    server.add_routes({
        {"/ws/system/logs", backend::GET | backend::POST, request_ws_system_logs},
    });

    std::string socket_name = DEFAULT_SOCKET_NAME;
    int backlog = DEFAULT_BACKLOG;
    int worker_num = 0;
    int opt = 0;

    while ((opt = getopt(argc, argv, "hn:b:w:")) != -1)
    {
        switch (opt)
        {
        case 'n':
            socket_name = optarg;
            break;
        case 'b':
            backlog = std::atoi(optarg);
            break;
        case 'w':
            worker_num = std::atoi(optarg);
            break;
        case 'h':
            std::cerr << "Usage: " << argv[0] << " [-n socket name] [-b backlog] [-w number of workers]" << std::endl;
            return 0;
        default:
            std::cerr << "Usage: " << argv[0] << " [-n socket name] [-b backlog] [-w number of workers]" << std::endl;
            return 1;
        }
    }

    server.init(socket_name.c_str(), backlog, worker_num);

    int ret = server.run();
    g_server.store(nullptr);
    return ret;
}
