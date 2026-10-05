#include "wsserver.h"
#include "wsworker.h"
#include "wsroute.h"
#include "raii_sqlite.h"
#include "raii_json.h"

#include <iostream>
#include <sstream>
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


static bool is_valid_callback(const std::string &callback)
{
    if (callback.empty() || callback.size() > 128) {
        return false;
    }
    static const std::regex valid_cb_regex(R"(^[a-zA-Z_$][a-zA-Z0-9_$]*(\.[a-zA-Z_$][a-zA-Z0-9_$]*)*$)");
    return std::regex_match(callback, valid_cb_regex);
}

static void wsdatabase_sqlite_write_json(FCGX_Stream *out_stream, const char *server_protocol, const std::string &callback, const char *db_pathname, const char *count_sql, const char *sql, int current_page, int page_size)
{
    if (!callback.empty() && !is_valid_callback(callback)) {
        throw std::invalid_argument("Invalid callback parameter");
    }

    db::sqlite_db db(db_pathname);

    auto count_stmt = db.prepare(count_sql);
    if (count_stmt.step() != SQLITE_ROW) {
        throw std::runtime_error("Stepping into first record for getting row count FAILED!");
    }
    int row_count = count_stmt.column_int(0);
    count_stmt.reset();

    auto data_stmt = db.prepare(sql);
    data_stmt.bind_int(1, page_size);
    data_stmt.bind_int(2, (current_page - 1) * page_size);

    auto json = json::json_value::make_object();
    auto json_rows = json::json_value::make_array();

    int column_count = -1;
    while (data_stmt.step() == SQLITE_ROW) {
        if (column_count == -1) column_count = data_stmt.column_count();

        auto json_row = json::json_value::make_object();
        for (int col = 0; col < column_count; col++) {
            const char *col_name = data_stmt.column_name(col);
            const char *col_val = data_stmt.column_text(col);
            json_row.add(col_name, json::json_value::make_string(col_val ? col_val : ""));
        }
        json_rows.add(std::move(json_row));
    }

    json.add("rows", std::move(json_rows));
    json.add("total", json::json_value::make_int((row_count % page_size) ? (row_count / page_size) + 1 : row_count / page_size));
    json.add("page", json::json_value::make_int(current_page));
    json.add("records", json::json_value::make_int(row_count));

    const char *json_str = json.to_string();
    std::string content;
    if (!callback.empty()) {
        content = callback + "(" + json_str + ")";
    } else {
        content = json_str;
    }

    FCGX_FPrintF(out_stream, "%s 200 OK\r\nContent-type: application/json\r\nX-Content-Type-Options: nosniff\r\nContent-Length: %d\r\n\r\n%s",
                 server_protocol, static_cast<int>(content.length()), content.c_str());
}

static const std::string &get_header(const std::map<std::string, std::string> &header, const std::string &key)
{
    static const std::string empty_str;
    auto it = header.find(key);
    return it != header.end() ? it->second : empty_str;
}

static void request_ws_db_tables(backend::wsworker *worker, const std::map<std::string, std::string> &header, __attribute__((unused)) const std::list<std::string> &uri_params)
{
    db::sqlite_db db("chinook.db");
    auto stmt = db.prepare("SELECT name FROM sqlite_master WHERE type ='table' AND name NOT LIKE 'sqlite_%'");

    auto json = json::json_value::make_object();
    auto json_data = json::json_value::make_array();

    while (stmt.step() == SQLITE_ROW) {
        const char *tbl_name = stmt.column_text(0);
        json_data.add(json::json_value::make_string(tbl_name ? tbl_name : ""));
    }

    json.add("data", std::move(json_data));

    const char *json_str = json.to_string();
    FCGX_FPrintF(worker->out(), "%s 200 OK\r\nContent-type: application/json\r\nX-Content-Type-Options: nosniff\r\nContent-Length: %d\r\n\r\n%s",
                 get_header(header, "SERVER_PROTOCOL").c_str(), static_cast<int>(std::strlen(json_str)), json_str);
}

static void request_ws_jsGrid_customers(backend::wsworker *worker, const std::map<std::string, std::string> &header, __attribute__((unused)) const std::list<std::string> &uri_params)
{
    auto args = worker->parse_args(get_header(header, "QUERY_STRING"));
    const auto &callback = args["callback"];
    if (!callback.empty() && !is_valid_callback(callback)) {
        throw std::invalid_argument("Invalid callback parameter");
    }

    int current_page = std::atoi(args["page"].data());
    int page_size = std::atoi(args["rows"].data());
    if (page_size <= 0) throw std::invalid_argument("page_size contains invalid value!");

    db::sqlite_db db("chinook.db");

    auto count_stmt = db.prepare("SELECT COUNT(*) FROM customers");
    if (count_stmt.step() != SQLITE_ROW) throw std::runtime_error("Can't step SQL!");
    int row_count = count_stmt.column_int(0);
    count_stmt.reset();

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

    auto stmt = db.prepare(sql);
    stmt.bind_int(1, page_size);
    stmt.bind_int(2, (current_page - 1) * page_size);

    auto json = json::json_value::make_object();
    auto json_rows = json::json_value::make_array();
    int column_count = -1;

    while (stmt.step() == SQLITE_ROW) {
        if (column_count == -1) column_count = stmt.column_count();

        auto json_row = json::json_value::make_object();
        for (int col = 0; col < column_count; col++) {
            const char *col_name = stmt.column_name(col);
            const char *col_val = stmt.column_text(col);
            json_row.add(col_name, json::json_value::make_string(col_val ? col_val : ""));
        }

        json_rows.add(std::move(json_row));
    }

    json.add("rows", std::move(json_rows));
    json.add("total", json::json_value::make_int((row_count % page_size) ? (row_count / page_size) + 1 : row_count / page_size));
    json.add("page", json::json_value::make_int(current_page));
    json.add("records", json::json_value::make_int(row_count));

    const char *json_str = json.to_string();
    std::string content;
    if (!callback.empty()) {
        content = callback + "(" + json_str + ")";
    } else {
        content = json_str;
    }

    FCGX_FPrintF(worker->out(), "%s 200 OK\r\nContent-type: application/json\r\nX-Content-Type-Options: nosniff\r\nContent-Length: %d\r\n\r\n%s",
                 get_header(header, "SERVER_PROTOCOL").c_str(), static_cast<int>(content.length()), content.c_str());
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

    const char *count_sql = "SELECT count(*) FROM artists";
    wsdatabase_sqlite_write_json(worker->out(), get_header(header, "SERVER_PROTOCOL").c_str(), args["callback"], "chinook.db", count_sql, sql.c_str(), current_page, page_size);
}

static void request_ws_jsGrid_artist_song_type(backend::wsworker *worker, const std::map<std::string, std::string> &header, __attribute__((unused)) const std::list<std::string> &uri_params)
{
    auto args = worker->parse_args(get_header(header, "QUERY_STRING"));
    int id = std::atoi(args["id"].data());
    if (id <= 0) throw std::invalid_argument("Invalid id.");

    db::sqlite_db db("chinook.db");

    std::string sql = "SELECT genres.Name as 'name', count(DISTINCT tracks.TrackId) as 'hvalue' FROM genres INNER JOIN tracks ON(tracks.GenreId = genres.GenreId) INNER JOIN albums ON(albums.AlbumId = tracks.AlbumId) WHERE albums.ArtistId = ? GROUP BY genres.Name ORDER BY `hvalue` DESC";
    auto stmt = db.prepare(sql);
    stmt.bind_int(1, id);

    auto json_rows = json::json_value::make_array();
    int column_count = -1;

    while (stmt.step() == SQLITE_ROW) {
        if (column_count == -1) column_count = stmt.column_count();

        auto json_row = json::json_value::make_object();
        for (int col = 0; col < column_count; col++) {
            const char *col_name = stmt.column_name(col);
            if (col != 1) {
                const char *col_val = stmt.column_text(col);
                json_row.add(col_name, json::json_value::make_string(col_val ? col_val : ""));
            } else {
                int col_val = stmt.column_int(col);
                json_row.add(col_name, json::json_value::make_int(col_val));
            }
        }

        json_rows.add(std::move(json_row));
    }

    const char *json_str = json_rows.to_string();

    FCGX_FPrintF(worker->out(), "%s 200 OK\r\nContent-type: application/json\r\nContent-Length: %d\r\n\r\n%s",
                 get_header(header, "SERVER_PROTOCOL").c_str(), static_cast<int>(std::strlen(json_str)), json_str);
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

    server.init(socket_name.c_str(), backlog, worker_num);

    int ret = server.run();
    g_server.store(nullptr);
    return ret;
}
