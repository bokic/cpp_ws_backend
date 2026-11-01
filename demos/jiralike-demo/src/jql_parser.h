#pragma once

#include "jql_lexer.h"
#include "raii_sqlite.h"
#include "time_tracking.h"
#include <algorithm>
#include <cctype>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace jql {

enum class Precedence {
    Lowest = 0,
    Or = 1,         // OR
    And = 2,        // AND
    Not = 3,        // NOT
    Comparison = 4  // =, !=, >, <, IN, ~, WAS, CHANGED
};

struct CustomFieldMeta {
    int id = 0;
    std::string field_key;
    std::string name;
    std::string field_type;
    std::string formula_expr;
};

struct OperandValue {
    bool is_function = false;
    bool is_empty = false;
    std::string value;
    std::vector<std::string> func_args;

    std::string to_string() const {
        if (is_empty) return "EMPTY";
        if (!is_function) return value;
        std::ostringstream oss;
        oss << value << "(";
        for (size_t i = 0; i < func_args.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << "\"" << func_args[i] << "\"";
        }
        oss << ")";
        return oss.str();
    }
};

struct HistoryPredicate {
    std::string kind; // "FROM", "TO", "BY", "BEFORE", "AFTER", "ON", "DURING"
    std::vector<OperandValue> values;
};

struct OrderByItem {
    std::string field;
    bool descending = false;
};

class SqlContext {
public:
    db::sqlite_db &conn;
    int current_user_id = 0;
    std::string current_username;
    std::string current_display_name;

    std::vector<std::string> params;
    bool uses_fts5 = false;
    std::string primary_fts_query;

    SqlContext(db::sqlite_db &db_conn,
               int uid = 0,
               std::string uname = "",
               std::string dname = "")
        : conn(db_conn),
          current_user_id(uid),
          current_username(std::move(uname)),
          current_display_name(std::move(dname)) {
        load_custom_fields();
    }

    std::string add_param(const std::string &val) {
        params.push_back(val);
        return "?";
    }

    const CustomFieldMeta *resolve_custom_field(const std::string &raw_field) const {
        std::string lower = to_lower_ascii(raw_field);
        if (lower.starts_with("cf[") && lower.ends_with("]")) {
            std::string inner = lower.substr(3, lower.size() - 4);
            auto it = cf_by_key_.find(inner);
            if (it != cf_by_key_.end()) return &custom_fields_[it->second];
            int id = std::atoi(inner.c_str());
            if (id > 0) {
                auto it_id = cf_by_id_.find(id);
                if (it_id != cf_by_id_.end()) return &custom_fields_[it_id->second];
            }
        }
        auto it_key = cf_by_key_.find(lower);
        if (it_key != cf_by_key_.end()) return &custom_fields_[it_key->second];

        auto it_name = cf_by_name_.find(lower);
        if (it_name != cf_by_name_.end()) return &custom_fields_[it_name->second];

        return nullptr;
    }

    // Evaluates a JQL operand (either a literal or a built-in function) into one or more resolved string values
    std::vector<std::string> resolve_operand(const OperandValue &op, const std::string &field_hint = "") {
        if (op.is_empty) {
            return {""};
        }
        if (!op.is_function) {
            return {op.value};
        }

        std::string fn = to_lower_ascii(op.value);
        if (fn == "currentuser") {
            if (current_user_id > 0) {
                return {std::to_string(current_user_id)};
            }
            if (!current_username.empty()) {
                return {current_username};
            }
            // Fallback to first Admin user in demo workspace if unauthenticated
            auto st = conn.prepare("SELECT id FROM users ORDER BY id ASC LIMIT 1;");
            if (st.step() == SQLITE_ROW) {
                return {std::to_string(st.column_int(0))};
            }
            return {"1"};
        }

        if (fn == "now" || fn == "startofday" || fn == "endofday" ||
            fn == "startofweek" || fn == "endofweek" ||
            fn == "startofmonth" || fn == "endofmonth" ||
            fn == "startofyear" || fn == "endofyear") {
            std::string offset = op.func_args.empty() ? "" : op.func_args[0];
            return {eval_date_function(fn, offset, field_hint)};
        }

        if (fn == "membersof") {
            std::vector<std::string> uids;
            if (!op.func_args.empty()) {
                std::string grp = op.func_args[0];
                auto st = conn.prepare(R"SQL(
                    SELECT CAST(id AS TEXT) FROM users
                    WHERE lower(role) = lower(?) OR lower(username) = lower(?) OR lower(display_name) = lower(?);
                )SQL");
                st.bind_text(1, grp);
                st.bind_text(2, grp);
                st.bind_text(3, grp);
                while (st.step() == SQLITE_ROW) {
                    uids.push_back(st.column_str(0));
                }
            }
            if (uids.empty()) uids.push_back("-999999");
            return uids;
        }

        if (fn == "opensprints") {
            std::vector<std::string> sids;
            auto st = conn.prepare("SELECT CAST(id AS TEXT) FROM sprints WHERE lower(status) = 'active';");
            while (st.step() == SQLITE_ROW) {
                sids.push_back(st.column_str(0));
            }
            if (sids.empty()) sids.push_back("-999999");
            return sids;
        }

        if (fn == "closedsprints") {
            std::vector<std::string> sids;
            auto st = conn.prepare("SELECT CAST(id AS TEXT) FROM sprints WHERE lower(status) IN ('closed', 'completed');");
            while (st.step() == SQLITE_ROW) {
                sids.push_back(st.column_str(0));
            }
            if (sids.empty()) sids.push_back("-999999");
            return sids;
        }

        if (fn == "futuresprints") {
            std::vector<std::string> sids;
            auto st = conn.prepare("SELECT CAST(id AS TEXT) FROM sprints WHERE lower(status) IN ('planned', 'future');");
            while (st.step() == SQLITE_ROW) {
                sids.push_back(st.column_str(0));
            }
            if (sids.empty()) sids.push_back("-999999");
            return sids;
        }

        if (fn == "subtaskissuetypes") {
            std::vector<std::string> types = {"Subtask", "Sub-task"};
            auto st = conn.prepare("SELECT name FROM issue_types WHERE is_subtask = 1;");
            while (st.step() == SQLITE_ROW) {
                types.push_back(st.column_str(0));
            }
            return types;
        }

        if (fn == "standardissuetypes") {
            std::vector<std::string> types = {"Story", "Task", "Bug", "Issue"};
            auto st = conn.prepare("SELECT name FROM issue_types WHERE is_subtask = 0;");
            while (st.step() == SQLITE_ROW) {
                types.push_back(st.column_str(0));
            }
            return types;
        }

        if (fn == "linkedissues") {
            std::vector<std::string> ids;
            if (!op.func_args.empty()) {
                std::string target_key = op.func_args[0];
                std::string link_type = op.func_args.size() > 1 ? op.func_args[1] : "";
                if (link_type.empty()) {
                    auto st = conn.prepare(R"SQL(
                        SELECT CAST(w2.id AS TEXT), w2.key
                        FROM work_item_links l
                        JOIN work_items w1 ON (w1.id = l.source_item_id OR w1.id = l.target_item_id)
                        JOIN work_items w2 ON (w2.id = CASE WHEN w1.id = l.source_item_id THEN l.target_item_id ELSE l.source_item_id END)
                        WHERE upper(w1.key) = upper(?) OR CAST(w1.id AS TEXT) = ?;
                    )SQL");
                    st.bind_text(1, target_key);
                    st.bind_text(2, target_key);
                    while (st.step() == SQLITE_ROW) {
                        ids.push_back(st.column_str(1));
                    }
                } else {
                    auto st = conn.prepare(R"SQL(
                        SELECT CAST(w2.id AS TEXT), w2.key
                        FROM work_item_links l
                        JOIN work_items w1 ON (w1.id = l.source_item_id OR w1.id = l.target_item_id)
                        JOIN work_items w2 ON (w2.id = CASE WHEN w1.id = l.source_item_id THEN l.target_item_id ELSE l.source_item_id END)
                        WHERE (upper(w1.key) = upper(?) OR CAST(w1.id AS TEXT) = ?)
                          AND lower(l.link_type) = lower(?);
                    )SQL");
                    st.bind_text(1, target_key);
                    st.bind_text(2, target_key);
                    st.bind_text(3, link_type);
                    while (st.step() == SQLITE_ROW) {
                        ids.push_back(st.column_str(1));
                    }
                }
            }
            if (ids.empty()) ids.push_back("__NONE__");
            return ids;
        }

        return {op.value};
    }

    static std::string sanitize_fts5_query(std::string_view raw) {
        std::string token;
        std::vector<std::string> tokens;
        for (char c : raw) {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
                token.push_back(c);
            } else if (!token.empty()) {
                tokens.push_back(token);
                token.clear();
            }
        }
        if (!token.empty()) {
            tokens.push_back(token);
        }
        if (tokens.empty()) {
            return "\"\"";
        }
        std::ostringstream oss;
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (i > 0) oss << " AND ";
            oss << "\"" << tokens[i] << "\"*";
        }
        return oss.str();
    }

private:
    std::vector<CustomFieldMeta> custom_fields_;
    std::unordered_map<std::string, size_t> cf_by_key_;
    std::unordered_map<std::string, size_t> cf_by_name_;
    std::unordered_map<int, size_t> cf_by_id_;

    void load_custom_fields() {
        try {
            auto st = conn.prepare("SELECT id, field_key, name, field_type, COALESCE(formula_expr, '') FROM custom_fields;");
            while (st.step() == SQLITE_ROW) {
                CustomFieldMeta meta;
                meta.id = st.column_int(0);
                meta.field_key = st.column_str(1);
                meta.name = st.column_str(2);
                meta.field_type = st.column_str(3);
                meta.formula_expr = st.column_str(4);
                size_t idx = custom_fields_.size();
                custom_fields_.push_back(meta);
                cf_by_id_[meta.id] = idx;
                cf_by_key_[to_lower_ascii(meta.field_key)] = idx;
                cf_by_name_[to_lower_ascii(meta.name)] = idx;
            }
        } catch (...) {
            // Ignore if custom_fields table does not exist yet
        }
    }

    static std::string parse_jira_offset_to_sqlite_modifier(const std::string &offset) {
        if (offset.empty()) return "";
        std::string s = offset;
        s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c); }), s.end());
        if (s.empty()) return "";

        char sign = '+';
        size_t idx = 0;
        if (s[0] == '+' || s[0] == '-') {
            sign = s[0];
            idx = 1;
        }
        std::string num_str;
        while (idx < s.size() && (std::isdigit(static_cast<unsigned char>(s[idx])) || s[idx] == '.')) {
            num_str.push_back(s[idx]);
            ++idx;
        }
        if (num_str.empty()) return "";
        int val = std::atoi(num_str.c_str());
        std::string unit = to_lower_ascii(s.substr(idx));

        if (unit == "w" || unit == "week" || unit == "weeks") {
            val *= 7;
            unit = "days";
        } else if (unit == "d" || unit == "day" || unit == "days" || unit.empty()) {
            unit = "days";
        } else if (unit == "h" || unit == "hr" || unit == "hour" || unit == "hours") {
            unit = "hours";
        } else if (unit == "m" || unit == "min" || unit == "minute" || unit == "minutes") {
            unit = "minutes";
        } else if (unit == "mo" || unit == "month" || unit == "months") {
            unit = "months";
        } else if (unit == "y" || unit == "yr" || unit == "year" || unit == "years") {
            unit = "years";
        } else {
            unit = "days";
        }

        return std::string(1, sign) + std::to_string(val) + " " + unit;
    }

    std::string eval_date_function(const std::string &fn, const std::string &offset, const std::string &field_hint) {
        std::string mod = parse_jira_offset_to_sqlite_modifier(offset);
        bool date_only = (field_hint == "due" || field_hint == "duedate" || field_hint == "due_date" ||
                          field_hint == "startdate" || field_hint == "start_date");
        std::string sql_fn = date_only ? "date" : "datetime";

        std::vector<std::string> mods;
        if (fn == "startofday") {
            mods.push_back("start of day");
        } else if (fn == "endofday") {
            mods.push_back("start of day");
            mods.push_back("+1 day");
            mods.push_back("-1 second");
        } else if (fn == "startofweek") {
            // Monday start of current week or 7 days window
            mods.push_back("-6 days");
            mods.push_back("start of day");
        } else if (fn == "endofweek") {
            mods.push_back("weekday 0");
            mods.push_back("start of day");
            mods.push_back("+1 day");
            mods.push_back("-1 second");
        } else if (fn == "startofmonth") {
            mods.push_back("start of month");
        } else if (fn == "endofmonth") {
            mods.push_back("start of month");
            mods.push_back("+1 month");
            mods.push_back("-1 second");
        } else if (fn == "startofyear") {
            mods.push_back("start of year");
        } else if (fn == "endofyear") {
            mods.push_back("start of year");
            mods.push_back("+1 year");
            mods.push_back("-1 second");
        }
        if (!mod.empty()) {
            mods.push_back(mod);
        }

        std::ostringstream q;
        q << "SELECT " << sql_fn << "('now'";
        for (size_t i = 0; i < mods.size(); ++i) {
            q << ", ?";
        }
        q << ");";

        auto st = conn.prepare(q.str());
        for (size_t i = 0; i < mods.size(); ++i) {
            st.bind_text(static_cast<int>(i + 1), mods[i]);
        }
        if (st.step() == SQLITE_ROW) {
            return st.column_str(0);
        }
        return "";
    }
};

struct JqlNode {
    virtual ~JqlNode() = default;
    virtual std::string to_sql(SqlContext &ctx) const = 0;
    virtual std::string to_debug_string() const = 0;
};

struct BinaryOpNode : JqlNode {
    std::string op; // "AND", "OR"
    std::unique_ptr<JqlNode> left;
    std::unique_ptr<JqlNode> right;

    BinaryOpNode(std::string o, std::unique_ptr<JqlNode> l, std::unique_ptr<JqlNode> r)
        : op(std::move(o)), left(std::move(l)), right(std::move(r)) {}

    std::string to_sql(SqlContext &ctx) const override {
        std::string l = left->to_sql(ctx);
        std::string r = right->to_sql(ctx);
        return "(" + l + " " + op + " " + r + ")";
    }

    std::string to_debug_string() const override {
        return "(" + left->to_debug_string() + " " + op + " " + right->to_debug_string() + ")";
    }
};

struct UnaryNotNode : JqlNode {
    std::unique_ptr<JqlNode> operand;

    explicit UnaryNotNode(std::unique_ptr<JqlNode> expr) : operand(std::move(expr)) {}

    std::string to_sql(SqlContext &ctx) const override {
        return "(NOT " + operand->to_sql(ctx) + ")";
    }

    std::string to_debug_string() const override {
        return "(NOT " + operand->to_debug_string() + ")";
    }
};

struct ClauseNode : JqlNode {
    std::string field;
    std::string op; // "=", "!=", ">", ">=", "<", "<=", "IN", "NOT IN", "~", "!~", "IS", "IS NOT", "WAS", "WAS IN", "WAS NOT", "WAS NOT IN", "CHANGED"
    std::vector<std::string> values;
    std::vector<OperandValue> operands;
    std::vector<HistoryPredicate> history_predicates;

    std::string to_debug_string() const override {
        std::ostringstream oss;
        oss << field << " " << op;
        if (!operands.empty()) {
            oss << " ";
            if (operands.size() > 1 || op == "IN" || op == "NOT IN" || op == "WAS IN" || op == "WAS NOT IN") {
                oss << "(";
                for (size_t i = 0; i < operands.size(); ++i) {
                    if (i > 0) oss << ", ";
                    oss << operands[i].to_string();
                }
                oss << ")";
            } else {
                oss << operands[0].to_string();
            }
        }
        for (const auto &hp : history_predicates) {
            oss << " " << hp.kind << " ";
            for (size_t i = 0; i < hp.values.size(); ++i) {
                if (i > 0) oss << ", ";
                oss << hp.values[i].to_string();
            }
        }
        return oss.str();
    }

    std::string to_sql(SqlContext &ctx) const override {
        std::string f = to_lower_ascii(field);

        // Expand all operands (including JQL functions like currentUser(), openSprints(), now("-7d"))
        std::vector<std::string> resolved_vals;
        bool has_empty_operand = false;
        for (const auto &opnd : operands) {
            if (opnd.is_empty || to_upper_ascii(opnd.value) == "EMPTY" || to_upper_ascii(opnd.value) == "NULL") {
                has_empty_operand = true;
            }
            auto expanded = ctx.resolve_operand(opnd, f);
            for (auto &v : expanded) {
                resolved_vals.push_back(std::move(v));
            }
        }
        if (resolved_vals.empty() && !values.empty()) {
            resolved_vals = values;
        }

        // 1. Historical Operators: WAS, WAS IN, WAS NOT, WAS NOT IN, CHANGED
        if (op == "WAS" || op == "WAS IN" || op == "WAS NOT" || op == "WAS NOT IN") {
            return compile_was_clause(ctx, f, resolved_vals);
        }
        if (op == "CHANGED") {
            return compile_changed_clause(ctx, f);
        }

        // 2. IS EMPTY / IS NOT EMPTY (or = EMPTY / != EMPTY)
        if (op == "IS" || (op == "=" && has_empty_operand)) {
            return compile_empty_check(ctx, f, true);
        }
        if (op == "IS NOT" || (op == "!=" && has_empty_operand)) {
            return compile_empty_check(ctx, f, false);
        }

        // 3. Check if field is a Custom Field (cf[...] or matching custom_fields key/name)
        if (const CustomFieldMeta *cf = ctx.resolve_custom_field(field)) {
            return compile_custom_field_clause(ctx, *cf, resolved_vals);
        }

        // 4. Full-Text Search field: text ~ "query"
        if (f == "text") {
            std::string term = resolved_vals.empty() ? "" : resolved_vals[0];
            bool neg = (op == "!~" || op == "!=");
            return compile_fts_clause(ctx, term, neg);
        }

        // 5. Multi-value relationship fields: component, fixVersion, affectedVersion
        if (f == "component" || f == "components") {
            return compile_component_clause(ctx, resolved_vals);
        }
        if (f == "fixversion" || f == "fixversions" || f == "fix_version") {
            return compile_version_clause(ctx, "fix", resolved_vals);
        }
        if (f == "affectedversion" || f == "affectedversions" || f == "affectsersion" ||
            f == "affectsversion" || f == "affectsversions" || f == "affects_version") {
            return compile_version_clause(ctx, "affects", resolved_vals);
        }

        // 6. Comments search: comment ~ "..."
        if (f == "comment" || f == "comments") {
            std::string term = resolved_vals.empty() ? "" : resolved_vals[0];
            bool neg = (op == "!~" || op == "!=");
            std::string p = ctx.add_param("%" + term + "%");
            std::string exists_sql = "EXISTS (SELECT 1 FROM comments cm WHERE cm.work_item_id = w.id AND cm.body LIKE " + p + ")";
            return neg ? "(NOT " + exists_sql + ")" : exists_sql;
        }

        // 7. Project field: project = "JIRA" or project = 1
        if (f == "project") {
            return compile_multi_match_clause(
                ctx,
                resolved_vals,
                [](SqlContext &c, const std::string &val) {
                    std::string p1 = c.add_param(val);
                    std::string p2 = c.add_param(val);
                    std::string p3 = c.add_param(val);
                    return "EXISTS (SELECT 1 FROM projects pr WHERE pr.id = w.project_id AND (upper(pr.key) = upper(" +
                           p1 + ") OR lower(pr.name) = lower(" + p2 + ") OR CAST(pr.id AS TEXT) = " + p3 + "))";
                });
        }

        // 8. Assignee / Reporter user fields
        if (f == "assignee" || f == "assignee_id") {
            return compile_user_clause(ctx, "w.assignee_id", "ua", resolved_vals);
        }
        if (f == "reporter" || f == "reporter_id" || f == "creator") {
            return compile_user_clause(ctx, "w.reporter_id", "ur", resolved_vals);
        }

        // 9. Sprint field: sprint = 1 or sprint = "Sprint 1" or sprint IN openSprints()
        if (f == "sprint" || f == "sprint_id") {
            return compile_multi_match_clause(
                ctx,
                resolved_vals,
                [](SqlContext &c, const std::string &val) {
                    std::string p1 = c.add_param(val);
                    std::string p2 = c.add_param(val);
                    return "(CAST(w.sprint_id AS TEXT) = " + p1 + " OR lower(COALESCE(sp.name, '')) = lower(" + p2 + "))";
                });
        }

        // 10. Resolution field: resolution = "Fixed" or resolution = "Unresolved"
        if (f == "resolution" || f == "resolution_id") {
            return compile_multi_match_clause(
                ctx,
                resolved_vals,
                [](SqlContext &c, const std::string &val) {
                    if (to_lower_ascii(val) == "unresolved" || to_lower_ascii(val) == "none") {
                        return std::string("(w.resolution_id IS NULL OR w.resolution_id = 0)");
                    }
                    std::string p1 = c.add_param(val);
                    std::string p2 = c.add_param(val);
                    return "(lower(COALESCE(res.name, '')) = lower(" + p1 + ") OR CAST(w.resolution_id AS TEXT) = " + p2 + ")";
                });
        }

        // 10b. Issue Security Level: security = "Public" or securityLevel = "Confidential / Security Vulnerability"
        if (f == "security" || f == "securitylevel" || f == "security_level" || f == "security_level_id") {
            return compile_multi_match_clause(
                ctx,
                resolved_vals,
                [](SqlContext &c, const std::string &val) {
                    if (to_lower_ascii(val) == "unsecured" || to_lower_ascii(val) == "none" || to_lower_ascii(val) == "empty" || to_lower_ascii(val) == "null") {
                        return std::string("(w.security_level_id IS NULL OR w.security_level_id = 0)");
                    }
                    std::string p1 = c.add_param(val);
                    std::string p2 = c.add_param(val);
                    return "(lower(COALESCE(isl.name, '')) = lower(" + p1 + ") OR CAST(w.security_level_id AS TEXT) = " + p2 + ")";
                });
        }

        // 11. Parent / Epic field
        if (f == "parent" || f == "parent_id" || f == "epic" || f == "parentepic" || f == "epiclink") {
            return compile_multi_match_clause(
                ctx,
                resolved_vals,
                [](SqlContext &c, const std::string &val) {
                    std::string p1 = c.add_param(val);
                    std::string p2 = c.add_param(val);
                    return "(upper(COALESCE(wp.key, '')) = upper(" + p1 + ") OR CAST(w.parent_id AS TEXT) = " + p2 + ")";
                });
        }

        // 12. Status Category field: statusCategory = "In Progress"
        if (f == "statuscategory" || f == "status_category") {
            return compile_multi_match_clause(
                ctx,
                resolved_vals,
                [](SqlContext &c, const std::string &val) {
                    std::string p = c.add_param(val);
                    return "EXISTS (SELECT 1 FROM workflow_statuses ws WHERE lower(ws.name) = lower(w.status) AND lower(ws.category) = lower(" + p + "))";
                });
        }

        // 13. Priority relational comparisons: priority > "Medium"
        if (f == "priority" && (op == ">" || op == ">=" || op == "<" || op == "<=")) {
            std::string val = resolved_vals.empty() ? "Medium" : resolved_vals[0];
            int target_rank = priority_rank(val);
            std::string rank_expr = sql_priority_rank_expr("w.priority");
            return "(" + rank_expr + " " + op + " " + std::to_string(target_rank) + ")";
        }

        // 13b. Time Tracking & Worklog fields: originalEstimate, remainingEstimate, timeSpent, worklogAuthor, worklogComment
        if (f == "worklogauthor" || f == "worklog_author") {
            return compile_multi_match_clause(
                ctx,
                resolved_vals,
                [](SqlContext &c, const std::string &val) {
                    std::string p1 = c.add_param(val);
                    std::string p2 = c.add_param(val);
                    std::string p3 = c.add_param(val);
                    return "EXISTS (SELECT 1 FROM worklogs wl LEFT JOIN users wlu ON wlu.id = wl.author_id "
                           "WHERE wl.work_item_id = w.id AND (CAST(wl.author_id AS TEXT) = " + p1 +
                           " OR lower(COALESCE(wlu.username, '')) = lower(" + p2 + ")" +
                           " OR lower(COALESCE(wlu.display_name, '')) = lower(" + p3 + ")))";
                });
        }
        if (f == "worklogcomment" || f == "worklog_comment" || f == "worklogdescription") {
            std::string term = resolved_vals.empty() ? "" : resolved_vals[0];
            bool neg = (op == "!~" || op == "!=");
            std::string p = ctx.add_param("%" + term + "%");
            std::string ex = "EXISTS (SELECT 1 FROM worklogs wl WHERE wl.work_item_id = w.id AND wl.description LIKE " + p + ")";
            return neg ? "(NOT " + ex + ")" : ex;
        }

        bool is_duration_col = (f == "originalestimate" || f == "original_estimate" || f == "original_estimate_seconds" ||
                                f == "remainingestimate" || f == "remaining_estimate" || f == "remaining_estimate_seconds" ||
                                f == "timeestimate" || f == "timespent" || f == "time_spent" || f == "time_spent_seconds");

        // 14. Standard column mapping on work_items
        std::string col = map_standard_column(f);
        bool is_numeric_col = (f == "storypoints" || f == "story_points" || f == "points" ||
                               f == "progress" || f == "id" || f == "board_order" || is_duration_col);

        if (op == "~" || op == "!~") {
            std::string term = resolved_vals.empty() ? "" : resolved_vals[0];
            if (f == "summary" || f == "title" || f == "description" || f == "steps_to_reproduce") {
                std::string fts_col = (f == "summary" || f == "title") ? "title" : f;
                std::string fts_expr = fts_col + " : (" + SqlContext::sanitize_fts5_query(term) + ")";
                ctx.uses_fts5 = true;
                if (ctx.primary_fts_query.empty()) {
                    ctx.primary_fts_query = SqlContext::sanitize_fts5_query(term);
                }
                std::string p_fts = ctx.add_param(fts_expr);
                std::string p_like = ctx.add_param("%" + term + "%");
                std::string match_sql =
                    "(w.id IN (SELECT CAST(work_item_id AS INTEGER) FROM work_items_fts WHERE work_items_fts MATCH " + p_fts + ") "
                    "OR " + col + " LIKE " + p_like + ")";
                return (op == "!~") ? "(NOT " + match_sql + ")" : match_sql;
            }
            std::string p = ctx.add_param("%" + term + "%");
            return op == "!~" ? "(COALESCE(" + col + ", '') NOT LIKE " + p + ")"
                              : "(" + col + " LIKE " + p + ")";
        }

        if (op == "IN" || op == "NOT IN") {
            if (resolved_vals.empty()) {
                return op == "IN" ? "(1 = 0)" : "(1 = 1)";
            }
            if (f == "labels" || f == "label") {
                std::ostringstream oss;
                oss << "(";
                for (size_t i = 0; i < resolved_vals.size(); ++i) {
                    if (i > 0) oss << (op == "IN" ? " OR " : " AND ");
                    std::string p = ctx.add_param("%" + resolved_vals[i] + "%");
                    oss << (op == "IN" ? "(" + col + " LIKE " + p + ")" : "(COALESCE(" + col + ", '') NOT LIKE " + p + ")");
                }
                oss << ")";
                return oss.str();
            }
            std::ostringstream oss;
            oss << "(lower(CAST(" << col << " AS TEXT)) " << op << " (";
            for (size_t i = 0; i < resolved_vals.size(); ++i) {
                if (i > 0) oss << ", ";
                oss << "lower(" << ctx.add_param(resolved_vals[i]) << ")";
            }
            oss << "))";
            return oss.str();
        }

        std::string val = resolved_vals.empty() ? "" : resolved_vals[0];
        if (f == "labels" || f == "label") {
            std::string p = ctx.add_param("%" + val + "%");
            return op == "!=" ? "(COALESCE(w.labels, '') NOT LIKE " + p + ")"
                              : "(w.labels LIKE " + p + ")";
        }

        if (f == "key" || f == "issue" || f == "issuekey") {
            if (op == "=" || op == "!=") {
                std::string p1 = ctx.add_param(val);
                std::string p2 = ctx.add_param(val);
                std::string expr = "(upper(w.key) = upper(" + p1 + ") OR CAST(w.id AS TEXT) = " + p2 + ")";
                return op == "!=" ? "(NOT " + expr + ")" : expr;
            }
        }

        if (is_numeric_col) {
            if (is_duration_col) {
                int64_t dur_sec = 0;
                std::string dur_err;
                bool has_alpha = false;
                for (unsigned char c : val) {
                    if (std::isalpha(c)) {
                        has_alpha = true;
                        break;
                    }
                }
                if (has_alpha && time_tracking::parse_duration_to_seconds(val, dur_sec, dur_err)) {
                    val = std::to_string(dur_sec);
                }
            }
            std::string p = ctx.add_param(val);
            return "(CAST(" + col + " AS REAL) " + op + " CAST(" + p + " AS REAL))";
        }

        if (op == "=" || op == "!=") {
            std::string p = ctx.add_param(val);
            if (op == "=") {
                return "(lower(CAST(" + col + " AS TEXT)) = lower(" + p + "))";
            } else {
                return "(lower(COALESCE(CAST(" + col + " AS TEXT), '')) != lower(" + p + "))";
            }
        }

        // >, >=, <, <= on dates or strings
        std::string p = ctx.add_param(val);
        return "( COALESCE(" + col + ", '') != '' AND " + col + " " + op + " " + p + " )";
    }

    static int priority_rank(const std::string &p) {
        std::string l = to_lower_ascii(p);
        if (l == "critical" || l == "blocker") return 5;
        if (l == "highest") return 4;
        if (l == "high") return 3;
        if (l == "medium") return 2;
        if (l == "low") return 1;
        if (l == "lowest" || l == "trivial") return 0;
        return 2;
    }

    static std::string sql_priority_rank_expr(const std::string &col_expr) {
        return "CASE lower(" + col_expr + ") "
               "WHEN 'critical' THEN 5 "
               "WHEN 'highest' THEN 4 "
               "WHEN 'high' THEN 3 "
               "WHEN 'medium' THEN 2 "
               "WHEN 'low' THEN 1 "
               "WHEN 'lowest' THEN 0 "
               "ELSE 2 END";
    }

    static std::string map_standard_column(const std::string &f) {
        if (f == "id") return "w.id";
        if (f == "key" || f == "issue" || f == "issuekey") return "w.key";
        if (f == "type" || f == "issuetype" || f == "issue_type") return "w.type";
        if (f == "category") return "w.category";
        if (f == "status") return "w.status";
        if (f == "priority") return "w.priority";
        if (f == "severity") return "w.severity";
        if (f == "summary" || f == "title") return "w.title";
        if (f == "description") return "w.description";
        if (f == "steps_to_reproduce" || f == "steps") return "w.steps_to_reproduce";
        if (f == "labels" || f == "label") return "w.labels";
        if (f == "storypoints" || f == "story_points" || f == "points") return "w.story_points";
        if (f == "progress") return "w.progress";
        if (f == "created" || f == "createddate" || f == "created_at") return "w.created_at";
        if (f == "updated" || f == "updateddate" || f == "updated_at") return "w.updated_at";
        if (f == "due" || f == "duedate" || f == "due_date") return "w.due_date";
        if (f == "startdate" || f == "start_date") return "w.start_date";
        if (f == "resolved" || f == "resolutiondate" || f == "resolution_date") return "w.resolution_date";
        if (f == "rank" || f == "order" || f == "board_order") return "w.board_order";
        if (f == "originalestimate" || f == "original_estimate" || f == "original_estimate_seconds") return "w.original_estimate_seconds";
        if (f == "remainingestimate" || f == "remaining_estimate" || f == "remaining_estimate_seconds" || f == "timeestimate") return "w.remaining_estimate_seconds";
        if (f == "timespent" || f == "time_spent" || f == "time_spent_seconds") return "w.time_spent_seconds";
        if (f == "tshirtsize" || f == "tshirt_size" || f == "tshirt") return "w.tshirt_size";
        return "w.title";
    }

private:
    template <typename BuilderFn>
    std::string compile_multi_match_clause(SqlContext &ctx,
                                           const std::vector<std::string> &vals,
                                           BuilderFn builder) const {
        if (vals.empty()) {
            return (op == "NOT IN" || op == "!=") ? "(1 = 1)" : "(1 = 0)";
        }
        bool is_neg = (op == "!=" || op == "NOT IN" || op == "!~");
        std::ostringstream oss;
        oss << "(";
        for (size_t i = 0; i < vals.size(); ++i) {
            if (i > 0) oss << " OR ";
            oss << builder(ctx, vals[i]);
        }
        oss << ")";
        return is_neg ? "(NOT " + oss.str() + ")" : oss.str();
    }

    std::string compile_user_clause(SqlContext &ctx,
                                    const std::string &fk_col,
                                    const std::string &alias,
                                    const std::vector<std::string> &vals) const {
        return compile_multi_match_clause(
            ctx,
            vals,
            [&](SqlContext &c, const std::string &val) {
                if (to_lower_ascii(val) == "unassigned" || to_lower_ascii(val) == "empty") {
                    return "(" + fk_col + " IS NULL OR " + fk_col + " = 0)";
                }
                std::string p1 = c.add_param(val);
                std::string p2 = c.add_param(val);
                std::string p3 = c.add_param(val);
                std::string p4 = c.add_param(val);
                return "(CAST(" + fk_col + " AS TEXT) = " + p1 +
                       " OR lower(COALESCE(" + alias + ".username, '')) = lower(" + p2 + ")" +
                       " OR lower(COALESCE(" + alias + ".display_name, '')) = lower(" + p3 + ")" +
                       " OR lower(COALESCE(" + alias + ".email, '')) = lower(" + p4 + "))";
            });
    }

    std::string compile_component_clause(SqlContext &ctx, const std::vector<std::string> &vals) const {
        return compile_multi_match_clause(
            ctx,
            vals,
            [](SqlContext &c, const std::string &val) {
                std::string p1 = c.add_param(val);
                std::string p2 = c.add_param(val);
                return "EXISTS (SELECT 1 FROM work_item_components wc "
                       "JOIN components cmp ON cmp.id = wc.component_id "
                       "WHERE wc.work_item_id = w.id AND (lower(cmp.name) = lower(" +
                       p1 + ") OR CAST(cmp.id AS TEXT) = " + p2 + "))";
            });
    }

    std::string compile_version_clause(SqlContext &ctx,
                                       const std::string &rel_type,
                                       const std::vector<std::string> &vals) const {
        return compile_multi_match_clause(
            ctx,
            vals,
            [&](SqlContext &c, const std::string &val) {
                std::string p_rel = c.add_param(rel_type);
                std::string p1 = c.add_param(val);
                std::string p2 = c.add_param(val);
                return "EXISTS (SELECT 1 FROM work_item_versions wv "
                       "JOIN versions ver ON ver.id = wv.version_id "
                       "WHERE wv.work_item_id = w.id AND wv.relation_type = " +
                       p_rel + " AND (lower(ver.name) = lower(" + p1 + ") OR CAST(ver.id AS TEXT) = " + p2 + "))";
            });
    }

    std::string compile_fts_clause(SqlContext &ctx, const std::string &term, bool negate) const {
        std::string fts_expr = SqlContext::sanitize_fts5_query(term);
        ctx.uses_fts5 = true;
        if (ctx.primary_fts_query.empty()) {
            ctx.primary_fts_query = fts_expr;
        }
        std::string p_fts = ctx.add_param(fts_expr);
        std::string p_like1 = ctx.add_param("%" + term + "%");
        std::string p_like2 = ctx.add_param("%" + term + "%");
        std::string p_like3 = ctx.add_param("%" + term + "%");
        std::string p_like4 = ctx.add_param("%" + term + "%");

        std::string match_sql =
            "(w.id IN (SELECT CAST(work_item_id AS INTEGER) FROM work_items_fts WHERE work_items_fts MATCH " + p_fts + ") "
            "OR w.key LIKE " + p_like1 + " "
            "OR w.title LIKE " + p_like2 + " "
            "OR w.description LIKE " + p_like3 + " "
            "OR w.labels LIKE " + p_like4 + ")";

        return negate ? "(NOT " + match_sql + ")" : match_sql;
    }

    std::string compile_empty_check(SqlContext &ctx, const std::string &f, bool is_empty) const {
        if (const CustomFieldMeta *cf = ctx.resolve_custom_field(field)) {
            std::string p_cf = ctx.add_param(std::to_string(cf->id));
            std::string non_empty_sql =
                "EXISTS (SELECT 1 FROM custom_field_values cfv WHERE cfv.work_item_id = w.id AND cfv.custom_field_id = CAST(" + p_cf + " AS INTEGER) "
                "AND (COALESCE(cfv.text_value, '') != '' OR cfv.numeric_value IS NOT NULL OR COALESCE(cfv.date_value, '') != '' OR COALESCE(cfv.user_value_id, 0) > 0))";
            return is_empty ? "(NOT " + non_empty_sql + ")" : non_empty_sql;
        }

        if (f == "assignee" || f == "assignee_id") {
            return is_empty ? "(w.assignee_id IS NULL OR w.assignee_id = 0)" : "(w.assignee_id IS NOT NULL AND w.assignee_id > 0)";
        }
        if (f == "reporter" || f == "reporter_id") {
            return is_empty ? "(w.reporter_id IS NULL OR w.reporter_id = 0)" : "(w.reporter_id IS NOT NULL AND w.reporter_id > 0)";
        }
        if (f == "sprint" || f == "sprint_id") {
            return is_empty ? "(w.sprint_id IS NULL OR w.sprint_id = 0)" : "(w.sprint_id IS NOT NULL AND w.sprint_id > 0)";
        }
        if (f == "parent" || f == "parent_id" || f == "epic" || f == "parentepic") {
            return is_empty ? "(w.parent_id IS NULL OR w.parent_id = 0)" : "(w.parent_id IS NOT NULL AND w.parent_id > 0)";
        }
        if (f == "resolution" || f == "resolution_id") {
            return is_empty ? "(w.resolution_id IS NULL OR w.resolution_id = 0)" : "(w.resolution_id IS NOT NULL AND w.resolution_id > 0)";
        }
        if (f == "security" || f == "securitylevel" || f == "security_level" || f == "security_level_id") {
            return is_empty ? "(w.security_level_id IS NULL OR w.security_level_id = 0)" : "(w.security_level_id IS NOT NULL AND w.security_level_id > 0)";
        }
        if (f == "component" || f == "components") {
            std::string ex = "EXISTS (SELECT 1 FROM work_item_components wc WHERE wc.work_item_id = w.id)";
            return is_empty ? "(NOT " + ex + ")" : ex;
        }
        if (f == "fixversion" || f == "fixversions" || f == "fix_version") {
            std::string ex = "EXISTS (SELECT 1 FROM work_item_versions wv WHERE wv.work_item_id = w.id AND wv.relation_type = 'fix')";
            return is_empty ? "(NOT " + ex + ")" : ex;
        }
        if (f == "affectedversion" || f == "affectedversions" || f == "affectsversion") {
            std::string ex = "EXISTS (SELECT 1 FROM work_item_versions wv WHERE wv.work_item_id = w.id AND wv.relation_type = 'affects')";
            return is_empty ? "(NOT " + ex + ")" : ex;
        }
        if (f == "comment" || f == "comments") {
            std::string ex = "EXISTS (SELECT 1 FROM comments cm WHERE cm.work_item_id = w.id)";
            return is_empty ? "(NOT " + ex + ")" : ex;
        }

        std::string col = map_standard_column(f);
        return is_empty ? "(" + col + " IS NULL OR TRIM(CAST(" + col + " AS TEXT)) = '')"
                        : "(" + col + " IS NOT NULL AND TRIM(CAST(" + col + " AS TEXT)) != '')";
    }

    std::string compile_custom_field_clause(SqlContext &ctx,
                                            const CustomFieldMeta &cf,
                                            const std::vector<std::string> &vals) const {
        if (cf.field_type == "formula") {
            std::string val = vals.empty() ? "0" : vals[0];
            std::string p = ctx.add_param(val);
            std::string formula_sql = "(CAST(w.story_points AS REAL) * (100.0 - CAST(w.progress AS REAL)) / 100.0)";
            return "(" + formula_sql + " " + (op == "~" ? "=" : op) + " CAST(" + p + " AS REAL))";
        }

        if (vals.empty()) {
            return "(1 = 0)";
        }

        bool is_neg = (op == "!=" || op == "NOT IN" || op == "!~");

        if (cf.field_type == "number" && (op == ">" || op == ">=" || op == "<" || op == "<=")) {
            std::string p_cf = ctx.add_param(std::to_string(cf.id));
            std::string p_val = ctx.add_param(vals[0]);
            return "EXISTS (SELECT 1 FROM custom_field_values cfv WHERE cfv.work_item_id = w.id "
                   "AND cfv.custom_field_id = CAST(" + p_cf + " AS INTEGER) "
                   "AND cfv.numeric_value IS NOT NULL AND cfv.numeric_value " + op + " CAST(" + p_val + " AS REAL))";
        }

        if ((cf.field_type == "date" || cf.field_type == "datetime") &&
            (op == ">" || op == ">=" || op == "<" || op == "<=")) {
            std::string p_cf = ctx.add_param(std::to_string(cf.id));
            std::string p_val = ctx.add_param(vals[0]);
            return "EXISTS (SELECT 1 FROM custom_field_values cfv WHERE cfv.work_item_id = w.id "
                   "AND cfv.custom_field_id = CAST(" + p_cf + " AS INTEGER) "
                   "AND COALESCE(cfv.date_value, cfv.text_value, '') != '' "
                   "AND COALESCE(cfv.date_value, cfv.text_value) " + op + " " + p_val + ")";
        }

        std::ostringstream oss;
        oss << "EXISTS (SELECT 1 FROM custom_field_values cfv "
               "LEFT JOIN users cfu ON cfu.id = cfv.user_value_id "
               "WHERE cfv.work_item_id = w.id AND cfv.custom_field_id = CAST("
            << ctx.add_param(std::to_string(cf.id)) << " AS INTEGER) AND (";

        for (size_t i = 0; i < vals.size(); ++i) {
            if (i > 0) oss << " OR ";
            const std::string &v = vals[i];
            if (op == "~" || op == "!~" || cf.field_type == "select_multi") {
                std::string p1 = ctx.add_param("%" + v + "%");
                std::string p2 = ctx.add_param("%" + v + "%");
                oss << "(COALESCE(cfv.text_value, '') LIKE " << p1 << " OR COALESCE(cfv.json_value, '') LIKE " << p2 << ")";
            } else if (cf.field_type == "number") {
                std::string p = ctx.add_param(v);
                oss << "(cfv.numeric_value IS NOT NULL AND cfv.numeric_value = CAST(" << p << " AS REAL))";
            } else if (cf.field_type == "user_picker_single") {
                std::string p1 = ctx.add_param(v);
                std::string p2 = ctx.add_param(v);
                std::string p3 = ctx.add_param(v);
                oss << "(CAST(cfv.user_value_id AS TEXT) = " << p1
                    << " OR lower(COALESCE(cfu.username, '')) = lower(" << p2 << ")"
                    << " OR lower(COALESCE(cfu.display_name, '')) = lower(" << p3 << "))";
            } else {
                std::string p1 = ctx.add_param(v);
                std::string p2 = ctx.add_param(v);
                oss << "(lower(COALESCE(cfv.text_value, '')) = lower(" << p1
                    << ") OR lower(COALESCE(cfv.date_value, '')) = lower(" << p2 << "))";
            }
        }
        oss << "))";

        return is_neg ? "(NOT " + oss.str() + ")" : oss.str();
    }

    std::string compile_was_clause(SqlContext &ctx,
                                   const std::string &f,
                                   const std::vector<std::string> &vals) const {
        (void)f;
        if (vals.empty()) return "(1 = 0)";
        bool is_neg = (op == "WAS NOT" || op == "WAS NOT IN");

        std::ostringstream hist_cond;
        hist_cond << "(";
        for (size_t i = 0; i < vals.size(); ++i) {
            if (i > 0) hist_cond << " OR ";
            std::string p1 = ctx.add_param(vals[i]);
            std::string p2 = ctx.add_param(vals[i]);
            hist_cond << "lower(wh.from_status) = lower(" << p1 << ") OR lower(wh.to_status) = lower(" << p2 << ")";
        }
        hist_cond << ")";

        append_history_predicates(ctx, hist_cond);

        std::ostringstream cur_cond;
        if (history_predicates.empty()) {
            cur_cond << " OR (";
            for (size_t i = 0; i < vals.size(); ++i) {
                if (i > 0) cur_cond << " OR ";
                std::string p = ctx.add_param(vals[i]);
                cur_cond << "lower(w.status) = lower(" << p << ")";
            }
            cur_cond << ")";
        }

        std::string full = "(EXISTS (SELECT 1 FROM workflow_history wh LEFT JOIN users whu ON whu.id = wh.changed_by "
                           "WHERE wh.work_item_id = w.id AND " + hist_cond.str() + ")" + cur_cond.str() + ")";
        return is_neg ? "(NOT " + full + ")" : full;
    }

    std::string compile_changed_clause(SqlContext &ctx, const std::string &f) const {
        (void)f;
        std::ostringstream hist_cond;
        hist_cond << "1 = 1";
        append_history_predicates(ctx, hist_cond);
        return "EXISTS (SELECT 1 FROM workflow_history wh LEFT JOIN users whu ON whu.id = wh.changed_by "
               "WHERE wh.work_item_id = w.id AND " + hist_cond.str() + ")";
    }

    void append_history_predicates(SqlContext &ctx, std::ostringstream &cond) const {
        for (const auto &hp : history_predicates) {
            if (hp.values.empty()) continue;
            auto resolved = ctx.resolve_operand(hp.values[0]);
            std::string v1 = resolved.empty() ? "" : resolved[0];
            if (hp.kind == "FROM") {
                cond << " AND lower(wh.from_status) = lower(" << ctx.add_param(v1) << ")";
            } else if (hp.kind == "TO") {
                cond << " AND lower(wh.to_status) = lower(" << ctx.add_param(v1) << ")";
            } else if (hp.kind == "BY") {
                std::string p1 = ctx.add_param(v1);
                std::string p2 = ctx.add_param(v1);
                std::string p3 = ctx.add_param(v1);
                cond << " AND (CAST(wh.changed_by AS TEXT) = " << p1
                     << " OR lower(COALESCE(whu.username, '')) = lower(" << p2 << ")"
                     << " OR lower(COALESCE(whu.display_name, '')) = lower(" << p3 << "))";
            } else if (hp.kind == "AFTER") {
                cond << " AND wh.created_at >= " << ctx.add_param(v1);
            } else if (hp.kind == "BEFORE") {
                cond << " AND wh.created_at <= " << ctx.add_param(v1);
            } else if (hp.kind == "ON") {
                cond << " AND date(wh.created_at) = date(" << ctx.add_param(v1) << ")";
            } else if (hp.kind == "DURING" && hp.values.size() >= 2) {
                auto resolved2 = ctx.resolve_operand(hp.values[1]);
                std::string v2 = resolved2.empty() ? "" : resolved2[0];
                cond << " AND wh.created_at >= " << ctx.add_param(v1)
                     << " AND wh.created_at <= " << ctx.add_param(v2);
            }
        }
    }
};

struct JqlQueryAst {
    std::unique_ptr<JqlNode> where_root;
    std::vector<OrderByItem> order_by;

    std::string to_where_sql(SqlContext &ctx) const {
        if (!where_root) {
            return "1 = 1";
        }
        return where_root->to_sql(ctx);
    }

    std::string to_order_by_sql(SqlContext &ctx) const {
        if (order_by.empty()) {
            if (ctx.uses_fts5 && !ctx.primary_fts_query.empty()) {
                return "w.board_order ASC, w.id ASC";
            }
            return "w.board_order ASC, w.id ASC";
        }

        std::ostringstream oss;
        for (size_t i = 0; i < order_by.size(); ++i) {
            if (i > 0) oss << ", ";
            std::string f = to_lower_ascii(order_by[i].field);
            std::string dir = order_by[i].descending ? "DESC" : "ASC";

            if (f == "priority") {
                oss << ClauseNode::sql_priority_rank_expr("w.priority") << " " << dir;
            } else if (f == "assignee") {
                oss << "lower(COALESCE(ua.display_name, 'zzzz')) " << dir;
            } else if (f == "reporter") {
                oss << "lower(COALESCE(ur.display_name, 'zzzz')) " << dir;
            } else if (f == "sprint") {
                oss << "lower(COALESCE(sp.name, 'zzzz')) " << dir;
            } else if (f == "resolution") {
                oss << "lower(COALESCE(res.name, 'zzzz')) " << dir;
            } else if (f == "security" || f == "securitylevel" || f == "security_level") {
                oss << "lower(COALESCE(isl.name, 'zzzz')) " << dir;
            } else if (const CustomFieldMeta *cf = ctx.resolve_custom_field(order_by[i].field)) {
                if (cf->field_type == "number") {
                    oss << "(SELECT cfv.numeric_value FROM custom_field_values cfv WHERE cfv.work_item_id = w.id AND cfv.custom_field_id = "
                        << cf->id << " LIMIT 1) " << dir;
                } else {
                    oss << "lower(COALESCE((SELECT cfv.text_value FROM custom_field_values cfv WHERE cfv.work_item_id = w.id AND cfv.custom_field_id = "
                        << cf->id << " LIMIT 1), '')) " << dir;
                }
            } else {
                std::string col = ClauseNode::map_standard_column(f);
                oss << col << " " << dir;
            }
        }
        oss << ", w.id ASC";
        return oss.str();
    }
};

class JqlParser {
public:
    explicit JqlParser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    static JqlQueryAst parse_query(std::string_view jql_str) {
        JqlLexer lexer(jql_str);
        auto tokens = lexer.tokenize();
        JqlParser parser(std::move(tokens));
        return parser.parse();
    }

    JqlQueryAst parse() {
        JqlQueryAst ast;

        if (current().type != TokenType::EndOfInput && current().type != TokenType::OrderBy) {
            ast.where_root = parse_expression(Precedence::Lowest);
        }

        if (current().type == TokenType::OrderBy) {
            advance(); // consume ORDER BY
            ast.order_by = parse_order_by_list();
        }

        if (current().type != TokenType::EndOfInput) {
            const Token &t = current();
            throw JqlSyntaxError(
                "Unexpected token '" + t.raw_text + "' at line " +
                    std::to_string(t.line) + ", col " + std::to_string(t.col),
                t.pos, std::max<size_t>(1, t.length), t.line, t.col);
        }

        return ast;
    }

private:
    std::vector<Token> tokens_;
    size_t idx_ = 0;

    const Token &current() const {
        if (idx_ < tokens_.size()) return tokens_[idx_];
        return tokens_.back();
    }

    const Token &advance() {
        const Token &t = current();
        if (idx_ + 1 < tokens_.size()) ++idx_;
        return t;
    }

    Precedence current_infix_precedence() const {
        switch (current().type) {
        case TokenType::Or:  return Precedence::Or;
        case TokenType::And: return Precedence::And;
        default:             return Precedence::Lowest;
        }
    }

    // Pratt parser evaluating expressions by operator precedence
    std::unique_ptr<JqlNode> parse_expression(Precedence min_prec) {
        std::unique_ptr<JqlNode> left = parse_prefix();

        while (static_cast<int>(current_infix_precedence()) > static_cast<int>(min_prec)) {
            Token op_tok = advance();
            Precedence prec = (op_tok.type == TokenType::Or) ? Precedence::Or : Precedence::And;
            std::string op_name = (op_tok.type == TokenType::Or) ? "OR" : "AND";

            std::unique_ptr<JqlNode> right = parse_expression(prec);
            left = std::make_unique<BinaryOpNode>(op_name, std::move(left), std::move(right));
        }

        return left;
    }

    std::unique_ptr<JqlNode> parse_prefix() {
        const Token &t = current();

        // Prefix NOT
        if (t.type == TokenType::Not) {
            advance();
            auto operand = parse_expression(Precedence::Not);
            return std::make_unique<UnaryNotNode>(std::move(operand));
        }

        // Parenthesized expression ( ... )
        if (t.type == TokenType::LParen) {
            Token open_tok = advance();
            if (current().type == TokenType::RParen) {
                throw JqlSyntaxError(
                    "Empty parentheses '()' are not allowed in JQL at col " + std::to_string(open_tok.col),
                    open_tok.pos, 2, open_tok.line, open_tok.col);
            }
            auto inner = parse_expression(Precedence::Lowest);
            if (current().type != TokenType::RParen) {
                const Token &cur = current();
                throw JqlSyntaxError(
                    "Expected closing parenthesis ')' to match '(' at col " + std::to_string(open_tok.col) +
                        ", but found '" + (cur.type == TokenType::EndOfInput ? "end of query" : cur.raw_text) + "'",
                    cur.pos, std::max<size_t>(1, cur.length), cur.line, cur.col);
            }
            advance(); // consume ')'
            return inner;
        }

        return parse_clause();
    }

    std::unique_ptr<JqlNode> parse_clause() {
        const Token &ft = current();
        if (ft.type != TokenType::Identifier &&
            ft.type != TokenType::CustomFieldRef &&
            ft.type != TokenType::String) {
            throw JqlSyntaxError(
                "Expected field name (e.g. project, status, assignee, summary, cf[key]) but found '" +
                    (ft.type == TokenType::EndOfInput ? "end of query" : ft.raw_text) + "' at col " +
                    std::to_string(ft.col),
                ft.pos, std::max<size_t>(1, ft.length), ft.line, ft.col);
        }

        auto clause = std::make_unique<ClauseNode>();
        clause->field = advance().text;

        const Token &op_tok = current();
        switch (op_tok.type) {
        case TokenType::Operator:
            clause->op = advance().text;
            {
                OperandValue val = parse_single_operand();
                clause->values.push_back(val.value);
                clause->operands.push_back(std::move(val));
            }
            break;

        case TokenType::In:
        case TokenType::NotIn:
            clause->op = (op_tok.type == TokenType::In) ? "IN" : "NOT IN";
            advance();
            parse_operand_list(*clause);
            break;

        case TokenType::Is:
        case TokenType::IsNot:
            clause->op = (op_tok.type == TokenType::Is) ? "IS" : "IS NOT";
            advance();
            if (current().type != TokenType::Empty &&
                !(current().type == TokenType::Identifier &&
                  (to_upper_ascii(current().text) == "EMPTY" || to_upper_ascii(current().text) == "NULL"))) {
                const Token &bad = current();
                throw JqlSyntaxError(
                    "Expected EMPTY or NULL after '" + clause->op + "' at col " + std::to_string(bad.col),
                    bad.pos, std::max<size_t>(1, bad.length), bad.line, bad.col);
            }
            advance();
            clause->values.push_back("EMPTY");
            clause->operands.push_back({false, true, "EMPTY", {}});
            break;

        case TokenType::Was:
        case TokenType::WasNot:
            clause->op = (op_tok.type == TokenType::Was) ? "WAS" : "WAS NOT";
            advance();
            {
                OperandValue val = parse_single_operand();
                clause->values.push_back(val.value);
                clause->operands.push_back(std::move(val));
            }
            parse_optional_history_predicates(*clause);
            break;

        case TokenType::WasIn:
        case TokenType::WasNotIn:
            clause->op = (op_tok.type == TokenType::WasIn) ? "WAS IN" : "WAS NOT IN";
            advance();
            parse_operand_list(*clause);
            parse_optional_history_predicates(*clause);
            break;

        case TokenType::Changed:
            clause->op = "CHANGED";
            advance();
            parse_optional_history_predicates(*clause);
            break;

        default:
            throw JqlSyntaxError(
                "Expected comparison operator (=, !=, ~, !~, >, >=, <, <=, IN, NOT IN, IS, WAS, CHANGED) after field '" +
                    clause->field + "', but found '" +
                    (op_tok.type == TokenType::EndOfInput ? "end of query" : op_tok.raw_text) + "' at col " +
                    std::to_string(op_tok.col),
                op_tok.pos, std::max<size_t>(1, op_tok.length), op_tok.line, op_tok.col);
        }

        return clause;
    }

    void parse_operand_list(ClauseNode &clause) {
        // Support both `field IN (a, b, c)` and `field IN openSprints()`
        if (current().type == TokenType::Identifier &&
            idx_ + 1 < tokens_.size() &&
            tokens_[idx_ + 1].type == TokenType::LParen) {
            OperandValue fn_op = parse_single_operand();
            values_or_push(clause, fn_op);
            return;
        }

        if (current().type != TokenType::LParen) {
            const Token &bad = current();
            throw JqlSyntaxError(
                "Expected '(' or function call after '" + clause.op + "' at col " + std::to_string(bad.col),
                bad.pos, std::max<size_t>(1, bad.length), bad.line, bad.col);
        }
        Token open_tok = advance(); // consume '('
        if (current().type == TokenType::RParen) {
            throw JqlSyntaxError(
                "Empty list '()' after '" + clause.op + "' at col " + std::to_string(open_tok.col),
                open_tok.pos, 2, open_tok.line, open_tok.col);
        }

        while (true) {
            OperandValue item = parse_single_operand();
            clause.values.push_back(item.value);
            clause.operands.push_back(std::move(item));

            if (current().type == TokenType::Comma) {
                advance();
                continue;
            }
            if (current().type == TokenType::RParen) {
                advance();
                break;
            }
            const Token &bad = current();
            throw JqlSyntaxError(
                "Expected ',' or ')' in value list, but found '" +
                    (bad.type == TokenType::EndOfInput ? "end of query" : bad.raw_text) + "' at col " +
                    std::to_string(bad.col),
                bad.pos, std::max<size_t>(1, bad.length), bad.line, bad.col);
        }
    }

    static void values_or_push(ClauseNode &clause, OperandValue &op) {
        clause.values.push_back(op.value);
        clause.operands.push_back(std::move(op));
    }

    OperandValue parse_single_operand() {
        const Token &t = current();
        if (t.type == TokenType::Empty) {
            advance();
            return {false, true, "EMPTY", {}};
        }

        if (t.type == TokenType::String || t.type == TokenType::Number) {
            Token tok = advance();
            return {false, false, tok.text, {}};
        }

        if (t.type == TokenType::Identifier) {
            Token ident = advance();
            // Check for function call: identifier(...)
            if (current().type == TokenType::LParen) {
                advance(); // consume '('
                std::vector<std::string> args;
                if (current().type != TokenType::RParen) {
                    while (true) {
                        const Token &arg_tok = current();
                        if (arg_tok.type == TokenType::String ||
                            arg_tok.type == TokenType::Number ||
                            arg_tok.type == TokenType::Identifier) {
                            args.push_back(advance().text);
                        } else {
                            throw JqlSyntaxError(
                                "Expected argument inside function '" + ident.text + "(...)' at col " +
                                    std::to_string(arg_tok.col),
                                arg_tok.pos, std::max<size_t>(1, arg_tok.length), arg_tok.line, arg_tok.col);
                        }
                        if (current().type == TokenType::Comma) {
                            advance();
                            continue;
                        }
                        break;
                    }
                }
                if (current().type != TokenType::RParen) {
                    const Token &bad = current();
                    throw JqlSyntaxError(
                        "Expected ')' after arguments to function '" + ident.text + "' at col " +
                            std::to_string(bad.col),
                        bad.pos, std::max<size_t>(1, bad.length), bad.line, bad.col);
                }
                advance(); // consume ')'
                return {true, false, ident.text, std::move(args)};
            }
            return {false, false, ident.text, {}};
        }

        throw JqlSyntaxError(
            "Expected value (string, number, identifier, or function) at col " + std::to_string(t.col) +
                ", but found '" + (t.type == TokenType::EndOfInput ? "end of query" : t.raw_text) + "'",
            t.pos, std::max<size_t>(1, t.length), t.line, t.col);
    }

    void parse_optional_history_predicates(ClauseNode &clause) {
        while (true) {
            TokenType tt = current().type;
            if (tt == TokenType::From || tt == TokenType::To || tt == TokenType::By ||
                tt == TokenType::Before || tt == TokenType::After || tt == TokenType::On) {
                std::string kind = to_upper_ascii(advance().text);
                OperandValue val = parse_single_operand();
                clause.history_predicates.push_back({kind, {std::move(val)}});
            } else if (tt == TokenType::During) {
                advance();
                if (current().type != TokenType::LParen) {
                    const Token &bad = current();
                    throw JqlSyntaxError(
                        "Expected '(start_date, end_date)' after DURING at col " + std::to_string(bad.col),
                        bad.pos, std::max<size_t>(1, bad.length), bad.line, bad.col);
                }
                advance(); // '('
                OperandValue d1 = parse_single_operand();
                if (current().type == TokenType::Comma) advance();
                OperandValue d2 = parse_single_operand();
                if (current().type != TokenType::RParen) {
                    const Token &bad = current();
                    throw JqlSyntaxError(
                        "Expected ')' after DURING range at col " + std::to_string(bad.col),
                        bad.pos, std::max<size_t>(1, bad.length), bad.line, bad.col);
                }
                advance(); // ')'
                clause.history_predicates.push_back({"DURING", {std::move(d1), std::move(d2)}});
            } else {
                break;
            }
        }
    }

    std::vector<OrderByItem> parse_order_by_list() {
        std::vector<OrderByItem> items;
        while (true) {
            const Token &ft = current();
            if (ft.type != TokenType::Identifier &&
                ft.type != TokenType::CustomFieldRef &&
                ft.type != TokenType::String) {
                throw JqlSyntaxError(
                    "Expected field name in ORDER BY clause at col " + std::to_string(ft.col),
                    ft.pos, std::max<size_t>(1, ft.length), ft.line, ft.col);
            }
            OrderByItem item;
            item.field = advance().text;
            item.descending = false;

            if (current().type == TokenType::Asc) {
                advance();
                item.descending = false;
            } else if (current().type == TokenType::Desc) {
                advance();
                item.descending = true;
            }

            items.push_back(std::move(item));

            if (current().type == TokenType::Comma) {
                advance();
                continue;
            }
            break;
        }
        return items;
    }
};

} // namespace jql
