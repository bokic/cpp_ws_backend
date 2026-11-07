#pragma once

#include "raii_sqlite.h"
#include "raii_json.h"
#include "crypto_utils.h"
#include "realtime_engine.h"
#include <string>
#include <vector>
#include <filesystem>
#include <sstream>
#include <iostream>
#include <chrono>

namespace bulk_trash {

// Snapshot a work item and its related tables into JSON
inline std::string snapshot_work_item(db::sqlite_db &conn,
                                      int item_id,
                                      std::string &out_key,
                                      int &out_proj_id,
                                      std::string &out_proj_key,
                                      std::string &out_type,
                                      std::string &out_title) {
    auto root = json::json_value::make_object();
    
    // 1. Work items row
    {
        auto stmt = conn.prepare(R"SQL(
            SELECT w.id, w.project_id, w.key, w.type, w.category, w.status, w.priority,
                   w.title, w.description, w.parent_id, w.sprint_id, w.assignee_id, w.reporter_id,
                   w.story_points, w.progress, w.start_date, w.due_date, w.severity, w.steps_to_reproduce,
                   w.labels, w.board_order, w.resolution_id, w.resolution_date,
                   w.original_estimate_seconds, w.remaining_estimate_seconds, w.time_spent_seconds,
                   w.tshirt_size, w.security_level_id, w.service_desk_id, w.request_type_id,
                   w.customer_reporter_name, w.customer_reporter_email, w.first_response_at,
                   w.is_customer_request, w.created_at, w.updated_at,
                   COALESCE(p.key, 'PROJ')
            FROM work_items w
            JOIN projects p ON p.id = w.project_id
            WHERE w.id = ?;
        )SQL");
        stmt.bind_int(1, item_id);
        if (stmt.step() != SQLITE_ROW) {
            return "";
        }

        out_proj_id = stmt.column_int(1);
        out_key = stmt.column_str(2);
        out_type = stmt.column_str(3);
        out_title = stmt.column_str(7);
        out_proj_key = stmt.column_str(36);

        auto wi = json::json_value::make_object();
        wi.add_int("id", stmt.column_int(0));
        wi.add_int("project_id", stmt.column_int(1));
        wi.add_str("key", stmt.column_str(2));
        wi.add_str("type", stmt.column_str(3));
        wi.add_str("category", stmt.column_str(4));
        wi.add_str("status", stmt.column_str(5));
        wi.add_str("priority", stmt.column_str(6));
        wi.add_str("title", stmt.column_str(7));
        wi.add_str("description", stmt.column_str(8));
        wi.add_nullable_int("parent_id", stmt.column_int(9));
        wi.add_nullable_int("sprint_id", stmt.column_int(10));
        wi.add_nullable_int("assignee_id", stmt.column_int(11));
        wi.add_nullable_int("reporter_id", stmt.column_int(12));
        wi.add_int("story_points", stmt.column_int(13));
        wi.add_int("progress", stmt.column_int(14));
        wi.add_str("start_date", stmt.column_str(15));
        wi.add_str("due_date", stmt.column_str(16));
        wi.add_str("severity", stmt.column_str(17));
        wi.add_str("steps_to_reproduce", stmt.column_str(18));
        wi.add_str("labels", stmt.column_str(19));
        wi.add_int("board_order", stmt.column_int(20));
        wi.add_nullable_int("resolution_id", stmt.column_int(21));
        wi.add_str("resolution_date", stmt.column_str(22));
        wi.add_int64("original_estimate_seconds", stmt.column_int64(23));
        wi.add_int64("remaining_estimate_seconds", stmt.column_int64(24));
        wi.add_int64("time_spent_seconds", stmt.column_int64(25));
        wi.add_str("tshirt_size", stmt.column_str(26));
        wi.add_nullable_int("security_level_id", stmt.column_int(27));
        wi.add_nullable_int("service_desk_id", stmt.column_int(28));
        wi.add_nullable_int("request_type_id", stmt.column_int(29));
        wi.add_str("customer_reporter_name", stmt.column_str(30));
        wi.add_str("customer_reporter_email", stmt.column_str(31));
        wi.add_str("first_response_at", stmt.column_str(32));
        wi.add_int("is_customer_request", stmt.column_int(33));
        wi.add_str("created_at", stmt.column_str(34));
        wi.add_str("updated_at", stmt.column_str(35));
        root.add("work_item", std::move(wi));
    }

    // 2. Custom fields
    {
        auto cf_arr = json::json_value::make_array();
        auto stmt = conn.prepare(R"SQL(
            SELECT custom_field_id, text_value, numeric_value, date_value, user_value_id, json_value
            FROM custom_field_values
            WHERE work_item_id = ?;
        )SQL");
        stmt.bind_int(1, item_id);
        while (stmt.step() == SQLITE_ROW) {
            auto cf = json::json_value::make_object();
            cf.add_int("custom_field_id", stmt.column_int(0));
            cf.add_str("text_value", stmt.column_str(1));
            cf.add_double("numeric_value", stmt.column_double(2));
            cf.add_str("date_value", stmt.column_str(3));
            cf.add_nullable_int("user_value_id", stmt.column_int(4));
            cf.add_str("json_value", stmt.column_str(5));
            cf_arr.add(std::move(cf));
        }
        root.add("custom_fields", std::move(cf_arr));
    }

    // 3. Components
    {
        auto comp_arr = json::json_value::make_array();
        auto stmt = conn.prepare("SELECT component_id FROM work_item_components WHERE work_item_id = ?;");
        stmt.bind_int(1, item_id);
        while (stmt.step() == SQLITE_ROW) {
            comp_arr.add(json::json_value::make_int(stmt.column_int(0)));
        }
        root.add("components", std::move(comp_arr));
    }

    // 4. Versions
    {
        auto ver_arr = json::json_value::make_array();
        auto stmt = conn.prepare("SELECT version_id, relation_type FROM work_item_versions WHERE work_item_id = ?;");
        stmt.bind_int(1, item_id);
        while (stmt.step() == SQLITE_ROW) {
            auto v = json::json_value::make_object();
            v.add_int("version_id", stmt.column_int(0));
            v.add_str("relation_type", stmt.column_str(1));
            ver_arr.add(std::move(v));
        }
        root.add("versions", std::move(ver_arr));
    }

    // 5. Attachments metadata
    {
        auto att_arr = json::json_value::make_array();
        auto stmt = conn.prepare("SELECT id, original_filename, blob_filename, size_bytes, mime_type, uploader_id, created_at FROM attachments WHERE work_item_id = ?;");
        stmt.bind_int(1, item_id);
        while (stmt.step() == SQLITE_ROW) {
            auto a = json::json_value::make_object();
            a.add_int("id", stmt.column_int(0));
            a.add_str("original_filename", stmt.column_str(1));
            a.add_str("blob_filename", stmt.column_str(2));
            a.add_int64("size_bytes", stmt.column_int64(3));
            a.add_str("mime_type", stmt.column_str(4));
            a.add_nullable_int("uploader_id", stmt.column_int(5));
            a.add_str("created_at", stmt.column_str(6));
            att_arr.add(std::move(a));
        }
        root.add("attachments", std::move(att_arr));
    }

    // 6. Comments
    {
        auto comm_arr = json::json_value::make_array();
        auto stmt = conn.prepare("SELECT id, author_id, body, created_at FROM comments WHERE work_item_id = ?;");
        stmt.bind_int(1, item_id);
        while (stmt.step() == SQLITE_ROW) {
            auto c = json::json_value::make_object();
            c.add_int("id", stmt.column_int(0));
            c.add_nullable_int("author_id", stmt.column_int(1));
            c.add_str("body", stmt.column_str(2));
            c.add_str("created_at", stmt.column_str(3));
            comm_arr.add(std::move(c));
        }
        root.add("comments", std::move(comm_arr));
    }

    // 7. Worklogs
    {
        auto wl_arr = json::json_value::make_array();
        auto stmt = conn.prepare("SELECT id, author_id, time_spent_seconds, started_at, description, created_at FROM worklogs WHERE work_item_id = ?;");
        stmt.bind_int(1, item_id);
        while (stmt.step() == SQLITE_ROW) {
            auto wl = json::json_value::make_object();
            wl.add_int("id", stmt.column_int(0));
            wl.add_nullable_int("author_id", stmt.column_int(1));
            wl.add_int64("time_spent_seconds", stmt.column_int64(2));
            wl.add_str("started_at", stmt.column_str(3));
            wl.add_str("description", stmt.column_str(4));
            wl.add_str("created_at", stmt.column_str(5));
            wl_arr.add(std::move(wl));
        }
        root.add("worklogs", std::move(wl_arr));
    }

    // 8. Item Links
    {
        auto lnk_arr = json::json_value::make_array();
        auto stmt = conn.prepare("SELECT id, source_item_id, target_item_id, link_type FROM work_item_links WHERE source_item_id = ? OR target_item_id = ?;");
        stmt.bind_int(1, item_id);
        stmt.bind_int(2, item_id);
        while (stmt.step() == SQLITE_ROW) {
            auto l = json::json_value::make_object();
            l.add_int("id", stmt.column_int(0));
            l.add_int("source_item_id", stmt.column_int(1));
            l.add_int("target_item_id", stmt.column_int(2));
            l.add_str("link_type", stmt.column_str(3));
            lnk_arr.add(std::move(l));
        }
        root.add("links", std::move(lnk_arr));
    }

    return root.to_string();
}

// Soft delete work item into trash_bin
inline int soft_delete_item(db::sqlite_db &conn,
                            int item_id,
                            int user_id,
                            const std::string &,
                            std::string &err_msg) {
    std::string item_key, proj_key, item_type, title;
    int proj_id = 0;

    std::string snapshot_json = snapshot_work_item(conn, item_id, item_key, proj_id, proj_key, item_type, title);
    if (snapshot_json.empty()) {
        err_msg = "Work item not found or snapshot failed";
        return 0;
    }

    int trash_id = 0;
    try {
        db::sqlite_transaction tx(conn);

        auto ins = conn.prepare(R"SQL(
            INSERT INTO trash_bin (
                original_id, item_key, project_id, project_key, item_type, title,
                item_data_json, deleted_by, deleted_at, purge_at
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, datetime('now'), datetime('now', '+30 days'));
        )SQL");
        ins.bind_int(1, item_id);
        ins.bind_text(2, item_key);
        ins.bind_int(3, proj_id);
        ins.bind_text(4, proj_key);
        ins.bind_text(5, item_type);
        ins.bind_text(6, title);
        ins.bind_text(7, snapshot_json);
        if (user_id > 0) ins.bind_int(8, user_id);
        else ins.bind_null(8);
        ins.step_done();
        trash_id = static_cast<int>(conn.last_insert_rowid());

        // Delete from work_items (cascades in DB, but files on disk remain for trash retention)
        auto del = conn.prepare("DELETE FROM work_items WHERE id = ?;");
        del.bind_int(1, item_id);
        del.step_done();

        tx.commit();
    } catch (const std::exception &ex) {
        err_msg = ex.what();
        return 0;
    }

    return trash_id;
}

// Restore work item from trash_bin
inline bool restore_item_from_trash(db::sqlite_db &conn,
                                    int trash_id,
                                    int,
                                    std::string &restored_key,
                                    std::string &err_msg) {
    std::string snap_str, item_key;
    int orig_id = 0;

    {
        auto stmt = conn.prepare("SELECT original_id, item_key, item_data_json FROM trash_bin WHERE id = ?;");
        stmt.bind_int(1, trash_id);
        if (stmt.step() != SQLITE_ROW) {
            err_msg = "Trash record not found";
            return false;
        }
        orig_id = stmt.column_int(0);
        item_key = stmt.column_str(1);
        snap_str = stmt.column_str(2);
    }

    auto root = json::json_value::parse(snap_str);
    if (!root) {
        err_msg = "Invalid snapshot JSON in trash";
        return false;
    }

    auto wi = root.get_child("work_item");
    if (!wi) {
        err_msg = "Missing work_item in snapshot";
        return false;
    }

    try {
        db::sqlite_transaction tx(conn);

        // Check if ID is occupied
        int target_id = orig_id;
        {
            auto chk = conn.prepare("SELECT 1 FROM work_items WHERE id = ?;");
            chk.bind_int(1, target_id);
            if (chk.step() == SQLITE_ROW) {
                // If occupied, will auto-increment
                target_id = 0;
            }
        }

        // Check if key is occupied
        std::string target_key = item_key;
        {
            auto chk_k = conn.prepare("SELECT 1 FROM work_items WHERE key = ?;");
            chk_k.bind_text(1, target_key);
            if (chk_k.step() == SQLITE_ROW) {
                target_key = target_key + "-RESTORED";
            }
        }

        if (target_id > 0) {
            auto ins = conn.prepare(R"SQL(
                INSERT INTO work_items (
                    id, project_id, key, type, category, status, priority, title, description,
                    parent_id, sprint_id, assignee_id, reporter_id, story_points, progress,
                    start_date, due_date, severity, steps_to_reproduce, labels, board_order,
                    resolution_id, resolution_date, original_estimate_seconds, remaining_estimate_seconds,
                    time_spent_seconds, tshirt_size, security_level_id, service_desk_id, request_type_id,
                    customer_reporter_name, customer_reporter_email, first_response_at, is_customer_request,
                    created_at, updated_at
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, datetime('now'));
            )SQL");
            ins.bind_int(1, target_id);
            ins.bind_int(2, wi.get_int("project_id", 1));
            ins.bind_text(3, target_key);
            ins.bind_text(4, wi.get_string("type", "Task"));
            ins.bind_text(5, wi.get_string("category", "New Feature"));
            ins.bind_text(6, wi.get_string("status", "To Do"));
            ins.bind_text(7, wi.get_string("priority", "Medium"));
            ins.bind_text(8, wi.get_string("title", "Untitled"));
            ins.bind_text(9, wi.get_string("description", ""));
            ins.bind_optional_int(10, wi.get_int("parent_id", 0));
            ins.bind_optional_int(11, wi.get_int("sprint_id", 0));
            ins.bind_optional_int(12, wi.get_int("assignee_id", 0));
            ins.bind_optional_int(13, wi.get_int("reporter_id", 1));
            ins.bind_int(14, wi.get_int("story_points", 0));
            ins.bind_int(15, wi.get_int("progress", 0));
            ins.bind_text(16, wi.get_string("start_date", ""));
            ins.bind_text(17, wi.get_string("due_date", ""));
            ins.bind_text(18, wi.get_string("severity", ""));
            ins.bind_text(19, wi.get_string("steps_to_reproduce", ""));
            ins.bind_text(20, wi.get_string("labels", ""));
            ins.bind_int(21, wi.get_int("board_order", 0));
            ins.bind_optional_int(22, wi.get_int("resolution_id", 0));
            ins.bind_text(23, wi.get_string("resolution_date", ""));
            ins.bind_int64(24, wi.get_int64("original_estimate_seconds", 0));
            ins.bind_int64(25, wi.get_int64("remaining_estimate_seconds", 0));
            ins.bind_int64(26, wi.get_int64("time_spent_seconds", 0));
            ins.bind_text(27, wi.get_string("tshirt_size", ""));
            ins.bind_optional_int(28, wi.get_int("security_level_id", 0));
            ins.bind_optional_int(29, wi.get_int("service_desk_id", 0));
            ins.bind_optional_int(30, wi.get_int("request_type_id", 0));
            ins.bind_text(31, wi.get_string("customer_reporter_name", ""));
            ins.bind_text(32, wi.get_string("customer_reporter_email", ""));
            ins.bind_text(33, wi.get_string("first_response_at", ""));
            ins.bind_int(34, wi.get_int("is_customer_request", 0));
            ins.bind_text(35, wi.get_string("created_at", ""));
            ins.step_done();
        } else {
            auto ins = conn.prepare(R"SQL(
                INSERT INTO work_items (
                    project_id, key, type, category, status, priority, title, description,
                    parent_id, sprint_id, assignee_id, reporter_id, story_points, progress,
                    start_date, due_date, severity, steps_to_reproduce, labels, board_order,
                    resolution_id, resolution_date, original_estimate_seconds, remaining_estimate_seconds,
                    time_spent_seconds, tshirt_size, security_level_id, service_desk_id, request_type_id,
                    customer_reporter_name, customer_reporter_email, first_response_at, is_customer_request,
                    created_at, updated_at
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, datetime('now'));
            )SQL");
            ins.bind_int(1, wi.get_int("project_id", 1));
            ins.bind_text(2, target_key);
            ins.bind_text(3, wi.get_string("type", "Task"));
            ins.bind_text(4, wi.get_string("category", "New Feature"));
            ins.bind_text(5, wi.get_string("status", "To Do"));
            ins.bind_text(6, wi.get_string("priority", "Medium"));
            ins.bind_text(7, wi.get_string("title", "Untitled"));
            ins.bind_text(8, wi.get_string("description", ""));
            ins.bind_optional_int(9, wi.get_int("parent_id", 0));
            ins.bind_optional_int(10, wi.get_int("sprint_id", 0));
            ins.bind_optional_int(11, wi.get_int("assignee_id", 0));
            ins.bind_optional_int(12, wi.get_int("reporter_id", 1));
            ins.bind_int(13, wi.get_int("story_points", 0));
            ins.bind_int(14, wi.get_int("progress", 0));
            ins.bind_text(15, wi.get_string("start_date", ""));
            ins.bind_text(16, wi.get_string("due_date", ""));
            ins.bind_text(17, wi.get_string("severity", ""));
            ins.bind_text(18, wi.get_string("steps_to_reproduce", ""));
            ins.bind_text(19, wi.get_string("labels", ""));
            ins.bind_int(20, wi.get_int("board_order", 0));
            ins.bind_optional_int(21, wi.get_int("resolution_id", 0));
            ins.bind_text(22, wi.get_string("resolution_date", ""));
            ins.bind_int64(23, wi.get_int64("original_estimate_seconds", 0));
            ins.bind_int64(24, wi.get_int64("remaining_estimate_seconds", 0));
            ins.bind_int64(25, wi.get_int64("time_spent_seconds", 0));
            ins.bind_text(26, wi.get_string("tshirt_size", ""));
            ins.bind_optional_int(27, wi.get_int("security_level_id", 0));
            ins.bind_optional_int(28, wi.get_int("service_desk_id", 0));
            ins.bind_optional_int(29, wi.get_int("request_type_id", 0));
            ins.bind_text(30, wi.get_string("customer_reporter_name", ""));
            ins.bind_text(31, wi.get_string("customer_reporter_email", ""));
            ins.bind_text(32, wi.get_string("first_response_at", ""));
            ins.bind_int(33, wi.get_int("is_customer_request", 0));
            ins.bind_text(34, wi.get_string("created_at", ""));
            ins.step_done();
            target_id = static_cast<int>(conn.last_insert_rowid());
        }

        // Restore custom fields
        auto cf_arr = root.get_child("custom_fields");
        if (cf_arr) {
            size_t len = cf_arr.array_length();
            auto ins_cf = conn.prepare(R"SQL(
                INSERT OR REPLACE INTO custom_field_values (
                    work_item_id, custom_field_id, text_value, numeric_value, date_value, user_value_id, json_value, updated_at
                ) VALUES (?, ?, ?, ?, ?, ?, ?, datetime('now'));
            )SQL");
            for (size_t i = 0; i < len; ++i) {
                auto cf = cf_arr.array_get(i);
                if (!cf) continue;
                ins_cf.bind_int(1, target_id);
                ins_cf.bind_int(2, cf.get_int("custom_field_id", 0));
                ins_cf.bind_text(3, cf.get_string("text_value", ""));
                ins_cf.bind_double(4, cf.get_double("numeric_value", 0.0));
                ins_cf.bind_text(5, cf.get_string("date_value", ""));
                ins_cf.bind_optional_int(6, cf.get_int("user_value_id", 0));
                ins_cf.bind_text(7, cf.get_string("json_value", ""));
                ins_cf.step_done();
                ins_cf.clear_and_reset();
            }
        }

        // Restore components
        auto comp_arr = root.get_child("components");
        if (comp_arr) {
            size_t len = comp_arr.array_length();
            auto ins_comp = conn.prepare("INSERT OR IGNORE INTO work_item_components (work_item_id, component_id) VALUES (?, ?);");
            for (size_t i = 0; i < len; ++i) {
                ins_comp.bind_int(1, target_id);
                ins_comp.bind_int(2, json_object_get_int(comp_arr.array_get(i).get()));
                ins_comp.step_done();
                ins_comp.clear_and_reset();
            }
        }

        // Restore versions
        auto ver_arr = root.get_child("versions");
        if (ver_arr) {
            size_t len = ver_arr.array_length();
            auto ins_ver = conn.prepare("INSERT OR IGNORE INTO work_item_versions (work_item_id, version_id, relation_type) VALUES (?, ?, ?);");
            for (size_t i = 0; i < len; ++i) {
                auto v = ver_arr.array_get(i);
                if (!v) continue;
                ins_ver.bind_int(1, target_id);
                ins_ver.bind_int(2, v.get_int("version_id", 0));
                ins_ver.bind_text(3, v.get_string("relation_type", "fix"));
                ins_ver.step_done();
                ins_ver.clear_and_reset();
            }
        }

        // Restore attachments
        auto att_arr = root.get_child("attachments");
        if (att_arr) {
            size_t len = att_arr.array_length();
            auto ins_att = conn.prepare("INSERT INTO attachments (work_item_id, original_filename, blob_filename, size_bytes, mime_type, uploader_id, created_at) VALUES (?, ?, ?, ?, ?, ?, ?);");
            for (size_t i = 0; i < len; ++i) {
                auto a = att_arr.array_get(i);
                if (!a) continue;
                ins_att.bind_int(1, target_id);
                ins_att.bind_text(2, a.get_string("original_filename", ""));
                ins_att.bind_text(3, a.get_string("blob_filename", ""));
                ins_att.bind_int64(4, a.get_int64("size_bytes", 0));
                ins_att.bind_text(5, a.get_string("mime_type", "application/octet-stream"));
                ins_att.bind_optional_int(6, a.get_int("uploader_id", 0));
                ins_att.bind_text(7, a.get_string("created_at", ""));
                ins_att.step_done();
                ins_att.clear_and_reset();
            }
        }

        // Restore comments
        auto comm_arr = root.get_child("comments");
        if (comm_arr) {
            size_t len = comm_arr.array_length();
            auto ins_c = conn.prepare("INSERT INTO comments (work_item_id, author_id, body, created_at) VALUES (?, ?, ?, ?);");
            for (size_t i = 0; i < len; ++i) {
                auto c = comm_arr.array_get(i);
                if (!c) continue;
                ins_c.bind_int(1, target_id);
                ins_c.bind_optional_int(2, c.get_int("author_id", 0));
                ins_c.bind_text(3, c.get_string("body", ""));
                ins_c.bind_text(4, c.get_string("created_at", ""));
                ins_c.step_done();
                ins_c.clear_and_reset();
            }
        }

        // Restore worklogs
        auto wl_arr = root.get_child("worklogs");
        if (wl_arr) {
            size_t len = wl_arr.array_length();
            auto ins_wl = conn.prepare("INSERT INTO worklogs (work_item_id, author_id, time_spent_seconds, started_at, description, created_at) VALUES (?, ?, ?, ?, ?, ?);");
            for (size_t i = 0; i < len; ++i) {
                auto wl = wl_arr.array_get(i);
                if (!wl) continue;
                ins_wl.bind_int(1, target_id);
                ins_wl.bind_optional_int(2, wl.get_int("author_id", 0));
                ins_wl.bind_int64(3, wl.get_int64("time_spent_seconds", 0));
                ins_wl.bind_text(4, wl.get_string("started_at", ""));
                ins_wl.bind_text(5, wl.get_string("description", ""));
                ins_wl.bind_text(6, wl.get_string("created_at", ""));
                ins_wl.step_done();
                ins_wl.clear_and_reset();
            }
        }

        // Restore links
        auto lnk_arr = root.get_child("links");
        if (lnk_arr) {
            size_t len = lnk_arr.array_length();
            auto ins_lnk = conn.prepare("INSERT OR IGNORE INTO work_item_links (source_item_id, target_item_id, link_type) VALUES (?, ?, ?);");
            for (size_t i = 0; i < len; ++i) {
                auto lnk = lnk_arr.array_get(i);
                if (!lnk) continue;
                int src = lnk.get_int("source_item_id", 0);
                int tgt = lnk.get_int("target_item_id", 0);
                if (src == orig_id) src = target_id;
                if (tgt == orig_id) tgt = target_id;
                if (src > 0 && tgt > 0) {
                    ins_lnk.bind_int(1, src);
                    ins_lnk.bind_int(2, tgt);
                    ins_lnk.bind_text(3, lnk.get_string("link_type", "relates to"));
                    ins_lnk.step_done();
                    ins_lnk.clear_and_reset();
                }
            }
        }

        // Remove from trash_bin
        auto del_t = conn.prepare("DELETE FROM trash_bin WHERE id = ?;");
        del_t.bind_int(1, trash_id);
        del_t.step_done();

        tx.commit();
        restored_key = target_key;
        return true;
    } catch (const std::exception &ex) {
        std::cerr << "[RESTORE ERROR] " << ex.what() << std::endl;
        err_msg = ex.what();
        return false;
    }
}

// Purge single item from trash_bin
inline bool purge_item_from_trash(db::sqlite_db &conn,
                                  int trash_id,
                                  const std::string &blobs_dir,
                                  std::string &err_msg) {
    std::string snap_str;
    {
        auto stmt = conn.prepare("SELECT item_data_json FROM trash_bin WHERE id = ?;");
        stmt.bind_int(1, trash_id);
        if (stmt.step() != SQLITE_ROW) {
            err_msg = "Trash record not found";
            return false;
        }
        snap_str = stmt.column_str(0);
    }

    // Clean blobs from filesystem
    auto root = json::json_value::parse(snap_str);
    if (root) {
        auto att_arr = root.get_child("attachments");
        if (att_arr) {
            size_t len = att_arr.array_length();
            for (size_t i = 0; i < len; ++i) {
                auto a = att_arr.array_get(i);
                if (!a) continue;
                std::string blob_name = a.get_string("blob_filename", "");
                if (!blob_name.empty()) {
                    std::filesystem::path p = std::filesystem::path(blobs_dir) / blob_name;
                    std::error_code ec;
                    std::filesystem::remove(p, ec);
                }
            }
        }
    }

    try {
        auto del = conn.prepare("DELETE FROM trash_bin WHERE id = ?;");
        del.bind_int(1, trash_id);
        del.step_done();
        return true;
    } catch (const std::exception &ex) {
        std::cerr << "[RESTORE ERROR] " << ex.what() << std::endl;
        err_msg = ex.what();
        return false;
    }
}

// Purge all items from trash_bin
inline int purge_all_trash(db::sqlite_db &conn, const std::string &blobs_dir) {
    int count = 0;
    std::vector<int> trash_ids;
    {
        auto stmt = conn.prepare("SELECT id FROM trash_bin;");
        while (stmt.step() == SQLITE_ROW) {
            trash_ids.push_back(stmt.column_int(0));
        }
    }

    for (int tid : trash_ids) {
        std::string err;
        if (purge_item_from_trash(conn, tid, blobs_dir, err)) {
            count++;
        }
    }
    return count;
}

// List all trash bin items with 30-day retention countdown
inline json::json_value list_trash_items(db::sqlite_db &conn) {
    auto arr = json::json_value::make_array();
    auto stmt = conn.prepare(R"SQL(
        SELECT t.id, t.original_id, t.item_key, t.project_id, t.project_key, t.item_type, t.title,
               t.deleted_by, COALESCE(u.display_name, 'System Administrator'), t.deleted_at, t.purge_at,
               CAST(MAX(0, ROUND(julianday(t.purge_at) - julianday('now'))) AS INTEGER) as days_remaining
        FROM trash_bin t
        LEFT JOIN users u ON u.id = t.deleted_by
        WHERE t.restored_at IS NULL
        ORDER BY t.deleted_at DESC;
    )SQL");

    while (stmt.step() == SQLITE_ROW) {
        auto obj = json::json_value::make_object();
        obj.add_int("id", stmt.column_int(0));
        obj.add_int("original_id", stmt.column_int(1));
        obj.add_str("item_key", stmt.column_str(2));
        obj.add_int("project_id", stmt.column_int(3));
        obj.add_str("project_key", stmt.column_str(4));
        obj.add_str("item_type", stmt.column_str(5));
        obj.add_str("title", stmt.column_str(6));
        obj.add_nullable_int("deleted_by", stmt.column_int(7));
        obj.add_str("deleted_by_name", stmt.column_str(8));
        obj.add_str("deleted_at", stmt.column_str(9));
        obj.add_str("purge_at", stmt.column_str(10));
        obj.add_int("days_remaining", stmt.column_int(11));
        arr.add(std::move(obj));
    }
    return arr;
}

// Clone issue with configurable flags (subtasks, attachments, links, summary prefix)
inline int clone_work_item(db::sqlite_db &conn,
                           int orig_id,
                           int user_id,
                           const json::json_value &options,
                           const std::string &blobs_dir,
                           std::string &new_key,
                           std::string &err_msg) {
    bool clone_subtasks = options.get_bool("clone_subtasks", false);
    bool clone_attachments = options.get_bool("clone_attachments", false);
    bool clone_links = options.get_bool("clone_links", false);
    std::string prefix_summary = options.get_string("prefix_summary", "CLONE - ");
    std::string custom_summary = options.get_string("summary", "");
    if (custom_summary.empty()) custom_summary = options.get_string("title", "");

    // Fetch original work item
    std::string o_type, o_cat, o_status, o_priority, o_title, o_desc, o_sev, o_steps, o_labels, o_tshirt;
    int o_proj_id = 0, o_assignee = 0, o_sp = 0, o_sec_level = 0, o_sd_id = 0, o_rt_id = 0;
    int64_t o_orig_sec = 0, o_rem_sec = 0;
    std::string o_key;

    {
        auto stmt = conn.prepare(R"SQL(
            SELECT project_id, key, type, category, status, priority, title, description,
                   assignee_id, story_points, severity, steps_to_reproduce, labels,
                   original_estimate_seconds, remaining_estimate_seconds, tshirt_size,
                   security_level_id, service_desk_id, request_type_id
            FROM work_items
            WHERE id = ?;
        )SQL");
        stmt.bind_int(1, orig_id);
        if (stmt.step() != SQLITE_ROW) {
            err_msg = "Original work item not found";
            return 0;
        }
        o_proj_id = stmt.column_int(0);
        o_key = stmt.column_str(1);
        o_type = stmt.column_str(2);
        o_cat = stmt.column_str(3);
        o_status = stmt.column_str(4);
        o_priority = stmt.column_str(5);
        o_title = stmt.column_str(6);
        o_desc = stmt.column_str(7);
        o_assignee = stmt.column_int(8);
        o_sp = stmt.column_int(9);
        o_sev = stmt.column_str(10);
        o_steps = stmt.column_str(11);
        o_labels = stmt.column_str(12);
        o_orig_sec = stmt.column_int64(13);
        o_rem_sec = stmt.column_int64(14);
        o_tshirt = stmt.column_str(15);
        o_sec_level = stmt.column_int(16);
        o_sd_id = stmt.column_int(17);
        o_rt_id = stmt.column_int(18);
    }

    int target_proj_id = options.get_int("project_id", o_proj_id);
    if (target_proj_id <= 0) target_proj_id = o_proj_id;

    std::string final_title = !custom_summary.empty() ? custom_summary : (prefix_summary + o_title);

    int new_item_id = 0;
    try {
        db::sqlite_transaction tx(conn);

        // Allocate key from target project
        std::string proj_key = "PROJ";
        int next_seq = 1;
        {
            auto pstmt = conn.prepare("SELECT key, issue_seq FROM projects WHERE id = ?;");
            pstmt.bind_int(1, target_proj_id);
            if (pstmt.step() == SQLITE_ROW) {
                proj_key = pstmt.column_str(0);
                next_seq = pstmt.column_int(1) + 1;
            }
        }
        {
            auto upd = conn.prepare("UPDATE projects SET issue_seq = ? WHERE id = ?;");
            upd.bind_int(1, next_seq);
            upd.bind_int(2, target_proj_id);
            upd.step_done();
        }

        new_key = proj_key + "-" + std::to_string(next_seq);

        auto ins = conn.prepare(R"SQL(
            INSERT INTO work_items (
                project_id, key, type, category, status, priority, title, description,
                parent_id, sprint_id, assignee_id, reporter_id, story_points,
                progress, start_date, due_date, severity, steps_to_reproduce, labels, board_order,
                resolution_id, resolution_date,
                original_estimate_seconds, remaining_estimate_seconds, time_spent_seconds, tshirt_size,
                security_level_id, service_desk_id, request_type_id
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, NULL, NULL, ?, ?, ?, 0, '', '', ?, ?, ?, ?, NULL, '', ?, ?, 0, ?, ?, ?, ?);
        )SQL");
        ins.bind_int(1, target_proj_id);
        ins.bind_text(2, new_key);
        ins.bind_text(3, o_type);
        ins.bind_text(4, o_cat);
        ins.bind_text(5, o_status);
        ins.bind_text(6, o_priority);
        ins.bind_text(7, final_title);
        ins.bind_text(8, o_desc);
        ins.bind_optional_int(9, o_assignee);
        ins.bind_optional_int(10, user_id > 0 ? user_id : 1);
        ins.bind_int(11, o_sp);
        ins.bind_text(12, o_sev);
        ins.bind_text(13, o_steps);
        ins.bind_text(14, o_labels);
        ins.bind_int(15, next_seq * 10);
        ins.bind_int64(16, o_orig_sec);
        ins.bind_int64(17, o_rem_sec);
        ins.bind_text(18, o_tshirt);
        ins.bind_optional_int(19, o_sec_level);
        ins.bind_optional_int(20, o_sd_id);
        ins.bind_optional_int(21, o_rt_id);
        ins.step_done();
        new_item_id = static_cast<int>(conn.last_insert_rowid());

        // Duplicate custom fields
        {
            auto cf_stmt = conn.prepare(R"SQL(
                INSERT INTO custom_field_values (
                    work_item_id, custom_field_id, text_value, numeric_value, date_value, user_value_id, json_value, updated_at
                )
                SELECT ?, custom_field_id, text_value, numeric_value, date_value, user_value_id, json_value, datetime('now')
                FROM custom_field_values
                WHERE work_item_id = ?;
            )SQL");
            cf_stmt.bind_int(1, new_item_id);
            cf_stmt.bind_int(2, orig_id);
            cf_stmt.step_done();
        }

        // Duplicate components
        {
            auto c_stmt = conn.prepare("INSERT OR IGNORE INTO work_item_components (work_item_id, component_id) SELECT ?, component_id FROM work_item_components WHERE work_item_id = ?;");
            c_stmt.bind_int(1, new_item_id);
            c_stmt.bind_int(2, orig_id);
            c_stmt.step_done();
        }

        // Duplicate versions
        {
            auto v_stmt = conn.prepare("INSERT OR IGNORE INTO work_item_versions (work_item_id, version_id, relation_type) SELECT ?, version_id, relation_type FROM work_item_versions WHERE work_item_id = ?;");
            v_stmt.bind_int(1, new_item_id);
            v_stmt.bind_int(2, orig_id);
            v_stmt.step_done();
        }

        // Clone attachments if requested
        if (clone_attachments) {
            auto att_stmt = conn.prepare("SELECT original_filename, blob_filename, size_bytes, mime_type FROM attachments WHERE work_item_id = ?;");
            att_stmt.bind_int(1, orig_id);
            std::vector<std::tuple<std::string, std::string, int64_t, std::string>> atts;
            while (att_stmt.step() == SQLITE_ROW) {
                atts.push_back({att_stmt.column_str(0), att_stmt.column_str(1), att_stmt.column_int64(2), att_stmt.column_str(3)});
            }
            att_stmt.reset();

            for (const auto &[fname, bname, fsize, ctype] : atts) {
                std::filesystem::path src = std::filesystem::path(blobs_dir) / bname;
                std::string new_blob = utils::generate_random_hex(12) + "_" + fname;
                std::filesystem::path dst = std::filesystem::path(blobs_dir) / new_blob;
                std::error_code ec;
                std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);

                auto ins_a = conn.prepare("INSERT INTO attachments (work_item_id, original_filename, blob_filename, size_bytes, mime_type, uploader_id) VALUES (?, ?, ?, ?, ?, ?);");
                ins_a.bind_int(1, new_item_id);
                ins_a.bind_text(2, fname);
                ins_a.bind_text(3, new_blob);
                ins_a.bind_int64(4, fsize);
                ins_a.bind_text(5, ctype);
                ins_a.bind_optional_int(6, user_id > 0 ? user_id : 1);
                ins_a.step_done();
            }
        }

        // Clone links: link to original item as "clones", plus copy existing links if requested
        {
            auto ins_lnk = conn.prepare("INSERT OR IGNORE INTO work_item_links (source_item_id, target_item_id, link_type) VALUES (?, ?, 'clones');");
            ins_lnk.bind_int(1, orig_id);
            ins_lnk.bind_int(2, new_item_id);
            ins_lnk.step_done();

            if (clone_links) {
                auto l_stmt = conn.prepare("SELECT target_item_id, link_type FROM work_item_links WHERE source_item_id = ? AND target_item_id != ?;");
                l_stmt.bind_int(1, orig_id);
                l_stmt.bind_int(2, new_item_id);
                while (l_stmt.step() == SQLITE_ROW) {
                    auto ins_l = conn.prepare("INSERT OR IGNORE INTO work_item_links (source_item_id, target_item_id, link_type) VALUES (?, ?, ?);");
                    ins_l.bind_int(1, new_item_id);
                    ins_l.bind_int(2, l_stmt.column_int(0));
                    ins_l.bind_text(3, l_stmt.column_str(1));
                    ins_l.step_done();
                }
            }
        }

        // Collect subtask IDs before committing outer transaction
        std::vector<int> subtask_ids;
        if (clone_subtasks) {
            auto s_stmt = conn.prepare("SELECT id FROM work_items WHERE parent_id = ?;");
            s_stmt.bind_int(1, orig_id);
            while (s_stmt.step() == SQLITE_ROW) {
                subtask_ids.push_back(s_stmt.column_int(0));
            }
        }

        // Workflow history
        auto hist = conn.prepare("INSERT INTO workflow_history (work_item_id, from_status, to_status, changed_by, comment) VALUES (?, 'New', ?, ?, ?);");
        hist.bind_int(1, new_item_id);
        hist.bind_text(2, o_status);
        hist.bind_optional_int(3, user_id > 0 ? user_id : 1);
        hist.bind_text(4, "Cloned from " + o_key);
        hist.step_done();

        tx.commit();

        // Clone subtasks after committing parent transaction to avoid nested SQLite transactions
        if (clone_subtasks) {
            for (int sub_id : subtask_ids) {
                std::string sub_key, sub_err;
                auto sub_opts = json::json_value::make_object();
                sub_opts.add_bool("clone_subtasks", false);
                sub_opts.add_bool("clone_attachments", clone_attachments);
                sub_opts.add_bool("clone_links", false);
                sub_opts.add_str("prefix_summary", "");
                int cloned_sub = clone_work_item(conn, sub_id, user_id, sub_opts, blobs_dir, sub_key, sub_err);
                if (cloned_sub > 0) {
                    auto upd_p = conn.prepare("UPDATE work_items SET parent_id = ? WHERE id = ?;");
                    upd_p.bind_int(1, new_item_id);
                    upd_p.bind_int(2, cloned_sub);
                    upd_p.step_done();
                }
            }
        }
    } catch (const std::exception &ex) {
        err_msg = ex.what();
        return 0;
    }

    return new_item_id;
}

// Move work item to another project, type, and/or status
inline bool move_work_item(db::sqlite_db &conn,
                           int item_id,
                           int user_id,
                           int target_project_id,
                           const std::string &target_type,
                           const std::string &target_status,
                           const json::json_value &cf_values,
                           std::string &old_key,
                           std::string &new_key,
                           std::string &err_msg) {
    int cur_proj_id = 0;
    std::string cur_type, cur_status;
    {
        auto stmt = conn.prepare("SELECT project_id, key, type, status FROM work_items WHERE id = ?;");
        stmt.bind_int(1, item_id);
        if (stmt.step() != SQLITE_ROW) {
            err_msg = "Work item not found";
            return false;
        }
        cur_proj_id = stmt.column_int(0);
        old_key = stmt.column_str(1);
        cur_type = stmt.column_str(2);
        cur_status = stmt.column_str(3);
    }

    if (target_project_id <= 0) target_project_id = cur_proj_id;
    std::string final_type = !target_type.empty() ? target_type : cur_type;
    std::string final_status = !target_status.empty() ? target_status : cur_status;

    try {
        db::sqlite_transaction tx(conn);

        if (target_project_id != cur_proj_id) {
            // Allocate new issue key from target project
            std::string target_proj_key = "PROJ";
            int next_seq = 1;
            {
                auto pstmt = conn.prepare("SELECT key, issue_seq FROM projects WHERE id = ?;");
                pstmt.bind_int(1, target_project_id);
                if (pstmt.step() == SQLITE_ROW) {
                    target_proj_key = pstmt.column_str(0);
                    next_seq = pstmt.column_int(1) + 1;
                }
            }
            {
                auto upd_seq = conn.prepare("UPDATE projects SET issue_seq = ? WHERE id = ?;");
                upd_seq.bind_int(1, next_seq);
                upd_seq.bind_int(2, target_project_id);
                upd_seq.step_done();
            }

            new_key = target_proj_key + "-" + std::to_string(next_seq);

            auto upd = conn.prepare("UPDATE work_items SET project_id = ?, key = ?, type = ?, status = ?, updated_at = datetime('now') WHERE id = ?;");
            upd.bind_int(1, target_project_id);
            upd.bind_text(2, new_key);
            upd.bind_text(3, final_type);
            upd.bind_text(4, final_status);
            upd.bind_int(5, item_id);
            upd.step_done();

            // Record workflow history comment
            auto hist = conn.prepare("INSERT INTO workflow_history (work_item_id, from_status, to_status, changed_by, comment) VALUES (?, ?, ?, ?, ?);");
            hist.bind_int(1, item_id);
            hist.bind_text(2, cur_status);
            hist.bind_text(3, final_status);
            hist.bind_optional_int(4, user_id > 0 ? user_id : 1);
            hist.bind_text(5, "Moved issue from " + old_key + " to " + new_key);
            hist.step_done();
        } else {
            new_key = old_key;
            auto upd = conn.prepare("UPDATE work_items SET type = ?, status = ?, updated_at = datetime('now') WHERE id = ?;");
            upd.bind_text(1, final_type);
            upd.bind_text(2, final_status);
            upd.bind_int(3, item_id);
            upd.step_done();

            if (final_status != cur_status) {
                auto hist = conn.prepare("INSERT INTO workflow_history (work_item_id, from_status, to_status, changed_by, comment) VALUES (?, ?, ?, ?, 'Status changed via Move/Convert wizard');");
                hist.bind_int(1, item_id);
                hist.bind_text(2, cur_status);
                hist.bind_text(3, final_status);
                hist.bind_optional_int(4, user_id > 0 ? user_id : 1);
                hist.step_done();
            }
        }

        // Apply any custom fields provided
        if (cf_values) {
            auto ups = conn.prepare(R"SQL(
                INSERT INTO custom_field_values (
                    work_item_id, custom_field_id, text_value, numeric_value, date_value, user_value_id, json_value, updated_at
                ) VALUES (?, ?, ?, 0, '', NULL, '', datetime('now'))
                ON CONFLICT(work_item_id, custom_field_id) DO UPDATE SET
                    text_value = excluded.text_value,
                    updated_at = datetime('now');
            )SQL");
            // If object or array, handle simple text values
            json_object_object_foreach(cf_values.get(), k, v) {
                if (k && v) {
                    int cid = std::atoi(k);
                    if (cid > 0) {
                        const char *s = json_object_get_string(v);
                        ups.bind_int(1, item_id);
                        ups.bind_int(2, cid);
                        ups.bind_text(3, s ? s : "");
                        ups.step_done();
                        ups.clear_and_reset();
                    }
                }
            }
        }

        tx.commit();
        return true;
    } catch (const std::exception &ex) {
        std::cerr << "[RESTORE ERROR] " << ex.what() << std::endl;
        err_msg = ex.what();
        return false;
    }
}

// Bulk operations executor (edit, transition, assign, move, delete)
inline json::json_value execute_bulk(db::sqlite_db &conn,
                                     int user_id,
                                     const json::json_value &body,
                                     const std::string &blobs_dir,
                                     std::string &err_msg) {
    auto res = json::json_value::make_object();
    std::string op = body.get_string("operation", "edit");
    auto item_ids = body.get_int_array("item_ids");

    if (item_ids.empty()) {
        err_msg = "No work item IDs provided for bulk operation";
        return json::json_value();
    }

    auto affected_ids = json::json_value::make_array();
    int processed_count = 0;

    try {
        if (op == "edit") {
            db::sqlite_transaction tx(conn);
            auto fields = body.get_child("fields");
            if (!fields) {
                err_msg = "Missing fields object for bulk edit";
                return json::json_value();
            }

            std::string priority = fields.get_string("priority", "");
            std::string status = fields.get_string("status", "");
            int assignee_id = fields.get_int("assignee_id", -1);
            int sprint_id = fields.get_int("sprint_id", -1);
            int story_points = fields.get_int("story_points", -1);
            std::string labels = fields.get_string("labels", "");
            std::string due_date = fields.get_string("due_date", "");

            for (int id : item_ids) {
                std::string sql = "UPDATE work_items SET updated_at = datetime('now') ";
                std::vector<std::string> binds;

                if (!priority.empty()) {
                    sql += ", priority = ? ";
                    binds.push_back(priority);
                }
                if (!status.empty()) {
                    sql += ", status = ? ";
                    binds.push_back(status);
                }
                if (assignee_id >= 0) {
                    sql += ", assignee_id = " + (assignee_id == 0 ? std::string("NULL") : std::to_string(assignee_id)) + " ";
                }
                if (sprint_id >= 0) {
                    sql += ", sprint_id = " + (sprint_id == 0 ? std::string("NULL") : std::to_string(sprint_id)) + " ";
                }
                if (story_points >= 0) {
                    sql += ", story_points = " + std::to_string(story_points) + " ";
                }
                if (!labels.empty()) {
                    sql += ", labels = ? ";
                    binds.push_back(labels);
                }
                if (!due_date.empty()) {
                    sql += ", due_date = ? ";
                    binds.push_back(due_date);
                }

                sql += " WHERE id = ?;";
                auto upd = conn.prepare(sql);
                int b_idx = 1;
                for (const auto &b : binds) {
                    upd.bind_text(b_idx++, b);
                }
                upd.bind_int(b_idx, id);
                upd.step_done();

                affected_ids.add(json::json_value::make_int(id));
                processed_count++;
            }
            tx.commit();
        } else if (op == "transition") {
            db::sqlite_transaction tx(conn);
            std::string to_status = body.get_string("to_status", "");
            std::string comment = body.get_string("comment", "Bulk status transition");
            int res_id = body.get_int("resolution_id", 0);

            if (to_status.empty()) {
                err_msg = "Missing to_status for bulk transition";
                return json::json_value();
            }

            for (int id : item_ids) {
                std::string cur_st;
                {
                    auto chk = conn.prepare("SELECT status FROM work_items WHERE id = ?;");
                    chk.bind_int(1, id);
                    if (chk.step() == SQLITE_ROW) {
                        cur_st = chk.column_str(0);
                    }
                }

                auto upd = conn.prepare(R"SQL(
                    UPDATE work_items
                    SET status = ?,
                        resolution_id = CASE WHEN ? > 0 THEN ? ELSE resolution_id END,
                        resolution_date = CASE WHEN ? > 0 THEN datetime('now') ELSE resolution_date END,
                        progress = CASE WHEN ? = 'Done' THEN 100 ELSE progress END,
                        remaining_estimate_seconds = CASE WHEN ? = 'Done' THEN 0 ELSE remaining_estimate_seconds END,
                        updated_at = datetime('now')
                    WHERE id = ?;
                )SQL");
                upd.bind_text(1, to_status);
                upd.bind_int(2, res_id);
                upd.bind_int(3, res_id);
                upd.bind_int(4, res_id);
                upd.bind_text(5, to_status);
                upd.bind_text(6, to_status);
                upd.bind_int(7, id);
                upd.step_done();

                auto hist = conn.prepare("INSERT INTO workflow_history (work_item_id, from_status, to_status, changed_by, comment) VALUES (?, ?, ?, ?, ?);");
                hist.bind_int(1, id);
                hist.bind_text(2, cur_st);
                hist.bind_text(3, to_status);
                hist.bind_optional_int(4, user_id > 0 ? user_id : 1);
                hist.bind_text(5, comment);
                hist.step_done();

                affected_ids.add(json::json_value::make_int(id));
                processed_count++;
            }
            tx.commit();
        } else if (op == "assign") {
            db::sqlite_transaction tx(conn);
            int assignee_id = body.get_int("assignee_id", 0);
            for (int id : item_ids) {
                auto upd = conn.prepare("UPDATE work_items SET assignee_id = ?, updated_at = datetime('now') WHERE id = ?;");
                upd.bind_optional_int(1, assignee_id);
                upd.bind_int(2, id);
                upd.step_done();

                affected_ids.add(json::json_value::make_int(id));
                processed_count++;
            }
            tx.commit();
        } else if (op == "delete") {
            bool soft = body.get_bool("soft_delete", true);
            for (int id : item_ids) {
                if (soft) {
                    std::string s_err;
                    int tid = soft_delete_item(conn, id, user_id, blobs_dir, s_err);
                    if (tid > 0) {
                        affected_ids.add(json::json_value::make_int(id));
                        processed_count++;
                    }
                } else {
                    auto b_stmt = conn.prepare("SELECT blob_filename FROM attachments WHERE work_item_id = ?;");
                    b_stmt.bind_int(1, id);
                    while (b_stmt.step() == SQLITE_ROW) {
                        std::filesystem::path p = std::filesystem::path(blobs_dir) / b_stmt.column_str(0);
                        std::error_code ec;
                        std::filesystem::remove(p, ec);
                    }
                    auto del = conn.prepare("DELETE FROM work_items WHERE id = ?;");
                    del.bind_int(1, id);
                    del.step_done();

                    affected_ids.add(json::json_value::make_int(id));
                    processed_count++;
                }
            }
        } else if (op == "move") {
            int target_proj_id = body.get_int("target_project_id", 0);
            std::string target_type = body.get_string("target_type", "");
            std::string target_status = body.get_string("target_status", "");
            auto cf_vals = body.get_child("custom_fields");

            for (int id : item_ids) {
                std::string ok, nk, m_err;
                if (move_work_item(conn, id, user_id, target_proj_id, target_type, target_status, cf_vals, ok, nk, m_err)) {
                    affected_ids.add(json::json_value::make_int(id));
                    processed_count++;
                }
            }
        } else {
            err_msg = "Unsupported bulk operation: " + op;
            return json::json_value();
        }
    } catch (const std::exception &ex) {
        err_msg = ex.what();
        return json::json_value();
    }

    res.add_bool("success", true);
    res.add_str("operation", op);
    res.add_int("total", static_cast<int>(item_ids.size()));
    res.add_int("processed_count", processed_count);
    res.add("affected_ids", std::move(affected_ids));
    return res;
}

} // namespace bulk_trash
