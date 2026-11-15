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
#include <cctype>
#include <unistd.h>
#include <csignal>
#include <atomic>
#include <thread>

#include <fcgio.h>
#include <json-c/json.h>
#include <systemd/sd-journal.h>

static const std::string &get_header(const std::map<std::string, std::string> &header, const std::string &key)
{
    static const std::string empty_str;
    auto it = header.find(key);
    return it != header.end() ? it->second : empty_str;
}

static bool is_valid_http_protocol(const std::string &proto)
{
    if (proto == "HTTP/1.1" || proto == "HTTP/1.0" ||
        proto == "HTTP/2" || proto == "HTTP/2.0" ||
        proto == "HTTP/3" || proto == "HTTP/3.0") {
        return true;
    }
    if (proto.rfind("HTTP/", 0) == 0 && proto.size() <= 10) {
        for (size_t i = 5; i < proto.size(); ++i) {
            char c = proto[i];
            if (c != '.' && !std::isdigit(static_cast<unsigned char>(c))) {
                return false;
            }
        }
        return true;
    }
    return false;
}

static void request_ws_system_logs(backend::wsworker *worker, const std::map<std::string, std::string> &header, __attribute__((unused)) const std::list<std::string> &uri_params)
{
    struct journal_deleter {
        void operator()(sd_journal *j) const noexcept {
            if (j) sd_journal_close(j);
        }
    };
    struct json_deleter {
        void operator()(json_object *j) const noexcept {
            if (j) json_object_put(j);
        }
    };

    sd_journal *raw_journal = nullptr;
    int r = sd_journal_open(&raw_journal, SD_JOURNAL_CURRENT_USER);
    if (r < 0 || raw_journal == nullptr) {
        throw std::runtime_error("Can't open journal!");
    }
    std::unique_ptr<sd_journal, journal_deleter> journal(raw_journal);

    std::unique_ptr<json_object, json_deleter> json(json_object_new_object());
    if (!json) {
        throw std::runtime_error("Failed to allocate JSON object");
    }
    auto json_data = json_object_new_array();
    if (!json_data) {
        throw std::runtime_error("Failed to allocate JSON array");
    }
    json_object_object_add(json.get(), "data", json_data);

    const char *msg = nullptr;
    size_t len = 0;
    int c = 0;
    if (sd_journal_seek_tail(journal.get()) < 0) {
        throw std::runtime_error("Can't seek to journal tail!");
    }
    while (sd_journal_previous(journal.get()) > 0)
    {
        r = sd_journal_get_data(journal.get(), "MESSAGE", reinterpret_cast<const void **>(&msg), &len);
        if (r == 0 && msg != nullptr) {
            // "MESSAGE=" prefix is 8 chars
            if (len >= 8) {
                json_object_array_add(json_data, json_object_new_string_len(reinterpret_cast<const char *>(msg + 8), static_cast<int>(len - 8)));
            } else {
                json_object_array_add(json_data, json_object_new_string(""));
            }
            if (++c >= 100) break;
        }
    }

    journal.reset();

    const char *json_str = json_object_get_string(json.get());
    std::string proto = get_header(header, "SERVER_PROTOCOL");
    const char *server_protocol = is_valid_http_protocol(proto) ? proto.c_str() : "HTTP/1.1";

    FCGX_FPrintF(worker->out(), "%s 200 OK\r\nStatus: 200 OK\r\nContent-type: application/json\r\nX-Content-Type-Options: nosniff\r\nContent-Length: %d\r\n\r\n%s",
                 server_protocol, static_cast<int>(std::strlen(json_str)), json_str);
}

static constexpr auto DEFAULT_SOCKET_NAME = ":9001";
static constexpr auto DEFAULT_BACKLOG = 100;

int main(int argc, char *argv[])
{
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

    sigset_t sig_mask;
    sigemptyset(&sig_mask);
    sigaddset(&sig_mask, SIGINT);
    sigaddset(&sig_mask, SIGTERM);
    sigaddset(&sig_mask, SIGUSR1);
    pthread_sigmask(SIG_BLOCK, &sig_mask, nullptr);

    backend::wsserver server;

    server.add_routes({
        {"/ws/system/logs", backend::GET | backend::POST, request_ws_system_logs},
    });

    server.init(socket_name.c_str(), backlog, worker_num);

    std::atomic<bool> stopping{false};
    std::thread sig_thread([&server, &stopping, sig_mask]() {
        int sig = 0;
        if (sigwait(&sig_mask, &sig) == 0 && !stopping.load(std::memory_order_acquire)) {
            server.shutdown();
        }
    });

    struct sig_thread_guard {
        std::thread &t;
        std::atomic<bool> &stopping;
        ~sig_thread_guard() {
            if (t.joinable()) {
                stopping.store(true, std::memory_order_release);
                pthread_kill(t.native_handle(), SIGUSR1);
                t.join();
            }
        }
    } sig_guard{sig_thread, stopping};

    int ret = server.run();
    return ret;
}
