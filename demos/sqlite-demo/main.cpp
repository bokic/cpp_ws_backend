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
#include <unistd.h>
#include <fcgio.h>
#include <csignal>
#include <atomic>

static std::shared_ptr<backend::db::connection_pool<backend::db::sqlite_database>> g_db_pool;

static const std::string &get_header(const std::map<std::string, std::string> &header, const std::string &key)
{
    static const std::string empty_str;
    auto it = header.find(key);
    return it != header.end() ? it->second : empty_str;
}

static void send_json_response(FCGX_Stream *out, const std::string &protocol, const std::string &body)
{
    FCGX_FPrintF(out, "%s 200 OK\r\nContent-type: application/json\r\nX-Content-Type-Options: nosniff\r\nContent-Length: %d\r\n\r\n%s",
                 protocol.c_str(), static_cast<int>(body.length()), body.c_str());
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

static void request_ws_jsGrid_customers(backend::wsworker *worker, const std::map<std::string, std::string> &header, __attribute__((unused)) const std::list<std::string> &uri_params)
{
    auto args = worker->parse_args(get_header(header, "QUERY_STRING"));
    int current_page = std::atoi(args["page"].data());
    int page_size = std::atoi(args["rows"].data());
    if (page_size <= 0) throw std::invalid_argument("page_size contains invalid value!");

    std::string sql;
    if (args["sidx"].empty()) {
        sql = "SELECT * FROM customers LIMIT ? OFFSET ?";
    } else {
        static std::regex rgx_order(R"(^[_a-z]+[\w]*$)", std::regex_constants::icase);
        static std::regex rgx_order_dir(R"(^asc$|^desc$)", std::regex_constants::icase);

        std::string order = args["sidx"];
        std::string order_dir = args["sord"];

        if (!std::regex_search(order, rgx_order)) throw std::invalid_argument("Invalid parameter!");
        if (!std::regex_search(order_dir, rgx_order_dir)) throw std::invalid_argument("Invalid parameter!");

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

    std::string body = backend::db::sql_to_json_paginated_string(*conn, q);
    send_json_response(worker->out(), get_header(header, "SERVER_PROTOCOL"), body);
}

static void request_ws_jsGrid_artists(backend::wsworker *worker, const std::map<std::string, std::string> &header, __attribute__((unused)) const std::list<std::string> &uri_params)
{
    auto args = worker->parse_args(get_header(header, "QUERY_STRING"));
    int current_page = std::atoi(args["page"].data());
    int page_size = std::atoi(args["rows"].data());
    if (page_size <= 0) throw std::invalid_argument("page_size contains invalid value!");

    std::string sql;
    if (args["sidx"].empty()) {
        sql = "SELECT artists.ArtistId as 'id', artists.Name as 'Artist', count(DISTINCT albums.AlbumId) as 'Albums', count(DISTINCT tracks.TrackId) as 'Tracks' FROM artists LEFT JOIN albums ON(albums.ArtistId = artists.ArtistId) LEFT JOIN tracks ON(tracks.AlbumId = albums.AlbumId) GROUP BY artists.Name LIMIT ? OFFSET ?";
    } else {
        static std::regex rgx_order(R"(^[_a-z]+[\w]*$)", std::regex_constants::icase);
        static std::regex rgx_order_dir(R"(^asc$|^desc$)", std::regex_constants::icase);

        std::string order = args["sidx"];
        std::string order_dir = args["sord"];

        if (!std::regex_search(order, rgx_order)) throw std::invalid_argument("Invalid parameter!");
        if (!std::regex_search(order_dir, rgx_order_dir)) throw std::invalid_argument("Invalid parameter!");

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

    std::string body = backend::db::sql_to_json_paginated_string(*conn, q);
    send_json_response(worker->out(), get_header(header, "SERVER_PROTOCOL"), body);
}

static void request_ws_jsGrid_artist_song_type(backend::wsworker *worker, const std::map<std::string, std::string> &header, __attribute__((unused)) const std::list<std::string> &uri_params)
{
    auto args = worker->parse_args(get_header(header, "QUERY_STRING"));
    int id = std::atoi(args["id"].data());
    if (id <= 0) throw std::invalid_argument("Invalid id.");

    auto conn = g_db_pool->acquire();
    std::string sql = "SELECT genres.Name as 'name', count(DISTINCT tracks.TrackId) as 'hvalue' FROM genres INNER JOIN tracks ON(tracks.GenreId = genres.GenreId) INNER JOIN albums ON(albums.AlbumId = tracks.AlbumId) WHERE albums.ArtistId = ? GROUP BY genres.Name ORDER BY `hvalue` DESC";

    std::string body = backend::db::sql_to_json_string(*conn, sql, id);
    send_json_response(worker->out(), get_header(header, "SERVER_PROTOCOL"), body);
}

static constexpr auto DEFAULT_SOCKET_NAME = ":9000";
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
        {"/ws/db/tables",               backend::POST, request_ws_db_tables                },
        {"/ws/jsGrid/customers",        backend::GET,  request_ws_jsGrid_customers         },
        {"/ws/jsGrid/artists",          backend::GET,  request_ws_jsGrid_artists           },
        {"/ws/jsGrid/artist_song_type", backend::POST, request_ws_jsGrid_artist_song_type  },
    });

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

    int ret = server.run();
    g_server.store(nullptr);
    return ret;
}
