#pragma once

#include "raii_sqlite.h"
#include "raii_json.h"
#include "jql_parser.h"
#include "collaboration_engine.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace automation {

thread_local int g_automation_depth = 0;
constexpr int MAX_AUTOMATION_DEPTH = 5;

enum class EventType {
    IssueCreated,
    StatusChanged,
    FieldValueChanged,
    CommentAdded,
    ScheduledCron,
    IncomingWebhook,
    Manual
};

inline std::string event_type_to_string(EventType type) {
    switch (type) {
        case EventType::IssueCreated:     return "issue_created";
        case EventType::StatusChanged:    return "status_changed";
        case EventType::FieldValueChanged:return "field_value_changed";
        case EventType::CommentAdded:     return "comment_added";
        case EventType::ScheduledCron:    return "scheduled_cron";
        case EventType::IncomingWebhook:  return "incoming_webhook";
        case EventType::Manual:           return "manual";
    }
    return "unknown";
}

inline EventType string_to_event_type(const std::string &str) {
    if (str == "issue_created")      return EventType::IssueCreated;
    if (str == "status_changed")     return EventType::StatusChanged;
    if (str == "field_value_changed" || str == "field_updated") return EventType::FieldValueChanged;
    if (str == "comment_added")      return EventType::CommentAdded;
    if (str == "scheduled_cron")     return EventType::ScheduledCron;
    if (str == "incoming_webhook")   return EventType::IncomingWebhook;
    return EventType::Manual;
}

struct AutomationEvent {
    EventType type = EventType::Manual;
    int project_id = 0;
    int issue_id = 0;
    std::string issue_key;
    int user_id = 0;
    std::string user_username;
    std::string user_display_name;
    std::string user_email;

    // StatusChanged
    std::string from_status;
    std::string to_status;

    // FieldValueChanged
    std::string field_name;
    std::string old_value;
    std::string new_value;

    // CommentAdded
    int comment_id = 0;
    std::string comment_body;

    // Cron / Webhook
    std::string webhook_payload;
    std::string cron_name;
};

inline std::string format_time_point(const std::chrono::system_clock::time_point &tp, bool date_only = false) {
    auto t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_buf{};
    gmtime_r(&t, &tm_buf);
    char buf[64];
    if (date_only) {
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_buf);
    } else {
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    }
    return std::string(buf);
}

// Smart Values Variable Evaluator
inline std::string evaluate_smart_values(db::sqlite_db &conn,
                                        const std::string &input_text,
                                        const AutomationEvent &ev,
                                        int target_issue_id = 0) {
    if (input_text.empty() || input_text.find("{{") == std::string::npos) {
        return input_text;
    }

    int eff_issue_id = target_issue_id > 0 ? target_issue_id : ev.issue_id;
    std::string issue_key = ev.issue_key;
    std::string summary, description, status, priority, type, category, labels, parent_key;
    std::string assignee_name = "Unassigned", assignee_user = "";
    std::string reporter_name = "Unknown", reporter_user = "";

    if (eff_issue_id > 0) {
        auto q = conn.prepare(R"SQL(
            SELECT w.key, w.title, COALESCE(w.description, ''), w.status, w.priority,
                   w.type, w.category, COALESCE(w.labels, ''),
                   COALESCE(u_ass.display_name, 'Unassigned'), COALESCE(u_ass.username, ''),
                   COALESCE(u_rep.display_name, 'Unknown'), COALESCE(u_rep.username, ''),
                   COALESCE(p_parent.key, '')
            FROM work_items w
            LEFT JOIN users u_ass ON u_ass.id = w.assignee_id
            LEFT JOIN users u_rep ON u_rep.id = w.reporter_id
            LEFT JOIN work_items p_parent ON p_parent.id = w.parent_id
            WHERE w.id = ?;
        )SQL");
        q.bind_int(1, eff_issue_id);
        if (q.step() == SQLITE_ROW) {
            issue_key = q.column_str(0);
            summary = q.column_str(1);
            description = q.column_str(2);
            status = q.column_str(3);
            priority = q.column_str(4);
            type = q.column_str(5);
            category = q.column_str(6);
            labels = q.column_str(7);
            assignee_name = q.column_str(8);
            assignee_user = q.column_str(9);
            reporter_name = q.column_str(10);
            reporter_user = q.column_str(11);
            parent_key = q.column_str(12);
        }
    }

    std::string initiator_name = !ev.user_display_name.empty() ? ev.user_display_name : "System";
    std::string initiator_user = !ev.user_username.empty() ? ev.user_username : "system";
    std::string initiator_email = !ev.user_email.empty() ? ev.user_email : "system@example.com";

    if (ev.user_id > 0 && ev.user_display_name.empty()) {
        auto uq = conn.prepare("SELECT username, display_name, email FROM users WHERE id = ?;");
        uq.bind_int(1, ev.user_id);
        if (uq.step() == SQLITE_ROW) {
            initiator_user = uq.column_str(0);
            initiator_name = uq.column_str(1);
            initiator_email = uq.column_str(2);
        }
    }

    auto now_tp = std::chrono::system_clock::now();
    std::string now_str = format_time_point(now_tp, false);
    std::string now_date = format_time_point(now_tp, true);

    std::string result = input_text;

    auto replace_all = [](std::string &s, const std::string &from, const std::string &to) {
        if (from.empty()) return;
        size_t start_pos = 0;
        while ((start_pos = s.find(from, start_pos)) != std::string::npos) {
            s.replace(start_pos, from.length(), to);
            start_pos += to.length();
        }
    };

    // Standard Issue Tokens
    replace_all(result, "{{issue.key}}", issue_key);
    replace_all(result, "{{issue.id}}", std::to_string(eff_issue_id));
    replace_all(result, "{{issue.summary}}", summary);
    replace_all(result, "{{issue.title}}", summary);
    replace_all(result, "{{issue.description}}", description);
    replace_all(result, "{{issue.status}}", status);
    replace_all(result, "{{issue.priority}}", priority);
    replace_all(result, "{{issue.type}}", type);
    replace_all(result, "{{issue.category}}", category);
    replace_all(result, "{{issue.labels}}", labels);
    replace_all(result, "{{issue.assignee}}", assignee_name);
    replace_all(result, "{{issue.assignee.displayName}}", assignee_name);
    replace_all(result, "{{issue.assignee.username}}", assignee_user);
    replace_all(result, "{{issue.reporter}}", reporter_name);
    replace_all(result, "{{issue.reporter.displayName}}", reporter_name);
    replace_all(result, "{{issue.reporter.username}}", reporter_user);
    replace_all(result, "{{issue.parent}}", parent_key);
    replace_all(result, "{{issue.parent.key}}", parent_key);

    // Initiator Tokens
    replace_all(result, "{{initiator.displayName}}", initiator_name);
    replace_all(result, "{{initiator.name}}", initiator_name);
    replace_all(result, "{{initiator.username}}", initiator_user);
    replace_all(result, "{{initiator.email}}", initiator_email);

    // Context / Event Tokens
    replace_all(result, "{{comment.body}}", ev.comment_body);
    replace_all(result, "{{field.from}}", ev.old_value);
    replace_all(result, "{{field.to}}", ev.new_value);
    replace_all(result, "{{now}}", now_str);
    replace_all(result, "{{now.date}}", now_date);

    // Dynamic now.plusDays(N) & now.minusDays(N)
    {
        std::regex re_plus(R"(\{\{now\.plusDays\((\d+)\)\}\})");
        std::smatch m;
        while (std::regex_search(result, m, re_plus)) {
            int days = std::stoi(m[1].str());
            auto shifted = now_tp + std::chrono::hours(24 * days);
            std::string shifted_str = format_time_point(shifted, true);
            replace_all(result, m[0].str(), shifted_str);
        }
    }
    {
        std::regex re_minus(R"(\{\{now\.minusDays\((\d+)\)\}\})");
        std::smatch m;
        while (std::regex_search(result, m, re_minus)) {
            int days = std::stoi(m[1].str());
            auto shifted = now_tp - std::chrono::hours(24 * days);
            std::string shifted_str = format_time_point(shifted, true);
            replace_all(result, m[0].str(), shifted_str);
        }
    }

    return result;
}

// Normalized sync for automation_triggers, automation_conditions, automation_actions
inline void sync_rule_child_tables(db::sqlite_db &conn,
                                   int rule_id,
                                   const std::string &trigger_type,
                                   const std::string &trigger_config,
                                   const std::string &conditions_json,
                                   const std::string &actions_json) {
    // Delete existing
    auto d1 = conn.prepare("DELETE FROM automation_triggers WHERE rule_id = ?;");
    d1.bind_int(1, rule_id);
    d1.step_done();

    auto d2 = conn.prepare("DELETE FROM automation_conditions WHERE rule_id = ?;");
    d2.bind_int(1, rule_id);
    d2.step_done();

    auto d3 = conn.prepare("DELETE FROM automation_actions WHERE rule_id = ?;");
    d3.bind_int(1, rule_id);
    d3.step_done();

    // Insert trigger
    auto ins_trig = conn.prepare("INSERT INTO automation_triggers (rule_id, trigger_type, config_json) VALUES (?, ?, ?);");
    ins_trig.bind_int(1, rule_id);
    ins_trig.bind_text(2, trigger_type);
    ins_trig.bind_text(3, trigger_config.empty() ? "{}" : trigger_config);
    ins_trig.step_done();

    // Insert conditions
    auto conds = json::json_value::parse(conditions_json);
    if (conds && conds.is_array()) {
        size_t len = conds.array_length();
        for (size_t i = 0; i < len; ++i) {
            auto c = conds.array_get(i);
            std::string c_type = c.get_string("type", "issue_field");
            std::string c_cfg = c.to_string();
            auto ins_c = conn.prepare("INSERT INTO automation_conditions (rule_id, condition_type, config_json, sort_order) VALUES (?, ?, ?, ?);");
            ins_c.bind_int(1, rule_id);
            ins_c.bind_text(2, c_type);
            ins_c.bind_text(3, c_cfg);
            ins_c.bind_int(4, static_cast<int>(i + 1));
            ins_c.step_done();
        }
    }

    // Insert actions
    auto acts = json::json_value::parse(actions_json);
    if (acts && acts.is_array()) {
        size_t len = acts.array_length();
        for (size_t i = 0; i < len; ++i) {
            auto a = acts.array_get(i);
            std::string a_type = a.get_string("type", "edit_field");
            std::string a_cfg = a.to_string();
            auto ins_a = conn.prepare("INSERT INTO automation_actions (rule_id, action_type, config_json, sort_order) VALUES (?, ?, ?, ?);");
            ins_a.bind_int(1, rule_id);
            ins_a.bind_text(2, a_type);
            ins_a.bind_text(3, a_cfg);
            ins_a.bind_int(4, static_cast<int>(i + 1));
            ins_a.step_done();
        }
    }
}

// Condition Evaluation
inline bool evaluate_condition(db::sqlite_db &conn,
                              const json::json_value &cond,
                              const AutomationEvent &ev,
                              int target_issue_id,
                              std::vector<std::string> &trace) {
    std::string cond_type = cond.get_string("type");
    int eff_issue_id = target_issue_id > 0 ? target_issue_id : ev.issue_id;

    if (cond_type == "jql") {
        std::string jql_query = cond.get_string("jql");
        if (jql_query.empty()) {
            trace.push_back("[Condition: JQL] Empty JQL expression, considered matched.");
            return true;
        }
        if (eff_issue_id <= 0) {
            trace.push_back("[Condition: JQL] No target issue available to evaluate JQL: FALSE");
            return false;
        }
        try {
            auto ast = jql::JqlParser::parse_query(jql_query);
            jql::SqlContext ctx(conn,
                                ev.user_id > 0 ? ev.user_id : 1,
                                !ev.user_username.empty() ? ev.user_username : "admin",
                                !ev.user_display_name.empty() ? ev.user_display_name : "System Administrator");
            std::string where_sql = ast.to_where_sql(ctx);
            std::string test_sql = "SELECT 1 FROM work_items w WHERE w.id = ? AND (" + where_sql + ") LIMIT 1;";
            auto stmt = conn.prepare(test_sql);
            stmt.bind_int(1, eff_issue_id);
            for (size_t i = 0; i < ctx.params.size(); ++i) {
                stmt.bind_text(static_cast<int>(i + 2), ctx.params[i]);
            }
            if (stmt.step() == SQLITE_ROW) {
                trace.push_back("[Condition: JQL] Match on '" + jql_query + "': TRUE");
                return true;
            } else {
                trace.push_back("[Condition: JQL] No match on '" + jql_query + "': FALSE");
                return false;
            }
        } catch (const std::exception &ex) {
            trace.push_back(std::string("[Condition: JQL] Error evaluating '") + jql_query + "': " + ex.what() + " (FALSE)");
            return false;
        }
    } else if (cond_type == "has_subtasks") {
        if (eff_issue_id <= 0) return false;
        auto stmt = conn.prepare("SELECT COUNT(*) FROM work_items WHERE parent_id = ?;");
        stmt.bind_int(1, eff_issue_id);
        int cnt = 0;
        if (stmt.step() == SQLITE_ROW) {
            cnt = stmt.column_int(0);
        }
        bool ok = (cnt > 0);
        trace.push_back("[Condition: has_subtasks] Subtask count = " + std::to_string(cnt) + ": " + (ok ? "TRUE" : "FALSE"));
        return ok;
    } else if (cond_type == "issue_field") {
        std::string field = cond.get_string("field");
        std::string op = cond.get_string("operator", "equals");
        std::string target_val = cond.get_string("value");

        if (eff_issue_id <= 0) return false;
        std::string actual_val;
        {
            auto q = conn.prepare(R"SQL(
                SELECT status, priority, type, category, COALESCE(labels, ''),
                       COALESCE(title, ''), COALESCE(description, '')
                FROM work_items WHERE id = ?;
            )SQL");
            q.bind_int(1, eff_issue_id);
            if (q.step() == SQLITE_ROW) {
                if (field == "status") actual_val = q.column_str(0);
                else if (field == "priority") actual_val = q.column_str(1);
                else if (field == "type") actual_val = q.column_str(2);
                else if (field == "category") actual_val = q.column_str(3);
                else if (field == "labels") actual_val = q.column_str(4);
                else if (field == "title" || field == "summary") actual_val = q.column_str(5);
                else if (field == "description") actual_val = q.column_str(6);
            }
        }

        auto to_lower = [](std::string s) {
            for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        };

        std::string l_actual = to_lower(actual_val);
        std::string l_target = to_lower(target_val);
        bool match = false;

        if (op == "equals" || op == "=") {
            match = (l_actual == l_target);
        } else if (op == "not_equals" || op == "!=") {
            match = (l_actual != l_target);
        } else if (op == "contains") {
            match = (l_actual.find(l_target) != std::string::npos);
        } else if (op == "starts_with") {
            match = (l_actual.rfind(l_target, 0) == 0);
        } else if (op == "is_empty") {
            match = actual_val.empty();
        } else if (op == "is_not_empty") {
            match = !actual_val.empty();
        } else if (op == "in") {
            std::stringstream ss(l_target);
            std::string item;
            while (std::getline(ss, item, ',')) {
                size_t first = item.find_first_not_of(" \t");
                size_t last = item.find_last_not_of(" \t");
                if (first != std::string::npos && last != std::string::npos) {
                    item = item.substr(first, last - first + 1);
                }
                if (item == l_actual) {
                    match = true;
                    break;
                }
            }
        } else {
            match = (l_actual == l_target);
        }

        trace.push_back("[Condition: issue_field] Field '" + field + "' ('" + actual_val + "') " + op + " '" + target_val + "': " + (match ? "TRUE" : "FALSE"));
        return match;
    } else if (cond_type == "user_condition") {
        std::string check = cond.get_string("check");
        if (check == "is_admin") {
            bool is_adm = false;
            if (ev.user_id > 0) {
                auto uq = conn.prepare("SELECT role FROM users WHERE id = ?;");
                uq.bind_int(1, ev.user_id);
                if (uq.step() == SQLITE_ROW) {
                    std::string r = uq.column_str(0);
                    is_adm = (r == "Admin" || r == "ADMIN");
                }
            }
            trace.push_back(std::string("[Condition: user_condition] Initiator is admin: ") + (is_adm ? "TRUE" : "FALSE"));
            return is_adm;
        } else if (check == "in_group") {
            std::string group_name = cond.get_string("group");
            bool in_grp = false;
            if (ev.user_id > 0) {
                auto gq = conn.prepare(R"SQL(
                    SELECT 1 FROM user_group_members ugm
                    INNER JOIN user_groups ug ON ug.id = ugm.group_id
                    WHERE ugm.user_id = ? AND ug.name = ?;
                )SQL");
                gq.bind_int(1, ev.user_id);
                gq.bind_text(2, group_name);
                if (gq.step() == SQLITE_ROW) {
                    in_grp = true;
                }
            }
            trace.push_back("[Condition: user_condition] Initiator in group '" + group_name + "': " + (in_grp ? "TRUE" : "FALSE"));
            return in_grp;
        }
    }

    trace.push_back("[Condition: unknown] Unhandled condition type '" + cond_type + "': TRUE");
    return true;
}

// Action Execution Forward Declarations
inline bool execute_action(db::sqlite_db &conn,
                          const json::json_value &action,
                          const AutomationEvent &ev,
                          int target_issue_id,
                          std::vector<std::string> &trace);

inline bool execute_actions_list(db::sqlite_db &conn,
                                const json::json_value &actions,
                                const AutomationEvent &ev,
                                int target_issue_id,
                                std::vector<std::string> &trace) {
    if (!actions.is_array()) return true;
    bool all_ok = true;
    size_t len = actions.array_length();
    for (size_t i = 0; i < len; ++i) {
        auto act = actions.array_get(i);
        if (!execute_action(conn, act, ev, target_issue_id, trace)) {
            all_ok = false;
        }
    }
    return all_ok;
}

inline bool execute_action(db::sqlite_db &conn,
                          const json::json_value &action,
                          const AutomationEvent &ev,
                          int target_issue_id,
                          std::vector<std::string> &trace) {
    std::string act_type = action.get_string("type");
    int eff_issue_id = target_issue_id > 0 ? target_issue_id : ev.issue_id;

    if (act_type == "transition_issue") {
        std::string target_status = action.get_string("status");
        if (target_status.empty()) {
            trace.push_back("[Action: transition_issue] Target status is empty: SKIPPED");
            return false;
        }
        if (eff_issue_id <= 0) {
            trace.push_back("[Action: transition_issue] No target issue ID: SKIPPED");
            return false;
        }

        std::string old_status = "";
        std::string item_key = "";
        {
            auto q = conn.prepare("SELECT key, status FROM work_items WHERE id = ?;");
            q.bind_int(1, eff_issue_id);
            if (q.step() == SQLITE_ROW) {
                item_key = q.column_str(0);
                old_status = q.column_str(1);
            }
        }

        if (old_status == target_status) {
            trace.push_back("[Action: transition_issue] Issue " + item_key + " is already in status '" + target_status + "': NOOP");
            return true;
        }

        int def_res_id = 0;
        if (target_status == "Done") {
            auto rq = conn.prepare("SELECT id FROM resolutions WHERE is_default = 1 LIMIT 1;");
            if (rq.step() == SQLITE_ROW) {
                def_res_id = rq.column_int(0);
            } else {
                auto rq2 = conn.prepare("SELECT id FROM resolutions ORDER BY id ASC LIMIT 1;");
                if (rq2.step() == SQLITE_ROW) {
                    def_res_id = rq2.column_int(0);
                }
            }
        }

        auto upd = conn.prepare(R"SQL(
            UPDATE work_items
            SET status = ?,
                progress = CASE WHEN ? = 'Done' THEN 100 ELSE progress END,
                resolution_id = CASE WHEN ? = 'Done' THEN COALESCE(resolution_id, ?) ELSE resolution_id END,
                resolution_date = CASE WHEN ? = 'Done' THEN COALESCE(NULLIF(resolution_date, ''), datetime('now')) ELSE resolution_date END,
                updated_at = datetime('now')
            WHERE id = ?;
        )SQL");
        upd.bind_text(1, target_status);
        upd.bind_text(2, target_status);
        upd.bind_text(3, target_status);
        upd.bind_optional_int(4, def_res_id);
        upd.bind_text(5, target_status);
        upd.bind_int(6, eff_issue_id);
        upd.step_done();

        // Record workflow history
        auto hist = conn.prepare(R"SQL(
            INSERT INTO workflow_history (work_item_id, from_status, to_status, changed_by, comment, transition_name, executed_post_functions)
            VALUES (?, ?, ?, ?, 'Automated transition by Automation Rule', 'Automation Action', 'automation_engine');
        )SQL");
        hist.bind_int(1, eff_issue_id);
        hist.bind_text(2, old_status);
        hist.bind_text(3, target_status);
        hist.bind_int(4, ev.user_id > 0 ? ev.user_id : 1);
        hist.step_done();

        // Changelog
        collaboration::record_changelog(conn, eff_issue_id, ev.user_id > 0 ? ev.user_id : 1, "status", old_status, target_status);

        trace.push_back("[Action: transition_issue] Successfully transitioned issue " + item_key + " (" + std::to_string(eff_issue_id) + ") from '" + old_status + "' to '" + target_status + "'");
        return true;
    } else if (act_type == "add_comment") {
        std::string raw_body = action.get_string("body");
        std::string body = evaluate_smart_values(conn, raw_body, ev, eff_issue_id);
        if (body.empty()) {
            trace.push_back("[Action: add_comment] Comment body is empty: SKIPPED");
            return false;
        }
        if (eff_issue_id <= 0) return false;

        auto ins = conn.prepare("INSERT INTO comments (work_item_id, author_id, body) VALUES (?, ?, ?);");
        ins.bind_int(1, eff_issue_id);
        ins.bind_int(2, ev.user_id > 0 ? ev.user_id : 1);
        ins.bind_text(3, body);
        ins.step_done();
        int cid = static_cast<int>(conn.last_insert_rowid());

        collaboration::record_changelog(conn, eff_issue_id, ev.user_id > 0 ? ev.user_id : 1, "comment", "", "Added automated comment #" + std::to_string(cid));
        trace.push_back("[Action: add_comment] Added automated comment #" + std::to_string(cid) + " to issue #" + std::to_string(eff_issue_id));
        return true;
    } else if (act_type == "edit_field") {
        std::string field = action.get_string("field");
        std::string raw_val = action.get_string("value");
        std::string val = evaluate_smart_values(conn, raw_val, ev, eff_issue_id);

        if (eff_issue_id <= 0 || field.empty()) return false;

        std::string sql;
        if (field == "priority") sql = "UPDATE work_items SET priority = ?, updated_at = datetime('now') WHERE id = ?;";
        else if (field == "labels") sql = "UPDATE work_items SET labels = ?, updated_at = datetime('now') WHERE id = ?;";
        else if (field == "description") sql = "UPDATE work_items SET description = ?, updated_at = datetime('now') WHERE id = ?;";
        else if (field == "title" || field == "summary") sql = "UPDATE work_items SET title = ?, updated_at = datetime('now') WHERE id = ?;";
        else if (field == "severity") sql = "UPDATE work_items SET severity = ?, updated_at = datetime('now') WHERE id = ?;";
        else if (field == "assignee_id") {
            int ass_id = std::stoi(val);
            auto upd = conn.prepare("UPDATE work_items SET assignee_id = ?, updated_at = datetime('now') WHERE id = ?;");
            upd.bind_int(1, ass_id);
            upd.bind_int(2, eff_issue_id);
            upd.step_done();
            collaboration::record_changelog(conn, eff_issue_id, ev.user_id > 0 ? ev.user_id : 1, "assignee", "", val);
            trace.push_back("[Action: edit_field] Updated assignee_id to " + val + " for issue #" + std::to_string(eff_issue_id));
            return true;
        }

        if (!sql.empty()) {
            auto upd = conn.prepare(sql);
            upd.bind_text(1, val);
            upd.bind_int(2, eff_issue_id);
            upd.step_done();
            collaboration::record_changelog(conn, eff_issue_id, ev.user_id > 0 ? ev.user_id : 1, field, "", val);
            trace.push_back("[Action: edit_field] Updated field '" + field + "' to '" + val + "' for issue #" + std::to_string(eff_issue_id));
            return true;
        } else {
            trace.push_back("[Action: edit_field] Unsupported field '" + field + "': SKIPPED");
            return false;
        }
    } else if (act_type == "create_subtask") {
        if (eff_issue_id <= 0) return false;
        int proj_id = 1;
        std::string parent_key;
        {
            auto q = conn.prepare("SELECT project_id, key FROM work_items WHERE id = ?;");
            q.bind_int(1, eff_issue_id);
            if (q.step() == SQLITE_ROW) {
                proj_id = q.column_int(0);
                parent_key = q.column_str(1);
            }
        }

        std::string raw_title = action.get_string("title", "Automated Subtask for {{issue.key}}");
        std::string title = evaluate_smart_values(conn, raw_title, ev, eff_issue_id);
        std::string sub_type = action.get_string("type", "Sub-task");
        std::string priority = action.get_string("priority", "Medium");

        std::string proj_key = "JIRA";
        int next_seq = 1;
        {
            auto pq = conn.prepare("SELECT key, issue_seq FROM projects WHERE id = ?;");
            pq.bind_int(1, proj_id);
            if (pq.step() == SQLITE_ROW) {
                proj_key = pq.column_str(0);
                next_seq = pq.column_int(1) + 1;
            }
        }
        {
            auto upd = conn.prepare("UPDATE projects SET issue_seq = ? WHERE id = ?;");
            upd.bind_int(1, next_seq);
            upd.bind_int(2, proj_id);
            upd.step_done();
        }

        std::string new_key = proj_key + "-" + std::to_string(next_seq);
        auto ins = conn.prepare(R"SQL(
            INSERT INTO work_items (
                project_id, key, type, category, status, priority, title, description,
                parent_id, reporter_id, story_points, progress, board_order
            ) VALUES (?, ?, ?, 'Subtask', 'To Do', ?, ?, '', ?, ?, 0, 0, ?);
        )SQL");
        ins.bind_int(1, proj_id);
        ins.bind_text(2, new_key);
        ins.bind_text(3, sub_type);
        ins.bind_text(4, priority);
        ins.bind_text(5, title);
        ins.bind_int(6, eff_issue_id);
        ins.bind_int(7, ev.user_id > 0 ? ev.user_id : 1);
        ins.bind_int(8, next_seq * 10);
        ins.step_done();
        int new_sub_id = static_cast<int>(conn.last_insert_rowid());

        trace.push_back("[Action: create_subtask] Created subtask " + new_key + " (#" + std::to_string(new_sub_id) + ") under parent " + parent_key);
        return true;
    } else if (act_type == "create_issue") {
        int proj_id = action.get_int("project_id", ev.project_id > 0 ? ev.project_id : 1);
        std::string raw_title = action.get_string("title", "Automated Issue from {{issue.key}}");
        std::string title = evaluate_smart_values(conn, raw_title, ev, eff_issue_id);
        std::string raw_desc = action.get_string("description", "");
        std::string desc = evaluate_smart_values(conn, raw_desc, ev, eff_issue_id);
        std::string itype = action.get_string("type", "Task");
        std::string priority = action.get_string("priority", "Medium");

        std::string proj_key = "JIRA";
        int next_seq = 1;
        {
            auto pq = conn.prepare("SELECT key, issue_seq FROM projects WHERE id = ?;");
            pq.bind_int(1, proj_id);
            if (pq.step() == SQLITE_ROW) {
                proj_key = pq.column_str(0);
                next_seq = pq.column_int(1) + 1;
            }
        }
        {
            auto upd = conn.prepare("UPDATE projects SET issue_seq = ? WHERE id = ?;");
            upd.bind_int(1, next_seq);
            upd.bind_int(2, proj_id);
            upd.step_done();
        }

        std::string new_key = proj_key + "-" + std::to_string(next_seq);
        auto ins = conn.prepare(R"SQL(
            INSERT INTO work_items (
                project_id, key, type, category, status, priority, title, description,
                reporter_id, story_points, progress, board_order
            ) VALUES (?, ?, ?, 'Feature', 'To Do', ?, ?, ?, ?, 0, 0, ?);
        )SQL");
        ins.bind_int(1, proj_id);
        ins.bind_text(2, new_key);
        ins.bind_text(3, itype);
        ins.bind_text(4, priority);
        ins.bind_text(5, title);
        ins.bind_text(6, desc);
        ins.bind_int(7, ev.user_id > 0 ? ev.user_id : 1);
        ins.bind_int(8, next_seq * 10);
        ins.step_done();
        int new_item_id = static_cast<int>(conn.last_insert_rowid());

        trace.push_back("[Action: create_issue] Created issue " + new_key + " (#" + std::to_string(new_item_id) + ")");
        return true;
    } else if (act_type == "http_request" || act_type == "send_webhook") {
        std::string raw_url = action.get_string("url");
        std::string url = evaluate_smart_values(conn, raw_url, ev, eff_issue_id);
        std::string method = action.get_string("method", "POST");
        std::string raw_body = action.get_string("body", "{}");
        std::string body = evaluate_smart_values(conn, raw_body, ev, eff_issue_id);

        trace.push_back("[Action: http_request] Dispatched " + method + " to '" + url + "' with payload: " + body + " (Simulated 200 OK)");
        return true;
    } else if (act_type == "send_notification") {
        std::string recipient = action.get_string("recipient", "reporter");
        std::string raw_msg = action.get_string("message");
        std::string msg = evaluate_smart_values(conn, raw_msg, ev, eff_issue_id);

        trace.push_back("[Action: send_notification] Notification queued for recipient '" + recipient + "': " + msg);
        return true;
    } else if (act_type == "branch") {
        std::string target = action.get_string("target", "subtasks");
        auto nested_acts = action.get_child("actions");

        if (target == "subtasks" || target == "all_subtasks") {
            if (eff_issue_id <= 0) return true;
            auto q = conn.prepare("SELECT id, key FROM work_items WHERE parent_id = ? ORDER BY id ASC;");
            q.bind_int(1, eff_issue_id);
            std::vector<std::pair<int, std::string>> children;
            while (q.step() == SQLITE_ROW) {
                children.emplace_back(q.column_int(0), q.column_str(1));
            }
            trace.push_back("[Branch: subtasks] Found " + std::to_string(children.size()) + " child subtask(s) under issue #" + std::to_string(eff_issue_id));
            for (const auto &child : children) {
                trace.push_back("  -> Branching to subtask " + child.second + " (#" + std::to_string(child.first) + ")");
                execute_actions_list(conn, nested_acts, ev, child.first, trace);
            }
            return true;
        } else if (target == "parent" || target == "parent_issue") {
            if (eff_issue_id <= 0) return true;
            int pid = 0;
            std::string pkey;
            {
                auto q = conn.prepare(R"SQL(
                    SELECT p.id, p.key FROM work_items c
                    INNER JOIN work_items p ON p.id = c.parent_id
                    WHERE c.id = ?;
                )SQL");
                q.bind_int(1, eff_issue_id);
                if (q.step() == SQLITE_ROW) {
                    pid = q.column_int(0);
                    pkey = q.column_str(1);
                }
            }
            if (pid > 0) {
                trace.push_back("[Branch: parent] Branching to parent issue " + pkey + " (#" + std::to_string(pid) + ")");
                execute_actions_list(conn, nested_acts, ev, pid, trace);
            } else {
                trace.push_back("[Branch: parent] Issue #" + std::to_string(eff_issue_id) + " has no parent: SKIPPED");
            }
            return true;
        } else if (target == "linked_issues") {
            if (eff_issue_id <= 0) return true;
            auto q = conn.prepare(R"SQL(
                SELECT w.id, w.key FROM work_item_links l
                INNER JOIN work_items w ON w.id = CASE WHEN l.source_item_id = ? THEN l.target_item_id ELSE l.source_item_id END
                WHERE l.source_item_id = ? OR l.target_item_id = ?;
            )SQL");
            q.bind_int(1, eff_issue_id);
            q.bind_int(2, eff_issue_id);
            q.bind_int(3, eff_issue_id);
            std::vector<std::pair<int, std::string>> linked;
            while (q.step() == SQLITE_ROW) {
                linked.emplace_back(q.column_int(0), q.column_str(1));
            }
            trace.push_back("[Branch: linked_issues] Found " + std::to_string(linked.size()) + " linked issue(s)");
            for (const auto &l : linked) {
                trace.push_back("  -> Branching to linked issue " + l.second + " (#" + std::to_string(l.first) + ")");
                execute_actions_list(conn, nested_acts, ev, l.first, trace);
            }
            return true;
        } else if (target == "jql") {
            std::string jql_query = action.get_string("jql");
            trace.push_back("[Branch: jql] Running JQL '" + jql_query + "'");
            try {
                auto ast = jql::JqlParser::parse_query(jql_query);
                jql::SqlContext ctx(conn,
                                    ev.user_id > 0 ? ev.user_id : 1,
                                    !ev.user_username.empty() ? ev.user_username : "admin",
                                    !ev.user_display_name.empty() ? ev.user_display_name : "System Administrator");
                std::string where_sql = ast.to_where_sql(ctx);
                std::string q_sql = "SELECT w.id, w.key FROM work_items w WHERE " + where_sql + " LIMIT 50;";
                auto stmt = conn.prepare(q_sql);
                for (size_t i = 0; i < ctx.params.size(); ++i) {
                    stmt.bind_text(static_cast<int>(i + 1), ctx.params[i]);
                }
                std::vector<std::pair<int, std::string>> matched;
                while (stmt.step() == SQLITE_ROW) {
                    matched.emplace_back(stmt.column_int(0), stmt.column_str(1));
                }
                trace.push_back("[Branch: jql] Found " + std::to_string(matched.size()) + " issue(s) matching JQL");
                for (const auto &m : matched) {
                    trace.push_back("  -> Branching to " + m.second + " (#" + std::to_string(m.first) + ")");
                    execute_actions_list(conn, nested_acts, ev, m.first, trace);
                }
            } catch (const std::exception &ex) {
                trace.push_back(std::string("[Branch: jql] Error executing JQL: ") + ex.what());
            }
            return true;
        }
    }

    trace.push_back("[Action: unknown] Unhandled action type '" + act_type + "': SKIPPED");
    return false;
}

// Single Rule Runner
inline bool run_rule(db::sqlite_db &conn,
                    int rule_id,
                    const std::string &rule_name,
                    const std::string &trigger_type,
                    const std::string &trigger_config_json,
                    const std::string &conditions_json,
                    const std::string &actions_json,
                    const AutomationEvent &ev,
                    std::string &out_status,
                    double &out_duration_ms,
                    std::string &out_trace) {
    auto t0 = std::chrono::steady_clock::now();
    std::vector<std::string> trace;
    trace.push_back("[Trigger] Event: '" + event_type_to_string(ev.type) + "', Rule: '" + rule_name + "' (#" + std::to_string(rule_id) + ")");
    if (ev.issue_id > 0) {
        trace.push_back("[Trigger Issue] Key: " + ev.issue_key + ", ID: " + std::to_string(ev.issue_id));
    }

    // 1. Trigger config filter check
    auto trig_cfg = json::json_value::parse(trigger_config_json);
    if (trig_cfg) {
        if (ev.type == EventType::StatusChanged) {
            std::string req_to = trig_cfg.get_string("to_status");
            std::string req_from = trig_cfg.get_string("from_status");
            if (!req_to.empty() && req_to != "*" && req_to != ev.to_status) {
                trace.push_back("[Trigger Filter] to_status mismatch: expected '" + req_to + "', got '" + ev.to_status + "': MISMATCH");
                out_status = "NO_ACTIONS";
                auto t1 = std::chrono::steady_clock::now();
                out_duration_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
                std::ostringstream ss;
                for (const auto &line : trace) ss << line << "\n";
                out_trace = ss.str();
                return false;
            }
            if (!req_from.empty() && req_from != "*" && req_from != ev.from_status) {
                trace.push_back("[Trigger Filter] from_status mismatch: expected '" + req_from + "', got '" + ev.from_status + "': MISMATCH");
                out_status = "NO_ACTIONS";
                auto t1 = std::chrono::steady_clock::now();
                out_duration_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
                std::ostringstream ss;
                for (const auto &line : trace) ss << line << "\n";
                out_trace = ss.str();
                return false;
            }
        } else if (ev.type == EventType::FieldValueChanged) {
            std::string req_field = trig_cfg.get_string("field_name");
            if (!req_field.empty() && req_field != "*" && req_field != ev.field_name) {
                trace.push_back("[Trigger Filter] field_name mismatch: expected '" + req_field + "', got '" + ev.field_name + "': MISMATCH");
                out_status = "NO_ACTIONS";
                auto t1 = std::chrono::steady_clock::now();
                out_duration_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
                std::ostringstream ss;
                for (const auto &line : trace) ss << line << "\n";
                out_trace = ss.str();
                return false;
            }
        }
    }

    // 2. Evaluate conditions
    auto conds = json::json_value::parse(conditions_json);
    if (conds && conds.is_array()) {
        size_t len = conds.array_length();
        for (size_t i = 0; i < len; ++i) {
            auto c = conds.array_get(i);
            if (!evaluate_condition(conn, c, ev, ev.issue_id, trace)) {
                trace.push_back("[Condition] Condition check failed. Halting rule execution: NO_ACTIONS");
                out_status = "NO_ACTIONS";
                auto t1 = std::chrono::steady_clock::now();
                out_duration_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
                std::ostringstream ss;
                for (const auto &line : trace) ss << line << "\n";
                out_trace = ss.str();
                return false;
            }
        }
    }

    // 3. Execute actions
    auto acts = json::json_value::parse(actions_json);
    bool any_executed = false;
    if (acts && acts.is_array()) {
        size_t len = acts.array_length();
        for (size_t i = 0; i < len; ++i) {
            auto a = acts.array_get(i);
            execute_action(conn, a, ev, ev.issue_id, trace);
            any_executed = true;
        }
    }

    out_status = any_executed ? "SUCCESS" : "NO_ACTIONS";
    auto t1 = std::chrono::steady_clock::now();
    out_duration_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    trace.push_back("[Result] Execution completed with status: " + out_status + " in " + std::to_string(out_duration_ms) + " ms");

    std::ostringstream ss;
    for (const auto &line : trace) ss << line << "\n";
    out_trace = ss.str();
    return (out_status == "SUCCESS");
}

// Event Dispatcher: publish/subscribe dispatcher
inline void dispatch_event(db::sqlite_db &conn, const AutomationEvent &ev) {
    if (g_automation_depth >= MAX_AUTOMATION_DEPTH) {
        std::cerr << "[automation_engine] Max automation depth (" << MAX_AUTOMATION_DEPTH << ") reached. Halting recursion." << std::endl;
        return;
    }
    g_automation_depth++;

    try {
        std::string ev_type_str = event_type_to_string(ev.type);

        // Find active rules matching this trigger type and project (or global)
        std::string sql = R"SQL(
            SELECT id, name, trigger_type, trigger_config, conditions_json, actions_json
            FROM automation_rules
            WHERE is_enabled = 1
              AND (trigger_type = ? OR trigger_type = 'manual')
              AND (project_id IS NULL OR ? = 0 OR project_id = ?);
        )SQL";
        auto stmt = conn.prepare(sql);
        stmt.bind_text(1, ev_type_str);
        stmt.bind_int(2, ev.project_id);
        stmt.bind_int(3, ev.project_id);

        struct RuleCandidate {
            int id;
            std::string name;
            std::string trigger_type;
            std::string trigger_config;
            std::string conditions_json;
            std::string actions_json;
        };
        std::vector<RuleCandidate> candidates;
        while (stmt.step() == SQLITE_ROW) {
            RuleCandidate rc;
            rc.id = stmt.column_int(0);
            rc.name = stmt.column_str(1);
            rc.trigger_type = stmt.column_str(2);
            rc.trigger_config = stmt.column_str(3);
            rc.conditions_json = stmt.column_str(4);
            rc.actions_json = stmt.column_str(5);
            candidates.push_back(std::move(rc));
        }

        for (const auto &cand : candidates) {
            std::string status, trace;
            double duration_ms = 0.0;
            try {
                run_rule(conn, cand.id, cand.name, cand.trigger_type, cand.trigger_config,
                         cand.conditions_json, cand.actions_json, ev,
                         status, duration_ms, trace);
            } catch (const std::exception &ex) {
                status = "ERROR";
                trace += std::string("\n[Fatal Exception] ") + ex.what();
            }

            // Record audit log
            try {
                auto ins_log = conn.prepare(R"SQL(
                    INSERT INTO automation_logs (rule_id, rule_name, trigger_type, issue_id, issue_key, status, duration_ms, log_trace)
                    VALUES (?, ?, ?, ?, ?, ?, ?, ?);
                )SQL");
                ins_log.bind_int(1, cand.id);
                ins_log.bind_text(2, cand.name);
                ins_log.bind_text(3, ev_type_str);
                ins_log.bind_optional_int(4, ev.issue_id);
                ins_log.bind_text(5, ev.issue_key);
                ins_log.bind_text(6, status);
                ins_log.bind_double(7, duration_ms);
                ins_log.bind_text(8, trace);
                ins_log.step_done();

                // Update rule execution count and last executed
                auto upd_rule = conn.prepare("UPDATE automation_rules SET execution_count = execution_count + 1, last_executed_at = datetime('now') WHERE id = ?;");
                upd_rule.bind_int(1, cand.id);
                upd_rule.step_done();
            } catch (const std::exception &ex) {
                std::cerr << "[automation_engine] Error inserting audit log: " << ex.what() << std::endl;
            }
        }
    } catch (const std::exception &ex) {
        std::cerr << "[automation_engine] Exception in dispatch_event: " << ex.what() << std::endl;
    }

    g_automation_depth--;
}

} // namespace automation
