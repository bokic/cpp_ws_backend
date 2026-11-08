#pragma once

#include "raii_json.h"
#include "raii_sqlite.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace itsm {

// ----------------------------------------------------------------------------
// Timestamp Helpers (UTC)
// ----------------------------------------------------------------------------

inline time_t parse_db_timestamp(const std::string &str) {
    if (str.empty()) return 0;
    struct tm tm_val {};
    int year = 0, month = 0, day = 0, hour = 0, min = 0, sec = 0;
    if (sscanf(str.c_str(), "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &min, &sec) >= 3 ||
        sscanf(str.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &min, &sec) >= 3) {
        tm_val.tm_year = year - 1900;
        tm_val.tm_mon = month - 1;
        tm_val.tm_mday = day;
        tm_val.tm_hour = hour;
        tm_val.tm_min = min;
        tm_val.tm_sec = sec;
        return timegm(&tm_val);
    }
    return 0;
}

inline std::string format_db_timestamp(time_t t) {
    if (t <= 0) return "";
    struct tm tm_val {};
    gmtime_r(&t, &tm_val);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_val);
    return std::string(buf);
}

inline std::string current_utc_timestamp() {
    return format_db_timestamp(time(nullptr));
}

// ----------------------------------------------------------------------------
// Duration Formatting
// ----------------------------------------------------------------------------

inline std::string format_duration_human(int64_t total_sec) {
    if (total_sec == 0) return "0m";
    bool neg = total_sec < 0;
    if (neg) total_sec = -total_sec;

    int64_t days = total_sec / 86400;
    int64_t hours = (total_sec % 86400) / 3600;
    int64_t minutes = (total_sec % 3600) / 60;

    std::ostringstream oss;
    if (neg) oss << "-";
    if (days > 0) oss << days << "d ";
    if (hours > 0 || days > 0) oss << hours << "h ";
    oss << minutes << "m";
    return oss.str();
}

// ----------------------------------------------------------------------------
// Business Calendar Working Hours Calculation
// ----------------------------------------------------------------------------

inline int64_t calculate_calendar_work_seconds(
    time_t t_start,
    time_t t_end,
    const std::string &days_of_week_csv,
    const std::string &start_time_str,
    const std::string &end_time_str,
    bool is_24x7
) {
    if (t_end <= t_start) return 0;
    if (is_24x7) return static_cast<int64_t>(t_end - t_start);

    std::set<int> valid_days;
    std::stringstream ss(days_of_week_csv);
    std::string token;
    while (std::getline(ss, token, ',')) {
        if (!token.empty()) {
            try {
                valid_days.insert(std::stoi(token));
            } catch (...) {}
        }
    }
    if (valid_days.empty()) {
        valid_days = {1, 2, 3, 4, 5}; // Mon-Fri default
    }

    int s_hour = 9, s_min = 0;
    int e_hour = 17, e_min = 0;
    sscanf(start_time_str.c_str(), "%d:%d", &s_hour, &s_min);
    sscanf(end_time_str.c_str(), "%d:%d", &e_hour, &e_min);
    int64_t s_sec = s_hour * 3600 + s_min * 60;
    int64_t e_sec = e_hour * 3600 + e_min * 60;
    if (e_sec <= s_sec) {
        return static_cast<int64_t>(t_end - t_start);
    }

    int64_t accumulated_work_sec = 0;

    // Align t_start to day midnight
    time_t cur = t_start;
    while (cur < t_end) {
        struct tm tm_val {};
        gmtime_r(&cur, &tm_val);

        // Midnight of current day
        tm_val.tm_hour = 0;
        tm_val.tm_min = 0;
        tm_val.tm_sec = 0;
        time_t day_midnight = timegm(&tm_val);

        // Day of week: tm_wday is 0=Sun, 1=Mon, ..., 6=Sat -> convert to 1=Mon..7=Sun
        int dow = (tm_val.tm_wday == 0) ? 7 : tm_val.tm_wday;

        time_t next_day = day_midnight + 86400;

        if (valid_days.count(dow)) {
            time_t work_start = day_midnight + s_sec;
            time_t work_end = day_midnight + e_sec;

            time_t range_start = std::max(t_start, work_start);
            time_t range_end = std::min(t_end, work_end);

            if (range_end > range_start) {
                accumulated_work_sec += (range_end - range_start);
            }
        }

        cur = next_day;
    }

    return accumulated_work_sec;
}

// ----------------------------------------------------------------------------
// Default ITSM Service Desk Seeding
// ----------------------------------------------------------------------------

inline void ensure_default_service_desk(db::sqlite_db &conn, int project_id) {
    if (project_id <= 0) return;

    // Check if desk already exists for this project
    {
        auto chk = conn.prepare("SELECT id FROM service_desks WHERE project_id = ? LIMIT 1;");
        chk.bind_int(1, project_id);
        if (chk.step() == SQLITE_ROW) {
            return;
        }
    }

    std::string proj_key;
    std::string proj_name;
    {
        auto pq = conn.prepare("SELECT key, name FROM projects WHERE id = ?;");
        pq.bind_int(1, project_id);
        if (pq.step() == SQLITE_ROW) {
            proj_key = pq.column_str(0);
            proj_name = pq.column_str(1);
        } else {
            return;
        }
    }

    std::string desk_key = proj_key + "-SD";
    {
        auto key_chk = conn.prepare("SELECT id FROM service_desks WHERE portal_key = ?;");
        key_chk.bind_text(1, desk_key);
        if (key_chk.step() == SQLITE_ROW) {
            desk_key = "SD-" + proj_key;
        }
    }

    int desk_id = 0;
    {
        auto ins = conn.prepare(R"SQL(
            INSERT INTO service_desks (project_id, name, description, portal_key, is_open_access)
            VALUES (?, ?, ?, ?, 1);
        )SQL");
        ins.bind_int(1, project_id);
        ins.bind_text(2, proj_name + " Service Desk");
        ins.bind_text(3, "Customer Support and IT Service Desk for " + proj_name);
        ins.bind_text(4, desk_key);
        ins.step_done();
        desk_id = static_cast<int>(conn.last_insert_rowid());
    }

    // Default Request Types
    struct ReqTypeDef {
        const char *name;
        const char *desc;
        const char *icon;
        const char *help;
        int order;
    };
    std::vector<ReqTypeDef> default_req_types = {
        {"Get IT Help", "General assistance, account issues, and IT support", "help_outline", "Please describe what assistance you need", 1},
        {"Report a System Problem", "System outage, software bug, or performance degradation", "bug_report", "Describe what is broken and its impact", 2},
        {"Request New Software or Equipment", "Hardware upgrades, monitor, peripheral, or new software licenses", "laptop", "Specify exact software name or hardware item", 3},
        {"Billing & Account Inquiries", "Invoices, payment questions, license renewal", "receipt_long", "Provide invoice or subscription ID", 4}
    };

    int default_issue_type_id = 0;
    {
        auto it_stmt = conn.prepare("SELECT id FROM issue_types WHERE name IN ('Task', 'Support', 'Issue') ORDER BY id ASC LIMIT 1;");
        if (it_stmt.step() == SQLITE_ROW) {
            default_issue_type_id = it_stmt.column_int(0);
        }
    }

    for (const auto &rt : default_req_types) {
        auto ins_rt = conn.prepare(R"SQL(
            INSERT INTO request_types (service_desk_id, issue_type_id, name, description, icon, help_text, display_order)
            VALUES (?, ?, ?, ?, ?, ?, ?);
        )SQL");
        ins_rt.bind_int(1, desk_id);
        ins_rt.bind_optional_int(2, default_issue_type_id > 0 ? default_issue_type_id : 0);
        ins_rt.bind_text(3, rt.name);
        ins_rt.bind_text(4, rt.desc);
        ins_rt.bind_text(5, rt.icon);
        ins_rt.bind_text(6, rt.help);
        ins_rt.bind_int(7, rt.order);
        ins_rt.step_done();
    }

    // Default Queues
    struct QueueDef {
        const char *name;
        const char *filter_type;
        const char *icon;
        const char *jql;
        int order;
    };
    std::vector<QueueDef> default_queues = {
        {"Unassigned Tickets", "unassigned", "inbox", "assignee is EMPTY AND resolution is EMPTY", 1},
        {"Open Incidents", "open_incidents", "report_problem", "resolution is EMPTY AND (priority = 'High' OR priority = 'Highest')", 2},
        {"Waiting on Customer", "waiting_customer", "hourglass_empty", "status = 'Waiting for customer'", 3},
        {"Pending First Response", "pending_reply", "mark_email_unread", "first_response_at IS NULL AND resolution is EMPTY", 4},
        {"All Requests", "all", "list_alt", "", 5}
    };

    for (const auto &q : default_queues) {
        auto ins_q = conn.prepare(R"SQL(
            INSERT INTO service_desk_queues (service_desk_id, name, filter_type, icon, jql_filter, display_order)
            VALUES (?, ?, ?, ?, ?, ?);
        )SQL");
        ins_q.bind_int(1, desk_id);
        ins_q.bind_text(2, q.name);
        ins_q.bind_text(3, q.filter_type);
        ins_q.bind_text(4, q.icon);
        ins_q.bind_text(5, q.jql);
        ins_q.bind_int(6, q.order);
        ins_q.step_done();
    }

    // Default SLA Metrics & Goals
    // 1. Time to first response (2h target for All, 30m for Highest, 1h for High)
    int metric1_id = 0;
    {
        auto ins_m = conn.prepare(R"SQL(
            INSERT INTO sla_metrics (service_desk_id, name, metric_key, description, calendar_id, start_condition, pause_condition, stop_condition)
            VALUES (?, 'Time to first response', 'first_response', 'Elapsed business hours from creation until an agent comments or replies', 1, 'issue_created', 'waiting_for_customer', 'first_response');
        )SQL");
        ins_m.bind_int(1, desk_id);
        ins_m.step_done();
        metric1_id = static_cast<int>(conn.last_insert_rowid());

        auto ins_g = conn.prepare(R"SQL(
            INSERT INTO sla_goals (metric_id, priority, target_duration_seconds, warning_duration_seconds)
            VALUES (?, ?, ?, ?);
        )SQL");
        ins_g.bind_int(1, metric1_id);
        ins_g.bind_text(2, "Highest");
        ins_g.bind_int(3, 1800); // 30 mins
        ins_g.bind_int(4, 600);
        ins_g.step_done();
        ins_g.clear_and_reset();

        ins_g.bind_int(1, metric1_id);
        ins_g.bind_text(2, "High");
        ins_g.bind_int(3, 3600); // 1 hour
        ins_g.bind_int(4, 900);
        ins_g.step_done();
        ins_g.clear_and_reset();

        ins_g.bind_int(1, metric1_id);
        ins_g.bind_text(2, "All");
        ins_g.bind_int(3, 7200); // 2 hours
        ins_g.bind_int(4, 1800);
        ins_g.step_done();
    }

    // 2. Time to resolution (8h target for All, 2h for Highest, 4h for High)
    int metric2_id = 0;
    {
        auto ins_m = conn.prepare(R"SQL(
            INSERT INTO sla_metrics (service_desk_id, name, metric_key, description, calendar_id, start_condition, pause_condition, stop_condition)
            VALUES (?, 'Time to resolution', 'resolution', 'Elapsed business hours from creation until the request is marked Done or Resolved', 1, 'issue_created', 'waiting_for_customer', 'resolved');
        )SQL");
        ins_m.bind_int(1, desk_id);
        ins_m.step_done();
        metric2_id = static_cast<int>(conn.last_insert_rowid());

        auto ins_g = conn.prepare(R"SQL(
            INSERT INTO sla_goals (metric_id, priority, target_duration_seconds, warning_duration_seconds)
            VALUES (?, ?, ?, ?);
        )SQL");
        ins_g.bind_int(1, metric2_id);
        ins_g.bind_text(2, "Highest");
        ins_g.bind_int(3, 7200); // 2 hours
        ins_g.bind_int(4, 1800);
        ins_g.step_done();
        ins_g.clear_and_reset();

        ins_g.bind_int(1, metric2_id);
        ins_g.bind_text(2, "High");
        ins_g.bind_int(3, 14400); // 4 hours
        ins_g.bind_int(4, 3600);
        ins_g.step_done();
        ins_g.clear_and_reset();

        ins_g.bind_int(1, metric2_id);
        ins_g.bind_text(2, "All");
        ins_g.bind_int(3, 28800); // 8 hours
        ins_g.bind_int(4, 7200);
        ins_g.step_done();
    }
}

// ----------------------------------------------------------------------------
// SLA Initialization & Evaluation for Work Items
// ----------------------------------------------------------------------------

inline void initialize_item_slas(db::sqlite_db &conn, int item_id, int service_desk_id, const std::string &priority) {
    if (item_id <= 0 || service_desk_id <= 0) return;

    auto mq = conn.prepare(R"SQL(
        SELECT m.id, m.metric_key
        FROM sla_metrics m
        WHERE m.service_desk_id = ?;
    )SQL");
    mq.bind_int(1, service_desk_id);

    std::vector<std::pair<int, std::string>> metrics;
    while (mq.step() == SQLITE_ROW) {
        metrics.push_back({mq.column_int(0), mq.column_str(1)});
    }
    mq.reset();

    for (const auto &m : metrics) {
        int metric_id = m.first;

        // Select best matching goal for priority
        int goal_id = 0;
        int target_duration = 14400; // default 4h
        {
            auto gq = conn.prepare(R"SQL(
                SELECT id, target_duration_seconds
                FROM sla_goals
                WHERE metric_id = ? AND (priority = ? OR priority = 'All' OR priority IS NULL)
                ORDER BY CASE WHEN priority = ? THEN 0 WHEN priority = 'All' THEN 1 ELSE 2 END
                LIMIT 1;
            )SQL");
            gq.bind_int(1, metric_id);
            gq.bind_text(2, priority);
            gq.bind_text(3, priority);
            if (gq.step() == SQLITE_ROW) {
                goal_id = gq.column_int(0);
                target_duration = gq.column_int(1);
            }
            gq.reset();
        }

        auto ins_sla = conn.prepare(R"SQL(
            INSERT OR IGNORE INTO work_item_slas (
                work_item_id, metric_id, goal_id, status,
                target_duration_seconds, elapsed_work_seconds, remaining_work_seconds,
                is_paused, is_completed, is_breached,
                started_at, last_calculated_at
            ) VALUES (?, ?, ?, 'running', ?, 0, ?, 0, 0, 0, datetime('now'), datetime('now'));
        )SQL");
        ins_sla.bind_int(1, item_id);
        ins_sla.bind_int(2, metric_id);
        ins_sla.bind_optional_int(3, goal_id);
        ins_sla.bind_int(4, target_duration);
        ins_sla.bind_int(5, target_duration);
        ins_sla.step_done();
    }
}

inline void evaluate_item_slas(db::sqlite_db &conn, int item_id) {
    if (item_id <= 0) return;

    // Get item info
    int sd_id = 0;
    std::string item_status;
    std::string priority;
    std::string first_resp_at;
    int res_id = 0;
    {
        auto q = conn.prepare(R"SQL(
            SELECT COALESCE(service_desk_id, 0), status, priority,
                   COALESCE(first_response_at, ''), COALESCE(resolution_id, 0)
            FROM work_items WHERE id = ?;
        )SQL");
        q.bind_int(1, item_id);
        if (q.step() == SQLITE_ROW) {
            sd_id = q.column_int(0);
            item_status = q.column_str(1);
            priority = q.column_str(2);
            first_resp_at = q.column_str(3);
            res_id = q.column_int(4);
        } else {
            return;
        }
        q.reset();
    }

    // Auto-init if item has service_desk_id but no SLAs yet
    if (sd_id > 0) {
        auto cnt_q = conn.prepare("SELECT COUNT(*) FROM work_item_slas WHERE work_item_id = ?;");
        cnt_q.bind_int(1, item_id);
        if (cnt_q.step() == SQLITE_ROW && cnt_q.column_int(0) == 0) {
            cnt_q.reset();
            initialize_item_slas(conn, item_id, sd_id, priority);
        } else {
            cnt_q.reset();
        }
    }

    struct SlaRow {
        int id;
        int metric_id;
        std::string metric_key;
        std::string pause_condition;
        std::string stop_condition;
        int target_duration;
        int warning_duration;
        int elapsed_sec;
        int remaining_sec;
        int is_paused;
        int is_completed;
        int is_breached;
        std::string started_at;
        std::string paused_at;
        std::string completed_at;
        std::string last_calc_at;
        std::string days_of_week;
        std::string start_time;
        std::string end_time;
        int is_24x7;
    };

    std::vector<SlaRow> slas;
    {
        auto q = conn.prepare(R"SQL(
            SELECT s.id, s.metric_id, m.metric_key,
                   COALESCE(m.pause_condition, 'waiting_for_customer'),
                   COALESCE(m.stop_condition, 'first_response'),
                   s.target_duration_seconds,
                   COALESCE(g.warning_duration_seconds, 3600),
                   s.elapsed_work_seconds,
                   s.remaining_work_seconds,
                   s.is_paused,
                   s.is_completed,
                   s.is_breached,
                   s.started_at,
                   COALESCE(s.paused_at, ''),
                   COALESCE(s.completed_at, ''),
                   s.last_calculated_at,
                   COALESCE(c.days_of_week, '1,2,3,4,5'),
                   COALESCE(c.start_time, '09:00'),
                   COALESCE(c.end_time, '17:00'),
                   COALESCE(c.is_24x7, 0)
            FROM work_item_slas s
            INNER JOIN sla_metrics m ON m.id = s.metric_id
            LEFT JOIN sla_goals g ON g.id = s.goal_id
            LEFT JOIN sla_calendars c ON c.id = m.calendar_id
            WHERE s.work_item_id = ?;
        )SQL");
        q.bind_int(1, item_id);

        while (q.step() == SQLITE_ROW) {
            SlaRow row;
            row.id = q.column_int(0);
            row.metric_id = q.column_int(1);
            row.metric_key = q.column_str(2);
            row.pause_condition = q.column_str(3);
            row.stop_condition = q.column_str(4);
            row.target_duration = q.column_int(5);
            row.warning_duration = q.column_int(6);
            row.elapsed_sec = q.column_int(7);
            row.remaining_sec = q.column_int(8);
            row.is_paused = q.column_int(9);
            row.is_completed = q.column_int(10);
            row.is_breached = q.column_int(11);
            row.started_at = q.column_str(12);
            row.paused_at = q.column_str(13);
            row.completed_at = q.column_str(14);
            row.last_calc_at = q.column_str(15);
            row.days_of_week = q.column_str(16);
            row.start_time = q.column_str(17);
            row.end_time = q.column_str(18);
            row.is_24x7 = q.column_int(19);
            slas.push_back(row);
        }
    }

    time_t now = time(nullptr);
    std::string now_str = format_db_timestamp(now);

    for (auto &sla : slas) {
        // If already completed, nothing to advance
        if (sla.is_completed) {
            continue;
        }

        // Check if stop condition is now satisfied
        bool should_complete = false;
        if (sla.metric_key == "first_response" && !first_resp_at.empty()) {
            should_complete = true;
        } else if (sla.metric_key == "resolution" && (res_id > 0 || item_status == "Done" || item_status == "Resolved" || item_status == "Closed")) {
            should_complete = true;
        }

        time_t t_last = parse_db_timestamp(sla.last_calc_at.empty() ? sla.started_at : sla.last_calc_at);
        if (t_last == 0) t_last = parse_db_timestamp(sla.started_at);
        if (t_last == 0) t_last = now;

        int64_t added_sec = 0;
        if (!sla.is_paused) {
            added_sec = calculate_calendar_work_seconds(
                t_last,
                now,
                sla.days_of_week,
                sla.start_time,
                sla.end_time,
                sla.is_24x7 != 0
            );
        }

        sla.elapsed_sec += static_cast<int>(added_sec);
        sla.remaining_sec = sla.target_duration - sla.elapsed_sec;
        sla.is_breached = (sla.remaining_sec < 0) ? 1 : 0;

        std::string new_status = "within";
        if (should_complete) {
            sla.is_completed = 1;
            sla.completed_at = now_str;
            new_status = "completed";
        } else if (sla.is_paused) {
            new_status = "paused";
        } else if (sla.remaining_sec < 0) {
            new_status = "breached";
        } else if (sla.remaining_sec <= sla.warning_duration) {
            new_status = "approaching";
        }

        auto upd = conn.prepare(R"SQL(
            UPDATE work_item_slas
            SET elapsed_work_seconds = ?,
                remaining_work_seconds = ?,
                status = ?,
                is_completed = ?,
                is_breached = ?,
                completed_at = CASE WHEN ? = 1 THEN ? ELSE completed_at END,
                last_calculated_at = ?
            WHERE id = ?;
        )SQL");
        upd.bind_int(1, sla.elapsed_sec);
        upd.bind_int(2, sla.remaining_sec);
        upd.bind_text(3, new_status);
        upd.bind_int(4, sla.is_completed);
        upd.bind_int(5, sla.is_breached);
        upd.bind_int(6, sla.is_completed);
        upd.bind_text(7, sla.completed_at);
        upd.bind_text(8, now_str);
        upd.bind_int(9, sla.id);
        upd.step_done();
    }
}

// ----------------------------------------------------------------------------
// Events: Comments & Status Transitions
// ----------------------------------------------------------------------------

inline void on_item_comment_added(db::sqlite_db &conn, int item_id, int author_user_id) {
    if (item_id <= 0) return;

    std::string cur_first_resp;
    std::string reporter_email;
    {
        auto q = conn.prepare("SELECT COALESCE(first_response_at, ''), COALESCE(customer_reporter_email, '') FROM work_items WHERE id = ?;");
        q.bind_int(1, item_id);
        if (q.step() == SQLITE_ROW) {
            cur_first_resp = q.column_str(0);
            reporter_email = q.column_str(1);
        }
    }

    if (!cur_first_resp.empty()) {
        return; // First response already captured
    }

    // Check author email
    std::string author_email;
    {
        auto uq = conn.prepare("SELECT email FROM users WHERE id = ?;");
        uq.bind_int(1, author_user_id);
        if (uq.step() == SQLITE_ROW) {
            author_email = uq.column_str(0);
        }
    }

    // Agent response: when author is logged in agent (or email differs from customer reporter)
    bool is_agent_reply = true;
    if (!reporter_email.empty() && author_email == reporter_email) {
        is_agent_reply = false;
    }

    if (is_agent_reply) {
        std::string now_str = current_utc_timestamp();
        auto upd = conn.prepare("UPDATE work_items SET first_response_at = ? WHERE id = ?;");
        upd.bind_text(1, now_str);
        upd.bind_int(2, item_id);
        upd.step_done();

        // Mark first response SLA as completed
        evaluate_item_slas(conn, item_id);
    }
}

inline void on_item_status_changed(db::sqlite_db &conn, int item_id, const std::string &old_status, const std::string &new_status) {
    if (item_id <= 0 || old_status == new_status) return;

    evaluate_item_slas(conn, item_id);

    bool now_waiting_customer = (new_status == "Waiting for customer" || new_status == "Waiting on customer" || new_status == "Pending Customer");
    bool was_waiting_customer = (old_status == "Waiting for customer" || old_status == "Waiting on customer" || old_status == "Pending Customer");

    std::string now_str = current_utc_timestamp();

    if (now_waiting_customer && !was_waiting_customer) {
        // Pause active SLAs
        auto upd = conn.prepare(R"SQL(
            UPDATE work_item_slas
            SET is_paused = 1, paused_at = ?, status = 'paused'
            WHERE work_item_id = ? AND is_completed = 0;
        )SQL");
        upd.bind_text(1, now_str);
        upd.bind_int(2, item_id);
        upd.step_done();
    } else if (was_waiting_customer && !now_waiting_customer) {
        // Resume active SLAs
        auto upd = conn.prepare(R"SQL(
            UPDATE work_item_slas
            SET is_paused = 0, paused_at = NULL, last_calculated_at = ?,
                status = CASE WHEN remaining_work_seconds < 0 THEN 'breached' ELSE 'running' END
            WHERE work_item_id = ? AND is_completed = 0;
        )SQL");
        upd.bind_text(1, now_str);
        upd.bind_int(2, item_id);
        upd.step_done();
    }

    if (new_status == "Done" || new_status == "Resolved" || new_status == "Closed") {
        evaluate_item_slas(conn, item_id);
    }
}

// ----------------------------------------------------------------------------
// Get Work Item SLAs JSON Payload
// ----------------------------------------------------------------------------

inline json::json_value get_item_slas_json(db::sqlite_db &conn, int item_id) {
    evaluate_item_slas(conn, item_id);

    auto arr = json::json_value::make_array();

    auto q = conn.prepare(R"SQL(
        SELECT s.id, s.metric_id, m.name, m.metric_key,
               s.status, s.target_duration_seconds, s.elapsed_work_seconds, s.remaining_work_seconds,
               s.is_paused, s.is_completed, s.is_breached,
               s.started_at, COALESCE(s.paused_at, ''), COALESCE(s.completed_at, ''),
               COALESCE(c.name, 'Standard Calendar')
        FROM work_item_slas s
        INNER JOIN sla_metrics m ON m.id = s.metric_id
        LEFT JOIN sla_calendars c ON c.id = m.calendar_id
        WHERE s.work_item_id = ?
        ORDER BY s.id ASC;
    )SQL");
    q.bind_int(1, item_id);

    while (q.step() == SQLITE_ROW) {
        auto obj = json::json_value::make_object();
        obj.add_int("id", q.column_int(0));
        obj.add_int("metric_id", q.column_int(1));
        obj.add_str("name", q.column_str(2));
        obj.add_str("metric_key", q.column_str(3));
        std::string status = q.column_str(4);
        obj.add_str("status", status);

        int64_t target_sec = q.column_int64(5);
        int64_t elapsed_sec = q.column_int64(6);
        int64_t rem_sec = q.column_int64(7);

        obj.add_int64("target_duration_seconds", target_sec);
        obj.add_int64("elapsed_work_seconds", elapsed_sec);
        obj.add_int64("remaining_work_seconds", rem_sec);

        obj.add_str("target_formatted", format_duration_human(target_sec));
        obj.add_str("elapsed_formatted", format_duration_human(elapsed_sec));
        obj.add_str("remaining_formatted", format_duration_human(rem_sec));

        obj.add_bool("is_paused", q.column_int(8) != 0);
        obj.add_bool("is_completed", q.column_int(9) != 0);
        obj.add_bool("is_breached", q.column_int(10) != 0);

        obj.add_str("started_at", q.column_str(11));
        obj.add_str("paused_at", q.column_str(12));
        obj.add_str("completed_at", q.column_str(13));
        obj.add_str("calendar_name", q.column_str(14));

        // Color helper
        std::string badge_color = "#36b37e"; // green
        if (status == "completed") {
            badge_color = "#0052cc"; // blue or muted
        } else if (status == "breached") {
            badge_color = "#de350b"; // red
        } else if (status == "approaching") {
            badge_color = "#ffab00"; // amber
        } else if (status == "paused") {
            badge_color = "#6b778c"; // grey
        }
        obj.add_str("badge_color", badge_color);

        arr.add(std::move(obj));
    }

    return arr;
}

// ----------------------------------------------------------------------------
// Canned Response Variable Substitution
// ----------------------------------------------------------------------------

inline std::string substitute_canned_response(
    const std::string &template_text,
    const std::map<std::string, std::string> &vars
) {
    std::string result = template_text;
    for (const auto &[k, v] : vars) {
        std::string placeholder = "{{" + k + "}}";
        size_t pos = 0;
        while ((pos = result.find(placeholder, pos)) != std::string::npos) {
            result.replace(pos, placeholder.length(), v);
            pos += v.length();
        }
    }
    return result;
}

// ----------------------------------------------------------------------------
// Customer Organizations & Directory Segregation
// ----------------------------------------------------------------------------

struct customer_organization {
    int id = 0;
    int service_desk_id = 0;
    std::string name;
    std::string domain;
    std::string created_at;
    int member_count = 0;
};

inline std::vector<customer_organization> list_customer_organizations(db::sqlite_db &conn, int service_desk_id = 0) {
    std::vector<customer_organization> list;
    std::string sql = R"SQL(
        SELECT o.id, o.service_desk_id, o.name, o.domain, o.created_at,
               (SELECT COUNT(*) FROM customer_organization_members m WHERE m.organization_id = o.id) AS member_count
        FROM customer_organizations o
    )SQL";
    if (service_desk_id > 0) {
        sql += " WHERE o.service_desk_id = ? ORDER BY o.name ASC;";
    } else {
        sql += " ORDER BY o.name ASC;";
    }

    auto stmt = conn.prepare(sql);
    if (service_desk_id > 0) {
        stmt.bind_int(1, service_desk_id);
    }
    while (stmt.step() == SQLITE_ROW) {
        customer_organization org;
        org.id = stmt.column_int(0);
        org.service_desk_id = stmt.column_int(1);
        org.name = stmt.column_str(2);
        org.domain = stmt.column_str(3);
        org.created_at = stmt.column_str(4);
        org.member_count = stmt.column_int(5);
        list.push_back(std::move(org));
    }
    return list;
}

inline bool user_is_organization_member(db::sqlite_db &conn, int user_id, int organization_id) {
    if (user_id <= 0 || organization_id <= 0) return false;
    auto stmt = conn.prepare("SELECT 1 FROM customer_organization_members WHERE organization_id = ? AND user_id = ? LIMIT 1;");
    stmt.bind_int(1, organization_id);
    stmt.bind_int(2, user_id);
    return stmt.step() == SQLITE_ROW;
}

inline std::vector<int> get_user_organization_ids(db::sqlite_db &conn, int user_id) {
    std::vector<int> org_ids;
    if (user_id <= 0) return org_ids;
    auto stmt = conn.prepare("SELECT organization_id FROM customer_organization_members WHERE user_id = ?;");
    stmt.bind_int(1, user_id);
    while (stmt.step() == SQLITE_ROW) {
        org_ids.push_back(stmt.column_int(0));
    }
    return org_ids;
}

inline bool user_can_access_ticket(db::sqlite_db &conn,
                                   int user_id,
                                   const std::string &user_type,
                                   const std::string &user_email,
                                   int work_item_id) {
    if (work_item_id <= 0) return false;
    if (user_type != "PORTAL_CUSTOMER") {
        return true; // Internal users follow standard scheme authorization
    }

    auto stmt = conn.prepare(R"SQL(
        SELECT reporter_id, COALESCE(customer_reporter_email, ''), COALESCE(customer_organization_id, 0),
               COALESCE(is_customer_request, 0), COALESCE(service_desk_id, 0)
        FROM work_items
        WHERE id = ?;
    )SQL");
    stmt.bind_int(1, work_item_id);
    if (stmt.step() != SQLITE_ROW) {
        return false;
    }

    int rep_id = stmt.column_int(0);
    std::string rep_email = stmt.column_str(1);
    int cust_org_id = stmt.column_int(2);
    int is_cust_req = stmt.column_int(3);
    int s_desk_id = stmt.column_int(4);

    if (is_cust_req == 0 && s_desk_id <= 0) {
        return false;
    }

    if (user_id > 0 && rep_id == user_id) return true;
    if (!user_email.empty() && !rep_email.empty()) {
        std::string u1 = user_email, u2 = rep_email;
        std::transform(u1.begin(), u1.end(), u1.begin(), ::tolower);
        std::transform(u2.begin(), u2.end(), u2.begin(), ::tolower);
        if (u1 == u2) return true;
    }

    if (user_id > 0) {
        if (cust_org_id > 0 && user_is_organization_member(conn, user_id, cust_org_id)) {
            return true;
        }

        if (rep_id > 0) {
            auto shared_stmt = conn.prepare(R"SQL(
                SELECT 1 FROM customer_organization_members m1
                INNER JOIN customer_organization_members m2 ON m1.organization_id = m2.organization_id
                WHERE m1.user_id = ? AND m2.user_id = ?
                LIMIT 1;
            )SQL");
            shared_stmt.bind_int(1, user_id);
            shared_stmt.bind_int(2, rep_id);
            if (shared_stmt.step() == SQLITE_ROW) {
                return true;
            }
        }
    }

    return false;
}

} // namespace itsm
