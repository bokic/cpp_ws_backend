#include "wsserver.h"
#include "wsworker.h"
#include "wsroute.h"

#include <iostream>
#include <sstream>
#include <vector>
#include <list>
#include <map>
#include <sqlite3.h>
#include <fcgio.h>
#include <json-c/json.h>
#include <memory.h>

#include <systemd/sd-journal.h>

using namespace std;

void request_ws_system_logs(backend::wsworker *worker, map<string, string> header, __attribute__((unused)) list<string> uri_params)
{
    sd_journal *journal = nullptr;
    const char *msg = nullptr;
    size_t len = 0;
    int r = 0;

    sd_journal_open(&journal, SD_JOURNAL_CURRENT_USER);

    auto json = json_object_new_object();
    auto json_data = json_object_new_array();
    json_object_object_add(json, "data", json_data);

    int c = 1;
    while((r = sd_journal_next(journal)) > 0)
    {
        r = sd_journal_get_data(journal, "MESSAGE", reinterpret_cast<const void **>(&msg), &len);

        json_object_array_add(json_data, json_object_new_string(reinterpret_cast<const char *>(msg + 8)));

        if (c >= 100) break;
        c++;
    }

    sd_journal_close(journal);
    journal = nullptr;

    const char* json_str = json_object_get_string(json);

    FCGX_FPrintF(worker->m_request->out, "%s 200 OK\r\nContent-type: application/json\r\nContent-Length: %d\r\n\r\n%s", header["SERVER_PROTOCOL"].c_str(), strlen(json_str), json_str);

    json_object_put(json);
}

void request_ws_db_tables(backend::wsworker *worker, map<string, string> header, __attribute__((unused)) list<string> uri_params)
{
    sqlite3_stmt *stmt = nullptr;
    sqlite3 *db = nullptr;
    int res = 0;

    res = sqlite3_open_v2("chinook.db", &db, SQLITE_OPEN_READONLY, nullptr);
    if (res) throw string("Can't open database!");

    res = sqlite3_prepare_v2(db, "SELECT name FROM sqlite_master WHERE type ='table' AND name NOT LIKE 'sqlite_%'", -1, &stmt, nullptr);

    auto json = json_object_new_object();
    auto json_data = json_object_new_array();
    json_object_object_add(json, "data", json_data);

    while ((res = sqlite3_step(stmt)) == SQLITE_ROW) {
        auto tbl_name = sqlite3_column_text(stmt, 0);
        json_object_array_add(json_data, json_object_new_string(reinterpret_cast<const char *>(tbl_name)));
    }

    res = sqlite3_finalize(stmt);

    res = sqlite3_close(db);

    const char* json_str = json_object_get_string(json);

    FCGX_FPrintF(worker->m_request->out, "%s 200 OK\r\nContent-type: application/json\r\nContent-Length: %d\r\n\r\n%s", header["SERVER_PROTOCOL"].c_str(), strlen(json_str), json_str);

    json_object_put(json);
}

void request_ws_jsGrid_customers(backend::wsworker *worker, std::map<std::string, std::string> header, __attribute__((unused)) list<string> uri_params)
{
    sqlite3_stmt *stmt = nullptr;
    sqlite3 *db = nullptr;
    int res = 0;

    auto args = worker->parse_args(header["QUERY_STRING"]);
    int current_page = 0;
    int row_count = 0;
    int page_size = 0;

    current_page = atoi(args["page"].data());

    page_size = atoi(args["rows"].data());
    if (page_size <= 0) throw string("page_size contains invalid value!");

    res = sqlite3_open_v2("chinook.db", &db, SQLITE_OPEN_READONLY, nullptr);
    if (res) throw string("Can't open database!");

    res = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM customers", -1, &stmt, nullptr);
    res = sqlite3_step(stmt);
    row_count = sqlite3_column_int(stmt, 0);
    res = sqlite3_finalize(stmt); stmt = nullptr;

    string sql;
    if (args["sidx"].empty()) {
        sql = "SELECT * FROM customers LIMIT ? OFFSET ?";
    } else {
        sql = "SELECT * FROM customers ORDER BY " + args["sidx"] + " " + args["sord"] +  " LIMIT ? OFFSET ?"; // FIXME: Prevent SQL injection.
    }

    res = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    res = sqlite3_bind_int(stmt, 1, page_size);
    res = sqlite3_bind_int(stmt, 2, (current_page - 1) * page_size);

    auto json = json_object_new_object();
    auto json_rows = json_object_new_array();
    int column_count = -1;

    while ((res = sqlite3_step(stmt)) == SQLITE_ROW) {

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
    json_object_object_add(json, "total", json_object_new_int(row_count % page_size? (row_count / page_size) + 1: row_count / page_size));
    json_object_object_add(json, "page", json_object_new_int(current_page));
    json_object_object_add(json, "records", json_object_new_int(row_count));

    res = sqlite3_finalize(stmt);

    res = sqlite3_close(db);

    const char* json_str = json_object_get_string(json);

    string content = args["callback"] + "(" + json_str + ")";

    FCGX_FPrintF(worker->m_request->out, "%s 200 OK\r\nContent-type: application/json\r\nContent-Length: %d\r\n\r\n%s", header["SERVER_PROTOCOL"].c_str(), content.length(), content.c_str());

    json_object_put(json);
}

void request_ws_jsGrid_artists(backend::wsworker *worker, std::map<std::string, std::string> header, __attribute__((unused)) list<string> uri_params)
{
    sqlite3_stmt *stmt = nullptr;
    sqlite3 *db = nullptr;
    int res = 0;

    auto args = worker->parse_args(header["QUERY_STRING"]);
    int current_page = 0;
    int row_count = 0;
    int page_size = 0;

    current_page = atoi(args["page"].data());

    page_size = atoi(args["rows"].data());
    if (page_size <= 0) throw string("page_size contains invalid value!");

    res = sqlite3_open_v2("chinook.db", &db, SQLITE_OPEN_READONLY, nullptr);
    if (res) throw string("Can't open database!");

    res = sqlite3_prepare_v2(db, "SELECT count(artists.Name) AS 'Count' FROM artists", -1, &stmt, nullptr);
    res = sqlite3_step(stmt);
    row_count = sqlite3_column_int(stmt, 0);
    res = sqlite3_finalize(stmt); stmt = nullptr;

    string sql;
    if (args["sidx"].empty()) {
        sql = "SELECT artists.ArtistId as 'id', artists.Name as 'Artist', count(DISTINCT albums.AlbumId) as 'Albums', count(DISTINCT tracks.TrackId) as 'Tracks' FROM artists LEFT JOIN albums ON(albums.ArtistId = artists.ArtistId) LEFT JOIN tracks ON(tracks.AlbumId = albums.AlbumId) GROUP BY artists.Name LIMIT ? OFFSET ?";
    } else {
        sql = "SELECT artists.ArtistId as 'id', artists.Name as 'Artist', count(DISTINCT albums.AlbumId) as 'Albums', count(DISTINCT tracks.TrackId) as 'Tracks' FROM artists LEFT JOIN albums ON(albums.ArtistId = artists.ArtistId) LEFT JOIN tracks ON(tracks.AlbumId = albums.AlbumId) GROUP BY artists.Name ORDER BY " + args["sidx"] + " " + args["sord"] +  " LIMIT ? OFFSET ?"; // FIXME: Prevent SQL injection.
    }

    res = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    res = sqlite3_bind_int(stmt, 1, page_size);
    res = sqlite3_bind_int(stmt, 2, (current_page - 1) * page_size);

    auto json = json_object_new_object();
    auto json_rows = json_object_new_array();
    int column_count = -1;

    while ((res = sqlite3_step(stmt)) == SQLITE_ROW) {

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
    json_object_object_add(json, "total", json_object_new_int(row_count % page_size? (row_count / page_size) + 1: row_count / page_size));
    json_object_object_add(json, "page", json_object_new_int(current_page));
    json_object_object_add(json, "records", json_object_new_int(row_count));

    res = sqlite3_finalize(stmt);

    res = sqlite3_close(db);

    const char* json_str = json_object_get_string(json);

    string content = args["callback"] + "(" + json_str + ")";

    FCGX_FPrintF(worker->m_request->out, "%s 200 OK\r\nContent-type: application/json\r\nContent-Length: %d\r\n\r\n%s", header["SERVER_PROTOCOL"].c_str(), content.length(), content.c_str());

    json_object_put(json);
}

void request_ws_jsGrid_artist_song_type(backend::wsworker *worker, std::map<std::string, std::string> header, __attribute__((unused)) list<string> uri_params)
{
    sqlite3_stmt *stmt = nullptr;
    sqlite3 *db = nullptr;
    int res = 0;

    auto args = worker->parse_args(header["QUERY_STRING"]);
    int id = 0;

    id = atoi(args["id"].data());
    if (id <= 0) throw string("Invalid id.");

    res = sqlite3_open_v2("chinook.db", &db, SQLITE_OPEN_READONLY, nullptr);
    if (res) throw string("Can't open database!");

    string sql = "SELECT genres.Name as 'name', count(DISTINCT tracks.TrackId) as 'hvalue' FROM genres INNER JOIN tracks ON(tracks.GenreId = genres.GenreId) INNER JOIN albums ON(albums.AlbumId = tracks.AlbumId) WHERE albums.ArtistId = ? GROUP BY genres.Name ORDER BY `hvalue` DESC";
    res = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    res = sqlite3_bind_int(stmt, 1, id);

    auto json_rows = json_object_new_array();
    int column_count = -1;

    while ((res = sqlite3_step(stmt)) == SQLITE_ROW) {

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

    res = sqlite3_close(db);

    const char* json = json_object_get_string(json_rows);

    string content = json;

    FCGX_FPrintF(worker->m_request->out, "%s 200 OK\r\nContent-type: application/json\r\nContent-Length: %d\r\n\r\n%s", header["SERVER_PROTOCOL"].c_str(), content.length(), content.c_str());

    json_object_put(json_rows);
}

static backend::route routeMap[] = {
    {"/ws/system/logs",             backend::POST, request_ws_system_logs              },
    {"/ws/db/tables",               backend::POST, request_ws_db_tables                },
    {"/ws/jsGrid/customers",        backend::POST, request_ws_jsGrid_customers         },
    {"/ws/jsGrid/artists",          backend::POST, request_ws_jsGrid_artists           },
    {"/ws/jsGrid/artist_song_type", backend::POST, request_ws_jsGrid_artist_song_type  },
};
