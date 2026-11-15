#include "wsserver.h"
#include "wsworker.h"
#include "wsroute.h"
#include "db/db.h"

#include <iostream>
#include <vector>
#include <regex>
#include <list>
#include <map>
#include <string>
#include <cstring>
#include <cstdlib>
#include <stdexcept>
#include <cctype>
#include <unistd.h>
#include <fcgio.h>
#include <csignal>
#include <atomic>
#include <thread>

static std::shared_ptr<backend::db::connection_pool<backend::db::sqlite_database>> g_db_pool;

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

static void send_http_headers(FCGX_Stream *out, const std::string &protocol)
{
    const char *proto = is_valid_http_protocol(protocol) ? protocol.c_str() : "HTTP/1.1";
    FCGX_FPrintF(out, "%s 200 OK\r\nStatus: 200 OK\r\nContent-type: application/json\r\nX-Content-Type-Options: nosniff\r\n\r\n", proto);
}

static void send_bad_request(FCGX_Stream *out, const std::string &protocol, const char *message)
{
    const char *proto = is_valid_http_protocol(protocol) ? protocol.c_str() : "HTTP/1.1";
    FCGX_FPrintF(out, "%s 400 Bad Request\r\nStatus: 400 Bad Request\r\nContent-type: text/plain\r\nX-Content-Type-Options: nosniff\r\n\r\n%s",
                 proto, message);
}

class deferred_http_sink final : public backend::db::stream_sink {
public:
    deferred_http_sink(FCGX_Stream *out, std::string protocol)
        : m_sink(out), m_out(out), m_protocol(std::move(protocol)) {}

    void write(std::string_view sv) override {
        ensure_headers();
        m_sink.write(sv);
    }

    void put(char c) override {
        ensure_headers();
        m_sink.put(c);
    }

private:
    void ensure_headers() {
        if (!m_headers_sent) {
            m_headers_sent = true;
            send_http_headers(m_out, m_protocol);
        }
    }

    backend::db::fcgx_sink m_sink;
    FCGX_Stream *m_out;
    std::string m_protocol;
    bool m_headers_sent = false;
};

static void send_json_response(FCGX_Stream *out, const std::string &protocol, const std::string &body)
{
    const char *proto = is_valid_http_protocol(protocol) ? protocol.c_str() : "HTTP/1.1";
    FCGX_FPrintF(out, "%s 200 OK\r\nStatus: 200 OK\r\nContent-type: application/json\r\nX-Content-Type-Options: nosniff\r\nContent-Length: %d\r\n\r\n%s",
                 proto, static_cast<int>(body.length()), body.c_str());
}

static void request_ws_db_tables(backend::wsworker *worker, const std::map<std::string, std::string> &header, __attribute__((unused)) const std::list<std::string> &uri_params)
{
    auto conn = g_db_pool->acquire();

    // Use sql_to_json to get tables list and envelope under "data"
    backend::db::json_object_guard json(json_object_new_object());
    json_object_object_add(json.get(), "data",
        backend::db::sql_to_json_object(*conn, "SELECT name FROM sqlite_master WHERE type ='table' AND name NOT LIKE 'sqlite_%'"));

    send_json_response(worker->out(), get_header(header, "SERVER_PROTOCOL"), json.to_string());
}

static constexpr int MAX_PAGE_SIZE = 1000;

static void request_ws_jsGrid_customers(backend::wsworker *worker, const std::map<std::string, std::string> &header, __attribute__((unused)) const std::list<std::string> &uri_params)
{
    std::string proto = get_header(header, "SERVER_PROTOCOL");
    auto args = worker->parse_args(get_header(header, "QUERY_STRING"));
    int current_page = std::atoi(args["page"].data());
    int page_size = std::atoi(args["rows"].data());
    if (page_size <= 0 || page_size > MAX_PAGE_SIZE) {
        send_bad_request(worker->out(), proto, "page_size contains invalid value!");
        return;
    }
    if (!args["callback"].empty() && !backend::db::is_valid_callback(args["callback"])) {
        send_bad_request(worker->out(), proto, "Invalid callback parameter!");
        return;
    }

    std::string sql;
    if (args["sidx"].empty()) {
        sql = "SELECT * FROM customers LIMIT ? OFFSET ?";
    } else {
        static std::regex rgx_order(R"(^[_a-z]+[\w]*$)", std::regex_constants::icase);
        static std::regex rgx_order_dir(R"(^asc$|^desc$)", std::regex_constants::icase);

        std::string order = args["sidx"];
        std::string order_dir = args["sord"];

        if (!std::regex_search(order, rgx_order) || !std::regex_search(order_dir, rgx_order_dir)) {
            send_bad_request(worker->out(), proto, "Invalid parameter!");
            return;
        }

        sql = "SELECT * FROM customers ORDER BY " + order + " " + order_dir +  " LIMIT ? OFFSET ?";
    }

    auto conn = g_db_pool->acquire();
    backend::db::sql_query q{
        .count_sql = "SELECT COUNT(*) FROM customers",
        .data_sql = sql,
        .page = current_page,
        .page_size = page_size,
        .callback = args["callback"]
    };

    deferred_http_sink sink(worker->out(), std::move(proto));
    backend::db::sql_to_json_paginated(sink, *conn, q);
}

static void request_ws_jsGrid_artists(backend::wsworker *worker, const std::map<std::string, std::string> &header, __attribute__((unused)) const std::list<std::string> &uri_params)
{
    std::string proto = get_header(header, "SERVER_PROTOCOL");
    auto args = worker->parse_args(get_header(header, "QUERY_STRING"));
    int current_page = std::atoi(args["page"].data());
    int page_size = std::atoi(args["rows"].data());
    if (page_size <= 0 || page_size > MAX_PAGE_SIZE) {
        send_bad_request(worker->out(), proto, "page_size contains invalid value!");
        return;
    }
    if (!args["callback"].empty() && !backend::db::is_valid_callback(args["callback"])) {
        send_bad_request(worker->out(), proto, "Invalid callback parameter!");
        return;
    }

    std::string sql;
    if (args["sidx"].empty()) {
        sql = "SELECT artists.ArtistId as 'id', artists.Name as 'Artist', count(DISTINCT albums.AlbumId) as 'Albums', count(DISTINCT tracks.TrackId) as 'Tracks' FROM artists LEFT JOIN albums ON(albums.ArtistId = artists.ArtistId) LEFT JOIN tracks ON(tracks.AlbumId = albums.AlbumId) GROUP BY artists.Name LIMIT ? OFFSET ?";
    } else {
        static std::regex rgx_order(R"(^[_a-z]+[\w]*$)", std::regex_constants::icase);
        static std::regex rgx_order_dir(R"(^asc$|^desc$)", std::regex_constants::icase);

        std::string order = args["sidx"];
        std::string order_dir = args["sord"];

        if (!std::regex_search(order, rgx_order) || !std::regex_search(order_dir, rgx_order_dir)) {
            send_bad_request(worker->out(), proto, "Invalid parameter!");
            return;
        }

        sql = "SELECT artists.ArtistId as 'id', artists.Name as 'Artist', count(DISTINCT albums.AlbumId) as 'Albums', count(DISTINCT tracks.TrackId) as 'Tracks' FROM artists LEFT JOIN albums ON(albums.ArtistId = artists.ArtistId) LEFT JOIN tracks ON(tracks.AlbumId = albums.AlbumId) GROUP BY artists.Name ORDER BY " + order + " " + order_dir + " LIMIT ? OFFSET ?";
    }

    auto conn = g_db_pool->acquire();
    backend::db::sql_query q{
        .count_sql = "SELECT count(*) FROM artists",
        .data_sql = sql,
        .page = current_page,
        .page_size = page_size,
        .callback = args["callback"]
    };

    deferred_http_sink sink(worker->out(), std::move(proto));
    backend::db::sql_to_json_paginated(sink, *conn, q);
}

static void request_ws_jsGrid_artist_song_type(backend::wsworker *worker, const std::map<std::string, std::string> &header, __attribute__((unused)) const std::list<std::string> &uri_params)
{
    std::string proto = get_header(header, "SERVER_PROTOCOL");
    auto args = worker->parse_args(get_header(header, "QUERY_STRING"));
    int id = std::atoi(args["id"].data());
    if (id <= 0) {
        send_bad_request(worker->out(), proto, "Invalid id.");
        return;
    }

    auto conn = g_db_pool->acquire();
    std::string sql = "SELECT genres.Name as 'name', count(DISTINCT tracks.TrackId) as 'hvalue' FROM genres INNER JOIN tracks ON(tracks.GenreId = genres.GenreId) INNER JOIN albums ON(albums.AlbumId = tracks.AlbumId) WHERE albums.ArtistId = ? GROUP BY genres.Name ORDER BY `hvalue` DESC";

    deferred_http_sink sink(worker->out(), std::move(proto));
    backend::db::sql_to_json(sink, *conn, sql, id);
}

static constexpr auto DEFAULT_SOCKET_NAME = ":9000";
static constexpr auto DEFAULT_BACKLOG = 100;

int main(int argc, char *argv[])
{
    std::string socket_name = DEFAULT_SOCKET_NAME;
    int backlog = DEFAULT_BACKLOG;
    int worker_num = 0;
    int opt = 0;

    while((opt = getopt(argc, argv, "hn:b:w:")) != -1)
    {
        switch(opt)
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
        {"/ws/db/tables",               backend::POST, request_ws_db_tables                },
        {"/ws/jsGrid/customers",        backend::GET,  request_ws_jsGrid_customers         },
        {"/ws/jsGrid/artists",          backend::GET,  request_ws_jsGrid_artists           },
        {"/ws/jsGrid/artist_song_type", backend::POST, request_ws_jsGrid_artist_song_type  },
    });

    // Initialize connection pool and register with server for graceful shutdown
    g_db_pool = std::make_shared<backend::db::connection_pool<backend::db::sqlite_database>>(
        []() {
            return std::make_unique<backend::db::sqlite_database>("chinook.db");
        },
        backend::db::pool_options{
            .min_size = 2,
            .max_size = 16,
            .acquire_timeout = std::chrono::milliseconds(3000),
            .idle_timeout = std::chrono::milliseconds(60000),
            .ping_on_acquire = true
        }
    );
    server.register_connection_pool(g_db_pool);

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
