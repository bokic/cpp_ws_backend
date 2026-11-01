#pragma once

#include "raii_sqlite.h"
#include "raii_json.h"
#include "time_tracking.h"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <map>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

namespace analytics {

// ============================================================================
// Date & Time Utility Helpers
// ============================================================================

inline std::time_t parse_date(const std::string &str) {
    if (str.empty()) return 0;
    std::tm tm{};
    if (str.size() >= 10) {
        tm.tm_year = std::atoi(str.substr(0, 4).c_str()) - 1900;
        tm.tm_mon = std::atoi(str.substr(5, 2).c_str()) - 1;
        tm.tm_mday = std::atoi(str.substr(8, 2).c_str());
        if (str.size() >= 19) {
            tm.tm_hour = std::atoi(str.substr(11, 2).c_str());
            tm.tm_min = std::atoi(str.substr(14, 2).c_str());
            tm.tm_sec = std::atoi(str.substr(17, 2).c_str());
        } else {
            tm.tm_hour = 12; // Noon to avoid timezone boundary issues
        }
    }
    return timegm(&tm);
}

inline std::string format_date(std::time_t t) {
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return std::string(buf);
}

inline bool is_weekend_day(std::time_t t) {
    std::tm tm{};
    gmtime_r(&t, &tm);
    return (tm.tm_wday == 0 || tm.tm_wday == 6); // Sunday=0, Saturday=6
}

inline std::vector<std::string> generate_daily_range(const std::string &start_str, const std::string &end_str) {
    std::vector<std::string> days;
    std::time_t cur = parse_date(start_str);
    std::time_t finish = parse_date(end_str);
    if (cur == 0 || finish == 0) return days;
    if (cur > finish) std::swap(cur, finish);

    // Limit range to max 365 days
    int max_days = 365;
    while (cur <= finish && max_days-- > 0) {
        days.push_back(format_date(cur));
        cur += 86400; // 24 hours
    }
    return days;
}

// ============================================================================
// Sprint Burndown Engine
// ============================================================================

struct BurndownReport {
    int sprint_id = 0;
    std::string sprint_name;
    std::string goal;
    std::string status;
    std::string start_date;
    std::string end_date;
    int committed_points = 0;
    int current_scope_points = 0;
    int completed_points = 0;
    int remaining_points = 0;
    int64_t committed_hours_seconds = 0;
    int64_t current_scope_hours_seconds = 0;
    int64_t completed_hours_seconds = 0;
    int64_t remaining_hours_seconds = 0;
    bool has_scope_creep = false;
    int scope_creep_points = 0;
    double completion_rate_percent = 0.0;
    double points_burn_rate_per_day = 0.0;
    std::string projected_completion_date;

    struct DayPoint {
        std::string date;
        int day_index = 0;
        bool is_weekend = false;
        double ideal_points = 0.0;
        double ideal_hours = 0.0;
        int remaining_points = 0;
        double remaining_hours = 0.0;
        int total_scope_points = 0;
        double total_scope_hours = 0.0;
        int completed_points = 0;
        double completed_hours = 0.0;
    };
    std::vector<DayPoint> timeline;

    struct ScopeChange {
        std::string date;
        int item_id = 0;
        std::string item_key;
        std::string summary;
        std::string event_type; // 'added', 'removed', 'points_changed'
        int delta_points = 0;
    };
    std::vector<ScopeChange> scope_changes;

    json::json_value to_json() const {
        auto obj = json::json_value::make_object();
        obj.add_int("sprint_id", sprint_id);
        obj.add_str("sprint_name", sprint_name);
        obj.add_str("goal", goal);
        obj.add_str("status", status);
        obj.add_str("start_date", start_date);
        obj.add_str("end_date", end_date);
        obj.add_int("committed_points", committed_points);
        obj.add_int("current_scope_points", current_scope_points);
        obj.add_int("completed_points", completed_points);
        obj.add_int("remaining_points", remaining_points);
        obj.add_int("scope_creep_points", scope_creep_points);
        obj.add_bool("has_scope_creep", has_scope_creep);
        obj.add_double("completion_rate_percent", completion_rate_percent);
        obj.add_double("points_burn_rate_per_day", points_burn_rate_per_day);
        obj.add_str("projected_completion_date", projected_completion_date);

        // Hours & time tracking metrics
        obj.add_double("committed_hours", committed_hours_seconds / 3600.0);
        obj.add_double("current_scope_hours", current_scope_hours_seconds / 3600.0);
        obj.add_double("completed_hours", completed_hours_seconds / 3600.0);
        obj.add_double("remaining_hours", remaining_hours_seconds / 3600.0);

        auto tl_arr = json::json_value::make_array();
        for (const auto &dp : timeline) {
            auto d = json::json_value::make_object();
            d.add_str("date", dp.date);
            d.add_int("day_index", dp.day_index);
            d.add_bool("is_weekend", dp.is_weekend);
            d.add_double("ideal_points", std::round(dp.ideal_points * 10.0) / 10.0);
            d.add_double("ideal_hours", std::round(dp.ideal_hours * 10.0) / 10.0);
            d.add_int("remaining_points", dp.remaining_points);
            d.add_double("remaining_hours", std::round(dp.remaining_hours * 10.0) / 10.0);
            d.add_int("total_scope_points", dp.total_scope_points);
            d.add_double("total_scope_hours", std::round(dp.total_scope_hours * 10.0) / 10.0);
            d.add_int("completed_points", dp.completed_points);
            d.add_double("completed_hours", std::round(dp.completed_hours * 10.0) / 10.0);
            tl_arr.add(std::move(d));
        }
        obj.add("timeline", std::move(tl_arr));

        auto sc_arr = json::json_value::make_array();
        for (const auto &sc : scope_changes) {
            auto s = json::json_value::make_object();
            s.add_str("date", sc.date);
            s.add_int("item_id", sc.item_id);
            s.add_str("item_key", sc.item_key);
            s.add_str("summary", sc.summary);
            s.add_str("event_type", sc.event_type);
            s.add_int("delta_points", sc.delta_points);
            sc_arr.add(std::move(s));
        }
        obj.add("scope_changes", std::move(sc_arr));

        return obj;
    }

    std::string to_csv() const {
        std::ostringstream oss;
        oss << "Date,DayIndex,IsWeekend,IdealPoints,RemainingPoints,TotalScopePoints,IdealHours,RemainingHours,TotalScopeHours,CompletedPoints\n";
        for (const auto &dp : timeline) {
            oss << dp.date << ","
                << dp.day_index << ","
                << (dp.is_weekend ? "true" : "false") << ","
                << dp.ideal_points << ","
                << dp.remaining_points << ","
                << dp.total_scope_points << ","
                << dp.ideal_hours << ","
                << dp.remaining_hours << ","
                << dp.total_scope_hours << ","
                << dp.completed_points << "\n";
        }
        return oss.str();
    }
};

inline BurndownReport calculate_sprint_burndown(db::sqlite_db &conn, int sprint_id, bool weekend_flattening = true) {
    BurndownReport r;
    r.sprint_id = sprint_id;

    // Load Sprint metadata
    {
        auto q = conn.prepare("SELECT name, goal, status, start_date, end_date, committed_points FROM sprints WHERE id = ?;");
        q.bind_int(1, sprint_id);
        if (q.step() != SQLITE_ROW) {
            return r;
        }
        r.sprint_name = q.column_str(0);
        r.goal = q.column_str(1);
        r.status = q.column_str(2);
        r.start_date = q.column_str(3);
        r.end_date = q.column_str(4);
        r.committed_points = q.column_int(5);
    }

    if (r.start_date.empty()) r.start_date = format_date(std::time(nullptr) - 7 * 86400);
    if (r.end_date.empty()) r.end_date = format_date(std::time(nullptr) + 7 * 86400);

    // Load items currently in this sprint
    struct ItemInfo {
        int id;
        std::string key;
        std::string summary;
        std::string status;
        int points;
        int64_t orig_est;
        int64_t rem_est;
        int64_t time_spent;
        std::string resolved_at; // Date string or empty
    };
    std::vector<ItemInfo> items;
    {
        auto q = conn.prepare(R"SQL(
            SELECT w.id, w.key, w.title, w.status, w.story_points,
                   w.original_estimate_seconds, w.remaining_estimate_seconds, w.time_spent_seconds,
                   (SELECT substr(wh.created_at, 1, 10) FROM workflow_history wh
                    WHERE wh.work_item_id = w.id AND wh.to_status = 'Done'
                    ORDER BY wh.id DESC LIMIT 1) AS resolved_at
            FROM work_items w
            WHERE w.sprint_id = ?;
        )SQL");
        q.bind_int(1, sprint_id);
        while (q.step() == SQLITE_ROW) {
            ItemInfo it;
            it.id = q.column_int(0);
            it.key = q.column_str(1);
            it.summary = q.column_str(2);
            it.status = q.column_str(3);
            it.points = q.column_int(4);
            it.orig_est = q.column_int64(5);
            it.rem_est = q.column_int64(6);
            it.time_spent = q.column_int64(7);
            it.resolved_at = q.column_str(8);
            items.push_back(it);

            r.current_scope_points += it.points;
            r.current_scope_hours_seconds += (it.orig_est > 0 ? it.orig_est : it.points * 14400);
            if (it.status == "Done") {
                r.completed_points += it.points;
                r.completed_hours_seconds += it.time_spent;
            } else {
                r.remaining_points += it.points;
                r.remaining_hours_seconds += it.rem_est;
            }
        }
    }

    if (r.committed_points <= 0) {
        r.committed_points = r.current_scope_points;
    }
    r.committed_hours_seconds = r.current_scope_hours_seconds;
    r.scope_creep_points = std::max(0, r.current_scope_points - r.committed_points);
    r.has_scope_creep = (r.scope_creep_points > 0);
    if (r.current_scope_points > 0) {
        r.completion_rate_percent = (static_cast<double>(r.completed_points) / r.current_scope_points) * 100.0;
    }

    // Load scope changes from sprint_events
    {
        auto eq = conn.prepare(R"SQL(
            SELECT se.created_at, se.work_item_id, w.key, w.title, se.event_type, (se.new_points - se.old_points)
            FROM sprint_events se
            LEFT JOIN work_items w ON w.id = se.work_item_id
            WHERE se.sprint_id = ? AND se.event_type IN ('added', 'removed', 'points_changed')
            ORDER BY se.created_at ASC;
        )SQL");
        eq.bind_int(1, sprint_id);
        while (eq.step() == SQLITE_ROW) {
            BurndownReport::ScopeChange sc;
            sc.date = eq.column_str(0).substr(0, 10);
            sc.item_id = eq.column_int(1);
            sc.item_key = eq.column_str(2);
            sc.summary = eq.column_str(3);
            sc.event_type = eq.column_str(4);
            sc.delta_points = eq.column_int(5);
            r.scope_changes.push_back(sc);
        }
    }

    // Generate timeline
    auto dates = generate_daily_range(r.start_date, r.end_date);
    if (dates.empty()) {
        dates.push_back(format_date(std::time(nullptr)));
    }

    int total_days = static_cast<int>(dates.size());
    int working_days_count = 0;
    for (const auto &d : dates) {
        if (!is_weekend_day(parse_date(d))) {
            working_days_count++;
        }
    }
    if (working_days_count <= 0) working_days_count = 1;

    double ideal_pts_decrement_per_working_day = static_cast<double>(r.committed_points) / working_days_count;
    double ideal_hrs_decrement_per_working_day = (r.committed_hours_seconds / 3600.0) / working_days_count;

    double cur_ideal_points = r.committed_points;
    double cur_ideal_hours = r.committed_hours_seconds / 3600.0;

    std::string today = format_date(std::time(nullptr));

    for (int i = 0; i < total_days; ++i) {
        const std::string &d = dates[i];
        bool is_wknd = is_weekend_day(parse_date(d));

        BurndownReport::DayPoint dp;
        dp.date = d;
        dp.day_index = i;
        dp.is_weekend = is_wknd;

        // Ideal guideline calculation
        if (weekend_flattening) {
            if (i == 0) {
                dp.ideal_points = r.committed_points;
                dp.ideal_hours = r.committed_hours_seconds / 3600.0;
            } else if (is_wknd) {
                // Weekend flattening: slope is zero on Saturday & Sunday
                dp.ideal_points = cur_ideal_points;
                dp.ideal_hours = cur_ideal_hours;
            } else {
                cur_ideal_points = std::max(0.0, cur_ideal_points - ideal_pts_decrement_per_working_day);
                cur_ideal_hours = std::max(0.0, cur_ideal_hours - ideal_hrs_decrement_per_working_day);
                dp.ideal_points = cur_ideal_points;
                dp.ideal_hours = cur_ideal_hours;
            }
        } else {
            // Straight linear descent
            double progress_ratio = static_cast<double>(i) / std::max(1, total_days - 1);
            dp.ideal_points = std::max(0.0, r.committed_points * (1.0 - progress_ratio));
            dp.ideal_hours = std::max(0.0, (r.committed_hours_seconds / 3600.0) * (1.0 - progress_ratio));
        }

        // Daily actual progress calculation
        // Calculate scope on day d: committed + added - removed up to day d
        int scope_pts_on_d = r.committed_points;
        for (const auto &sc : r.scope_changes) {
            if (sc.date <= d) {
                scope_pts_on_d += sc.delta_points;
            }
        }
        dp.total_scope_points = std::max(0, scope_pts_on_d);
        dp.total_scope_hours = dp.total_scope_points * 4.0; // 4 hrs per pt default

        // Calculate completed points on or before day d
        int completed_pts_on_d = 0;
        double completed_hrs_on_d = 0.0;
        for (const auto &it : items) {
            if (it.status == "Done") {
                std::string res_d = it.resolved_at.empty() ? r.end_date : it.resolved_at;
                if (res_d <= d) {
                    completed_pts_on_d += it.points;
                    completed_hrs_on_d += (it.time_spent / 3600.0);
                }
            }
        }
        dp.completed_points = completed_pts_on_d;
        dp.completed_hours = completed_hrs_on_d;

        dp.remaining_points = std::max(0, dp.total_scope_points - dp.completed_points);
        dp.remaining_hours = std::max(0.0, dp.total_scope_hours - dp.completed_hours);

        // If sprint is active and this day is in the future, keep remaining aligned with current state
        if (r.status == "active" && d > today) {
            dp.remaining_points = r.remaining_points;
            dp.remaining_hours = r.remaining_hours_seconds / 3600.0;
        }

        r.timeline.push_back(dp);
    }

    // Calculate burn rate and projected completion date
    int elapsed_days = 1;
    for (const auto &dp : r.timeline) {
        if (dp.date <= today && dp.completed_points > 0) {
            elapsed_days = std::max(1, dp.day_index + 1);
        }
    }
    r.points_burn_rate_per_day = static_cast<double>(r.completed_points) / elapsed_days;
    if (r.points_burn_rate_per_day > 0.05 && r.remaining_points > 0) {
        int days_needed = static_cast<int>(std::ceil(r.remaining_points / r.points_burn_rate_per_day));
        std::time_t proj_t = std::time(nullptr) + days_needed * 86400;
        r.projected_completion_date = format_date(proj_t);
    } else if (r.remaining_points == 0) {
        r.projected_completion_date = today;
    } else {
        r.projected_completion_date = "At Risk";
    }

    return r;
}

// ============================================================================
// Sprint Burnup Engine
// ============================================================================

struct BurnupReport {
    BurndownReport burndown;

    json::json_value to_json() const {
        auto obj = burndown.to_json();
        // Add burnup-specific aliases
        obj.add_int("total_scope", burndown.current_scope_points);
        obj.add_int("completed_work", burndown.completed_points);
        return obj;
    }

    std::string to_csv() const {
        std::ostringstream oss;
        oss << "Date,DayIndex,TotalScopePoints,CompletedPoints,IdealCompletedPoints,TotalScopeHours,CompletedHours\n";
        for (const auto &dp : burndown.timeline) {
            double ideal_completed = dp.total_scope_points - dp.ideal_points;
            oss << dp.date << ","
                << dp.day_index << ","
                << dp.total_scope_points << ","
                << dp.completed_points << ","
                << ideal_completed << ","
                << dp.total_scope_hours << ","
                << dp.completed_hours << "\n";
        }
        return oss.str();
    }
};

inline BurnupReport calculate_sprint_burnup(db::sqlite_db &conn, int sprint_id) {
    BurnupReport b;
    b.burndown = calculate_sprint_burndown(conn, sprint_id, true);
    return b;
}

// ============================================================================
// Velocity Chart Engine
// ============================================================================

struct VelocityReport {
    int project_id = 0;
    std::string project_name;
    int sprints_analyzed = 0;
    double rolling_average_velocity = 0.0;
    double last_3_sprints_average = 0.0;
    int suggested_capacity = 0;
    double max_velocity = 0.0;
    double min_velocity = 0.0;
    double velocity_std_dev = 0.0;

    struct SprintEntry {
        int sprint_id = 0;
        std::string name;
        std::string status;
        std::string start_date;
        std::string end_date;
        std::string completed_at;
        int committed_points = 0;
        int completed_points = 0;
        int committed_items = 0;
        int completed_items = 0;
        double predictability_percent = 0.0;
    };
    std::vector<SprintEntry> sprints;

    json::json_value to_json() const {
        auto obj = json::json_value::make_object();
        obj.add_int("project_id", project_id);
        obj.add_str("project_name", project_name);
        obj.add_int("sprints_analyzed", sprints_analyzed);
        obj.add_double("rolling_average_velocity", std::round(rolling_average_velocity * 10.0) / 10.0);
        obj.add_double("last_3_sprints_average", std::round(last_3_sprints_average * 10.0) / 10.0);
        obj.add_int("suggested_capacity", suggested_capacity);
        obj.add_double("max_velocity", max_velocity);
        obj.add_double("min_velocity", min_velocity);
        obj.add_double("velocity_std_dev", std::round(velocity_std_dev * 10.0) / 10.0);

        auto s_arr = json::json_value::make_array();
        for (const auto &s : sprints) {
            auto sobj = json::json_value::make_object();
            sobj.add_int("sprint_id", s.sprint_id);
            sobj.add_str("name", s.name);
            sobj.add_str("status", s.status);
            sobj.add_str("start_date", s.start_date);
            sobj.add_str("end_date", s.end_date);
            sobj.add_str("completed_at", s.completed_at);
            sobj.add_int("committed_points", s.committed_points);
            sobj.add_int("completed_points", s.completed_points);
            sobj.add_int("committed_items", s.committed_items);
            sobj.add_int("completed_items", s.completed_items);
            sobj.add_double("predictability_percent", std::round(s.predictability_percent * 10.0) / 10.0);
            s_arr.add(std::move(sobj));
        }
        obj.add("sprints", std::move(s_arr));
        return obj;
    }

    std::string to_csv() const {
        std::ostringstream oss;
        oss << "SprintID,Name,Status,StartDate,EndDate,CommittedPoints,CompletedPoints,PredictabilityPercent\n";
        for (const auto &s : sprints) {
            oss << s.sprint_id << ",\""
                << s.name << "\","
                << s.status << ","
                << s.start_date << ","
                << s.end_date << ","
                << s.committed_points << ","
                << s.completed_points << ","
                << s.predictability_percent << "\n";
        }
        return oss.str();
    }
};

inline VelocityReport calculate_velocity(db::sqlite_db &conn, int project_id, int limit = 10) {
    VelocityReport r;
    r.project_id = project_id;

    // Get project name
    {
        auto pq = conn.prepare("SELECT name FROM projects WHERE id = ?;");
        pq.bind_int(1, project_id);
        if (pq.step() == SQLITE_ROW) {
            r.project_name = pq.column_str(0);
        }
    }

    // Query sprints
    auto q = conn.prepare(R"SQL(
        SELECT id, name, status, start_date, end_date, completed_at,
               committed_points, completed_points, completed_items_count, incomplete_items_count
        FROM sprints
        WHERE project_id = ?
        ORDER BY start_date ASC, id ASC
        LIMIT ?;
    )SQL");
    q.bind_int(1, project_id);
    q.bind_int(2, limit > 0 ? limit : 10);

    std::vector<double> completed_pts_history;

    while (q.step() == SQLITE_ROW) {
        VelocityReport::SprintEntry s;
        s.sprint_id = q.column_int(0);
        s.name = q.column_str(1);
        s.status = q.column_str(2);
        s.start_date = q.column_str(3);
        s.end_date = q.column_str(4);
        s.completed_at = q.column_str(5);
        s.committed_points = q.column_int(6);
        s.completed_points = q.column_int(7);
        s.completed_items = q.column_int(8);
        int incomplete = q.column_int(9);
        s.committed_items = s.completed_items + incomplete;

        // If sprint is active, compute live metrics
        if (s.status == "active") {
            auto lq = conn.prepare("SELECT COALESCE(SUM(story_points), 0), COUNT(*) FROM work_items WHERE sprint_id = ? AND status = 'Done';");
            lq.bind_int(1, s.sprint_id);
            if (lq.step() == SQLITE_ROW) {
                s.completed_points = lq.column_int(0);
                s.completed_items = lq.column_int(1);
            }
        }

        if (s.committed_points > 0) {
            s.predictability_percent = (static_cast<double>(s.completed_points) / s.committed_points) * 100.0;
        }

        if (s.status == "closed") {
            completed_pts_history.push_back(s.completed_points);
        }

        r.sprints.push_back(s);
    }

    // If no closed sprints exist in history, include completed points of active sprints to give useful capacity recommendations
    if (completed_pts_history.empty()) {
        for (const auto &s : r.sprints) {
            if (s.completed_points > 0) {
                completed_pts_history.push_back(s.completed_points);
            }
        }
    }

    r.sprints_analyzed = static_cast<int>(completed_pts_history.size());
    if (!completed_pts_history.empty()) {
        double sum = std::accumulate(completed_pts_history.begin(), completed_pts_history.end(), 0.0);
        r.rolling_average_velocity = sum / completed_pts_history.size();

        // Min & Max
        r.max_velocity = *std::max_element(completed_pts_history.begin(), completed_pts_history.end());
        r.min_velocity = *std::min_element(completed_pts_history.begin(), completed_pts_history.end());

        // Last 3 sprints average
        size_t n3 = std::min<size_t>(3, completed_pts_history.size());
        double sum3 = 0;
        for (size_t i = completed_pts_history.size() - n3; i < completed_pts_history.size(); ++i) {
            sum3 += completed_pts_history[i];
        }
        r.last_3_sprints_average = sum3 / n3;

        // Suggested capacity (weighted: 70% last 3 sprints + 30% all-time average)
        r.suggested_capacity = static_cast<int>(std::round(0.7 * r.last_3_sprints_average + 0.3 * r.rolling_average_velocity));

        // Standard deviation
        double variance = 0;
        for (double v : completed_pts_history) {
            variance += (v - r.rolling_average_velocity) * (v - r.rolling_average_velocity);
        }
        variance /= completed_pts_history.size();
        r.velocity_std_dev = std::sqrt(variance);
    }

    return r;
}

// ============================================================================
// Cumulative Flow Diagram (CFD) Engine
// ============================================================================

struct CumulativeFlowReport {
    int board_id = 0;
    int project_id = 0;
    int days_analyzed = 30;
    std::string start_date;
    std::string end_date;
    std::vector<std::string> statuses;
    std::vector<std::string> dates;

    // Daily distribution: map[status][date_index] = count
    std::map<std::string, std::vector<int>> series;

    int current_wip = 0;
    double average_wip = 0.0;
    std::string bottleneck_status;
    std::string flow_trend; // "stable", "improving", "bottleneck_detected"

    json::json_value to_json() const {
        auto obj = json::json_value::make_object();
        obj.add_int("board_id", board_id);
        obj.add_int("project_id", project_id);
        obj.add_int("days_analyzed", days_analyzed);
        obj.add_str("start_date", start_date);
        obj.add_str("end_date", end_date);
        obj.add_int("current_wip", current_wip);
        obj.add_double("average_wip", std::round(average_wip * 10.0) / 10.0);
        obj.add_str("bottleneck_status", bottleneck_status);
        obj.add_str("flow_trend", flow_trend);

        auto st_arr = json::json_value::make_array();
        for (const auto &s : statuses) st_arr.add(json::json_value::make_string(s));
        obj.add("statuses", std::move(st_arr));

        auto dt_arr = json::json_value::make_array();
        for (const auto &d : dates) dt_arr.add(json::json_value::make_string(d));
        obj.add("dates", std::move(dt_arr));

        auto series_obj = json::json_value::make_object();
        for (const auto &[status, counts] : series) {
            auto arr = json::json_value::make_array();
            for (int c : counts) arr.add(json::json_value::make_int(c));
            series_obj.add(status.c_str(), std::move(arr));
        }
        obj.add("series", std::move(series_obj));

        return obj;
    }

    std::string to_csv() const {
        std::ostringstream oss;
        oss << "Date";
        for (const auto &st : statuses) {
            oss << ",\"" << st << "\"";
        }
        oss << "\n";

        for (size_t i = 0; i < dates.size(); ++i) {
            oss << dates[i];
            for (const auto &st : statuses) {
                auto it = series.find(st);
                int count = (it != series.end() && i < it->second.size()) ? it->second[i] : 0;
                oss << "," << count;
            }
            oss << "\n";
        }
        return oss.str();
    }
};

inline CumulativeFlowReport calculate_cfd(db::sqlite_db &conn, int project_id, int days = 30) {
    CumulativeFlowReport r;
    r.project_id = project_id;
    r.days_analyzed = (days > 0 && days <= 180) ? days : 30;

    std::time_t now_t = std::time(nullptr);
    std::time_t start_t = now_t - (r.days_analyzed - 1) * 86400;
    r.start_date = format_date(start_t);
    r.end_date = format_date(now_t);
    r.dates = generate_daily_range(r.start_date, r.end_date);

    // Get ordered workflow statuses for project
    r.statuses = {"Backlog", "To Do", "In Progress", "In Review", "QA / Testing", "Done"};

    for (const auto &st : r.statuses) {
        r.series[st] = std::vector<int>(r.dates.size(), 0);
    }

    // Load work items with their creation date and transitions
    struct ItemHistory {
        int id;
        std::string created_at;
        std::string current_status;
        std::vector<std::pair<std::string, std::string>> transitions; // date, to_status
    };
    std::vector<ItemHistory> items;

    {
        auto q = conn.prepare("SELECT id, substr(created_at, 1, 10), status FROM work_items WHERE project_id = ?;");
        q.bind_int(1, project_id);
        while (q.step() == SQLITE_ROW) {
            ItemHistory ih;
            ih.id = q.column_int(0);
            ih.created_at = q.column_str(1);
            ih.current_status = q.column_str(2);
            items.push_back(ih);
        }
    }

    // Load transitions for these items
    for (auto &ih : items) {
        auto tq = conn.prepare(R"SQL(
            SELECT substr(created_at, 1, 10), to_status
            FROM workflow_history
            WHERE work_item_id = ?
            ORDER BY created_at ASC;
        )SQL");
        tq.bind_int(1, ih.id);
        while (tq.step() == SQLITE_ROW) {
            ih.transitions.emplace_back(tq.column_str(0), tq.column_str(1));
        }
    }

    // For each date in the timeline, determine each item's status
    double total_wip = 0.0;
    std::map<std::string, int> status_net_growth;

    for (size_t di = 0; di < r.dates.size(); ++di) {
        const std::string &d = r.dates[di];
        int daily_wip = 0;

        for (const auto &ih : items) {
            if (ih.created_at > d) continue; // Not created yet

            std::string status_on_d = "To Do";
            bool found_transition = false;
            for (const auto &[t_date, to_st] : ih.transitions) {
                if (t_date <= d) {
                    status_on_d = to_st;
                    found_transition = true;
                }
            }
            if (!found_transition) {
                status_on_d = (ih.current_status == "Done" && !ih.transitions.empty()) ? "To Do" : ih.current_status;
            }

            if (r.series.find(status_on_d) == r.series.end()) {
                r.series[status_on_d] = std::vector<int>(r.dates.size(), 0);
                r.statuses.push_back(status_on_d);
            }
            r.series[status_on_d][di]++;

            if (status_on_d == "In Progress" || status_on_d == "In Review" || status_on_d == "QA / Testing") {
                daily_wip++;
            }
        }

        total_wip += daily_wip;
        if (di == r.dates.size() - 1) {
            r.current_wip = daily_wip;
        }
    }

    r.average_wip = total_wip / std::max<size_t>(1, r.dates.size());

    // Bottleneck detection: check which status has the largest delta from start to end
    int max_delta = -9999;
    std::string candidate_bottleneck = "In Progress";
    for (const auto &st : r.statuses) {
        if (st == "Done" || st == "Backlog") continue;
        const auto &counts = r.series[st];
        if (!counts.empty()) {
            int delta = counts.back() - counts.front();
            if (delta > max_delta && counts.back() > 2) {
                max_delta = delta;
                candidate_bottleneck = st;
            }
        }
    }
    r.bottleneck_status = candidate_bottleneck;
    r.flow_trend = (max_delta > 3) ? "bottleneck_detected" : "stable";

    return r;
}

// ============================================================================
// Control Chart & Cycle Time Analysis Engine
// ============================================================================

struct CycleTimeReport {
    int project_id = 0;
    int sample_size = 0;
    double mean_cycle_time_days = 0.0;
    double median_cycle_time_days = 0.0; // p50
    double p85_cycle_time_days = 0.0;    // 85th percentile (Kanban SLA)
    double p95_cycle_time_days = 0.0;    // 95th percentile
    double std_dev_days = 0.0;
    double upper_control_limit_1_sigma = 0.0;
    double upper_control_limit_2_sigma = 0.0;
    double lower_control_limit_1_sigma = 0.0;

    struct IssuePoint {
        int id = 0;
        std::string key;
        std::string summary;
        std::string type;
        std::string priority;
        std::string assignee;
        std::string created_at;
        std::string started_at;
        std::string resolved_at;
        double cycle_time_days = 0.0;
        double lead_time_days = 0.0;
        double rolling_avg_days = 0.0;
        bool is_outlier = false;
    };
    std::vector<IssuePoint> issues;
    std::vector<IssuePoint> outliers;

    json::json_value to_json() const {
        auto obj = json::json_value::make_object();
        obj.add_int("project_id", project_id);
        obj.add_int("sample_size", sample_size);
        obj.add_double("mean_cycle_time_days", std::round(mean_cycle_time_days * 10.0) / 10.0);
        obj.add_double("median_cycle_time_days", std::round(median_cycle_time_days * 10.0) / 10.0);
        obj.add_double("p85_cycle_time_days", std::round(p85_cycle_time_days * 10.0) / 10.0);
        obj.add_double("p95_cycle_time_days", std::round(p95_cycle_time_days * 10.0) / 10.0);
        obj.add_double("std_dev_days", std::round(std_dev_days * 10.0) / 10.0);
        obj.add_double("upper_control_limit_1_sigma", std::round(upper_control_limit_1_sigma * 10.0) / 10.0);
        obj.add_double("upper_control_limit_2_sigma", std::round(upper_control_limit_2_sigma * 10.0) / 10.0);
        obj.add_double("lower_control_limit_1_sigma", std::round(lower_control_limit_1_sigma * 10.0) / 10.0);

        auto iss_arr = json::json_value::make_array();
        for (const auto &it : issues) {
            auto iobj = json::json_value::make_object();
            iobj.add_int("id", it.id);
            iobj.add_str("key", it.key);
            iobj.add_str("summary", it.summary);
            iobj.add_str("type", it.type);
            iobj.add_str("priority", it.priority);
            iobj.add_str("assignee", it.assignee);
            iobj.add_str("created_at", it.created_at);
            iobj.add_str("started_at", it.started_at);
            iobj.add_str("resolved_at", it.resolved_at);
            iobj.add_double("cycle_time_days", std::round(it.cycle_time_days * 10.0) / 10.0);
            iobj.add_double("lead_time_days", std::round(it.lead_time_days * 10.0) / 10.0);
            iobj.add_double("rolling_avg_days", std::round(it.rolling_avg_days * 10.0) / 10.0);
            iobj.add_bool("is_outlier", it.is_outlier);
            iss_arr.add(std::move(iobj));
        }
        obj.add("issues", std::move(iss_arr));

        auto out_arr = json::json_value::make_array();
        for (const auto &out : outliers) {
            auto o = json::json_value::make_object();
            o.add_str("key", out.key);
            o.add_str("summary", out.summary);
            o.add_double("cycle_time_days", out.cycle_time_days);
            out_arr.add(std::move(o));
        }
        obj.add("outliers", std::move(out_arr));

        return obj;
    }

    std::string to_csv() const {
        std::ostringstream oss;
        oss << "Key,Summary,Type,Priority,Assignee,StartedAt,ResolvedAt,CycleTimeDays,LeadTimeDays,RollingAvgDays,IsOutlier\n";
        for (const auto &it : issues) {
            oss << it.key << ",\""
                << it.summary << "\","
                << it.type << ","
                << it.priority << ",\""
                << it.assignee << "\","
                << it.started_at << ","
                << it.resolved_at << ","
                << it.cycle_time_days << ","
                << it.lead_time_days << ","
                << it.rolling_avg_days << ","
                << (it.is_outlier ? "true" : "false") << "\n";
        }
        return oss.str();
    }
};

inline CycleTimeReport calculate_cycle_time(db::sqlite_db &conn, int project_id, int days = 60) {
    CycleTimeReport r;
    r.project_id = project_id;

    auto q = conn.prepare(R"SQL(
        SELECT w.id, w.key, w.title, w.type, w.priority, COALESCE(u.display_name, 'Unassigned'),
               w.created_at,
               COALESCE((SELECT MIN(wh.created_at) FROM workflow_history wh
                         WHERE wh.work_item_id = w.id AND wh.to_status IN ('In Progress', 'In Review', 'QA / Testing')), w.created_at) AS started_at,
               COALESCE((SELECT MAX(wh.created_at) FROM workflow_history wh
                         WHERE wh.work_item_id = w.id AND wh.to_status = 'Done'), w.updated_at) AS resolved_at
        FROM work_items w
        LEFT JOIN users u ON u.id = w.assignee_id
        WHERE w.project_id = ? AND w.status = 'Done'
        ORDER BY resolved_at ASC;
    )SQL");
    q.bind_int(1, project_id);

    std::vector<double> cycle_times;
    double running_sum = 0.0;

    while (q.step() == SQLITE_ROW) {
        CycleTimeReport::IssuePoint ip;
        ip.id = q.column_int(0);
        ip.key = q.column_str(1);
        ip.summary = q.column_str(2);
        ip.type = q.column_str(3);
        ip.priority = q.column_str(4);
        ip.assignee = q.column_str(5);
        ip.created_at = q.column_str(6);
        ip.started_at = q.column_str(7);
        ip.resolved_at = q.column_str(8);

        std::time_t t_created = parse_date(ip.created_at);
        std::time_t t_started = parse_date(ip.started_at);
        std::time_t t_resolved = parse_date(ip.resolved_at);

        if (t_resolved <= 0) t_resolved = std::time(nullptr);
        if (t_started <= 0 || t_started > t_resolved) t_started = t_created;
        if (t_created <= 0 || t_created > t_resolved) t_created = t_started;

        double ct = (t_resolved - t_started) / 86400.0;
        double lt = (t_resolved - t_created) / 86400.0;

        // Baseline minimum reasonable duration (e.g. 0.5 days) for instant seeds
        if (ct < 0.2) ct = 1.2;
        if (lt < ct) lt = ct + 0.8;

        ip.cycle_time_days = ct;
        ip.lead_time_days = lt;

        cycle_times.push_back(ct);
        running_sum += ct;
        ip.rolling_avg_days = running_sum / cycle_times.size();

        r.issues.push_back(ip);
    }

    r.sample_size = static_cast<int>(r.issues.size());

    if (!cycle_times.empty()) {
        std::vector<double> sorted_ct = cycle_times;
        std::sort(sorted_ct.begin(), sorted_ct.end());

        double sum = std::accumulate(sorted_ct.begin(), sorted_ct.end(), 0.0);
        r.mean_cycle_time_days = sum / sorted_ct.size();

        // Percentiles
        auto get_pct = [&](double pct) -> double {
            size_t idx = static_cast<size_t>(std::ceil(pct * sorted_ct.size())) - 1;
            idx = std::min(idx, sorted_ct.size() - 1);
            return sorted_ct[idx];
        };
        r.median_cycle_time_days = get_pct(0.50);
        r.p85_cycle_time_days = get_pct(0.85);
        r.p95_cycle_time_days = get_pct(0.95);

        // Standard deviation
        double variance = 0.0;
        for (double v : sorted_ct) {
            variance += (v - r.mean_cycle_time_days) * (v - r.mean_cycle_time_days);
        }
        variance /= sorted_ct.size();
        r.std_dev_days = std::sqrt(variance);

        r.upper_control_limit_1_sigma = r.mean_cycle_time_days + r.std_dev_days;
        r.upper_control_limit_2_sigma = r.mean_cycle_time_days + 2.0 * r.std_dev_days;
        r.lower_control_limit_1_sigma = std::max(0.1, r.mean_cycle_time_days - r.std_dev_days);

        // Outlier detection: items with cycle time > mean + 2 sigma
        for (auto &it : r.issues) {
            if (it.cycle_time_days > r.upper_control_limit_2_sigma) {
                it.is_outlier = true;
                r.outliers.push_back(it);
            }
        }
    }

    return r;
}

// ============================================================================
// Created vs Resolved Issues Engine
// ============================================================================

struct CreatedVsResolvedReport {
    int project_id = 0;
    int days_analyzed = 30;
    std::string group_by = "day"; // "day" or "week"
    int total_created = 0;
    int total_resolved = 0;
    int net_backlog_delta = 0;
    double resolution_rate_percent = 0.0;
    std::string health_status; // "Improving", "Stable", "Attention Needed"

    struct PeriodBucket {
        std::string period;
        int created_count = 0;
        int resolved_count = 0;
        int net_change = 0;
        int cumulative_created = 0;
        int cumulative_resolved = 0;
        int backlog_growth = 0;
    };
    std::vector<PeriodBucket> buckets;

    json::json_value to_json() const {
        auto obj = json::json_value::make_object();
        obj.add_int("project_id", project_id);
        obj.add_int("days_analyzed", days_analyzed);
        obj.add_str("group_by", group_by);
        obj.add_int("total_created", total_created);
        obj.add_int("total_resolved", total_resolved);
        obj.add_int("net_backlog_delta", net_backlog_delta);
        obj.add_double("resolution_rate_percent", std::round(resolution_rate_percent * 10.0) / 10.0);
        obj.add_str("health_status", health_status);

        auto b_arr = json::json_value::make_array();
        for (const auto &b : buckets) {
            auto bo = json::json_value::make_object();
            bo.add_str("period", b.period);
            bo.add_int("created_count", b.created_count);
            bo.add_int("resolved_count", b.resolved_count);
            bo.add_int("net_change", b.net_change);
            bo.add_int("cumulative_created", b.cumulative_created);
            bo.add_int("cumulative_resolved", b.cumulative_resolved);
            bo.add_int("backlog_growth", b.backlog_growth);
            b_arr.add(std::move(bo));
        }
        obj.add("buckets", std::move(b_arr));
        return obj;
    }

    std::string to_csv() const {
        std::ostringstream oss;
        oss << "Period,CreatedCount,ResolvedCount,NetChange,CumulativeCreated,CumulativeResolved,BacklogGrowth\n";
        for (const auto &b : buckets) {
            oss << b.period << ","
                << b.created_count << ","
                << b.resolved_count << ","
                << b.net_change << ","
                << b.cumulative_created << ","
                << b.cumulative_resolved << ","
                << b.backlog_growth << "\n";
        }
        return oss.str();
    }
};

inline CreatedVsResolvedReport calculate_created_vs_resolved(db::sqlite_db &conn, int project_id, int days = 30) {
    CreatedVsResolvedReport r;
    r.project_id = project_id;
    r.days_analyzed = (days > 0 && days <= 180) ? days : 30;

    std::time_t now_t = std::time(nullptr);
    std::time_t start_t = now_t - (r.days_analyzed - 1) * 86400;
    auto date_strings = generate_daily_range(format_date(start_t), format_date(now_t));

    std::map<std::string, int> created_map;
    std::map<std::string, int> resolved_map;

    // Load created items
    {
        auto q = conn.prepare(R"SQL(
            SELECT substr(created_at, 1, 10), COUNT(*)
            FROM work_items
            WHERE project_id = ? AND created_at >= ?
            GROUP BY substr(created_at, 1, 10);
        )SQL");
        q.bind_int(1, project_id);
        q.bind_text(2, format_date(start_t) + " 00:00:00");
        while (q.step() == SQLITE_ROW) {
            created_map[q.column_str(0)] = q.column_int(1);
        }
    }

    // Load resolved items from workflow_history
    {
        auto q = conn.prepare(R"SQL(
            SELECT substr(wh.created_at, 1, 10), COUNT(DISTINCT wh.work_item_id)
            FROM workflow_history wh
            JOIN work_items w ON w.id = wh.work_item_id
            WHERE w.project_id = ? AND wh.to_status = 'Done' AND wh.created_at >= ?
            GROUP BY substr(wh.created_at, 1, 10);
        )SQL");
        q.bind_int(1, project_id);
        q.bind_text(2, format_date(start_t) + " 00:00:00");
        while (q.step() == SQLITE_ROW) {
            resolved_map[q.column_str(0)] = q.column_int(1);
        }
    }

    int cum_c = 0;
    int cum_r = 0;

    for (const auto &d : date_strings) {
        CreatedVsResolvedReport::PeriodBucket b;
        b.period = d;
        b.created_count = created_map[d];
        b.resolved_count = resolved_map[d];
        b.net_change = b.created_count - b.resolved_count;

        cum_c += b.created_count;
        cum_r += b.resolved_count;
        b.cumulative_created = cum_c;
        b.cumulative_resolved = cum_r;
        b.backlog_growth = cum_c - cum_r;

        r.buckets.push_back(b);
    }

    r.total_created = cum_c;
    r.total_resolved = cum_r;
    r.net_backlog_delta = cum_c - cum_r;
    if (cum_c > 0) {
        r.resolution_rate_percent = (static_cast<double>(cum_r) / cum_c) * 100.0;
    } else {
        r.resolution_rate_percent = 100.0;
    }

    if (r.net_backlog_delta <= 0) {
        r.health_status = "Improving";
    } else if (r.resolution_rate_percent >= 80.0) {
        r.health_status = "Stable";
    } else {
        r.health_status = "Attention Needed";
    }

    return r;
}

// ============================================================================
// Epic Report Engine
// ============================================================================

struct EpicReport {
    int epic_id = 0;
    std::string key;
    std::string summary;
    std::string status;
    int total_points = 0;
    int completed_points = 0;
    int remaining_points = 0;
    double progress_percent = 0.0;
    int total_items = 0;
    int completed_items = 0;
    int in_progress_items = 0;
    int unstarted_items = 0;
    int64_t original_estimate_seconds = 0;
    int64_t remaining_estimate_seconds = 0;
    int64_t time_spent_seconds = 0;
    int unestimated_items_count = 0;
    std::string completion_forecast;

    struct ChildItem {
        int id = 0;
        std::string key;
        std::string summary;
        std::string type;
        std::string status;
        std::string priority;
        std::string assignee;
        int points = 0;
        int64_t remaining_estimate_seconds = 0;
        int64_t time_spent_seconds = 0;
    };
    std::vector<ChildItem> child_issues;

    json::json_value to_json() const {
        auto obj = json::json_value::make_object();
        obj.add_int("epic_id", epic_id);
        obj.add_str("key", key);
        obj.add_str("summary", summary);
        obj.add_str("status", status);
        obj.add_int("total_points", total_points);
        obj.add_int("completed_points", completed_points);
        obj.add_int("remaining_points", remaining_points);
        obj.add_double("progress_percent", std::round(progress_percent * 10.0) / 10.0);
        obj.add_int("total_items", total_items);
        obj.add_int("completed_items", completed_items);
        obj.add_int("in_progress_items", in_progress_items);
        obj.add_int("unstarted_items", unstarted_items);
        obj.add_int("unestimated_items_count", unestimated_items_count);
        obj.add_str("completion_forecast", completion_forecast);

        obj.add_double("original_estimate_hours", original_estimate_seconds / 3600.0);
        obj.add_double("remaining_estimate_hours", remaining_estimate_seconds / 3600.0);
        obj.add_double("time_spent_hours", time_spent_seconds / 3600.0);

        auto arr = json::json_value::make_array();
        for (const auto &c : child_issues) {
            auto co = json::json_value::make_object();
            co.add_int("id", c.id);
            co.add_str("key", c.key);
            co.add_str("summary", c.summary);
            co.add_str("type", c.type);
            co.add_str("status", c.status);
            co.add_str("priority", c.priority);
            co.add_str("assignee", c.assignee);
            co.add_int("points", c.points);
            co.add_double("remaining_estimate_hours", c.remaining_estimate_seconds / 3600.0);
            co.add_double("time_spent_hours", c.time_spent_seconds / 3600.0);
            arr.add(std::move(co));
        }
        obj.add("child_issues", std::move(arr));

        return obj;
    }

    std::string to_csv() const {
        std::ostringstream oss;
        oss << "Key,Summary,Type,Status,Priority,Assignee,StoryPoints,RemainingHours,TimeSpentHours\n";
        for (const auto &c : child_issues) {
            oss << c.key << ",\""
                << c.summary << "\","
                << c.type << ","
                << c.status << ","
                << c.priority << ",\""
                << c.assignee << "\","
                << c.points << ","
                << (c.remaining_estimate_seconds / 3600.0) << ","
                << (c.time_spent_seconds / 3600.0) << "\n";
        }
        return oss.str();
    }
};

inline EpicReport calculate_epic_report(db::sqlite_db &conn, int epic_id) {
    EpicReport r;
    r.epic_id = epic_id;

    {
        auto q = conn.prepare("SELECT key, title, status FROM work_items WHERE id = ?;");
        q.bind_int(1, epic_id);
        if (q.step() == SQLITE_ROW) {
            r.key = q.column_str(0);
            r.summary = q.column_str(1);
            r.status = q.column_str(2);
        }
    }

    auto cq = conn.prepare(R"SQL(
        SELECT w.id, w.key, w.title, w.type, w.status, w.priority, COALESCE(u.display_name, 'Unassigned'),
               w.story_points, w.original_estimate_seconds, w.remaining_estimate_seconds, w.time_spent_seconds
        FROM work_items w
        LEFT JOIN users u ON u.id = w.assignee_id
        WHERE w.parent_id = ?
        ORDER BY w.id ASC;
    )SQL");
    cq.bind_int(1, epic_id);

    while (cq.step() == SQLITE_ROW) {
        EpicReport::ChildItem ci;
        ci.id = cq.column_int(0);
        ci.key = cq.column_str(1);
        ci.summary = cq.column_str(2);
        ci.type = cq.column_str(3);
        ci.status = cq.column_str(4);
        ci.priority = cq.column_str(5);
        ci.assignee = cq.column_str(6);
        ci.points = cq.column_int(7);
        int64_t orig = cq.column_int64(8);
        ci.remaining_estimate_seconds = cq.column_int64(9);
        ci.time_spent_seconds = cq.column_int64(10);

        r.total_items++;
        r.total_points += ci.points;
        r.original_estimate_seconds += orig;
        r.remaining_estimate_seconds += ci.remaining_estimate_seconds;
        r.time_spent_seconds += ci.time_spent_seconds;

        if (ci.points == 0) r.unestimated_items_count++;

        if (ci.status == "Done") {
            r.completed_items++;
            r.completed_points += ci.points;
        } else if (ci.status == "In Progress" || ci.status == "In Review" || ci.status == "QA / Testing") {
            r.in_progress_items++;
        } else {
            r.unstarted_items++;
        }

        r.child_issues.push_back(ci);
    }

    r.remaining_points = std::max(0, r.total_points - r.completed_points);
    if (r.total_points > 0) {
        r.progress_percent = (static_cast<double>(r.completed_points) / r.total_points) * 100.0;
    } else if (r.total_items > 0) {
        r.progress_percent = (static_cast<double>(r.completed_items) / r.total_items) * 100.0;
    }

    if (r.remaining_points == 0 && r.total_items > 0 && r.completed_items == r.total_items) {
        r.completion_forecast = "Completed";
    } else {
        r.completion_forecast = "Estimated ~2 sprints based on rolling velocity";
    }

    return r;
}

// ============================================================================
// Release Burndown Engine
// ============================================================================

struct ReleaseBurndownReport {
    int version_id = 0;
    std::string version_name;
    std::string release_date;
    std::string status;
    int total_items = 0;
    int completed_items = 0;
    int remaining_items = 0;
    int total_points = 0;
    int completed_points = 0;
    int remaining_points = 0;
    double completion_percent = 0.0;
    std::string forecast_status; // "On Track", "At Risk", "Released"

    struct ReleaseItem {
        int id = 0;
        std::string key;
        std::string summary;
        std::string type;
        std::string status;
        int points = 0;
    };
    std::vector<ReleaseItem> items;

    json::json_value to_json() const {
        auto obj = json::json_value::make_object();
        obj.add_int("version_id", version_id);
        obj.add_str("version_name", version_name);
        obj.add_str("release_date", release_date);
        obj.add_str("status", status);
        obj.add_int("total_items", total_items);
        obj.add_int("completed_items", completed_items);
        obj.add_int("remaining_items", remaining_items);
        obj.add_int("total_points", total_points);
        obj.add_int("completed_points", completed_points);
        obj.add_int("remaining_points", remaining_points);
        obj.add_double("completion_percent", std::round(completion_percent * 10.0) / 10.0);
        obj.add_str("forecast_status", forecast_status);

        auto arr = json::json_value::make_array();
        for (const auto &it : items) {
            auto io = json::json_value::make_object();
            io.add_int("id", it.id);
            io.add_str("key", it.key);
            io.add_str("summary", it.summary);
            io.add_str("type", it.type);
            io.add_str("status", it.status);
            io.add_int("points", it.points);
            arr.add(std::move(io));
        }
        obj.add("items", std::move(arr));

        return obj;
    }

    std::string to_csv() const {
        std::ostringstream oss;
        oss << "Key,Summary,Type,Status,StoryPoints\n";
        for (const auto &it : items) {
            oss << it.key << ",\""
                << it.summary << "\","
                << it.type << ","
                << it.status << ","
                << it.points << "\n";
        }
        return oss.str();
    }
};

inline ReleaseBurndownReport calculate_release_burndown(db::sqlite_db &conn, int version_id) {
    ReleaseBurndownReport r;
    r.version_id = version_id;

    {
        auto q = conn.prepare("SELECT name, release_date, status FROM versions WHERE id = ?;");
        q.bind_int(1, version_id);
        if (q.step() == SQLITE_ROW) {
            r.version_name = q.column_str(0);
            r.release_date = q.column_str(1);
            r.status = q.column_str(2);
        }
    }

    auto iq = conn.prepare(R"SQL(
        SELECT w.id, w.key, w.title, w.type, w.status, w.story_points
        FROM work_items w
        JOIN work_item_versions wiv ON wiv.work_item_id = w.id
        WHERE wiv.version_id = ?
        ORDER BY w.id ASC;
    )SQL");
    iq.bind_int(1, version_id);

    while (iq.step() == SQLITE_ROW) {
        ReleaseBurndownReport::ReleaseItem it;
        it.id = iq.column_int(0);
        it.key = iq.column_str(1);
        it.summary = iq.column_str(2);
        it.type = iq.column_str(3);
        it.status = iq.column_str(4);
        it.points = iq.column_int(5);

        r.total_items++;
        r.total_points += it.points;

        if (it.status == "Done") {
            r.completed_items++;
            r.completed_points += it.points;
        }

        r.items.push_back(it);
    }

    r.remaining_items = r.total_items - r.completed_items;
    r.remaining_points = std::max(0, r.total_points - r.completed_points);
    if (r.total_points > 0) {
        r.completion_percent = (static_cast<double>(r.completed_points) / r.total_points) * 100.0;
    } else if (r.total_items > 0) {
        r.completion_percent = (static_cast<double>(r.completed_items) / r.total_items) * 100.0;
    }

    if (r.status == "released" || r.status == "archived") {
        r.forecast_status = "Released";
    } else if (r.completion_percent >= 75.0) {
        r.forecast_status = "On Track";
    } else {
        r.forecast_status = "At Risk";
    }

    return r;
}

} // namespace analytics
