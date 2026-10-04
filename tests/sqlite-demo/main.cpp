#include "wsserver.h"
#include "wsworker.h"
#include "wsroute.h"

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
#include <sqlite3.h>
#include <fcgio.h>
#include <json-c/json.h>

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

    sqlite3_stmt *stmt = nullptr;
    sqlite3 *db = nullptr;
    const char *err = nullptr;

    struct json_object *json = nullptr;
    struct json_object *json_rows = nullptr;
    const char* json_str = nullptr;

    std::string content;
    int column_count = -1;
    int row_count = 0;
    int res = 0;

    try {
        res = sqlite3_open_v2(db_pathname, &db, SQLITE_OPEN_READONLY, nullptr);
        if (res) {
            err = "Can't open database!";
            goto exit;
        }

        res = sqlite3_prepare_v2(db, count_sql, -1, &stmt, nullptr);
        if (res) {
            err = "Preparing row count SQL statement FAILED!";
            goto exit;
        }

        res = sqlite3_step(stmt);
        if (res != SQLITE_ROW) {
            err = "Stepping into first record for getting row count FAILED!";
            goto exit;
        }

        row_count = sqlite3_column_int(stmt, 0);
        res = sqlite3_finalize(stmt); stmt = nullptr;
        if (res) {
            err = "Call to sqlite3_finalize() FAILED!";
            goto exit;
        }

        res = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        if (res) {
            err = "Preparing SQL statement FAILED!";
            goto exit;
        }

        res = sqlite3_bind_int(stmt, 1, page_size);
        if (res) {
            err = "Call to sqlite3_bind_int() for page_size FAILED!";
            goto exit;
        }

        res = sqlite3_bind_int(stmt, 2, (current_page - 1) * page_size);
        if (res) {
            err = "Call to sqlite3_bind_int() for current_page FAILED!";
            goto exit;
        }

        json = json_object_new_object();
        if (json == nullptr) {
            err = "Function json_object_new_object() returned NULL!";
            goto exit;
        }

        json_rows = json_object_new_array();
        if (json_rows == nullptr) {
            err = "Function json_object_new_array() returned NULL!";
            goto exit;
        }

        while (sqlite3_step(stmt) == SQLITE_ROW) {

            if (column_count == -1) column_count = sqlite3_column_count(stmt);

            auto json_row = json_object_new_object();
            for(int col = 0; col < column_count; col++) {
                auto col_name = sqlite3_column_name(stmt, col);
                auto col_val = reinterpret_cast<const char *>(sqlite3_column_text(stmt, col));

                if (col_val)
                    res = json_object_object_add(json_row, col_name, json_object_new_string(col_val));
                else
                    res = json_object_object_add(json_row, col_name, json_object_new_string(""));
                if (res) {
                    err = "Call to json_object_object_add() FAILED!";
                    goto exit;
                }
            }

            res = json_object_array_add(json_rows, json_row);
            if (res) {
                err = "Call to json_object_array_add() FAILED!";
                goto exit;
            }
        }

        res = json_object_object_add(json, "rows", json_rows);
        if (res) {
            err = "Call to json_object_object_add() for rows FAILED!";
            goto exit;
        }

        res = json_object_object_add(json, "total", json_object_new_int((row_count % page_size)? (row_count / page_size) + 1: row_count / page_size));
        if (res) {
            err = "Call to json_object_object_add() for total FAILED!";
            goto exit;
        }

        res = json_object_object_add(json, "page", json_object_new_int(current_page));
        if (res) {
            err = "Call to json_object_object_add() for page FAILED!";
            goto exit;
        }

        res = json_object_object_add(json, "records", json_object_new_int(row_count));
        if (res) {
            err = "Call to json_object_object_add() for records FAILED!";
            goto exit;
        }

        res = sqlite3_finalize(stmt);
        if (res) {
            err = "Call to sqlite3_finalize() FAILED!";
            goto exit;
        }

        res = sqlite3_close(db);
        if (res) {
            err = "Call to sqlite3_close() FAILED!";
            goto exit;
        }

        json_str = json_object_get_string(json);
        if (json_str == nullptr) {
            err = "Call to json_object_get_string() FAILED!";
            goto exit;
        }

        std::string content;
        if (!callback.empty()) {
            if (!is_valid_callback(callback)) {
                err = "Invalid callback parameter";
                goto exit;
            }
            content = callback + "(" + json_str + ")";
        } else {
            content = json_str;
        }

        FCGX_FPrintF(out_stream, "%s 200 OK\r\nContent-type: application/json\r\nX-Content-Type-Options: nosniff\r\nContent-Length: %d\r\n\r\n%s", server_protocol, content.length(), content.c_str());

        json_object_put(json);
    } catch (...) {
        throw;
    }

exit:
    if (err)
        throw std::runtime_error(err);
}

static const std::string &get_header(const std::map<std::string, std::string> &header, const std::string &key)
{
    static const std::string empty_str;
    auto it = header.find(key);
    return it != header.end() ? it->second : empty_str;
}

static void request_ws_db_tables(backend::wsworker *worker, const std::map<std::string, std::string> &header, __attribute__((unused)) const std::list<std::string> &uri_params)
{
    sqlite3_stmt *stmt = nullptr;
    sqlite3 *db = nullptr;
    int res = 0;

    res = sqlite3_open_v2("chinook.db", &db, SQLITE_OPEN_READONLY, nullptr);
    if (res) throw std::runtime_error("Can't open database!");

    res = sqlite3_prepare_v2(db, "SELECT name FROM sqlite_master WHERE type ='table' AND name NOT LIKE 'sqlite_%'", -1, &stmt, nullptr);
    if (res) throw std::runtime_error("Can't prepare SQL!");

    auto json = json_object_new_object();
    auto json_data = json_object_new_array();
    json_object_object_add(json, "data", json_data);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        auto tbl_name = sqlite3_column_text(stmt, 0);
        json_object_array_add(json_data, json_object_new_string(reinterpret_cast<const char *>(tbl_name)));
    }

    res = sqlite3_finalize(stmt);
    if (res) throw std::runtime_error("Can't finalize SQL!");

    res = sqlite3_close(db);
    if (res) throw std::runtime_error("Can't close SQL!");

    const char* json_str = json_object_get_string(json);

    FCGX_FPrintF(worker->out(), "%s 200 OK\r\nContent-type: application/json\r\nContent-Length: %d\r\n\r\n%s", get_header(header, "SERVER_PROTOCOL").c_str(), std::strlen(json_str), json_str);

    json_object_put(json);
}

static void request_ws_jsGrid_customers(backend::wsworker *worker, const std::map<std::string, std::string> &header, __attribute__((unused)) const std::list<std::string> &uri_params)
{
    sqlite3_stmt *stmt = nullptr;
    sqlite3 *db = nullptr;
    int res = 0;

    auto args = worker->parse_args(get_header(header, "QUERY_STRING"));
    const auto &callback = args["callback"];
    if (!callback.empty() && !is_valid_callback(callback)) {
        throw std::invalid_argument("Invalid callback parameter");
    }

    int current_page = 0;
    int row_count = 0;
    int page_size = 0;

    current_page = std::atoi(args["page"].data());

    page_size = std::atoi(args["rows"].data());
    if (page_size <= 0) throw std::invalid_argument("page_size contains invalid value!");

    res = sqlite3_open_v2("chinook.db", &db, SQLITE_OPEN_READONLY, nullptr);
    if (res) throw std::runtime_error("Can't open database!");

    res = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM customers", -1, &stmt, nullptr);
    if (res) throw std::runtime_error("Can't prepare SQL!");

    res = sqlite3_step(stmt);
    if (res != SQLITE_ROW) throw std::runtime_error("Can't step SQL!");

    row_count = sqlite3_column_int(stmt, 0);
    res = sqlite3_finalize(stmt);
    if (res) throw std::runtime_error("Can't finalize statement!");
    stmt = nullptr;

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

    res = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (res) throw std::runtime_error("Can't prepare SQL!");

    res = sqlite3_bind_int(stmt, 1, page_size);
    if (res) throw std::runtime_error("SQL parameter bind fail!");

    res = sqlite3_bind_int(stmt, 2, (current_page - 1) * page_size);
    if (res) throw std::runtime_error("SQL parameter bind fail!");

    auto json = json_object_new_object();
    auto json_rows = json_object_new_array();
    int column_count = -1;

    while (sqlite3_step(stmt) == SQLITE_ROW) {

        if (column_count == -1) column_count = sqlite3_column_count(stmt);

        auto json_row = json_object_new_object();
        for(int col = 0; col < column_count; col++) {
            auto col_name = sqlite3_column_name(stmt, col);
            auto col_val = reinterpret_cast<const char *>(sqlite3_column_text(stmt, col));

            if (col_val)
                json_object_object_add(json_row, col_name, json_object_new_string(col_val));
            else
                json_object_object_add(json_row, col_name, json_object_new_string(""));
        }

        json_object_array_add(json_rows, json_row);
    }

    json_object_object_add(json, "rows", json_rows);
    json_object_object_add(json, "total", json_object_new_int((row_count % page_size)? (row_count / page_size) + 1: row_count / page_size));
    json_object_object_add(json, "page", json_object_new_int(current_page));
    json_object_object_add(json, "records", json_object_new_int(row_count));

    res = sqlite3_finalize(stmt);
    if (res) throw std::runtime_error("sqlite finalize fail!");

    res = sqlite3_close(db);
    if (res) throw std::runtime_error("sqlite close fail!");

    const char* json_str = json_object_get_string(json);

    std::string content;
    if (!callback.empty()) {
        content = callback + "(" + json_str + ")";
    } else {
        content = json_str;
    }

    FCGX_FPrintF(worker->out(), "%s 200 OK\r\nContent-type: application/json\r\nX-Content-Type-Options: nosniff\r\nContent-Length: %d\r\n\r\n%s", get_header(header, "SERVER_PROTOCOL").c_str(), content.length(), content.c_str());

    json_object_put(json);
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
    sqlite3_stmt *stmt = nullptr;
    sqlite3 *db = nullptr;
    int res = 0;

    auto args = worker->parse_args(get_header(header, "QUERY_STRING"));
    int id = 0;

    id = std::atoi(args["id"].data());
    if (id <= 0) throw std::invalid_argument("Invalid id.");

    res = sqlite3_open_v2("chinook.db", &db, SQLITE_OPEN_READONLY, nullptr);
    if (res) throw std::runtime_error("Can't open database!");

    std::string sql = "SELECT genres.Name as 'name', count(DISTINCT tracks.TrackId) as 'hvalue' FROM genres INNER JOIN tracks ON(tracks.GenreId = genres.GenreId) INNER JOIN albums ON(albums.AlbumId = tracks.AlbumId) WHERE albums.ArtistId = ? GROUP BY genres.Name ORDER BY `hvalue` DESC";
    res = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (res) throw std::runtime_error("sqlite prepare fail!");

    res = sqlite3_bind_int(stmt, 1, id);
    if (res) throw std::runtime_error("SQL parameter bind fail!");

    auto json_rows = json_object_new_array();
    int column_count = -1;

    while (sqlite3_step(stmt) == SQLITE_ROW) {

        if (column_count == -1) column_count = sqlite3_column_count(stmt);

        auto json_row = json_object_new_object();
        for(int col = 0; col < column_count; col++) {
            auto col_name = sqlite3_column_name(stmt, col);
            if (col != 1) {
                auto col_val = reinterpret_cast<const char *>(sqlite3_column_text(stmt, col));

                if (col_val)
                    json_object_object_add(json_row, col_name, json_object_new_string(col_val));
                else
                    json_object_object_add(json_row, col_name, json_object_new_string(""));
            } else {
                auto col_val = sqlite3_column_int(stmt, col);

                json_object_object_add(json_row, col_name, json_object_new_int(col_val));
            }
        }

        json_object_array_add(json_rows, json_row);
    }

    res = sqlite3_finalize(stmt);
    if (res) throw std::runtime_error("sqlite finalize fail!");

    res = sqlite3_close(db);
    if (res) throw std::runtime_error("sqlite close fail!");

    const char* json = json_object_get_string(json_rows);

    std::string content = json;

    FCGX_FPrintF(worker->out(), "%s 200 OK\r\nContent-type: application/json\r\nContent-Length: %d\r\n\r\n%s", get_header(header, "SERVER_PROTOCOL").c_str(), content.length(), content.c_str());

    json_object_put(json_rows);
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
