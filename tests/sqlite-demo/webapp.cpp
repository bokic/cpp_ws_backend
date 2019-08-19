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


using namespace std;

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

    FCGX_FPrintF(worker->m_request.get()->out, "%s 200 OK\r\nContent-type: application/json\r\nContent-Length: %d\r\n\r\n%s", header["SERVER_PROTOCOL"].c_str(), strlen(json_str), json_str);

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

    FCGX_FPrintF(worker->m_request.get()->out, "%s 200 OK\r\nContent-type: application/json\r\nContent-Length: %d\r\n\r\n%s", header["SERVER_PROTOCOL"].c_str(), content.length(), content.c_str());

    json_object_put(json);
}

static backend::route routeMap[] = {
    {"/ws/db/tables",        backend::POST, request_ws_db_tables       },
    {"/ws/jsGrid/customers", backend::POST, request_ws_jsGrid_customers},
};
