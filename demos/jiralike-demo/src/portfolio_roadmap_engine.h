#pragma once

#include "raii_sqlite.h"
#include "raii_json.h"
#include "realtime_engine.h"

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <set>
#include <unordered_set>
#include <queue>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <sstream>
#include <iostream>
#include <iomanip>

namespace portfolio {

// --- Date Utilities ---

inline std::time_t parse_date_to_epoch(const std::string &str) {
    if (str.size() < 10) return 0;
    std::tm tm{};
    tm.tm_year = std::atoi(str.substr(0, 4).c_str()) - 1900;
    tm.tm_mon  = std::atoi(str.substr(5, 2).c_str()) - 1;
    tm.tm_mday = std::atoi(str.substr(8, 2).c_str());
    tm.tm_hour = 12; // Noon UTC to avoid daylight saving/boundary issues
    tm.tm_min  = 0;
    tm.tm_sec  = 0;
    return timegm(&tm);
}

inline std::string epoch_to_date_str(std::time_t t) {
    if (t <= 0) return "";
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return std::string(buf);
}

inline int days_between(const std::string &d1, const std::string &d2) {
    std::time_t t1 = parse_date_to_epoch(d1);
    std::time_t t2 = parse_date_to_epoch(d2);
    if (t1 == 0 || t2 == 0) return 0;
    return static_cast<int>(std::round(std::difftime(t2, t1) / 86400.0));
}

inline std::string add_days(const std::string &base_date, int days) {
    std::time_t t = parse_date_to_epoch(base_date);
    if (t == 0) return "";
    t += static_cast<std::time_t>(days) * 86400;
    return epoch_to_date_str(t);
}

inline std::string get_today_str() {
    std::time_t now = std::time(nullptr);
    return epoch_to_date_str(now);
}

inline bool is_done_status(const std::string &status) {
    std::string s = status;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return (s == "done" || s == "closed" || s == "resolved" || s == "finished" || s == "completed");
}

inline bool is_valid_dependency_type(const std::string &link_type) {
    std::string t = link_type;
    std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return std::toupper(c); });
    return (t == "FS" || t == "SS" || t == "FF" || t == "SF" || t == "BLOCKS" || t == "FINISH_TO_START");
}

inline std::string normalize_dep_type(const std::string &link_type) {
    std::string t = link_type;
    std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return std::toupper(c); });
    if (t == "BLOCKS" || t == "FINISH_TO_START" || t == "FS") return "FS";
    if (t == "START_TO_START" || t == "SS") return "SS";
    if (t == "FINISH_TO_FINISH" || t == "FF") return "FF";
    if (t == "START_TO_FINISH" || t == "SF") return "SF";
    return "FS";
}

// --- Rollup Data Structures ---

struct RollupItem {
    int id = 0;
    int project_id = 0;
    std::string project_key;
    std::string key;
    std::string type;
    std::string status;
    std::string priority;
    std::string title;
    int parent_id = 0;
    int sprint_id = 0;
    std::string sprint_name;
    int assignee_id = 0;
    std::string assignee_name;
    std::string start_date;
    std::string due_date;
    int story_points = 0;
    int progress = 0;
    int64_t original_estimate_seconds = 0;
    int64_t remaining_estimate_seconds = 0;
    int64_t time_spent_seconds = 0;
    int hierarchy_level = 0; // 2=Initiative, 1=Epic, 0=Story/Task, -1=Subtask

    // Aggregated Rollup Values
    int rollup_story_points = 0;
    int64_t rollup_original_estimate_seconds = 0;
    int64_t rollup_remaining_estimate_seconds = 0;
    int64_t rollup_time_spent_seconds = 0;
    int rollup_progress = 0;
    std::string rollup_start_date;
    std::string rollup_due_date;
    int child_count = 0;
    int completed_child_count = 0;
    std::vector<int> child_ids;
};

// Recursive rollup computation helper
inline void compute_node_rollup(int node_id,
                                std::unordered_map<int, RollupItem> &items,
                                std::unordered_set<int> &visited) {
    if (visited.count(node_id)) return;
    visited.insert(node_id);

    auto it = items.find(node_id);
    if (it == items.end()) return;

    RollupItem &cur = it->second;

    // First recursively compute all children
    for (int cid : cur.child_ids) {
        compute_node_rollup(cid, items, visited);
    }

    if (cur.child_ids.empty()) {
        // Leaf node: direct values become rollups
        cur.rollup_story_points = cur.story_points;
        cur.rollup_original_estimate_seconds = cur.original_estimate_seconds;
        cur.rollup_remaining_estimate_seconds = cur.remaining_estimate_seconds;
        cur.rollup_time_spent_seconds = cur.time_spent_seconds;
        cur.rollup_progress = cur.progress;
        cur.rollup_start_date = cur.start_date;
        cur.rollup_due_date = cur.due_date;
        cur.child_count = 0;
        cur.completed_child_count = is_done_status(cur.status) ? 1 : 0;
    } else {
        // Parent node: aggregate from children
        int sum_sp = 0;
        int64_t sum_orig = 0;
        int64_t sum_rem = 0;
        int64_t sum_spent = 0;
        int total_children = 0;
        int total_completed = 0;
        std::string min_start = cur.start_date;
        std::string max_due = cur.due_date;

        for (int cid : cur.child_ids) {
            auto cit = items.find(cid);
            if (cit == items.end()) continue;
            const RollupItem &child = cit->second;

            sum_sp += child.rollup_story_points;
            sum_orig += child.rollup_original_estimate_seconds;
            sum_rem += child.rollup_remaining_estimate_seconds;
            sum_spent += child.rollup_time_spent_seconds;
            total_children += (child.child_count + 1);
            total_completed += (child.completed_child_count + (is_done_status(child.status) ? 1 : 0));

            if (!child.rollup_start_date.empty()) {
                if (min_start.empty() || child.rollup_start_date < min_start) {
                    min_start = child.rollup_start_date;
                }
            }
            if (!child.rollup_due_date.empty()) {
                if (max_due.empty() || child.rollup_due_date > max_due) {
                    max_due = child.rollup_due_date;
                }
            }
        }

        // Add parent's own values if present
        cur.rollup_story_points = sum_sp + cur.story_points;
        cur.rollup_original_estimate_seconds = sum_orig + cur.original_estimate_seconds;
        cur.rollup_remaining_estimate_seconds = sum_rem + cur.remaining_estimate_seconds;
        cur.rollup_time_spent_seconds = sum_spent + cur.time_spent_seconds;
        cur.child_count = total_children;
        cur.completed_child_count = total_completed;
        cur.rollup_start_date = min_start;
        cur.rollup_due_date = max_due;

        // Progress rollup calculation
        if (cur.rollup_time_spent_seconds + cur.rollup_remaining_estimate_seconds > 0) {
            cur.rollup_progress = static_cast<int>(
                (cur.rollup_time_spent_seconds * 100) /
                (cur.rollup_time_spent_seconds + cur.rollup_remaining_estimate_seconds)
            );
        } else if (cur.rollup_story_points > 0 && sum_sp > 0) {
            // SP completion ratio
            int done_sp = 0;
            for (int cid : cur.child_ids) {
                auto cit = items.find(cid);
                if (cit != items.end() && is_done_status(cit->second.status)) {
                    done_sp += cit->second.rollup_story_points;
                }
            }
            cur.rollup_progress = (done_sp * 100) / sum_sp;
        } else if (total_children > 0) {
            cur.rollup_progress = (total_completed * 100) / total_children;
        } else {
            cur.rollup_progress = cur.progress;
        }
        if (cur.rollup_progress > 100) cur.rollup_progress = 100;
    }
}

// Convert RollupItem to JSON object
inline json::json_value rollup_item_to_json(const RollupItem &item,
                                            const std::unordered_map<int, RollupItem> &all_items,
                                            bool include_children_tree = true) {
    auto obj = json::json_value::make_object();
    obj.add_int("id", item.id);
    obj.add_int("project_id", item.project_id);
    obj.add_str("project_key", item.project_key);
    obj.add_str("key", item.key);
    obj.add_str("type", item.type);
    obj.add_str("status", item.status);
    obj.add_str("priority", item.priority);
    obj.add_str("title", item.title);
    obj.add_nullable_int("parent_id", item.parent_id > 0 ? item.parent_id : 0);
    obj.add_nullable_int("sprint_id", item.sprint_id > 0 ? item.sprint_id : 0);
    obj.add_str("sprint_name", item.sprint_name);
    obj.add_nullable_int("assignee_id", item.assignee_id > 0 ? item.assignee_id : 0);
    obj.add_str("assignee_name", item.assignee_name);
    obj.add_str("start_date", item.start_date);
    obj.add_str("due_date", item.due_date);
    obj.add_int("story_points", item.story_points);
    obj.add_int("progress", item.progress);
    obj.add_int64("original_estimate_seconds", item.original_estimate_seconds);
    obj.add_int64("remaining_estimate_seconds", item.remaining_estimate_seconds);
    obj.add_int64("time_spent_seconds", item.time_spent_seconds);
    obj.add_int("hierarchy_level", item.hierarchy_level);

    // Rollups
    obj.add_int("rollup_story_points", item.rollup_story_points);
    obj.add_int64("rollup_original_estimate_seconds", item.rollup_original_estimate_seconds);
    obj.add_int64("rollup_remaining_estimate_seconds", item.rollup_remaining_estimate_seconds);
    obj.add_int64("rollup_time_spent_seconds", item.rollup_time_spent_seconds);
    obj.add_int("rollup_progress", item.rollup_progress);
    obj.add_str("rollup_start_date", item.rollup_start_date);
    obj.add_str("rollup_due_date", item.rollup_due_date);
    obj.add_int("child_count", item.child_count);
    obj.add_int("completed_child_count", item.completed_child_count);

    if (include_children_tree) {
        auto ch_arr = json::json_value::make_array();
        for (int cid : item.child_ids) {
            auto cit = all_items.find(cid);
            if (cit != all_items.end()) {
                ch_arr.add(rollup_item_to_json(cit->second, all_items, true));
            }
        }
        obj.add("children", std::move(ch_arr));
    }
    return obj;
}

// GET /api/portfolio/rollups
inline json::json_value compute_portfolio_rollups(db::sqlite_db &conn, int project_id = 0) {
    std::string sql = R"SQL(
        SELECT w.id, w.project_id, p.key as project_key, w.key, w.type, w.status, w.priority, w.title,
               COALESCE(w.parent_id, 0), COALESCE(w.sprint_id, 0), COALESCE(s.name, ''),
               COALESCE(w.assignee_id, 0), COALESCE(u.display_name, ''),
               COALESCE(w.start_date, ''), COALESCE(w.due_date, ''),
               w.story_points, w.progress,
               COALESCE(w.original_estimate_seconds, 0),
               COALESCE(w.remaining_estimate_seconds, 0),
               COALESCE(w.time_spent_seconds, 0),
               COALESCE(it.hierarchy_level, 0)
        FROM work_items w
        JOIN projects p ON p.id = w.project_id
        LEFT JOIN sprints s ON s.id = w.sprint_id
        LEFT JOIN users u ON u.id = w.assignee_id
        LEFT JOIN issue_types it ON it.name = w.type
    )SQL";

    if (project_id > 0) {
        sql += " WHERE w.project_id = " + std::to_string(project_id);
    }
    sql += " ORDER BY w.id ASC;";

    auto stmt = conn.prepare(sql);
    std::unordered_map<int, RollupItem> items;

    while (stmt.step() == SQLITE_ROW) {
        RollupItem item;
        item.id = stmt.column_int(0);
        item.project_id = stmt.column_int(1);
        item.project_key = stmt.column_str(2);
        item.key = stmt.column_str(3);
        item.type = stmt.column_str(4);
        item.status = stmt.column_str(5);
        item.priority = stmt.column_str(6);
        item.title = stmt.column_str(7);
        item.parent_id = stmt.column_int(8);
        item.sprint_id = stmt.column_int(9);
        item.sprint_name = stmt.column_str(10);
        item.assignee_id = stmt.column_int(11);
        item.assignee_name = stmt.column_str(12);
        item.start_date = stmt.column_str(13);
        item.due_date = stmt.column_str(14);
        item.story_points = stmt.column_int(15);
        item.progress = stmt.column_int(16);
        item.original_estimate_seconds = stmt.column_int64(17);
        item.remaining_estimate_seconds = stmt.column_int64(18);
        item.time_spent_seconds = stmt.column_int64(19);
        item.hierarchy_level = stmt.column_int(20);

        // Standardize hierarchy_level if not populated in issue_types table
        if (item.type == "Initiative") item.hierarchy_level = 2;
        else if (item.type == "Epic") item.hierarchy_level = 1;
        else if (item.type == "Subtask") item.hierarchy_level = -1;

        items[item.id] = std::move(item);
    }

    // Connect child_ids
    std::vector<int> root_ids;
    for (auto &pair : items) {
        int pid = pair.second.parent_id;
        if (pid > 0 && items.find(pid) != items.end()) {
            items[pid].child_ids.push_back(pair.first);
        } else {
            root_ids.push_back(pair.first);
        }
    }

    // Compute rollups recursively for all items
    std::unordered_set<int> visited;
    for (int rid : root_ids) {
        compute_node_rollup(rid, items, visited);
    }
    // Compute any remaining unvisited disconnected nodes
    for (auto &pair : items) {
        if (!visited.count(pair.first)) {
            compute_node_rollup(pair.first, items, visited);
        }
    }

    // Build Response JSON
    auto res = json::json_value::make_object();
    res.add_int("project_id", project_id);
    res.add_int("total_items", static_cast<int>(items.size()));

    int total_sp = 0;
    int64_t total_orig_sec = 0;
    int64_t total_rem_sec = 0;
    int64_t total_spent_sec = 0;
    int initiatives_count = 0;
    int epics_count = 0;
    int stories_count = 0;
    int completed_count = 0;

    auto items_arr = json::json_value::make_array();
    for (const auto &pair : items) {
        const auto &it = pair.second;
        if (it.type == "Initiative") initiatives_count++;
        else if (it.type == "Epic") epics_count++;
        else stories_count++;

        if (is_done_status(it.status)) completed_count++;

        // Only add leaf story points and estimates to portfolio totals to avoid double-counting
        if (it.child_ids.empty()) {
            total_sp += it.story_points;
            total_orig_sec += it.original_estimate_seconds;
            total_rem_sec += it.remaining_estimate_seconds;
            total_spent_sec += it.time_spent_seconds;
        }

        items_arr.add(rollup_item_to_json(it, items, false));
    }
    res.add("items", std::move(items_arr));

    // Hierarchical Tree
    auto tree_arr = json::json_value::make_array();
    for (int rid : root_ids) {
        auto it = items.find(rid);
        if (it != items.end()) {
            tree_arr.add(rollup_item_to_json(it->second, items, true));
        }
    }
    res.add("tree", std::move(tree_arr));

    // Summary object
    auto summary = json::json_value::make_object();
    summary.add_int("initiatives_count", initiatives_count);
    summary.add_int("epics_count", epics_count);
    summary.add_int("stories_count", stories_count);
    summary.add_int("completed_items_count", completed_count);
    summary.add_int("total_story_points", total_sp);
    summary.add_double("total_original_hours", std::round((total_orig_sec / 3600.0) * 10.0) / 10.0);
    summary.add_double("total_remaining_hours", std::round((total_rem_sec / 3600.0) * 10.0) / 10.0);
    summary.add_double("total_spent_hours", std::round((total_spent_sec / 3600.0) * 10.0) / 10.0);

    int overall_progress = 0;
    if (total_spent_sec + total_rem_sec > 0) {
        overall_progress = static_cast<int>((total_spent_sec * 100) / (total_spent_sec + total_rem_sec));
    } else if (!items.empty()) {
        overall_progress = (completed_count * 100) / static_cast<int>(items.size());
    }
    summary.add_int("overall_progress", overall_progress);
    res.add("summary", std::move(summary));

    return res;
}

// --- Dependency Management & Cycle Detection ---

// Checks if adding directed edge source_id -> target_id would form a circular dependency
inline bool check_cyclic_dependency(db::sqlite_db &conn,
                                    int source_id,
                                    int target_id,
                                    int exclude_link_id = 0,
                                    std::string *out_cycle_info = nullptr) {
    if (source_id <= 0 || target_id <= 0) return false;
    if (source_id == target_id) {
        if (out_cycle_info) *out_cycle_info = "Self-referencing dependency: item cannot depend on itself.";
        return true;
    }

    // Build directed adjacency graph from work_item_links for dependency link types
    // An edge u -> v means task v depends on task u (u precedes v).
    // Adding source -> target creates a cycle iff target can ALREADY reach source!
    auto stmt = conn.prepare(R"SQL(
        SELECT id, source_item_id, target_item_id, link_type
        FROM work_item_links
        WHERE link_type IN ('FS', 'SS', 'FF', 'SF', 'blocks', 'finish_to_start', 'start_to_start', 'finish_to_finish', 'start_to_finish');
    )SQL");

    std::unordered_map<int, std::vector<int>> adj;
    while (stmt.step() == SQLITE_ROW) {
        int lid = stmt.column_int(0);
        if (lid == exclude_link_id) continue;
        int u = stmt.column_int(1);
        int v = stmt.column_int(2);
        adj[u].push_back(v);
    }

    // BFS search from target_id to find if source_id is reachable
    std::unordered_set<int> visited;
    std::queue<std::pair<int, std::vector<int>>> q;
    q.push({target_id, {target_id}});
    visited.insert(target_id);

    while (!q.empty()) {
        auto [cur, path] = q.front();
        q.pop();

        if (cur == source_id) {
            if (out_cycle_info) {
                std::ostringstream oss;
                oss << "Circular dependency detected: ";
                for (size_t i = 0; i < path.size(); ++i) {
                    oss << path[i] << " -> ";
                }
                oss << source_id;
                *out_cycle_info = oss.str();
            }
            return true;
        }

        auto it = adj.find(cur);
        if (it != adj.end()) {
            for (int nxt : it->second) {
                if (!visited.count(nxt)) {
                    visited.insert(nxt);
                    auto npath = path;
                    npath.push_back(nxt);
                    q.push({nxt, npath});
                }
            }
        }
    }

    return false;
}

// GET /api/portfolio/dependencies
inline json::json_value list_dependencies(db::sqlite_db &conn, int project_id = 0, int item_id = 0) {
    std::string sql = R"SQL(
        SELECT l.id, l.source_item_id, l.target_item_id, l.link_type, COALESCE(l.lag_days, 0),
               ws.key, ws.title, ws.status, ws.type, ws.start_date, ws.due_date,
               wt.key, wt.title, wt.status, wt.type, wt.start_date, wt.due_date,
               ws.project_id
        FROM work_item_links l
        JOIN work_items ws ON ws.id = l.source_item_id
        JOIN work_items wt ON wt.id = l.target_item_id
        WHERE 1=1
    )SQL";

    if (project_id > 0) {
        sql += " AND (ws.project_id = " + std::to_string(project_id) + " OR wt.project_id = " + std::to_string(project_id) + ")";
    }
    if (item_id > 0) {
        sql += " AND (l.source_item_id = " + std::to_string(item_id) + " OR l.target_item_id = " + std::to_string(item_id) + ")";
    }
    sql += " ORDER BY l.id ASC;";

    auto stmt = conn.prepare(sql);
    auto arr = json::json_value::make_array();
    while (stmt.step() == SQLITE_ROW) {
        auto obj = json::json_value::make_object();
        obj.add_int("id", stmt.column_int(0));
        obj.add_int("source_item_id", stmt.column_int(1));
        obj.add_int("target_item_id", stmt.column_int(2));
        obj.add_str("link_type", stmt.column_str(3));
        obj.add_int("lag_days", stmt.column_int(4));
        obj.add_str("source_key", stmt.column_str(5));
        obj.add_str("source_title", stmt.column_str(6));
        obj.add_str("source_status", stmt.column_str(7));
        obj.add_str("source_type", stmt.column_str(8));
        obj.add_str("source_start_date", stmt.column_str(9));
        obj.add_str("source_due_date", stmt.column_str(10));
        obj.add_str("target_key", stmt.column_str(11));
        obj.add_str("target_title", stmt.column_str(12));
        obj.add_str("target_status", stmt.column_str(13));
        obj.add_str("target_type", stmt.column_str(14));
        obj.add_str("target_start_date", stmt.column_str(15));
        obj.add_str("target_due_date", stmt.column_str(16));
        arr.add(std::move(obj));
    }
    return arr;
}

// POST /api/portfolio/dependencies
inline int create_dependency(db::sqlite_db &conn,
                             int source_id,
                             int target_id,
                             const std::string &raw_link_type,
                             int lag_days,
                             std::string &out_err) {
    if (source_id <= 0 || target_id <= 0) {
        out_err = "Valid source_item_id and target_item_id are required.";
        return -1;
    }
    if (source_id == target_id) {
        out_err = "An issue cannot depend on itself.";
        return -1;
    }

    std::string link_type = normalize_dep_type(raw_link_type);

    // Verify both items exist
    {
        auto stmt = conn.prepare("SELECT COUNT(*) FROM work_items WHERE id IN (?, ?);");
        stmt.bind_int(1, source_id);
        stmt.bind_int(2, target_id);
        if (stmt.step() == SQLITE_ROW && stmt.column_int(0) < 2) {
            out_err = "One or both work items do not exist.";
            return -1;
        }
    }

    // Check for circular dependency
    std::string cycle_info;
    if (check_cyclic_dependency(conn, source_id, target_id, 0, &cycle_info)) {
        out_err = "Cyclic dependency detected: " + cycle_info;
        return -2; // Code -2 represents cycle conflict
    }

    // Insert dependency link
    db::sqlite_transaction tx(conn);
    auto ins = conn.prepare(R"SQL(
        INSERT INTO work_item_links (source_item_id, target_item_id, link_type, lag_days)
        VALUES (?, ?, ?, ?);
    )SQL");
    ins.bind_int(1, source_id);
    ins.bind_int(2, target_id);
    ins.bind_text(3, link_type);
    ins.bind_int(4, lag_days);
    ins.step_done();
    int new_id = static_cast<int>(conn.last_insert_rowid());
    tx.commit();
    return new_id;
}

// --- Critical Path Method (CPM) Algorithm ---

struct CPMNode {
    int id = 0;
    std::string key;
    std::string title;
    std::string type;
    std::string status;
    int duration_days = 1;
    int fixed_start_offset = 0;
    bool has_fixed_start = false;

    // CPM calculation fields (relative day offsets from T0)
    int es = 0; // Earliest Start
    int ef = 0; // Earliest Finish
    int ls = 0; // Latest Start
    int lf = 0; // Latest Finish
    int total_float = 0;
    int free_float = 0;
    bool is_critical = false;

    // Resolved calendar dates
    std::string earliest_start_date;
    std::string earliest_finish_date;
    std::string latest_start_date;
    std::string latest_finish_date;
};

struct CPMEdge {
    int source_id = 0;
    int target_id = 0;
    std::string link_type = "FS"; // FS, SS, FF, SF
    int lag_days = 0;
};

// GET /api/portfolio/critical-path
inline json::json_value calculate_critical_path(db::sqlite_db &conn, int project_id = 0) {
    // 1. Fetch work items
    std::string sql = R"SQL(
        SELECT w.id, w.key, w.title, w.type, w.status,
               COALESCE(w.start_date, ''), COALESCE(w.due_date, ''),
               COALESCE(w.original_estimate_seconds, 0),
               w.story_points, p.name as project_name
        FROM work_items w
        JOIN projects p ON p.id = w.project_id
    )SQL";
    if (project_id > 0) {
        sql += " WHERE w.project_id = " + std::to_string(project_id);
    }
    sql += " ORDER BY w.id ASC;";

    auto stmt = conn.prepare(sql);
    std::unordered_map<int, CPMNode> nodes;
    std::string earliest_project_date;
    std::string project_name = "Project";

    while (stmt.step() == SQLITE_ROW) {
        CPMNode node;
        node.id = stmt.column_int(0);
        node.key = stmt.column_str(1);
        node.title = stmt.column_str(2);
        node.type = stmt.column_str(3);
        node.status = stmt.column_str(4);
        std::string s_date = stmt.column_str(5);
        std::string d_date = stmt.column_str(6);
        int64_t orig_est = stmt.column_int64(7);
        int sp = stmt.column_int(8);
        project_name = stmt.column_str(9);

        // Determine duration in days
        if (!s_date.empty() && !d_date.empty()) {
            int diff = days_between(s_date, d_date);
            node.duration_days = std::max(1, diff + 1);
        } else if (orig_est > 0) {
            node.duration_days = std::max(1, static_cast<int>(orig_est / 28800)); // 8h/day
        } else if (sp > 0) {
            node.duration_days = std::max(1, sp);
        } else {
            node.duration_days = 1;
        }

        if (!s_date.empty()) {
            node.has_fixed_start = true;
            if (earliest_project_date.empty() || s_date < earliest_project_date) {
                earliest_project_date = s_date;
            }
        }

        nodes[node.id] = std::move(node);
    }

    if (nodes.empty()) {
        auto res = json::json_value::make_object();
        res.add_int("project_id", project_id);
        res.add_str("project_name", project_name);
        res.add_int("total_duration_days", 0);
        res.add_str("project_start_date", get_today_str());
        res.add_str("project_end_date", get_today_str());
        res.add("critical_path", json::json_value::make_array());
        res.add("bottlenecks", json::json_value::make_array());
        res.add("nodes", json::json_value::make_array());
        res.add_int("dependency_count", 0);
        return res;
    }

    if (earliest_project_date.empty()) {
        earliest_project_date = get_today_str();
    }

    // Set relative start offsets from T0
    for (auto &pair : nodes) {
        CPMNode &n = pair.second;
        if (n.has_fixed_start) {
            auto s_stmt = conn.prepare("SELECT COALESCE(start_date, '') FROM work_items WHERE id = ?;");
            s_stmt.bind_int(1, n.id);
            if (s_stmt.step() == SQLITE_ROW) {
                std::string sd = s_stmt.column_str(0);
                if (!sd.empty()) {
                    n.fixed_start_offset = std::max(0, days_between(earliest_project_date, sd));
                }
            }
        }
    }

    // 2. Fetch dependencies
    std::vector<CPMEdge> edges;
    std::unordered_map<int, std::vector<CPMEdge>> outgoing;
    std::unordered_map<int, std::vector<CPMEdge>> incoming;
    std::unordered_map<int, int> in_degree;

    for (const auto &pair : nodes) {
        in_degree[pair.first] = 0;
    }

    {
        std::string dep_sql = R"SQL(
            SELECT source_item_id, target_item_id, link_type, COALESCE(lag_days, 0)
            FROM work_item_links
            WHERE link_type IN ('FS', 'SS', 'FF', 'SF', 'blocks', 'finish_to_start', 'start_to_start', 'finish_to_finish', 'start_to_finish');
        )SQL";
        auto dep_stmt = conn.prepare(dep_sql);
        while (dep_stmt.step() == SQLITE_ROW) {
            int u = dep_stmt.column_int(0);
            int v = dep_stmt.column_int(1);
            if (nodes.find(u) != nodes.end() && nodes.find(v) != nodes.end()) {
                CPMEdge e;
                e.source_id = u;
                e.target_id = v;
                e.link_type = normalize_dep_type(dep_stmt.column_str(2));
                e.lag_days = dep_stmt.column_int(3);
                edges.push_back(e);
                outgoing[u].push_back(e);
                incoming[v].push_back(e);
                in_degree[v]++;
            }
        }
    }

    // 3. Forward Pass via Topological Sort (Kahn's Algorithm)
    std::queue<int> topo_q;
    std::vector<int> topo_order;
    for (const auto &pair : nodes) {
        if (in_degree[pair.first] == 0) {
            topo_q.push(pair.first);
        }
    }

    // Initialize ES and EF
    for (auto &pair : nodes) {
        pair.second.es = pair.second.fixed_start_offset;
        pair.second.ef = pair.second.es + pair.second.duration_days;
    }

    while (!topo_q.empty()) {
        int u = topo_q.front();
        topo_q.pop();
        topo_order.push_back(u);

        const CPMNode &u_node = nodes[u];

        for (const auto &edge : outgoing[u]) {
            int v = edge.target_id;
            CPMNode &v_node = nodes[v];
            int lag = edge.lag_days;

            if (edge.link_type == "FS") {
                v_node.es = std::max(v_node.es, u_node.ef + lag);
            } else if (edge.link_type == "SS") {
                v_node.es = std::max(v_node.es, u_node.es + lag);
            } else if (edge.link_type == "FF") {
                v_node.es = std::max(v_node.es, u_node.ef + lag - v_node.duration_days);
            } else if (edge.link_type == "SF") {
                v_node.es = std::max(v_node.es, u_node.es + lag - v_node.duration_days);
            }
            v_node.ef = v_node.es + v_node.duration_days;

            if (--in_degree[v] == 0) {
                topo_q.push(v);
            }
        }
    }

    // If topological sort missed nodes due to cycles, append remaining
    for (const auto &pair : nodes) {
        if (std::find(topo_order.begin(), topo_order.end(), pair.first) == topo_order.end()) {
            topo_order.push_back(pair.first);
        }
    }

    // Determine total project duration
    int total_project_finish = 0;
    for (const auto &pair : nodes) {
        total_project_finish = std::max(total_project_finish, pair.second.ef);
    }

    // 4. Backward Pass in Reverse Topological Order
    for (auto &pair : nodes) {
        pair.second.lf = total_project_finish;
        pair.second.ls = pair.second.lf - pair.second.duration_days;
    }

    for (auto it = topo_order.rbegin(); it != topo_order.rend(); ++it) {
        int u = *it;
        CPMNode &u_node = nodes[u];

        // Constrain LF by outgoing edges
        for (const auto &edge : outgoing[u]) {
            int v = edge.target_id;
            const CPMNode &v_node = nodes[v];
            int lag = edge.lag_days;

            if (edge.link_type == "FS") {
                u_node.lf = std::min(u_node.lf, v_node.ls - lag);
            } else if (edge.link_type == "SS") {
                u_node.lf = std::min(u_node.lf, v_node.ls - lag + u_node.duration_days);
            } else if (edge.link_type == "FF") {
                u_node.lf = std::min(u_node.lf, v_node.lf - lag);
            } else if (edge.link_type == "SF") {
                u_node.lf = std::min(u_node.lf, v_node.lf - lag + u_node.duration_days);
            }
        }
        u_node.ls = u_node.lf - u_node.duration_days;
    }

    // 5. Compute Floats & Critical Path
    std::vector<int> critical_ids;
    for (auto &pair : nodes) {
        CPMNode &n = pair.second;
        n.total_float = std::max(0, n.ls - n.es);

        // Free float: slack before delaying any immediate successor's ES
        int min_succ_slack = n.total_float;
        for (const auto &edge : outgoing[n.id]) {
            const CPMNode &succ = nodes[edge.target_id];
            int slack = succ.es - (n.ef + edge.lag_days);
            if (slack < min_succ_slack) min_succ_slack = slack;
        }
        n.free_float = std::max(0, min_succ_slack);

        n.is_critical = (n.total_float == 0);
        if (n.is_critical) {
            critical_ids.push_back(n.id);
        }

        // Calendar dates
        n.earliest_start_date = add_days(earliest_project_date, n.es);
        n.earliest_finish_date = add_days(earliest_project_date, n.ef);
        n.latest_start_date = add_days(earliest_project_date, n.ls);
        n.latest_finish_date = add_days(earliest_project_date, n.lf);
    }

    // Sort critical path items chronologically by ES
    std::sort(critical_ids.begin(), critical_ids.end(), [&](int a, int b) {
        if (nodes[a].es != nodes[b].es) return nodes[a].es < nodes[b].es;
        return nodes[a].id < nodes[b].id;
    });

    // 6. Build JSON Response
    auto res = json::json_value::make_object();
    res.add_int("project_id", project_id);
    res.add_str("project_name", project_name);
    res.add_int("total_duration_days", total_project_finish);
    res.add_str("project_start_date", earliest_project_date);
    res.add_str("project_end_date", add_days(earliest_project_date, total_project_finish));
    res.add_int("dependency_count", static_cast<int>(edges.size()));

    auto crit_arr = json::json_value::make_array();
    auto bottleneck_arr = json::json_value::make_array();
    for (int cid : critical_ids) {
        const auto &n = nodes[cid];
        auto c_obj = json::json_value::make_object();
        c_obj.add_int("id", n.id);
        c_obj.add_str("key", n.key);
        c_obj.add_str("title", n.title);
        c_obj.add_str("type", n.type);
        c_obj.add_str("status", n.status);
        c_obj.add_int("duration_days", n.duration_days);
        c_obj.add_str("earliest_start_date", n.earliest_start_date);
        c_obj.add_str("earliest_finish_date", n.earliest_finish_date);
        crit_arr.add(std::move(c_obj));

        // Bottlenecks are active (not completed) critical items
        if (!is_done_status(n.status)) {
            auto b_obj = json::json_value::make_object();
            b_obj.add_int("id", n.id);
            b_obj.add_str("key", n.key);
            b_obj.add_str("title", n.title);
            b_obj.add_str("status", n.status);
            bottleneck_arr.add(std::move(b_obj));
        }
    }
    res.add("critical_path", std::move(crit_arr));
    res.add("bottlenecks", std::move(bottleneck_arr));

    auto nodes_arr = json::json_value::make_array();
    for (const auto &pair : nodes) {
        const auto &n = pair.second;
        auto n_obj = json::json_value::make_object();
        n_obj.add_int("id", n.id);
        n_obj.add_str("key", n.key);
        n_obj.add_str("title", n.title);
        n_obj.add_str("type", n.type);
        n_obj.add_str("status", n.status);
        n_obj.add_int("duration_days", n.duration_days);
        n_obj.add_int("es", n.es);
        n_obj.add_int("ef", n.ef);
        n_obj.add_int("ls", n.ls);
        n_obj.add_int("lf", n.lf);
        n_obj.add_int("total_float", n.total_float);
        n_obj.add_int("free_float", n.free_float);
        n_obj.add_bool("is_critical", n.is_critical);
        n_obj.add_str("earliest_start_date", n.earliest_start_date);
        n_obj.add_str("earliest_finish_date", n.earliest_finish_date);
        n_obj.add_str("latest_start_date", n.latest_start_date);
        n_obj.add_str("latest_finish_date", n.latest_finish_date);
        nodes_arr.add(std::move(n_obj));
    }
    res.add("nodes", std::move(nodes_arr));

    return res;
}

// --- Timeline Baselines Engine ---

// POST /api/portfolio/baselines
inline int create_timeline_baseline(db::sqlite_db &conn,
                                    int project_id,
                                    const std::string &name,
                                    const std::string &description,
                                    int user_id,
                                    std::string &out_err) {
    if (project_id <= 0 || name.empty()) {
        out_err = "Project ID and baseline name are required.";
        return -1;
    }

    db::sqlite_transaction tx(conn);

    // 1. Insert header
    auto ins_h = conn.prepare(R"SQL(
        INSERT INTO timeline_baselines (project_id, name, description, created_by)
        VALUES (?, ?, ?, ?);
    )SQL");
    ins_h.bind_int(1, project_id);
    ins_h.bind_text(2, name);
    ins_h.bind_text(3, description);
    ins_h.bind_optional_int(4, user_id > 0 ? user_id : 0);
    ins_h.step_done();
    int baseline_id = static_cast<int>(conn.last_insert_rowid());

    // 2. Snapshot current work items
    auto ins_item = conn.prepare(R"SQL(
        INSERT INTO timeline_baseline_items (
            baseline_id, work_item_id, item_key, title, start_date, due_date, duration_days, story_points, status
        )
        SELECT ?, w.id, w.key, w.title,
               COALESCE(w.start_date, ''), COALESCE(w.due_date, ''),
               CASE
                   WHEN w.start_date IS NOT NULL AND w.due_date IS NOT NULL AND w.start_date != '' AND w.due_date != ''
                   THEN MAX(1, CAST((julianday(w.due_date) - julianday(w.start_date) + 1) AS INTEGER))
                   ELSE 1
               END,
               w.story_points, w.status
        FROM work_items w
        WHERE w.project_id = ?;
    )SQL");
    ins_item.bind_int(1, baseline_id);
    ins_item.bind_int(2, project_id);
    ins_item.step_done();

    tx.commit();
    return baseline_id;
}

// GET /api/portfolio/baselines
inline json::json_value list_timeline_baselines(db::sqlite_db &conn, int project_id = 0) {
    std::string sql = R"SQL(
        SELECT b.id, b.project_id, p.name as project_name, b.name, b.description,
               b.created_by, COALESCE(u.display_name, 'System'), b.created_at,
               COUNT(bi.id) as item_count
        FROM timeline_baselines b
        JOIN projects p ON p.id = b.project_id
        LEFT JOIN users u ON u.id = b.created_by
        LEFT JOIN timeline_baseline_items bi ON bi.baseline_id = b.id
    )SQL";
    if (project_id > 0) {
        sql += " WHERE b.project_id = " + std::to_string(project_id);
    }
    sql += " GROUP BY b.id ORDER BY b.id DESC;";

    auto stmt = conn.prepare(sql);
    auto arr = json::json_value::make_array();
    while (stmt.step() == SQLITE_ROW) {
        auto obj = json::json_value::make_object();
        obj.add_int("id", stmt.column_int(0));
        obj.add_int("project_id", stmt.column_int(1));
        obj.add_str("project_name", stmt.column_str(2));
        obj.add_str("name", stmt.column_str(3));
        obj.add_str("description", stmt.column_str(4));
        obj.add_nullable_int("created_by", stmt.column_int(5));
        obj.add_str("created_by_name", stmt.column_str(6));
        obj.add_str("created_at", stmt.column_str(7));
        obj.add_int("item_count", stmt.column_int(8));
        arr.add(std::move(obj));
    }
    return arr;
}

// GET /api/portfolio/baselines/:id
inline json::json_value get_baseline_detail(db::sqlite_db &conn, int baseline_id) {
    auto b_stmt = conn.prepare(R"SQL(
        SELECT b.id, b.project_id, p.name, b.name, b.description,
               b.created_by, COALESCE(u.display_name, 'System'), b.created_at
        FROM timeline_baselines b
        JOIN projects p ON p.id = b.project_id
        LEFT JOIN users u ON u.id = b.created_by
        WHERE b.id = ?;
    )SQL");
    b_stmt.bind_int(1, baseline_id);
    if (b_stmt.step() != SQLITE_ROW) {
        return json::json_value::make_null();
    }

    auto res = json::json_value::make_object();
    res.add_int("id", b_stmt.column_int(0));
    res.add_int("project_id", b_stmt.column_int(1));
    res.add_str("project_name", b_stmt.column_str(2));
    res.add_str("name", b_stmt.column_str(3));
    res.add_str("description", b_stmt.column_str(4));
    res.add_nullable_int("created_by", b_stmt.column_int(5));
    res.add_str("created_by_name", b_stmt.column_str(6));
    res.add_str("created_at", b_stmt.column_str(7));

    // Fetch snapshot items joined with live work_items
    auto i_stmt = conn.prepare(R"SQL(
        SELECT bi.id, bi.work_item_id, bi.item_key, bi.title,
               bi.start_date as baseline_start, bi.due_date as baseline_due,
               bi.duration_days as baseline_duration, bi.story_points as baseline_sp,
               bi.status as baseline_status,
               COALESCE(w.start_date, '') as current_start,
               COALESCE(w.due_date, '') as current_due,
               COALESCE(w.status, bi.status) as current_status,
               COALESCE(w.progress, 0) as current_progress
        FROM timeline_baseline_items bi
        LEFT JOIN work_items w ON w.id = bi.work_item_id
        WHERE bi.baseline_id = ?
        ORDER BY bi.id ASC;
    )SQL");
    i_stmt.bind_int(1, baseline_id);

    int total_items = 0;
    int slipped_count = 0;
    int ahead_count = 0;
    int on_track_count = 0;
    int total_slip_days = 0;

    auto items_arr = json::json_value::make_array();
    while (i_stmt.step() == SQLITE_ROW) {
        total_items++;
        auto obj = json::json_value::make_object();
        obj.add_int("id", i_stmt.column_int(0));
        obj.add_int("work_item_id", i_stmt.column_int(1));
        obj.add_str("item_key", i_stmt.column_str(2));
        obj.add_str("title", i_stmt.column_str(3));
        std::string b_start = i_stmt.column_str(4);
        std::string b_due   = i_stmt.column_str(5);
        int b_dur           = i_stmt.column_int(6);
        int b_sp            = i_stmt.column_int(7);
        std::string b_stat  = i_stmt.column_str(8);

        std::string c_start = i_stmt.column_str(9);
        std::string c_due   = i_stmt.column_str(10);
        std::string c_stat  = i_stmt.column_str(11);
        int c_prog          = i_stmt.column_int(12);

        obj.add_str("baseline_start_date", b_start);
        obj.add_str("baseline_due_date", b_due);
        obj.add_int("baseline_duration_days", b_dur);
        obj.add_int("baseline_story_points", b_sp);
        obj.add_str("baseline_status", b_stat);

        obj.add_str("current_start_date", c_start);
        obj.add_str("current_due_date", c_due);
        obj.add_str("current_status", c_stat);
        obj.add_int("current_progress", c_prog);

        // Slippage variance metrics
        int start_var = 0;
        int due_var = 0;
        if (!b_start.empty() && !c_start.empty()) {
            start_var = days_between(b_start, c_start);
        }
        if (!b_due.empty() && !c_due.empty()) {
            due_var = days_between(b_due, c_due);
        }

        obj.add_int("start_variance_days", start_var);
        obj.add_int("due_variance_days", due_var);

        std::string var_status = "on_track";
        if (due_var > 0) {
            var_status = "slipped";
            slipped_count++;
            total_slip_days += due_var;
        } else if (due_var < 0) {
            var_status = "ahead";
            ahead_count++;
        } else {
            on_track_count++;
        }
        obj.add_str("variance_status", var_status);

        items_arr.add(std::move(obj));
    }

    res.add("items", std::move(items_arr));
    res.add_int("total_items", total_items);
    res.add_int("slipped_items_count", slipped_count);
    res.add_int("ahead_items_count", ahead_count);
    res.add_int("on_track_items_count", on_track_count);
    double avg_slip = total_items > 0 ? (static_cast<double>(total_slip_days) / total_items) : 0.0;
    res.add_double("average_slip_days", std::round(avg_slip * 10.0) / 10.0);

    return res;
}

// DELETE /api/portfolio/baselines/:id
inline bool delete_timeline_baseline(db::sqlite_db &conn, int baseline_id) {
    db::sqlite_transaction tx(conn);
    auto del = conn.prepare("DELETE FROM timeline_baselines WHERE id = ?;");
    del.bind_int(1, baseline_id);
    del.step_done();
    tx.commit();
    return true;
}

// --- Team Capacity & Workload Allocation Aggregator ---

struct CapacityUserSummary {
    int user_id = 0;
    std::string username;
    std::string display_name;
    std::string avatar_color = "#0052CC";
    double weekly_capacity_hours = 40.0;
    double total_capacity_hours = 80.0;
    double allocated_hours = 0.0;
    double completed_hours = 0.0;
    double utilization_percentage = 0.0;
    std::string allocation_status = "under_allocated"; // under_allocated (<80%), balanced (80-105%), over_allocated (>105%)
    struct AssignedWorkItem {
        int id = 0;
        std::string key;
        std::string title;
        std::string type;
        std::string status;
        double estimate_hours = 0.0;
        std::string start_date;
        std::string due_date;
    };
    std::vector<AssignedWorkItem> items;
};

// GET /api/portfolio/capacity
inline json::json_value calculate_team_capacity(db::sqlite_db &conn,
                                               int project_id = 0,
                                               int sprint_id = 0,
                                               const std::string &start_date_param = "",
                                               const std::string &end_date_param = "") {
    std::string start_date = start_date_param;
    std::string end_date = end_date_param;

    // If sprint_id is specified, obtain dates from sprints table
    if (sprint_id > 0) {
        auto sp_stmt = conn.prepare("SELECT COALESCE(start_date, ''), COALESCE(end_date, '') FROM sprints WHERE id = ?;");
        sp_stmt.bind_int(1, sprint_id);
        if (sp_stmt.step() == SQLITE_ROW) {
            std::string s1 = sp_stmt.column_str(0);
            std::string s2 = sp_stmt.column_str(1);
            if (!s1.empty() && start_date.empty()) start_date = s1;
            if (!s2.empty() && end_date.empty()) end_date = s2;
        }
    }

    // Default to a 14-day 2-week window from today if missing
    if (start_date.empty()) {
        start_date = get_today_str();
    }
    if (end_date.empty()) {
        end_date = add_days(start_date, 14);
    }

    int span_days = std::max(1, days_between(start_date, end_date));
    double weeks_count = std::max(0.5, span_days / 7.0);

    // 1. Fetch users and their capacity configs
    std::string u_sql = R"SQL(
        SELECT u.id, u.username, u.display_name,
               COALESCE(
                   (SELECT weekly_capacity_hours FROM user_capacities uc WHERE uc.user_id = u.id AND uc.project_id = ?),
                   (SELECT weekly_capacity_hours FROM user_capacities uc WHERE uc.user_id = u.id AND uc.project_id IS NULL),
                   40.0
               )
        FROM users u
        ORDER BY u.id ASC;
    )SQL";
    auto u_stmt = conn.prepare(u_sql);
    u_stmt.bind_int(1, project_id);

    std::map<int, CapacityUserSummary> users;
    while (u_stmt.step() == SQLITE_ROW) {
        CapacityUserSummary cus;
        cus.user_id = u_stmt.column_int(0);
        cus.username = u_stmt.column_str(1);
        cus.display_name = u_stmt.column_str(2);
        cus.weekly_capacity_hours = u_stmt.column_double(3);
        cus.total_capacity_hours = cus.weekly_capacity_hours * weeks_count;
        users[cus.user_id] = std::move(cus);
    }

    // 2. Fetch assigned active work items
    std::string w_sql = R"SQL(
        SELECT w.id, w.key, w.title, w.type, w.status, w.assignee_id,
               COALESCE(w.remaining_estimate_seconds, 0),
               COALESCE(w.original_estimate_seconds, 0),
               w.story_points,
               COALESCE(w.time_spent_seconds, 0),
               COALESCE(w.start_date, ''), COALESCE(w.due_date, '')
        FROM work_items w
        WHERE w.assignee_id IS NOT NULL AND w.assignee_id > 0
    )SQL";

    if (project_id > 0) {
        w_sql += " AND w.project_id = " + std::to_string(project_id);
    }
    if (sprint_id > 0) {
        w_sql += " AND w.sprint_id = " + std::to_string(sprint_id);
    }

    auto w_stmt = conn.prepare(w_sql);
    while (w_stmt.step() == SQLITE_ROW) {
        int wid = w_stmt.column_int(0);
        std::string key = w_stmt.column_str(1);
        std::string title = w_stmt.column_str(2);
        std::string type = w_stmt.column_str(3);
        std::string status = w_stmt.column_str(4);
        int aid = w_stmt.column_int(5);
        int64_t rem_sec = w_stmt.column_int64(6);
        int64_t orig_sec = w_stmt.column_int64(7);
        int sp = w_stmt.column_int(8);
        int64_t spent_sec = w_stmt.column_int64(9);
        std::string sd = w_stmt.column_str(10);
        std::string dd = w_stmt.column_str(11);

        double est_hours = 0.0;
        if (!is_done_status(status)) {
            if (rem_sec > 0) {
                est_hours = rem_sec / 3600.0;
            } else if (orig_sec > 0) {
                est_hours = orig_sec / 3600.0;
            } else if (sp > 0) {
                est_hours = sp * 8.0; // 8 hours per story point default
            } else {
                est_hours = 4.0; // Default nominal 4h
            }
        }

        auto uit = users.find(aid);
        if (uit != users.end()) {
            uit->second.allocated_hours += est_hours;
            uit->second.completed_hours += (spent_sec / 3600.0);
            CapacityUserSummary::AssignedWorkItem awi;
            awi.id = wid;
            awi.key = key;
            awi.title = title;
            awi.type = type;
            awi.status = status;
            awi.estimate_hours = est_hours;
            awi.start_date = sd;
            awi.due_date = dd;
            uit->second.items.push_back(awi);
        }
    }

    // 3. Aggregate totals and statuses
    double team_total_cap = 0.0;
    double team_total_alloc = 0.0;
    double team_total_completed = 0.0;

    auto u_arr = json::json_value::make_array();
    for (auto &pair : users) {
        auto &u = pair.second;
        team_total_cap += u.total_capacity_hours;
        team_total_alloc += u.allocated_hours;
        team_total_completed += u.completed_hours;

        if (u.total_capacity_hours > 0) {
            u.utilization_percentage = (u.allocated_hours / u.total_capacity_hours) * 100.0;
        } else {
            u.utilization_percentage = 0.0;
        }

        if (u.utilization_percentage > 105.0) {
            u.allocation_status = "over_allocated";
        } else if (u.utilization_percentage >= 80.0) {
            u.allocation_status = "balanced";
        } else {
            u.allocation_status = "under_allocated";
        }

        auto u_obj = json::json_value::make_object();
        u_obj.add_int("user_id", u.user_id);
        u_obj.add_str("username", u.username);
        u_obj.add_str("display_name", u.display_name);
        u_obj.add_str("avatar_color", u.avatar_color);
        u_obj.add_double("weekly_capacity_hours", u.weekly_capacity_hours);
        u_obj.add_double("total_capacity_hours", std::round(u.total_capacity_hours * 10.0) / 10.0);
        u_obj.add_double("allocated_hours", std::round(u.allocated_hours * 10.0) / 10.0);
        u_obj.add_double("completed_hours", std::round(u.completed_hours * 10.0) / 10.0);
        u_obj.add_double("utilization_percentage", std::round(u.utilization_percentage * 10.0) / 10.0);
        u_obj.add_str("allocation_status", u.allocation_status);

        auto itm_arr = json::json_value::make_array();
        for (const auto &item : u.items) {
            auto itm = json::json_value::make_object();
            itm.add_int("id", item.id);
            itm.add_str("key", item.key);
            itm.add_str("title", item.title);
            itm.add_str("type", item.type);
            itm.add_str("status", item.status);
            itm.add_double("estimate_hours", std::round(item.estimate_hours * 10.0) / 10.0);
            itm.add_str("start_date", item.start_date);
            itm.add_str("due_date", item.due_date);
            itm_arr.add(std::move(itm));
        }
        u_obj.add("assigned_items", std::move(itm_arr));
        u_arr.add(std::move(u_obj));
    }

    auto res = json::json_value::make_object();
    res.add_int("project_id", project_id);
    res.add_int("sprint_id", sprint_id);
    res.add_str("start_date", start_date);
    res.add_str("end_date", end_date);
    res.add_double("weeks_count", std::round(weeks_count * 10.0) / 10.0);
    res.add_double("team_total_capacity_hours", std::round(team_total_cap * 10.0) / 10.0);
    res.add_double("team_total_allocated_hours", std::round(team_total_alloc * 10.0) / 10.0);
    res.add_double("team_total_completed_hours", std::round(team_total_completed * 10.0) / 10.0);

    double team_util = team_total_cap > 0 ? ((team_total_alloc / team_total_cap) * 100.0) : 0.0;
    res.add_double("team_utilization_percentage", std::round(team_util * 10.0) / 10.0);
    res.add("users", std::move(u_arr));

    return res;
}

// GET /api/portfolio/capacity/configs
inline json::json_value get_capacity_configs(db::sqlite_db &conn, int project_id = 0) {
    auto stmt = conn.prepare(R"SQL(
        SELECT uc.id, uc.user_id, u.display_name, u.username,
               COALESCE(uc.project_id, 0), uc.weekly_capacity_hours, uc.updated_at
        FROM user_capacities uc
        JOIN users u ON u.id = uc.user_id
        WHERE (? = 0 OR uc.project_id = ? OR uc.project_id IS NULL)
        ORDER BY uc.user_id ASC;
    )SQL");
    stmt.bind_int(1, project_id);
    stmt.bind_int(2, project_id);

    auto arr = json::json_value::make_array();
    while (stmt.step() == SQLITE_ROW) {
        auto obj = json::json_value::make_object();
        obj.add_int("id", stmt.column_int(0));
        obj.add_int("user_id", stmt.column_int(1));
        obj.add_str("display_name", stmt.column_str(2));
        obj.add_str("username", stmt.column_str(3));
        obj.add_nullable_int("project_id", stmt.column_int(4));
        obj.add_double("weekly_capacity_hours", stmt.column_double(5));
        obj.add_str("updated_at", stmt.column_str(6));
        arr.add(std::move(obj));
    }
    return arr;
}

// PUT /api/portfolio/capacity/configs
inline bool set_capacity_config(db::sqlite_db &conn, int user_id, int project_id, double weekly_hours) {
    if (user_id <= 0 || weekly_hours <= 0.0) return false;

    db::sqlite_transaction tx(conn);
    if (project_id > 0) {
        auto del = conn.prepare("DELETE FROM user_capacities WHERE user_id = ? AND project_id = ?;");
        del.bind_int(1, user_id);
        del.bind_int(2, project_id);
        del.step_done();
        auto ins = conn.prepare("INSERT INTO user_capacities (user_id, project_id, weekly_capacity_hours, updated_at) VALUES (?, ?, ?, datetime('now'));");
        ins.bind_int(1, user_id);
        ins.bind_int(2, project_id);
        ins.bind_double(3, weekly_hours);
        ins.step_done();
    } else {
        auto del = conn.prepare("DELETE FROM user_capacities WHERE user_id = ? AND project_id IS NULL;");
        del.bind_int(1, user_id);
        del.step_done();
        auto ins = conn.prepare("INSERT INTO user_capacities (user_id, project_id, weekly_capacity_hours, updated_at) VALUES (?, NULL, ?, datetime('now'));");
        ins.bind_int(1, user_id);
        ins.bind_double(2, weekly_hours);
        ins.step_done();
    }
    tx.commit();
    return true;
}

// Reschedule Work Item Date Range (used by Gantt drag-to-reschedule)
inline bool reschedule_item(db::sqlite_db &conn,
                            int item_id,
                            const std::string &start_date,
                            const std::string &due_date,
                            int user_id,
                            const std::string &user_name,
                            std::string &out_err) {
    if (item_id <= 0) {
        out_err = "Invalid work item ID.";
        return false;
    }

    std::string item_key;
    int project_id = 0;
    std::string old_start, old_due;
    {
        auto stmt = conn.prepare("SELECT key, project_id, COALESCE(start_date, ''), COALESCE(due_date, '') FROM work_items WHERE id = ?;");
        stmt.bind_int(1, item_id);
        if (stmt.step() != SQLITE_ROW) {
            out_err = "Work item not found.";
            return false;
        }
        item_key = stmt.column_str(0);
        project_id = stmt.column_int(1);
        old_start = stmt.column_str(2);
        old_due = stmt.column_str(3);
    }

    db::sqlite_transaction tx(conn);
    auto upd = conn.prepare("UPDATE work_items SET start_date = ?, due_date = ?, updated_at = datetime('now') WHERE id = ?;");
    upd.bind_text(1, start_date);
    upd.bind_text(2, due_date);
    upd.bind_int(3, item_id);
    upd.step_done();

    // Changelog entries
    if (old_start != start_date) {
        auto chg = conn.prepare(R"SQL(
            INSERT INTO work_item_changelog (work_item_id, field_name, old_value, new_value, changed_by_id)
            VALUES (?, 'start_date', ?, ?, ?);
        )SQL");
        chg.bind_int(1, item_id);
        chg.bind_text(2, old_start);
        chg.bind_text(3, start_date);
        chg.bind_optional_int(4, user_id > 0 ? user_id : 0);
        chg.step_done();
    }
    if (old_due != due_date) {
        auto chg = conn.prepare(R"SQL(
            INSERT INTO work_item_changelog (work_item_id, field_name, old_value, new_value, changed_by_id)
            VALUES (?, 'due_date', ?, ?, ?);
        )SQL");
        chg.bind_int(1, item_id);
        chg.bind_text(2, old_due);
        chg.bind_text(3, due_date);
        chg.bind_optional_int(4, user_id > 0 ? user_id : 0);
        chg.step_done();
    }
    tx.commit();

    // Broadcast SSE realtime update
    auto payload = json::json_value::make_object();
    payload.add_int("id", item_id);
    payload.add_str("key", item_key);
    payload.add_str("start_date", start_date);
    payload.add_str("due_date", due_date);
    realtime::get_engine().publish("item_updated", payload.to_string(), user_id, user_name, item_id, item_key);

    return true;
}

} // namespace portfolio
