#pragma once

#include "raii_sqlite.h"
#include "raii_json.h"
#include "analytics_engine.h"
#include "jql_lexer.h"
#include "jql_parser.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace dashboard {

// ============================================================================
// Layout Types and Catalog Definitions
// ============================================================================

inline bool is_valid_layout(const std::string &layout) {
    return layout == "1_column" ||
           layout == "2_column_equal" ||
           layout == "2_column_split" ||
           layout == "3_column";
}

inline bool is_valid_share_type(const std::string &share_type) {
    return share_type == "private" ||
           share_type == "project" ||
           share_type == "public";
}

// Return gadget catalog describing all 8 standard gadgets
inline json::json_value get_gadget_catalog() {
    auto catalog = json::json_value::make_array();

    auto add_item = [&](const std::string &type,
                        const std::string &title,
                        const std::string &desc,
                        const std::string &category,
                        const std::string &icon,
                        const std::string &default_prefs) {
        auto g = json::json_value::make_object();
        g.add_str("gadget_type", type);
        g.add_str("title", title);
        g.add_str("description", desc);
        g.add_str("category", category);
        g.add_str("icon", icon);
        g.add_str("default_preferences", default_prefs);
        catalog.add(std::move(g));
    };

    add_item("filter_results",
             "Filter Results",
             "Display a paginated, customizable table of issues matching any saved JQL filter or ad-hoc query.",
             "Issue Tracking",
             "table",
             R"({"filter_id":1,"columns":["key","title","status","priority","assignee","story_points"],"limit":10})");

    add_item("pie_chart",
             "Pie Chart",
             "Interactive distribution pie chart grouping issues by Assignee, Status, Priority, Component, or Issue Type.",
             "Charts & Visualizations",
             "pie-chart",
             R"({"statistic_type":"status","filter_id":0,"jql":""})");

    add_item("created_vs_resolved",
             "Created vs Resolved Chart",
             "Time-series chart showing incoming issues vs completed throughput over 14, 30, 60, or 90 days.",
             "Agile & Quality",
             "trending-up",
             R"({"days":30})");

    add_item("sprint_burndown",
             "Sprint Burndown",
             "Real-time burndown progress tracking remaining work vs ideal guideline slope for active sprints.",
             "Agile & Scrum",
             "activity",
             R"({"sprint_id":0,"unit":"points"})");

    add_item("two_dimensional_stats",
             "Two-Dimensional Filter Statistics",
             "Cross-tabulation matrix correlating two issue dimensions (e.g. Assignee vs Status, Priority vs Status).",
             "Reports & Matrix",
             "grid",
             R"({"x_axis":"status","y_axis":"assignee","filter_id":0})");

    add_item("assigned_to_me",
             "Assigned to Me",
             "Quick personal queue of active issues assigned to the current user, ordered by priority.",
             "Personal Work",
             "user-check",
             R"({"limit":10,"include_resolved":false})");

    add_item("activity_stream",
             "Activity Stream",
             "Chronological audit stream of recent activity including issue transitions, comments, and worklogs.",
             "Activity & Collaboration",
             "clock",
             R"({"limit":15})");

    add_item("roadmap_summary",
             "Roadmap & Release Summary",
             "High-level milestone progress tracker displaying active releases, version completion, and epic progress.",
             "Roadmap & Portfolio",
             "map",
             R"({"project_id":1})");

    return catalog;
}

// ============================================================================
// Gadget Data Computations
// ============================================================================

// 1. Filter Results Gadget
inline json::json_value compute_filter_results(db::sqlite_db &conn,
                                               int current_user_id,
                                               const std::string &current_user_name,
                                               const json::json_value &prefs) {
    int filter_id = prefs.get_int("filter_id", 0);
    std::string jql_query = prefs.get_string("jql");
    int limit = std::clamp(prefs.get_int("limit", 10), 1, 100);
    int offset = std::max(0, prefs.get_int("offset", 0));

    // Resolve filter JQL if filter_id provided
    std::string filter_name = "Custom Filter";
    if (filter_id > 0) {
        auto fstmt = conn.prepare("SELECT name, jql_query FROM saved_filters WHERE id = ?;");
        fstmt.bind_int(1, filter_id);
        if (fstmt.step() == SQLITE_ROW) {
            filter_name = fstmt.column_str(0);
            if (jql_query.empty()) {
                jql_query = fstmt.column_str(1);
            }
        }
    }

    if (jql_query.empty()) {
        jql_query = "status != 'Done' ORDER BY priority DESC, id DESC";
    }

    std::string where_sql = "1=1";
    std::string order_by_sql = "w.id DESC";
    std::vector<std::string> bind_params;

    try {
        jql::JqlLexer lexer(jql_query);
        auto tokens = lexer.tokenize();
        jql::JqlParser parser(tokens);
        auto ast = parser.parse();
        jql::SqlContext ctx(conn, current_user_id, current_user_name, current_user_name);
        where_sql = ast.to_where_sql(ctx);
        bind_params = ctx.params;
        if (!ast.order_by.empty()) {
            std::ostringstream oss;
            for (size_t i = 0; i < ast.order_by.size(); ++i) {
                if (i > 0) oss << ", ";
                std::string fld = ast.order_by[i].field;
                std::string col = "w.id";
                if (fld == "key") col = "w.key";
                else if (fld == "title" || fld == "summary") col = "w.title";
                else if (fld == "status") col = "w.status";
                else if (fld == "priority") col = "w.priority";
                else if (fld == "created") col = "w.created_at";
                else if (fld == "updated") col = "w.updated_at";
                else if (fld == "points" || fld == "storypoints") col = "w.story_points";
                oss << col << (ast.order_by[i].descending ? " DESC" : " ASC");
            }
            order_by_sql = oss.str();
        }
    } catch (const std::exception &ex) {
        where_sql = "w.status != 'Done'";
    }

    // Count total items
    int total_count = 0;
    {
        std::string count_sql = "SELECT COUNT(*) FROM work_items w WHERE " + where_sql + ";";
        auto cstmt = conn.prepare(count_sql);
        for (size_t i = 0; i < bind_params.size(); ++i) {
            cstmt.bind_text(static_cast<int>(i + 1), bind_params[i]);
        }
        if (cstmt.step() == SQLITE_ROW) {
            total_count = cstmt.column_int(0);
        }
    }

    // Fetch paginated items
    std::string fetch_sql = R"SQL(
        SELECT w.id, w.key, w.title, w.type, w.status, w.priority, w.story_points,
               w.remaining_estimate_seconds, w.time_spent_seconds,
               u.id, u.display_name, u.avatar_color,
               w.created_at, w.updated_at
        FROM work_items w
        LEFT JOIN users u ON u.id = w.assignee_id
        WHERE )SQL" + where_sql + " ORDER BY " + order_by_sql + " LIMIT ? OFFSET ?;";

    auto stmt = conn.prepare(fetch_sql);
    int pidx = 1;
    for (const auto &p : bind_params) {
        stmt.bind_text(pidx++, p);
    }
    stmt.bind_int(pidx++, limit);
    stmt.bind_int(pidx++, offset);

    auto items_arr = json::json_value::make_array();
    while (stmt.step() == SQLITE_ROW) {
        auto it = json::json_value::make_object();
        it.add_int("id", stmt.column_int(0));
        it.add_str("key", stmt.column_str(1));
        it.add_str("title", stmt.column_str(2));
        it.add_str("type", stmt.column_str(3));
        it.add_str("status", stmt.column_str(4));
        it.add_str("priority", stmt.column_str(5));
        it.add_int("story_points", stmt.column_int(6));
        it.add_int("remaining_estimate_seconds", stmt.column_int(7));
        it.add_int("time_spent_seconds", stmt.column_int(8));
        if (!stmt.column_is_null(9)) {
            auto uobj = json::json_value::make_object();
            uobj.add_int("id", stmt.column_int(9));
            uobj.add_str("display_name", stmt.column_str(10));
            uobj.add_str("avatar_color", stmt.column_str(11));
            it.add("assignee", std::move(uobj));
        } else {
            it.add("assignee", json::json_value::make_null());
        }
        it.add_str("created_at", stmt.column_str(12));
        it.add_str("updated_at", stmt.column_str(13));
        items_arr.add(std::move(it));
    }

    auto res = json::json_value::make_object();
    res.add_str("filter_name", filter_name);
    res.add_str("jql", jql_query);
    res.add_int("total_count", total_count);
    res.add_int("limit", limit);
    res.add_int("offset", offset);
    res.add("items", std::move(items_arr));
    return res;
}

// 2. Pie Chart Gadget
inline json::json_value compute_pie_chart(db::sqlite_db &conn,
                                          int current_user_id,
                                          const std::string &current_user_name,
                                          const json::json_value &prefs) {
    std::string stat_type = prefs.get_string("statistic_type", "status");
    int filter_id = prefs.get_int("filter_id", 0);
    std::string jql_query = prefs.get_string("jql");

    std::string where_sql = "1=1";
    std::vector<std::string> bind_params;

    if (filter_id > 0) {
        auto fstmt = conn.prepare("SELECT jql_query FROM saved_filters WHERE id = ?;");
        fstmt.bind_int(1, filter_id);
        if (fstmt.step() == SQLITE_ROW && jql_query.empty()) {
            jql_query = fstmt.column_str(0);
        }
    }

    if (!jql_query.empty()) {
        try {
            jql::JqlLexer lexer(jql_query);
            auto tokens = lexer.tokenize();
            jql::JqlParser parser(tokens);
            auto ast = parser.parse();
            jql::SqlContext ctx(conn, current_user_id, current_user_name, current_user_name);
            where_sql = ast.to_where_sql(ctx);
            bind_params = ctx.params;
        } catch (...) {
            where_sql = "1=1";
        }
    }

    std::string group_expr = "w.status";
    if (stat_type == "assignee") {
        group_expr = "COALESCE(u.display_name, 'Unassigned')";
    } else if (stat_type == "priority") {
        group_expr = "w.priority";
    } else if (stat_type == "issue_type" || stat_type == "type") {
        group_expr = "w.type";
    } else if (stat_type == "component") {
        group_expr = "COALESCE(c.name, 'No Component')";
    }

    std::string query_sql = "SELECT " + group_expr + " AS grp, COUNT(w.id) AS cnt "
                            "FROM work_items w "
                            "LEFT JOIN users u ON u.id = w.assignee_id "
                            "LEFT JOIN work_item_components wic ON wic.work_item_id = w.id "
                            "LEFT JOIN components c ON c.id = wic.component_id "
                            "WHERE " + where_sql + " "
                            "GROUP BY grp ORDER BY cnt DESC;";

    auto stmt = conn.prepare(query_sql);
    for (size_t i = 0; i < bind_params.size(); ++i) {
        stmt.bind_text(static_cast<int>(i + 1), bind_params[i]);
    }

    struct Slice {
        std::string label;
        int count;
        std::string color;
    };
    std::vector<Slice> raw_slices;
    int total_count = 0;

    // Palette generator
    static const std::vector<std::string> palette = {
        "#0052CC", "#00875A", "#FFAB00", "#FF5630", "#6554C0",
        "#00B8D9", "#36B37E", "#FF7452", "#8777D9", "#4A6B82"
    };
    size_t col_idx = 0;

    while (stmt.step() == SQLITE_ROW) {
        std::string lbl = stmt.column_str(0);
        int cnt = stmt.column_int(1);
        total_count += cnt;

        std::string clr = palette[col_idx % palette.size()];
        if (lbl == "Done") clr = "#36B37E";
        else if (lbl == "In Progress") clr = "#0052CC";
        else if (lbl == "To Do") clr = "#42526E";
        else if (lbl == "In Review") clr = "#6554C0";
        else if (lbl == "QA / Testing") clr = "#FFAB00";
        else if (lbl == "Critical" || lbl == "Highest") clr = "#DE350B";
        else if (lbl == "High") clr = "#FF5630";
        else if (lbl == "Medium") clr = "#FFAB00";
        else if (lbl == "Low" || lbl == "Lowest") clr = "#00875A";

        raw_slices.push_back({lbl, cnt, clr});
        col_idx++;
    }

    auto slices_arr = json::json_value::make_array();
    for (const auto &s : raw_slices) {
        double pct = total_count > 0 ? std::round((static_cast<double>(s.count) * 1000.0) / total_count) / 10.0 : 0.0;
        auto obj = json::json_value::make_object();
        obj.add_str("label", s.label);
        obj.add_int("count", s.count);
        obj.add_double("percentage", pct);
        obj.add_str("color", s.color);
        slices_arr.add(std::move(obj));
    }

    auto res = json::json_value::make_object();
    res.add_str("statistic_type", stat_type);
    res.add_int("total", total_count);
    res.add("slices", std::move(slices_arr));
    return res;
}

// 3. Created vs Resolved Gadget
inline json::json_value compute_created_vs_resolved(db::sqlite_db &conn,
                                                    int current_user_id,
                                                    const json::json_value &prefs) {
    int days = prefs.get_int("days", 30);
    if (days != 14 && days != 30 && days != 60 && days != 90) {
        days = 30;
    }
    int project_id = prefs.get_int("project_id", 0);

    auto report = analytics::calculate_created_vs_resolved(conn, project_id, days);
    return report.to_json();
}

// 4. Sprint Burndown Gadget
inline json::json_value compute_sprint_burndown(db::sqlite_db &conn,
                                                int current_user_id,
                                                const json::json_value &prefs) {
    int sprint_id = prefs.get_int("sprint_id", 0);
    std::string unit = prefs.get_string("unit", "points");

    // If sprint_id == 0, auto-find active sprint
    if (sprint_id == 0) {
        auto sstmt = conn.prepare("SELECT id FROM sprints WHERE status = 'active' ORDER BY is_current DESC, id DESC LIMIT 1;");
        if (sstmt.step() == SQLITE_ROW) {
            sprint_id = sstmt.column_int(0);
        } else {
            // Fallback to any sprint
            auto fstmt = conn.prepare("SELECT id FROM sprints ORDER BY id DESC LIMIT 1;");
            if (fstmt.step() == SQLITE_ROW) {
                sprint_id = fstmt.column_int(0);
            }
        }
    }

    if (sprint_id == 0) {
        auto empty_res = json::json_value::make_object();
        empty_res.add_str("error", "No active or historical sprint found.");
        return empty_res;
    }

    auto report = analytics::calculate_sprint_burndown(conn, sprint_id, true);
    return report.to_json();
}

// 5. Two-Dimensional Filter Statistics Gadget
inline json::json_value compute_two_dimensional_stats(db::sqlite_db &conn,
                                                      int current_user_id,
                                                      const std::string &current_user_name,
                                                      const json::json_value &prefs) {
    std::string x_axis = prefs.get_string("x_axis", "status");
    std::string y_axis = prefs.get_string("y_axis", "assignee");
    int filter_id = prefs.get_int("filter_id", 0);
    std::string jql_query = prefs.get_string("jql");

    std::string where_sql = "1=1";
    std::vector<std::string> bind_params;

    if (filter_id > 0) {
        auto fstmt = conn.prepare("SELECT jql_query FROM saved_filters WHERE id = ?;");
        fstmt.bind_int(1, filter_id);
        if (fstmt.step() == SQLITE_ROW && jql_query.empty()) {
            jql_query = fstmt.column_str(0);
        }
    }

    if (!jql_query.empty()) {
        try {
            jql::JqlLexer lexer(jql_query);
            auto tokens = lexer.tokenize();
            jql::JqlParser parser(tokens);
            auto ast = parser.parse();
            jql::SqlContext ctx(conn, current_user_id, current_user_name, current_user_name);
            where_sql = ast.to_where_sql(ctx);
            bind_params = ctx.params;
        } catch (...) {
            where_sql = "1=1";
        }
    }

    auto map_field_expr = [](const std::string &dim) -> std::string {
        if (dim == "assignee") return "COALESCE(u.display_name, 'Unassigned')";
        if (dim == "priority") return "w.priority";
        if (dim == "status") return "w.status";
        if (dim == "issue_type" || dim == "type") return "w.type";
        if (dim == "component") return "COALESCE(c.name, 'No Component')";
        return "w.status";
    };

    std::string x_expr = map_field_expr(x_axis);
    std::string y_expr = map_field_expr(y_axis);

    std::string query_sql = "SELECT " + x_expr + " AS x_dim, " + y_expr + " AS y_dim, COUNT(w.id) AS cnt "
                            "FROM work_items w "
                            "LEFT JOIN users u ON u.id = w.assignee_id "
                            "LEFT JOIN work_item_components wic ON wic.work_item_id = w.id "
                            "LEFT JOIN components c ON c.id = wic.component_id "
                            "WHERE " + where_sql + " "
                            "GROUP BY x_dim, y_dim;";

    auto stmt = conn.prepare(query_sql);
    for (size_t i = 0; i < bind_params.size(); ++i) {
        stmt.bind_text(static_cast<int>(i + 1), bind_params[i]);
    }

    std::map<std::pair<std::string, std::string>, int> counts;
    std::set<std::string> x_set;
    std::set<std::string> y_set;

    while (stmt.step() == SQLITE_ROW) {
        std::string x_val = stmt.column_str(0);
        std::string y_val = stmt.column_str(1);
        int cnt = stmt.column_int(2);
        counts[{x_val, y_val}] = cnt;
        x_set.insert(x_val);
        y_set.insert(y_val);
    }

    // Default headers if empty
    if (x_set.empty()) {
        if (x_axis == "status") { x_set = {"To Do", "In Progress", "In Review", "Done"}; }
        else { x_set = {"Value 1", "Value 2"}; }
    }
    if (y_set.empty()) {
        y_set = {"No items"};
    }

    std::vector<std::string> x_labels(x_set.begin(), x_set.end());
    std::vector<std::string> y_labels(y_set.begin(), y_set.end());

    auto x_labels_arr = json::json_value::make_array();
    for (const auto &xl : x_labels) x_labels_arr.add(json::json_value::make_string(xl));

    auto y_labels_arr = json::json_value::make_array();
    for (const auto &yl : y_labels) y_labels_arr.add(json::json_value::make_string(yl));

    auto rows_arr = json::json_value::make_array();
    std::vector<int> col_totals(x_labels.size(), 0);
    int grand_total = 0;

    for (size_t yi = 0; yi < y_labels.size(); ++yi) {
        auto row_obj = json::json_value::make_object();
        row_obj.add_str("label", y_labels[yi]);

        auto cells_arr = json::json_value::make_array();
        int row_sum = 0;
        for (size_t xi = 0; xi < x_labels.size(); ++xi) {
            auto it = counts.find({x_labels[xi], y_labels[yi]});
            int cell_val = (it != counts.end()) ? it->second : 0;
            cells_arr.add(json::json_value::make_int(cell_val));
            row_sum += cell_val;
            col_totals[xi] += cell_val;
            grand_total += cell_val;
        }
        row_obj.add("values", std::move(cells_arr));
        row_obj.add_int("total", row_sum);
        rows_arr.add(std::move(row_obj));
    }

    auto col_totals_arr = json::json_value::make_array();
    for (int ct : col_totals) col_totals_arr.add(json::json_value::make_int(ct));

    auto res = json::json_value::make_object();
    res.add_str("x_axis", x_axis);
    res.add_str("y_axis", y_axis);
    res.add("x_labels", std::move(x_labels_arr));
    res.add("y_labels", std::move(y_labels_arr));
    res.add("rows", std::move(rows_arr));
    res.add("col_totals", std::move(col_totals_arr));
    res.add_int("grand_total", grand_total);
    return res;
}

// 6. Assigned to Me Gadget
inline json::json_value compute_assigned_to_me(db::sqlite_db &conn,
                                               int current_user_id,
                                               const json::json_value &prefs) {
    int limit = std::clamp(prefs.get_int("limit", 10), 1, 50);
    bool include_resolved = prefs.get_bool("include_resolved", false);

    std::string sql = R"SQL(
        SELECT w.id, w.key, w.title, w.type, w.status, w.priority, w.story_points,
               w.due_date, w.remaining_estimate_seconds, w.updated_at,
               p.key AS project_key
        FROM work_items w
        JOIN projects p ON p.id = w.project_id
        WHERE w.assignee_id = ? )SQL";

    if (!include_resolved) {
        sql += " AND w.status != 'Done' ";
    }
    sql += " ORDER BY CASE w.priority WHEN 'Critical' THEN 1 WHEN 'Highest' THEN 2 WHEN 'High' THEN 3 WHEN 'Medium' THEN 4 ELSE 5 END, w.updated_at DESC LIMIT ?;";

    auto stmt = conn.prepare(sql);
    stmt.bind_int(1, current_user_id);
    stmt.bind_int(2, limit);

    auto items_arr = json::json_value::make_array();
    int total_count = 0;
    while (stmt.step() == SQLITE_ROW) {
        total_count++;
        auto it = json::json_value::make_object();
        it.add_int("id", stmt.column_int(0));
        it.add_str("key", stmt.column_str(1));
        it.add_str("title", stmt.column_str(2));
        it.add_str("type", stmt.column_str(3));
        it.add_str("status", stmt.column_str(4));
        it.add_str("priority", stmt.column_str(5));
        it.add_int("story_points", stmt.column_int(6));
        it.add_str("due_date", stmt.column_str(7));
        it.add_int("remaining_estimate_seconds", stmt.column_int(8));
        it.add_str("updated_at", stmt.column_str(9));
        it.add_str("project_key", stmt.column_str(10));
        items_arr.add(std::move(it));
    }

    auto res = json::json_value::make_object();
    res.add_int("count", total_count);
    res.add("items", std::move(items_arr));
    return res;
}

// 7. Activity Stream Gadget
inline json::json_value compute_activity_stream(db::sqlite_db &conn,
                                                int current_user_id,
                                                const json::json_value &prefs) {
    int limit = std::clamp(prefs.get_int("limit", 15), 1, 50);
    int project_id = prefs.get_int("project_id", 0);

    // Collect latest activities from workflow history, comments, and worklogs
    std::string sql = R"SQL(
        SELECT
            'transition' AS event_type,
            wh.id AS event_id,
            wh.created_at AS timestamp,
            COALESCE(u.display_name, 'System') AS user_name,
            COALESCE(u.avatar_color, '#0052CC') AS avatar_color,
            w.id AS item_id,
            w.key AS item_key,
            w.title AS item_title,
            wh.from_status || ' -> ' || wh.to_status AS details
        FROM workflow_history wh
        JOIN work_items w ON w.id = wh.work_item_id
        LEFT JOIN users u ON u.id = wh.changed_by
        WHERE (? = 0 OR w.project_id = ?)

        UNION ALL

        SELECT
            'comment' AS event_type,
            c.id AS event_id,
            c.created_at AS timestamp,
            COALESCE(u.display_name, 'System') AS user_name,
            COALESCE(u.avatar_color, '#00875A') AS avatar_color,
            w.id AS item_id,
            w.key AS item_key,
            w.title AS item_title,
            SUBSTR(c.body, 1, 80) AS details
        FROM comments c
        JOIN work_items w ON w.id = c.work_item_id
        LEFT JOIN users u ON u.id = c.author_id
        WHERE (? = 0 OR w.project_id = ?)

        UNION ALL

        SELECT
            'worklog' AS event_type,
            wl.id AS event_id,
            wl.started_at AS timestamp,
            COALESCE(u.display_name, 'System') AS user_name,
            COALESCE(u.avatar_color, '#6554C0') AS avatar_color,
            w.id AS item_id,
            w.key AS item_key,
            w.title AS item_title,
            ('Logged ' || CAST(ROUND(wl.time_spent_seconds / 3600.0, 1) AS TEXT) || 'h' || CASE WHEN wl.description != '' THEN ': ' || SUBSTR(wl.description, 1, 50) ELSE '' END) AS details
        FROM worklogs wl
        JOIN work_items w ON w.id = wl.work_item_id
        LEFT JOIN users u ON u.id = wl.author_id
        WHERE (? = 0 OR w.project_id = ?)

        ORDER BY timestamp DESC
        LIMIT ?;
    )SQL";

    auto stmt = conn.prepare(sql);
    stmt.bind_int(1, project_id);
    stmt.bind_int(2, project_id);
    stmt.bind_int(3, project_id);
    stmt.bind_int(4, project_id);
    stmt.bind_int(5, project_id);
    stmt.bind_int(6, project_id);
    stmt.bind_int(7, limit);

    auto items_arr = json::json_value::make_array();
    while (stmt.step() == SQLITE_ROW) {
        auto it = json::json_value::make_object();
        it.add_str("event_type", stmt.column_str(0));
        it.add_int("event_id", stmt.column_int(1));
        it.add_str("timestamp", stmt.column_str(2));
        it.add_str("user_name", stmt.column_str(3));
        it.add_str("avatar_color", stmt.column_str(4));
        it.add_int("item_id", stmt.column_int(5));
        it.add_str("item_key", stmt.column_str(6));
        it.add_str("item_title", stmt.column_str(7));
        it.add_str("details", stmt.column_str(8));
        items_arr.add(std::move(it));
    }

    auto res = json::json_value::make_object();
    res.add("activities", std::move(items_arr));
    return res;
}

// 8. Roadmap Summary Gadget
inline json::json_value compute_roadmap_summary(db::sqlite_db &conn,
                                                int current_user_id,
                                                const json::json_value &prefs) {
    int project_id = prefs.get_int("project_id", 1);

    // Fetch Versions
    auto vstmt = conn.prepare(R"SQL(
        SELECT v.id, v.name, v.release_date, v.status,
               COUNT(w.id) AS total_issues,
               SUM(CASE WHEN w.status = 'Done' THEN 1 ELSE 0 END) AS done_issues
        FROM versions v
        LEFT JOIN work_item_versions wiv ON wiv.version_id = v.id
        LEFT JOIN work_items w ON w.id = wiv.work_item_id
        WHERE v.project_id = ?
        GROUP BY v.id
        ORDER BY v.status ASC, v.release_date ASC;
    )SQL");
    vstmt.bind_int(1, project_id);

    auto versions_arr = json::json_value::make_array();
    while (vstmt.step() == SQLITE_ROW) {
        auto v = json::json_value::make_object();
        v.add_int("id", vstmt.column_int(0));
        v.add_str("name", vstmt.column_str(1));
        v.add_str("release_date", vstmt.column_str(2));
        v.add_str("status", vstmt.column_str(3));
        int total = vstmt.column_int(4);
        int done = vstmt.column_int(5);
        double pct = total > 0 ? std::round((static_cast<double>(done) * 1000.0) / total) / 10.0 : 0.0;
        v.add_int("total_issues", total);
        v.add_int("done_issues", done);
        v.add_double("progress_percentage", pct);
        versions_arr.add(std::move(v));
    }

    // Fetch Epics
    auto estmt = conn.prepare(R"SQL(
        SELECT e.id, e.key, e.title, e.status,
               COALESCE(SUM(c.story_points), 0) AS total_points,
               COALESCE(SUM(CASE WHEN c.status = 'Done' THEN c.story_points ELSE 0 END), 0) AS done_points,
               COUNT(c.id) AS child_count
        FROM work_items e
        LEFT JOIN work_items c ON c.parent_id = e.id
        WHERE e.type = 'Epic' AND e.project_id = ?
        GROUP BY e.id
        ORDER BY e.id ASC;
    )SQL");
    estmt.bind_int(1, project_id);

    auto epics_arr = json::json_value::make_array();
    while (estmt.step() == SQLITE_ROW) {
        auto ep = json::json_value::make_object();
        ep.add_int("id", estmt.column_int(0));
        ep.add_str("key", estmt.column_str(1));
        ep.add_str("title", estmt.column_str(2));
        ep.add_str("status", estmt.column_str(3));
        int total_pts = estmt.column_int(4);
        int done_pts = estmt.column_int(5);
        int child_cnt = estmt.column_int(6);
        double pct = total_pts > 0 ? std::round((static_cast<double>(done_pts) * 1000.0) / total_pts) / 10.0 : 0.0;
        ep.add_int("total_points", total_pts);
        ep.add_int("done_points", done_pts);
        ep.add_int("child_count", child_cnt);
        ep.add_double("progress_percentage", pct);
        epics_arr.add(std::move(ep));
    }

    auto res = json::json_value::make_object();
    res.add_int("project_id", project_id);
    res.add("versions", std::move(versions_arr));
    res.add("epics", std::move(epics_arr));
    return res;
}

// Master dispatch for computing gadget data
inline json::json_value execute_gadget(db::sqlite_db &conn,
                                       int current_user_id,
                                       const std::string &current_user_name,
                                       const std::string &gadget_type,
                                       const json::json_value &prefs) {
    if (gadget_type == "filter_results") {
        return compute_filter_results(conn, current_user_id, current_user_name, prefs);
    } else if (gadget_type == "pie_chart") {
        return compute_pie_chart(conn, current_user_id, current_user_name, prefs);
    } else if (gadget_type == "created_vs_resolved") {
        return compute_created_vs_resolved(conn, current_user_id, prefs);
    } else if (gadget_type == "sprint_burndown") {
        return compute_sprint_burndown(conn, current_user_id, prefs);
    } else if (gadget_type == "two_dimensional_stats") {
        return compute_two_dimensional_stats(conn, current_user_id, current_user_name, prefs);
    } else if (gadget_type == "assigned_to_me") {
        return compute_assigned_to_me(conn, current_user_id, prefs);
    } else if (gadget_type == "activity_stream") {
        return compute_activity_stream(conn, current_user_id, prefs);
    } else if (gadget_type == "roadmap_summary") {
        return compute_roadmap_summary(conn, current_user_id, prefs);
    }

    auto err = json::json_value::make_object();
    err.add_str("error", "Unknown gadget_type: " + gadget_type);
    return err;
}

} // namespace dashboard
