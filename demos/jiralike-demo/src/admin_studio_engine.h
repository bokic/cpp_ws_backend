#pragma once

#include "wsserver.h"
#include "wsworker.h"
#include "wsroute.h"

#include "raii_sqlite.h"
#include "raii_json.h"
#include "crypto_utils.h"

#include <json-c/json.h>
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace admin_studio {

inline bool is_valid_identifier(const std::string &name) {
    if (name.empty() || name.size() > 64) return false;
    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
            return false;
        }
    }
    return true;
}

inline bool table_exists(db::sqlite_db &conn, const std::string &table) {
    if (!is_valid_identifier(table)) return false;
    auto stmt = conn.prepare("SELECT 1 FROM sqlite_master WHERE type='table' AND name = ?;");
    stmt.bind_text(1, table);
    return stmt.step() == SQLITE_ROW;
}

struct column_meta {
    int cid = 0;
    std::string name;
    std::string type;
    bool notnull = false;
    std::string dflt_value;
    int pk = 0;
};

inline std::vector<column_meta> get_table_columns(db::sqlite_db &conn, const std::string &table) {
    std::vector<column_meta> cols;
    if (!is_valid_identifier(table)) return cols;
    std::string q = "PRAGMA table_info(\"" + table + "\");";
    auto stmt = conn.prepare(q);
    while (stmt.step() == SQLITE_ROW) {
        column_meta cm;
        cm.cid = stmt.column_int(0);
        cm.name = stmt.column_str(1);
        cm.type = stmt.column_str(2);
        cm.notnull = stmt.column_int(3) != 0;
        cm.dflt_value = stmt.column_is_null(4) ? "" : stmt.column_str(4);
        cm.pk = stmt.column_int(5);
        cols.push_back(cm);
    }
    return cols;
}

inline void bind_json_to_statement(db::sqlite_stmt &stmt, int idx, json_object *val) {
    if (!val || json_object_is_type(val, json_type_null)) {
        stmt.bind_null(idx);
    } else if (json_object_is_type(val, json_type_boolean)) {
        stmt.bind_int(idx, json_object_get_boolean(val) ? 1 : 0);
    } else if (json_object_is_type(val, json_type_int)) {
        stmt.bind_int64(idx, json_object_get_int64(val));
    } else if (json_object_is_type(val, json_type_double)) {
        stmt.bind_double(idx, json_object_get_double(val));
    } else if (json_object_is_type(val, json_type_string)) {
        const char *s = json_object_get_string(val);
        stmt.bind_text(idx, s ? s : "");
    } else {
        const char *s = json_object_to_json_string_ext(val, JSON_C_TO_STRING_PLAIN);
        stmt.bind_text(idx, s ? s : "");
    }
}

} // namespace admin_studio

// 1. GET /api/admin/tables
static void handle_admin_tables(backend::wsworker *worker,
                                const std::map<std::string, std::string> &header,
                                const std::list<std::string> &) {
    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated || u_auth.role != "Admin") {
        send_error(worker, header, "403 Forbidden", "Admin privileges required");
        return;
    }

    auto stmt = conn.prepare(R"SQL(
        SELECT name FROM sqlite_master 
        WHERE type = 'table' 
          AND name NOT LIKE 'sqlite_%' 
          AND name NOT LIKE 'work_items_fts_%'
        ORDER BY name ASC;
    )SQL");

    auto tables_arr = json::json_value::make_array();

    while (stmt.step() == SQLITE_ROW) {
        std::string table_name = stmt.column_str(0);
        auto t_obj = json::json_value::make_object();
        t_obj.add_str("name", table_name);

        // Row count
        int64_t count = 0;
        try {
            auto count_stmt = conn.prepare("SELECT COUNT(*) FROM \"" + table_name + "\";");
            if (count_stmt.step() == SQLITE_ROW) {
                count = count_stmt.column_int64(0);
            }
        } catch (...) {
            count = 0;
        }
        t_obj.add_int64("row_count", count);

        // Columns
        auto cols_arr = json::json_value::make_array();
        auto cols = admin_studio::get_table_columns(conn, table_name);
        for (const auto &cm : cols) {
            auto cm_obj = json::json_value::make_object();
            cm_obj.add_int("cid", cm.cid);
            cm_obj.add_str("name", cm.name);
            cm_obj.add_str("type", cm.type);
            cm_obj.add_bool("notnull", cm.notnull);
            cm_obj.add_str("default_value", cm.dflt_value);
            cm_obj.add_int("pk", cm.pk);
            cols_arr.add(std::move(cm_obj));
        }
        t_obj.add("columns", std::move(cols_arr));

        // Foreign keys
        auto fks_arr = json::json_value::make_array();
        try {
            auto fk_stmt = conn.prepare("PRAGMA foreign_key_list(\"" + table_name + "\");");
            while (fk_stmt.step() == SQLITE_ROW) {
                auto fk_obj = json::json_value::make_object();
                fk_obj.add_int("id", fk_stmt.column_int(0));
                fk_obj.add_str("table", fk_stmt.column_str(2));
                fk_obj.add_str("from", fk_stmt.column_str(3));
                fk_obj.add_str("to", fk_stmt.column_str(4));
                fk_obj.add_str("on_update", fk_stmt.column_str(5));
                fk_obj.add_str("on_delete", fk_stmt.column_str(6));
                fks_arr.add(std::move(fk_obj));
            }
        } catch (...) {}
        t_obj.add("foreign_keys", std::move(fks_arr));

        tables_arr.add(std::move(t_obj));
    }

    send_json(worker, header, "200 OK", tables_arr);
}

// 2. GET / POST /api/admin/tables/([A-Za-z0-9_]+)
static void handle_admin_table_detail(backend::wsworker *worker,
                                      const std::map<std::string, std::string> &header,
                                      const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing table name");
        return;
    }
    std::string table = uri_params.front();
    if (!admin_studio::is_valid_identifier(table)) {
        send_error(worker, header, "400 Bad Request", "Invalid table name format");
        return;
    }

    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated || u_auth.role != "Admin") {
        send_error(worker, header, "403 Forbidden", "Admin privileges required");
        return;
    }

    if (!admin_studio::table_exists(conn, table)) {
        send_error(worker, header, "404 Not Found", "Table '" + table + "' does not exist");
        return;
    }

    std::string method = get_header(header, "REQUEST_METHOD");

    if (method == "GET") {
        auto args = worker->parse_args(get_header(header, "QUERY_STRING"));
        int page = std::max(1, std::atoi(args["page"].c_str()));
        int page_size = std::atoi(args["pageSize"].c_str());
        if (page_size <= 0) page_size = 50;
        if (page_size > 200) page_size = 200;
        int offset = (page - 1) * page_size;

        std::string search = args["search"];
        std::string sort_by = args["sortBy"];
        std::string sort_dir = args["sortDir"];
        if (sort_dir != "DESC" && sort_dir != "desc") sort_dir = "ASC";
        else sort_dir = "DESC";

        auto cols = admin_studio::get_table_columns(conn, table);
        std::set<std::string> valid_col_names;
        std::string pk_col = "";
        std::vector<std::string> text_cols;

        for (const auto &c : cols) {
            valid_col_names.insert(c.name);
            if (c.pk > 0 && pk_col.empty()) pk_col = c.name;
            std::string upper_type = c.type;
            std::transform(upper_type.begin(), upper_type.end(), upper_type.begin(), ::toupper);
            if (upper_type.find("CHAR") != std::string::npos || 
                upper_type.find("TEXT") != std::string::npos ||
                upper_type.find("CLOB") != std::string::npos ||
                upper_type.empty()) {
                text_cols.push_back(c.name);
            }
        }

        std::string where_clause;
        std::vector<std::string> search_binds;
        if (!search.empty() && !text_cols.empty()) {
            std::ostringstream oss;
            oss << " WHERE (";
            for (size_t i = 0; i < text_cols.size(); ++i) {
                if (i > 0) oss << " OR ";
                oss << "\"" << text_cols[i] << "\" LIKE ?";
                search_binds.push_back("%" + search + "%");
            }
            oss << ")";
            where_clause = oss.str();
        }

        // Count total
        int64_t total = 0;
        {
            std::string count_sql = "SELECT COUNT(*) FROM \"" + table + "\"" + where_clause + ";";
            auto cstmt = conn.prepare(count_sql);
            for (size_t i = 0; i < search_binds.size(); ++i) {
                cstmt.bind_text(static_cast<int>(i + 1), search_binds[i]);
            }
            if (cstmt.step() == SQLITE_ROW) {
                total = cstmt.column_int64(0);
            }
        }

        // Ordering
        std::string order_clause;
        if (!sort_by.empty() && valid_col_names.count(sort_by)) {
            order_clause = " ORDER BY \"" + sort_by + "\" " + sort_dir;
        } else if (!pk_col.empty()) {
            order_clause = " ORDER BY \"" + pk_col + "\" ASC";
        } else {
            order_clause = " ORDER BY rowid ASC";
        }

        std::string query_sql = "SELECT * FROM \"" + table + "\"" + where_clause + order_clause + " LIMIT ? OFFSET ?;";
        auto qstmt = conn.prepare(query_sql);
        int bind_idx = 1;
        for (const auto &sb : search_binds) {
            qstmt.bind_text(bind_idx++, sb);
        }
        qstmt.bind_int(bind_idx++, page_size);
        qstmt.bind_int(bind_idx++, offset);

        auto rows_arr = json::json_value::make_array();
        while (qstmt.step() == SQLITE_ROW) {
            auto row_obj = json::json_value::make_object();
            int num_cols = qstmt.column_count();
            for (int i = 0; i < num_cols; ++i) {
                const char *cname = qstmt.column_name(i);
                if (qstmt.column_is_null(i)) {
                    row_obj.add_null(cname);
                } else {
                    int ctype = qstmt.column_type(i);
                    if (ctype == SQLITE_INTEGER) {
                        row_obj.add_int64(cname, qstmt.column_int64(i));
                    } else if (ctype == SQLITE_FLOAT) {
                        row_obj.add_double(cname, qstmt.column_double(i));
                    } else {
                        row_obj.add_str(cname, qstmt.column_str(i));
                    }
                }
            }
            rows_arr.add(std::move(row_obj));
        }

        auto res = json::json_value::make_object();
        res.add_str("table", table);
        res.add_int64("total", total);
        res.add_int("page", page);
        res.add_int("pageSize", page_size);

        auto meta_arr = json::json_value::make_array();
        for (const auto &cm : cols) {
            auto mo = json::json_value::make_object();
            mo.add_str("name", cm.name);
            mo.add_str("type", cm.type);
            mo.add_bool("notnull", cm.notnull);
            mo.add_str("default_value", cm.dflt_value);
            mo.add_int("pk", cm.pk);
            meta_arr.add(std::move(mo));
        }
        res.add("columns", std::move(meta_arr));
        res.add("rows", std::move(rows_arr));

        send_json(worker, header, "200 OK", res);
        return;
    }

    if (method == "POST") {
        auto body = json::json_value::parse(worker->body());
        if (!body) {
            send_error(worker, header, "400 Bad Request", "Invalid JSON body");
            return;
        }

        auto cols = admin_studio::get_table_columns(conn, table);
        std::set<std::string> valid_cols;
        for (const auto &c : cols) valid_cols.insert(c.name);

        std::vector<std::string> insert_cols;
        std::vector<json_object *> insert_vals;

        json_object *body_obj = body.get();
        if (json_object_is_type(body_obj, json_type_object)) {
            json_object_object_foreach(body_obj, key, val) {
                if (valid_cols.count(key)) {
                    insert_cols.push_back(key);
                    insert_vals.push_back(val);
                }
            }
        }

        if (insert_cols.empty()) {
            send_error(worker, header, "400 Bad Request", "No valid columns provided for table " + table);
            return;
        }

        std::ostringstream sql;
        sql << "INSERT INTO \"" << table << "\" (";
        for (size_t i = 0; i < insert_cols.size(); ++i) {
            if (i > 0) sql << ", ";
            sql << "\"" << insert_cols[i] << "\"";
        }
        sql << ") VALUES (";
        for (size_t i = 0; i < insert_cols.size(); ++i) {
            if (i > 0) sql << ", ";
            sql << "?";
        }
        sql << ");";

        try {
            db::sqlite_transaction tx(conn);
            auto ins = conn.prepare(sql.str());
            for (size_t i = 0; i < insert_vals.size(); ++i) {
                admin_studio::bind_json_to_statement(ins, static_cast<int>(i + 1), insert_vals[i]);
            }
            ins.step_done();
            int64_t new_id = conn.last_insert_rowid();
            tx.commit();

            auto res = json::json_value::make_object();
            res.add_bool("success", true);
            res.add_int64("id", new_id);
            res.add_str("table", table);
            send_json(worker, header, "201 Created", res);
            return;
        } catch (const std::exception &ex) {
            send_error(worker, header, "400 Bad Request", std::string("Database error: ") + ex.what());
            return;
        }
    }

    send_error(worker, header, "405 Method Not Allowed", "Allowed: GET, POST");
}

// 3. PUT / DELETE /api/admin/tables/([A-Za-z0-9_]+)/row
static void handle_admin_table_row(backend::wsworker *worker,
                                   const std::map<std::string, std::string> &header,
                                   const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing table name");
        return;
    }
    std::string table = uri_params.front();
    if (!admin_studio::is_valid_identifier(table)) {
        send_error(worker, header, "400 Bad Request", "Invalid table name format");
        return;
    }

    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated || u_auth.role != "Admin") {
        send_error(worker, header, "403 Forbidden", "Admin privileges required");
        return;
    }

    if (!admin_studio::table_exists(conn, table)) {
        send_error(worker, header, "404 Not Found", "Table '" + table + "' does not exist");
        return;
    }

    std::string method = get_header(header, "REQUEST_METHOD");
    auto cols = admin_studio::get_table_columns(conn, table);
    std::set<std::string> valid_cols;
    std::set<std::string> pk_cols;
    for (const auto &c : cols) {
        valid_cols.insert(c.name);
        if (c.pk > 0) pk_cols.insert(c.name);
    }

    if (method == "PUT") {
        auto body = json::json_value::parse(worker->body());
        if (!body) {
            send_error(worker, header, "400 Bad Request", "Invalid JSON body");
            return;
        }

        std::vector<std::string> where_cols;
        std::vector<json_object *> where_vals;
        std::vector<std::string> set_cols;
        std::vector<json_object *> set_vals;

        json_object *body_obj = body.get();
        json_object *keys_obj = nullptr;
        json_object *values_obj = nullptr;

        bool has_explicit_keys = json_object_object_get_ex(body_obj, "keys", &keys_obj);
        bool has_explicit_values = json_object_object_get_ex(body_obj, "values", &values_obj);

        if (has_explicit_keys && keys_obj && json_object_is_type(keys_obj, json_type_object)) {
            json_object_object_foreach(keys_obj, k, v) {
                if (valid_cols.count(k)) {
                    where_cols.push_back(k);
                    where_vals.push_back(v);
                }
            }
        }
        if (has_explicit_values && values_obj && json_object_is_type(values_obj, json_type_object)) {
            json_object_object_foreach(values_obj, k, v) {
                if (valid_cols.count(k)) {
                    set_cols.push_back(k);
                    set_vals.push_back(v);
                }
            }
        }

        // If not using explicit keys/values structure, infer from PK columns
        if (where_cols.empty() && set_cols.empty() && json_object_is_type(body_obj, json_type_object)) {
            json_object_object_foreach(body_obj, k, v) {
                if (valid_cols.count(k)) {
                    if (pk_cols.count(k)) {
                        where_cols.push_back(k);
                        where_vals.push_back(v);
                    } else {
                        set_cols.push_back(k);
                        set_vals.push_back(v);
                    }
                }
            }
        }

        if (where_cols.empty()) {
            send_error(worker, header, "400 Bad Request", "Missing primary key or condition to identify row for update");
            return;
        }
        if (set_cols.empty()) {
            send_error(worker, header, "400 Bad Request", "No fields to update");
            return;
        }

        std::ostringstream sql;
        sql << "UPDATE \"" << table << "\" SET ";
        for (size_t i = 0; i < set_cols.size(); ++i) {
            if (i > 0) sql << ", ";
            sql << "\"" << set_cols[i] << "\" = ?";
        }
        sql << " WHERE ";
        for (size_t i = 0; i < where_cols.size(); ++i) {
            if (i > 0) sql << " AND ";
            sql << "\"" << where_cols[i] << "\" = ?";
        }
        sql << ";";

        try {
            db::sqlite_transaction tx(conn);
            auto upd = conn.prepare(sql.str());
            int bidx = 1;
            for (auto *val : set_vals) {
                admin_studio::bind_json_to_statement(upd, bidx++, val);
            }
            for (auto *val : where_vals) {
                admin_studio::bind_json_to_statement(upd, bidx++, val);
            }
            upd.step_done();
            int changes = conn.changes();
            tx.commit();

            auto res = json::json_value::make_object();
            res.add_bool("success", true);
            res.add_int("changes", changes);
            res.add_str("table", table);
            send_json(worker, header, "200 OK", res);
            return;
        } catch (const std::exception &ex) {
            send_error(worker, header, "400 Bad Request", std::string("Database error: ") + ex.what());
            return;
        }
    }

    if (method == "DELETE") {
        std::vector<std::string> where_cols;
        std::vector<json_object *> where_vals;

        auto body = json::json_value::parse(worker->body());
        if (body) {
            json_object *body_obj = body.get();
            json_object *keys_obj = nullptr;
            if (json_object_object_get_ex(body_obj, "keys", &keys_obj) && keys_obj && json_object_is_type(keys_obj, json_type_object)) {
                json_object_object_foreach(keys_obj, k, v) {
                    if (valid_cols.count(k)) {
                        where_cols.push_back(k);
                        where_vals.push_back(v);
                    }
                }
            } else if (json_object_is_type(body_obj, json_type_object)) {
                json_object_object_foreach(body_obj, k, v) {
                    if (valid_cols.count(k)) {
                        where_cols.push_back(k);
                        where_vals.push_back(v);
                    }
                }
            }
        }

        // Fallback to query args if body had no keys
        std::vector<json::json_value> owned_where_vals;
        if (where_cols.empty()) {
            auto args = worker->parse_args(get_header(header, "QUERY_STRING"));
            for (const auto &[k, v] : args) {
                if (valid_cols.count(k)) {
                    where_cols.push_back(k);
                    owned_where_vals.push_back(json::json_value::make_string(v));
                    where_vals.push_back(owned_where_vals.back().get());
                }
            }
        }

        if (where_cols.empty()) {
            send_error(worker, header, "400 Bad Request", "Missing primary key or condition to identify row for deletion");
            return;
        }

        std::ostringstream sql;
        sql << "DELETE FROM \"" << table << "\" WHERE ";
        for (size_t i = 0; i < where_cols.size(); ++i) {
            if (i > 0) sql << " AND ";
            sql << "\"" << where_cols[i] << "\" = ?";
        }
        sql << ";";

        try {
            db::sqlite_transaction tx(conn);
            auto del = conn.prepare(sql.str());
            int bidx = 1;
            for (auto *val : where_vals) {
                admin_studio::bind_json_to_statement(del, bidx++, val);
            }
            del.step_done();
            int changes = conn.changes();
            tx.commit();

            auto res = json::json_value::make_object();
            res.add_bool("success", true);
            res.add_int("changes", changes);
            res.add_str("table", table);
            send_json(worker, header, "200 OK", res);
            return;
        } catch (const std::exception &ex) {
            send_error(worker, header, "400 Bad Request", std::string("Database error: ") + ex.what());
            return;
        }
    }

    send_error(worker, header, "405 Method Not Allowed", "Allowed: PUT, DELETE");
}

// 4. PUT / DELETE /api/users/([0-9]+)
static void handle_user_detail(backend::wsworker *worker,
                               const std::map<std::string, std::string> &header,
                               const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing user ID");
        return;
    }
    int uid = std::atoi(uri_params.front().c_str());
    if (uid <= 0) {
        send_error(worker, header, "400 Bad Request", "Invalid user ID");
        return;
    }

    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated || u_auth.role != "Admin") {
        send_error(worker, header, "403 Forbidden", "Admin privileges required");
        return;
    }

    std::string method = get_header(header, "REQUEST_METHOD");

    if (method == "PUT") {
        auto body = json::json_value::parse(worker->body());
        if (!body) {
            send_error(worker, header, "400 Bad Request", "Invalid JSON body");
            return;
        }

        std::string email = body.get_string("email");
        std::string dname = body.get_string("display_name");
        std::string role = body.get_string("role");
        std::string color = body.get_string("avatar_color");
        std::string pw = body.get_string("password");
        std::string user_type = body.get_string("user_type");

        try {
            db::sqlite_transaction tx(conn);
            std::string sql = R"SQL(
                UPDATE users SET
                    email = CASE WHEN ? != '' THEN ? ELSE email END,
                    display_name = CASE WHEN ? != '' THEN ? ELSE display_name END,
                    role = CASE WHEN ? != '' THEN ? ELSE role END,
                    avatar_color = CASE WHEN ? != '' THEN ? ELSE avatar_color END,
                    password_hash = CASE WHEN ? != '' THEN ? ELSE password_hash END,
                    user_type = CASE WHEN ? != '' THEN ? ELSE user_type END
                WHERE id = ?;
            )SQL";

            auto upd = conn.prepare(sql);
            std::string p_hash = pw.empty() ? "" : utils::hash_password(pw);
            upd.bind_text(1, email);
            upd.bind_text(2, email);
            upd.bind_text(3, dname);
            upd.bind_text(4, dname);
            upd.bind_text(5, role);
            upd.bind_text(6, role);
            upd.bind_text(7, color);
            upd.bind_text(8, color);
            upd.bind_text(9, p_hash);
            upd.bind_text(10, p_hash);
            upd.bind_text(11, user_type);
            upd.bind_text(12, user_type);
            upd.bind_int(13, uid);
            upd.step_done();
            tx.commit();

            auto sel = conn.prepare("SELECT id, username, email, display_name, role, avatar_color, created_at, COALESCE(user_type, 'INTERNAL') FROM users WHERE id = ?;");
            sel.bind_int(1, uid);
            if (sel.step() == SQLITE_ROW) {
                auth_user updated_u{
                    sel.column_int(0),
                    sel.column_str(1),
                    sel.column_str(2),
                    sel.column_str(3),
                    sel.column_str(4),
                    sel.column_str(5),
                    sel.column_str(7),
                    true
                };
                send_json(worker, header, "200 OK", user_to_json(updated_u));
                return;
            } else {
                send_error(worker, header, "404 Not Found", "User not found");
                return;
            }
        } catch (const std::exception &ex) {
            send_error(worker, header, "400 Bad Request", ex.what());
            return;
        }
    }

    if (method == "DELETE") {
        if (u_auth.id == uid) {
            send_error(worker, header, "400 Bad Request", "Cannot delete your own active administrator account");
            return;
        }

        try {
            db::sqlite_transaction tx(conn);
            // Clean up user sessions and tokens
            auto del_sess = conn.prepare("DELETE FROM sessions WHERE user_id = ?;");
            del_sess.bind_int(1, uid);
            del_sess.step_done();

            auto del_pat = conn.prepare("DELETE FROM personal_access_tokens WHERE user_id = ?;");
            del_pat.bind_int(1, uid);
            del_pat.step_done();

            auto del_ug = conn.prepare("DELETE FROM user_group_members WHERE user_id = ?;");
            del_ug.bind_int(1, uid);
            del_ug.step_done();

            auto del_u = conn.prepare("DELETE FROM users WHERE id = ?;");
            del_u.bind_int(1, uid);
            del_u.step_done();
            tx.commit();

            auto res = json::json_value::make_object();
            res.add_bool("deleted", true);
            res.add_int("id", uid);
            send_json(worker, header, "200 OK", res);
            return;
        } catch (const std::exception &ex) {
            send_error(worker, header, "400 Bad Request", ex.what());
            return;
        }
    }

    send_error(worker, header, "405 Method Not Allowed", "Allowed: PUT, DELETE");
}

// 5. PUT / DELETE /api/projects/([0-9]+)
static void handle_project_detail(backend::wsworker *worker,
                                  const std::map<std::string, std::string> &header,
                                  const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing project ID");
        return;
    }
    int proj_id = std::atoi(uri_params.front().c_str());
    if (proj_id <= 0) {
        send_error(worker, header, "400 Bad Request", "Invalid project ID");
        return;
    }

    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated || u_auth.role != "Admin") {
        send_error(worker, header, "403 Forbidden", "Admin privileges required");
        return;
    }

    std::string method = get_header(header, "REQUEST_METHOD");

    if (method == "PUT") {
        auto body = json::json_value::parse(worker->body());
        if (!body) {
            send_error(worker, header, "400 Bad Request", "Invalid JSON body");
            return;
        }

        std::string name = body.get_string("name");
        std::string key = body.get_string("key");
        bool has_desc = body.has_key("description");
        std::string description = body.get_string("description");
        int lead_user_id = body.get_int("lead_user_id", 0);
        bool has_def_assignee = body.has_key("default_assignee_type");
        std::string default_assignee_type = body.get_string("default_assignee_type", "PROJECT_LEAD");

        try {
            db::sqlite_transaction tx(conn);
            std::string sql = R"SQL(
                UPDATE projects SET
                    name = CASE WHEN ? != '' THEN ? ELSE name END,
                    key = CASE WHEN ? != '' THEN ? ELSE key END,
                    description = CASE WHEN ? != 0 THEN ? ELSE description END,
                    lead_user_id = CASE WHEN ? > 0 THEN ? ELSE lead_user_id END,
                    default_assignee_type = CASE WHEN ? != 0 THEN ? ELSE default_assignee_type END
                WHERE id = ?;
            )SQL";

            auto upd = conn.prepare(sql);
            upd.bind_text(1, name);
            upd.bind_text(2, name);
            upd.bind_text(3, key);
            upd.bind_text(4, key);
            upd.bind_int(5, has_desc ? 1 : 0);
            upd.bind_text(6, description);
            upd.bind_int(7, lead_user_id);
            upd.bind_int(8, lead_user_id);
            upd.bind_int(9, has_def_assignee ? 1 : 0);
            upd.bind_text(10, default_assignee_type);
            upd.bind_int(11, proj_id);
            upd.step_done();
            tx.commit();

            auto sel = conn.prepare(R"SQL(
                SELECT p.id, p.key, p.name, p.description, p.lead_user_id,
                       COALESCE(u.display_name, ''), p.created_at,
                       COALESCE(p.default_assignee_type, 'PROJECT_LEAD')
                FROM projects p
                LEFT JOIN users u ON u.id = p.lead_user_id
                WHERE p.id = ?;
            )SQL");
            sel.bind_int(1, proj_id);
            if (sel.step() == SQLITE_ROW) {
                auto res = json::json_value::make_object();
                res.add_int("id", sel.column_int(0));
                res.add_str("key", sel.column_str(1));
                res.add_str("name", sel.column_str(2));
                res.add_str("description", sel.column_str(3));
                res.add_int("lead_user_id", sel.column_int(4));
                res.add_str("lead_user_name", sel.column_str(5));
                res.add_str("created_at", sel.column_str(6));
                res.add_str("default_assignee_type", sel.column_str(7));
                send_json(worker, header, "200 OK", res);
                return;
            } else {
                send_error(worker, header, "404 Not Found", "Project not found");
                return;
            }
        } catch (const std::exception &ex) {
            send_error(worker, header, "400 Bad Request", ex.what());
            return;
        }
    }

    if (method == "DELETE") {
        try {
            db::sqlite_transaction tx(conn);
            // Cascade delete project dependencies
            {
                auto s1 = conn.prepare("DELETE FROM components WHERE project_id = ?;");
                s1.bind_int(1, proj_id);
                s1.step_done();
                auto s2 = conn.prepare("DELETE FROM versions WHERE project_id = ?;");
                s2.bind_int(1, proj_id);
                s2.step_done();
                auto s3 = conn.prepare("DELETE FROM sprints WHERE project_id = ?;");
                s3.bind_int(1, proj_id);
                s3.step_done();
                auto s4 = conn.prepare("DELETE FROM boards WHERE project_id = ?;");
                s4.bind_int(1, proj_id);
                s4.step_done();
                auto s5 = conn.prepare("DELETE FROM work_items WHERE project_id = ?;");
                s5.bind_int(1, proj_id);
                s5.step_done();
            }
            
            auto del = conn.prepare("DELETE FROM projects WHERE id = ?;");
            del.bind_int(1, proj_id);
            del.step_done();
            tx.commit();

            auto res = json::json_value::make_object();
            res.add_bool("deleted", true);
            res.add_int("id", proj_id);
            send_json(worker, header, "200 OK", res);
            return;
        } catch (const std::exception &ex) {
            send_error(worker, header, "400 Bad Request", ex.what());
            return;
        }
    }

    send_error(worker, header, "405 Method Not Allowed", "Allowed: PUT, DELETE");
}

// 6. PUT / DELETE /api/comments/([0-9]+)
static void handle_comment_detail(backend::wsworker *worker,
                                  const std::map<std::string, std::string> &header,
                                  const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing comment ID");
        return;
    }
    int comment_id = std::atoi(uri_params.front().c_str());

    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    // Check comment ownership or admin privilege
    int author_id = 0;
    int work_item_id = 0;
    {
        auto stmt = conn.prepare("SELECT author_id, work_item_id FROM comments WHERE id = ?;");
        stmt.bind_int(1, comment_id);
        if (stmt.step() == SQLITE_ROW) {
            author_id = stmt.column_int(0);
            work_item_id = stmt.column_int(1);
        } else {
            send_error(worker, header, "404 Not Found", "Comment not found");
            return;
        }
    }

    if (u_auth.role != "Admin" && u_auth.id != author_id) {
        send_error(worker, header, "403 Forbidden", "You can only edit or delete your own comments");
        return;
    }

    std::string method = get_header(header, "REQUEST_METHOD");

    if (method == "PUT") {
        auto body = json::json_value::parse(worker->body());
        if (!body) {
            send_error(worker, header, "400 Bad Request", "Invalid JSON body");
            return;
        }

        std::string new_body = body.get_string("body");
        if (new_body.empty()) {
            send_error(worker, header, "400 Bad Request", "Comment body cannot be empty");
            return;
        }

        try {
            db::sqlite_transaction tx(conn);
            auto upd = conn.prepare("UPDATE comments SET body = ? WHERE id = ?;");
            upd.bind_text(1, new_body);
            upd.bind_int(2, comment_id);
            upd.step_done();

            if (body.has_key("is_internal")) {
                bool is_int = body.get_bool("is_internal", false) || (body.get_int("is_internal", 0) != 0);
                auto upd_int = conn.prepare("UPDATE comments SET is_internal = ? WHERE id = ?;");
                upd_int.bind_int(1, is_int ? 1 : 0);
                upd_int.bind_int(2, comment_id);
                upd_int.step_done();
            }

            // Refresh mentions
            bool eff_is_internal = false;
            {
                auto q_int = conn.prepare("SELECT COALESCE(is_internal, 0) FROM comments WHERE id = ?;");
                q_int.bind_int(1, comment_id);
                if (q_int.step() == SQLITE_ROW) {
                    eff_is_internal = (q_int.column_int(0) != 0);
                }
            }
            auto del_m = conn.prepare("DELETE FROM work_item_mentions WHERE comment_id = ?;");
            del_m.bind_int(1, comment_id);
            del_m.step_done();
            collaboration::process_mentions_and_links(conn, work_item_id, comment_id, u_auth.id, new_body, eff_is_internal);

            tx.commit();

            auto res = json::json_value::make_object();
            res.add_int("id", comment_id);
            res.add_str("body", new_body);
            res.add_bool("updated", true);
            send_json(worker, header, "200 OK", res);
            return;
        } catch (const std::exception &ex) {
            send_error(worker, header, "400 Bad Request", ex.what());
            return;
        }
    }

    if (method == "DELETE") {
        try {
            db::sqlite_transaction tx(conn);
            auto del_m = conn.prepare("DELETE FROM work_item_mentions WHERE comment_id = ?;");
            del_m.bind_int(1, comment_id);
            del_m.step_done();

            auto del_c = conn.prepare("DELETE FROM comments WHERE id = ?;");
            del_c.bind_int(1, comment_id);
            del_c.step_done();
            tx.commit();

            auto res = json::json_value::make_object();
            res.add_bool("deleted", true);
            res.add_int("id", comment_id);
            send_json(worker, header, "200 OK", res);
            return;
        } catch (const std::exception &ex) {
            send_error(worker, header, "400 Bad Request", ex.what());
            return;
        }
    }

    send_error(worker, header, "405 Method Not Allowed", "Allowed: PUT, DELETE");
}

// 7. PUT / DELETE /api/resolutions/([0-9]+)
static void handle_resolution_detail(backend::wsworker *worker,
                                     const std::map<std::string, std::string> &header,
                                     const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing resolution ID");
        return;
    }
    int res_id = std::atoi(uri_params.front().c_str());

    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated || u_auth.role != "Admin") {
        send_error(worker, header, "403 Forbidden", "Admin privileges required");
        return;
    }

    std::string method = get_header(header, "REQUEST_METHOD");

    if (method == "PUT") {
        auto body = json::json_value::parse(worker->body());
        if (!body) {
            send_error(worker, header, "400 Bad Request", "Invalid JSON body");
            return;
        }

        std::string name = body.get_string("name");
        std::string description = body.get_string("description");
        int sort_order = body.get_int("sort_order", 100);
        bool is_default = body.get_bool("is_default", false);

        try {
            db::sqlite_transaction tx(conn);
            if (is_default) {
                conn.exec("UPDATE resolutions SET is_default = 0;");
            }

            auto upd = conn.prepare(R"SQL(
                UPDATE resolutions SET
                    name = CASE WHEN ? != '' THEN ? ELSE name END,
                    description = ?,
                    sort_order = ?,
                    is_default = ?
                WHERE id = ?;
            )SQL");
            upd.bind_text(1, name);
            upd.bind_text(2, name);
            upd.bind_text(3, description);
            upd.bind_int(4, sort_order);
            upd.bind_int(5, is_default ? 1 : 0);
            upd.bind_int(6, res_id);
            upd.step_done();
            tx.commit();

            auto sel = conn.prepare("SELECT id, name, description, sort_order, is_default, created_at FROM resolutions WHERE id = ?;");
            sel.bind_int(1, res_id);
            if (sel.step() == SQLITE_ROW) {
                auto res = json::json_value::make_object();
                res.add_int("id", sel.column_int(0));
                res.add_str("name", sel.column_str(1));
                res.add_str("description", sel.column_str(2));
                res.add_int("sort_order", sel.column_int(3));
                res.add_bool("is_default", sel.column_int(4) != 0);
                res.add_str("created_at", sel.column_str(5));
                send_json(worker, header, "200 OK", res);
                return;
            } else {
                send_error(worker, header, "404 Not Found", "Resolution not found");
                return;
            }
        } catch (const std::exception &ex) {
            send_error(worker, header, "400 Bad Request", ex.what());
            return;
        }
    }

    if (method == "DELETE") {
        try {
            db::sqlite_transaction tx(conn);
            auto del = conn.prepare("DELETE FROM resolutions WHERE id = ?;");
            del.bind_int(1, res_id);
            del.step_done();
            tx.commit();

            auto res = json::json_value::make_object();
            res.add_bool("deleted", true);
            res.add_int("id", res_id);
            send_json(worker, header, "200 OK", res);
            return;
        } catch (const std::exception &ex) {
            send_error(worker, header, "400 Bad Request", ex.what());
            return;
        }
    }

    send_error(worker, header, "405 Method Not Allowed", "Allowed: PUT, DELETE");
}

// 8. PUT / DELETE /api/issue-types/([0-9]+)
static void handle_issue_type_detail(backend::wsworker *worker,
                                     const std::map<std::string, std::string> &header,
                                     const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing issue type ID");
        return;
    }
    int it_id = std::atoi(uri_params.front().c_str());

    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated || u_auth.role != "Admin") {
        send_error(worker, header, "403 Forbidden", "Admin privileges required");
        return;
    }

    std::string method = get_header(header, "REQUEST_METHOD");

    if (method == "PUT") {
        auto body = json::json_value::parse(worker->body());
        if (!body) {
            send_error(worker, header, "400 Bad Request", "Invalid JSON body");
            return;
        }

        std::string name = body.get_string("name");
        std::string description = body.get_string("description");
        std::string color = body.get_string("icon_color");
        if (color.empty()) color = body.get_string("color");
        bool is_subtask = body.get_bool("is_subtask", false);
        int hierarchy_level = body.get_int("hierarchy_level", 0);

        try {
            db::sqlite_transaction tx(conn);
            auto upd = conn.prepare(R"SQL(
                UPDATE issue_types SET
                    name = CASE WHEN ? != '' THEN ? ELSE name END,
                    description = ?,
                    icon_color = CASE WHEN ? != '' THEN ? ELSE icon_color END,
                    is_subtask = ?,
                    hierarchy_level = ?
                WHERE id = ?;
            )SQL");
            upd.bind_text(1, name);
            upd.bind_text(2, name);
            upd.bind_text(3, description);
            upd.bind_text(4, color);
            upd.bind_text(5, color);
            upd.bind_int(6, is_subtask ? 1 : 0);
            upd.bind_int(7, hierarchy_level);
            upd.bind_int(8, it_id);
            upd.step_done();
            tx.commit();

            auto sel = conn.prepare("SELECT id, name, description, icon_color, is_subtask, hierarchy_level, created_at FROM issue_types WHERE id = ?;");
            sel.bind_int(1, it_id);
            if (sel.step() == SQLITE_ROW) {
                auto res = json::json_value::make_object();
                res.add_int("id", sel.column_int(0));
                res.add_str("name", sel.column_str(1));
                res.add_str("description", sel.column_str(2));
                res.add_str("icon_color", sel.column_str(3));
                res.add_str("color", sel.column_str(3));
                res.add_bool("is_subtask", sel.column_int(4) != 0);
                res.add_int("hierarchy_level", sel.column_int(5));
                res.add_str("created_at", sel.column_str(6));
                send_json(worker, header, "200 OK", res);
                return;
            } else {
                send_error(worker, header, "404 Not Found", "Issue type not found");
                return;
            }
        } catch (const std::exception &ex) {
            send_error(worker, header, "400 Bad Request", ex.what());
            return;
        }
    }

    if (method == "DELETE") {
        try {
            db::sqlite_transaction tx(conn);
            auto del_si = conn.prepare("DELETE FROM issue_type_scheme_items WHERE issue_type_id = ?;");
            del_si.bind_int(1, it_id);
            del_si.step_done();

            auto del = conn.prepare("DELETE FROM issue_types WHERE id = ?;");
            del.bind_int(1, it_id);
            del.step_done();
            tx.commit();

            auto res = json::json_value::make_object();
            res.add_bool("deleted", true);
            res.add_int("id", it_id);
            send_json(worker, header, "200 OK", res);
            return;
        } catch (const std::exception &ex) {
            send_error(worker, header, "400 Bad Request", ex.what());
            return;
        }
    }

    send_error(worker, header, "405 Method Not Allowed", "Allowed: PUT, DELETE");
}

// 9. PUT / DELETE /api/custom-field-options/([0-9]+)
static void handle_custom_field_option_detail(backend::wsworker *worker,
                                              const std::map<std::string, std::string> &header,
                                              const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing option ID");
        return;
    }
    int opt_id = std::atoi(uri_params.front().c_str());

    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated || u_auth.role != "Admin") {
        send_error(worker, header, "403 Forbidden", "Admin privileges required");
        return;
    }

    std::string method = get_header(header, "REQUEST_METHOD");

    if (method == "PUT") {
        auto body = json::json_value::parse(worker->body());
        if (!body) {
            send_error(worker, header, "400 Bad Request", "Invalid JSON body");
            return;
        }

        std::string val = body.get_string("value");
        std::string color = body.get_string("color");
        bool has_sort = body.has_key("sort_order");
        int sort_order = body.get_int("sort_order", 0);
        bool has_active = body.has_key("is_active");
        bool is_active = body.get_bool("is_active", true);
        bool has_default = body.has_key("is_default");
        bool is_default = body.get_bool("is_default", false);

        try {
            db::sqlite_transaction tx(conn);
            auto upd = conn.prepare(R"SQL(
                UPDATE custom_field_options SET
                    value = CASE WHEN ? != '' THEN ? ELSE value END,
                    color = CASE WHEN ? != '' THEN ? ELSE color END,
                    sort_order = CASE WHEN ? != 0 THEN ? ELSE sort_order END,
                    is_active = CASE WHEN ? != 0 THEN ? ELSE is_active END,
                    is_default = CASE WHEN ? != 0 THEN ? ELSE is_default END
                WHERE id = ?;
            )SQL");
            upd.bind_text(1, val);
            upd.bind_text(2, val);
            upd.bind_text(3, color);
            upd.bind_text(4, color);
            upd.bind_int(5, has_sort ? 1 : 0);
            upd.bind_int(6, sort_order);
            upd.bind_int(7, has_active ? 1 : 0);
            upd.bind_int(8, is_active ? 1 : 0);
            upd.bind_int(9, has_default ? 1 : 0);
            upd.bind_int(10, is_default ? 1 : 0);
            upd.bind_int(11, opt_id);
            upd.step_done();
            tx.commit();

            auto sel = conn.prepare("SELECT id, custom_field_id, value, color, sort_order, is_active, is_default FROM custom_field_options WHERE id = ?;");
            sel.bind_int(1, opt_id);
            if (sel.step() == SQLITE_ROW) {
                auto res = json::json_value::make_object();
                res.add_int("id", sel.column_int(0));
                res.add_int("custom_field_id", sel.column_int(1));
                res.add_str("value", sel.column_str(2));
                res.add_str("color", sel.column_str(3));
                res.add_int("sort_order", sel.column_int(4));
                res.add_bool("is_active", sel.column_int(5) != 0);
                res.add_bool("is_default", sel.column_int(6) != 0);
                send_json(worker, header, "200 OK", res);
                return;
            } else {
                send_error(worker, header, "404 Not Found", "Option not found");
                return;
            }
        } catch (const std::exception &ex) {
            send_error(worker, header, "400 Bad Request", ex.what());
            return;
        }
    }

    if (method == "DELETE") {
        try {
            db::sqlite_transaction tx(conn);
            auto del = conn.prepare("DELETE FROM custom_field_options WHERE id = ?;");
            del.bind_int(1, opt_id);
            del.step_done();
            tx.commit();

            auto res = json::json_value::make_object();
            res.add_bool("deleted", true);
            res.add_int("id", opt_id);
            send_json(worker, header, "200 OK", res);
            return;
        } catch (const std::exception &ex) {
            send_error(worker, header, "400 Bad Request", ex.what());
            return;
        }
    }

    send_error(worker, header, "405 Method Not Allowed", "Allowed: PUT, DELETE");
}

// 10. PUT / DELETE /api/workflow-schemes/([0-9]+)
static void handle_workflow_scheme_detail(backend::wsworker *worker,
                                          const std::map<std::string, std::string> &header,
                                          const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing workflow scheme ID");
        return;
    }
    int sid = std::atoi(uri_params.front().c_str());

    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated || u_auth.role != "Admin") {
        send_error(worker, header, "403 Forbidden", "Admin privileges required");
        return;
    }

    std::string method = get_header(header, "REQUEST_METHOD");

    if (method == "PUT") {
        auto body = json::json_value::parse(worker->body());
        if (!body) {
            send_error(worker, header, "400 Bad Request", "Invalid JSON body");
            return;
        }

        std::string name = body.get_string("name");
        std::string description = body.get_string("description");
        int workflow_id = body.get_int("workflow_id", 0);
        std::string issue_type = body.get_string("issue_type");
        bool is_default = body.get_bool("is_default", false);

        try {
            db::sqlite_transaction tx(conn);
            auto upd = conn.prepare(R"SQL(
                UPDATE workflow_schemes SET
                    name = CASE WHEN ? != '' THEN ? ELSE name END,
                    description = ?,
                    workflow_id = CASE WHEN ? > 0 THEN ? ELSE workflow_id END,
                    issue_type = CASE WHEN ? != '' THEN ? ELSE issue_type END,
                    is_default = ?
                WHERE id = ?;
            )SQL");
            upd.bind_text(1, name);
            upd.bind_text(2, name);
            upd.bind_text(3, description);
            upd.bind_int(4, workflow_id);
            upd.bind_int(5, workflow_id);
            upd.bind_text(6, issue_type);
            upd.bind_text(7, issue_type);
            upd.bind_int(8, is_default ? 1 : 0);
            upd.bind_int(9, sid);
            upd.step_done();
            tx.commit();

            auto sel = conn.prepare("SELECT id, name, description, workflow_id, issue_type, is_default, created_at FROM workflow_schemes WHERE id = ?;");
            sel.bind_int(1, sid);
            if (sel.step() == SQLITE_ROW) {
                auto res = json::json_value::make_object();
                res.add_int("id", sel.column_int(0));
                res.add_str("name", sel.column_str(1));
                res.add_str("description", sel.column_str(2));
                res.add_int("workflow_id", sel.column_int(3));
                res.add_str("issue_type", sel.column_str(4));
                res.add_bool("is_default", sel.column_int(5) != 0);
                res.add_str("created_at", sel.column_str(6));
                send_json(worker, header, "200 OK", res);
                return;
            } else {
                send_error(worker, header, "404 Not Found", "Scheme not found");
                return;
            }
        } catch (const std::exception &ex) {
            send_error(worker, header, "400 Bad Request", ex.what());
            return;
        }
    }

    if (method == "DELETE") {
        try {
            db::sqlite_transaction tx(conn);
            auto del = conn.prepare("DELETE FROM workflow_schemes WHERE id = ?;");
            del.bind_int(1, sid);
            del.step_done();
            tx.commit();

            auto res = json::json_value::make_object();
            res.add_bool("deleted", true);
            res.add_int("id", sid);
            send_json(worker, header, "200 OK", res);
            return;
        } catch (const std::exception &ex) {
            send_error(worker, header, "400 Bad Request", ex.what());
            return;
        }
    }

    send_error(worker, header, "405 Method Not Allowed", "Allowed: PUT, DELETE");
}

// 11. DELETE log tables maintenance
static void handle_clear_automation_logs(backend::wsworker *worker,
                                        const std::map<std::string, std::string> &header,
                                        const std::list<std::string> &) {
    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated || u_auth.role != "Admin") {
        send_error(worker, header, "403 Forbidden", "Admin privileges required");
        return;
    }
    conn.exec("DELETE FROM automation_logs;");
    auto res = json::json_value::make_object();
    res.add_bool("cleared", true);
    send_json(worker, header, "200 OK", res);
}

static void handle_clear_mail_logs(backend::wsworker *worker,
                                   const std::map<std::string, std::string> &header,
                                   const std::list<std::string> &) {
    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated || u_auth.role != "Admin") {
        send_error(worker, header, "403 Forbidden", "Admin privileges required");
        return;
    }
    conn.exec("DELETE FROM inbound_mail_logs;");
    auto res = json::json_value::make_object();
    res.add_bool("cleared", true);
    send_json(worker, header, "200 OK", res);
}

static void handle_clear_webhook_deliveries(backend::wsworker *worker,
                                            const std::map<std::string, std::string> &header,
                                            const std::list<std::string> &) {
    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated || u_auth.role != "Admin") {
        send_error(worker, header, "403 Forbidden", "Admin privileges required");
        return;
    }
    conn.exec("DELETE FROM webhook_deliveries;");
    auto res = json::json_value::make_object();
    res.add_bool("cleared", true);
    send_json(worker, header, "200 OK", res);
}
