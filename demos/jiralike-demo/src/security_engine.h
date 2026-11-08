#pragma once

#include "raii_json.h"
#include "raii_sqlite.h"
#include <algorithm>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace security {

inline std::string to_lower_ascii(std::string_view str) {
    std::string s(str);
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

inline bool user_is_admin(db::sqlite_db &conn, int user_id, const std::string &given_role = "") {
    if (user_id <= 0) return false;

    // Portal customers are strictly segregated and can never hold admin privileges
    {
        auto tstmt = conn.prepare("SELECT COALESCE(user_type, 'INTERNAL') FROM users WHERE id = ?;");
        tstmt.bind_int(1, user_id);
        if (tstmt.step() == SQLITE_ROW && tstmt.column_text(0) == "PORTAL_CUSTOMER") {
            return false;
        }
    }

    std::string role = given_role;
    if (role.empty()) {
        auto stmt = conn.prepare("SELECT role FROM users WHERE id = ?;");
        stmt.bind_int(1, user_id);
        if (stmt.step() == SQLITE_ROW) {
            role = stmt.column_text(0);
        }
    }
    if (role == "Admin" || role == "Administrator" || to_lower_ascii(role) == "admin") {
        return true;
    }

    // Check jira-administrators group membership
    auto gstmt = conn.prepare(R"SQL(
        SELECT 1 FROM user_group_members ugm
        INNER JOIN user_groups g ON g.id = ugm.group_id
        WHERE ugm.user_id = ? AND g.name = 'jira-administrators'
        LIMIT 1;
    )SQL");
    gstmt.bind_int(1, user_id);
    if (gstmt.step() == SQLITE_ROW) {
        return true;
    }

    return false;
}

inline bool role_matches(const std::string &grant_role, const std::string &user_role) {
    std::string gr = to_lower_ascii(grant_role);
    std::string ur = to_lower_ascii(user_role);
    if (gr == ur) return true;

    if (gr == "administrators" || gr == "admin" || gr == "administrator") {
        return ur == "admin" || ur == "administrator" || ur == "administrators";
    }
    if (gr == "developers" || gr == "developer") {
        return ur == "developer" || ur == "developers";
    }
    if (gr == "qa" || gr == "qa engineer" || gr == "quality assurance") {
        return ur == "qa" || ur == "qa engineer" || ur == "quality assurance";
    }
    if (gr == "product managers" || gr == "project manager" || gr == "product manager" || gr == "pm") {
        return ur == "product managers" || ur == "project manager" || ur == "product manager" || ur == "pm";
    }
    if (gr == "viewers" || gr == "viewer") {
        return ur == "viewer" || ur == "viewers";
    }
    return false;
}

inline bool user_matches_grant(db::sqlite_db &conn,
                               int user_id,
                               const std::string &user_role,
                               const std::string &grantee_type,
                               const std::string &grantee_value,
                               int project_id,
                               int reporter_id = 0,
                               int assignee_id = 0,
                               int project_lead_id = 0,
                               int work_item_id = 0) {
    if (user_id <= 0) return false;

    if (grantee_type == "anyone") {
        return true;
    }

    if (grantee_type == "user") {
        if (grantee_value == std::to_string(user_id)) return true;
        auto ustmt = conn.prepare("SELECT username FROM users WHERE id = ?;");
        ustmt.bind_int(1, user_id);
        if (ustmt.step() == SQLITE_ROW) {
            if (to_lower_ascii(ustmt.column_text(0)) == to_lower_ascii(grantee_value)) {
                return true;
            }
        }
        return false;
    }

    if (grantee_type == "group") {
        auto gstmt = conn.prepare(R"SQL(
            SELECT 1 FROM user_group_members ugm
            INNER JOIN user_groups g ON g.id = ugm.group_id
            WHERE ugm.user_id = ? AND lower(g.name) = lower(?)
            LIMIT 1;
        )SQL");
        gstmt.bind_int(1, user_id);
        gstmt.bind_text(2, grantee_value);
        if (gstmt.step() == SQLITE_ROW) {
            return true;
        }
        return false;
    }

    if (grantee_type == "role") {
        // 1. Check project_role_members for project-specific assignment
        if (project_id > 0) {
            auto rstmt = conn.prepare(R"SQL(
                SELECT role_name FROM project_role_members
                WHERE project_id = ? AND user_id = ?;
            )SQL");
            rstmt.bind_int(1, project_id);
            rstmt.bind_int(2, user_id);
            while (rstmt.step() == SQLITE_ROW) {
                if (role_matches(grantee_value, rstmt.column_text(0))) {
                    return true;
                }
            }
        }
        // 2. Check global user role
        if (!user_role.empty() && role_matches(grantee_value, user_role)) {
            return true;
        }
        return false;
    }

    if (grantee_type == "reporter" || (grantee_type == "dynamic" && to_lower_ascii(grantee_value) == "reporter")) {
        return reporter_id > 0 && user_id == reporter_id;
    }
    if (grantee_type == "assignee" || (grantee_type == "dynamic" && to_lower_ascii(grantee_value) == "assignee")) {
        return assignee_id > 0 && user_id == assignee_id;
    }
    if (grantee_type == "lead" || (grantee_type == "dynamic" && to_lower_ascii(grantee_value) == "lead")) {
        int lead = project_lead_id;
        if (lead <= 0 && project_id > 0) {
            auto pstmt = conn.prepare("SELECT COALESCE(lead_user_id, 0) FROM projects WHERE id = ?;");
            pstmt.bind_int(1, project_id);
            if (pstmt.step() == SQLITE_ROW) {
                lead = pstmt.column_int(0);
            }
        }
        return lead > 0 && user_id == lead;
    }

    if (grantee_type == "custom_field" && work_item_id > 0) {
        // Query work_item_custom_fields for the custom field matching grantee_value
        try {
            auto cf_stmt = conn.prepare(R"SQL(
                SELECT wicf.value FROM work_item_custom_fields wicf
                INNER JOIN custom_fields cf ON cf.id = wicf.custom_field_id
                WHERE wicf.work_item_id = ? AND (CAST(cf.id AS TEXT) = ? OR lower(cf.name) = lower(?) OR lower(cf.field_key) = lower(?))
                LIMIT 1;
            )SQL");
            cf_stmt.bind_int(1, work_item_id);
            cf_stmt.bind_text(2, grantee_value);
            cf_stmt.bind_text(3, grantee_value);
            cf_stmt.bind_text(4, grantee_value);
            if (cf_stmt.step() == SQLITE_ROW) {
                std::string val = cf_stmt.column_text(0);
                if (val == std::to_string(user_id)) {
                    return true;
                }
                auto ustmt = conn.prepare("SELECT username FROM users WHERE id = ?;");
                ustmt.bind_int(1, user_id);
                if (ustmt.step() == SQLITE_ROW) {
                    if (to_lower_ascii(ustmt.column_text(0)) == to_lower_ascii(val)) {
                        return true;
                    }
                }
            }
        } catch (...) {
            try {
                auto cf_stmt = conn.prepare(R"SQL(
                    SELECT COALESCE(CASE WHEN cfv.user_value_id > 0 THEN CAST(cfv.user_value_id AS TEXT) ELSE cfv.text_value END, '')
                    FROM custom_field_values cfv
                    INNER JOIN custom_fields cf ON cf.id = cfv.custom_field_id
                    WHERE cfv.work_item_id = ? AND (CAST(cf.id AS TEXT) = ? OR lower(cf.name) = lower(?) OR lower(cf.field_key) = lower(?))
                    LIMIT 1;
                )SQL");
                cf_stmt.bind_int(1, work_item_id);
                cf_stmt.bind_text(2, grantee_value);
                cf_stmt.bind_text(3, grantee_value);
                cf_stmt.bind_text(4, grantee_value);
                if (cf_stmt.step() == SQLITE_ROW) {
                    std::string val = cf_stmt.column_text(0);
                    if (val == std::to_string(user_id)) {
                        return true;
                    }
                    auto ustmt = conn.prepare("SELECT username FROM users WHERE id = ?;");
                    ustmt.bind_int(1, user_id);
                    if (ustmt.step() == SQLITE_ROW) {
                        if (to_lower_ascii(ustmt.column_text(0)) == to_lower_ascii(val)) {
                            return true;
                        }
                    }
                }
            } catch (...) {}
        }
    }

    return false;
}

inline bool has_permission(db::sqlite_db &conn,
                           int user_id,
                           int project_id,
                           const std::string &perm_key,
                           int reporter_id = 0,
                           int assignee_id = 0,
                           int work_item_id = 0) {
    if (user_id <= 0) return false;

    std::string user_role;
    std::string user_type = "INTERNAL";
    {
        auto ustmt = conn.prepare("SELECT role, COALESCE(user_type, 'INTERNAL') FROM users WHERE id = ?;");
        ustmt.bind_int(1, user_id);
        if (ustmt.step() == SQLITE_ROW) {
            user_role = ustmt.column_text(0);
            user_type = ustmt.column_text(1);
        }
    }

    // Segregated portal customers have zero access to internal project permission schemes
    if (user_type == "PORTAL_CUSTOMER") {
        return false;
    }

    if (user_is_admin(conn, user_id, user_role)) {
        return true;
    }

    int scheme_id = 1;
    int lead_id = 0;
    if (project_id > 0) {
        auto pstmt = conn.prepare("SELECT COALESCE(permission_scheme_id, 1), COALESCE(lead_user_id, 0) FROM projects WHERE id = ?;");
        pstmt.bind_int(1, project_id);
        if (pstmt.step() == SQLITE_ROW) {
            scheme_id = pstmt.column_int(0);
            lead_id = pstmt.column_int(1);
            if (scheme_id <= 0) scheme_id = 1;
        }
    }

    auto stmt = conn.prepare(R"SQL(
        SELECT grantee_type, grantee_value
        FROM permission_scheme_grants
        WHERE scheme_id = ? AND permission_key = ?;
    )SQL");
    stmt.bind_int(1, scheme_id);
    stmt.bind_text(2, perm_key);

    while (stmt.step() == SQLITE_ROW) {
        std::string g_type = stmt.column_text(0);
        std::string g_val  = stmt.column_text(1);
        if (user_matches_grant(conn, user_id, user_role, g_type, g_val, project_id, reporter_id, assignee_id, lead_id, work_item_id)) {
            return true;
        }
    }

    return false;
}

inline bool can_access_security_level(db::sqlite_db &conn,
                                      int user_id,
                                      int project_id,
                                      int security_level_id,
                                      int reporter_id = 0,
                                      int assignee_id = 0,
                                      int work_item_id = 0) {
    if (security_level_id <= 0) {
        return true;
    }
    if (user_id <= 0) return false;

    std::string user_role;
    std::string user_type = "INTERNAL";
    {
        auto ustmt = conn.prepare("SELECT role, COALESCE(user_type, 'INTERNAL') FROM users WHERE id = ?;");
        ustmt.bind_int(1, user_id);
        if (ustmt.step() == SQLITE_ROW) {
            user_role = ustmt.column_text(0);
            user_type = ustmt.column_text(1);
        }
    }

    if (user_type == "PORTAL_CUSTOMER") {
        return false;
    }

    if (user_is_admin(conn, user_id, user_role)) {
        return true;
    }

    int lead_id = 0;
    if (project_id > 0) {
        auto pstmt = conn.prepare("SELECT COALESCE(lead_user_id, 0) FROM projects WHERE id = ?;");
        pstmt.bind_int(1, project_id);
        if (pstmt.step() == SQLITE_ROW) {
            lead_id = pstmt.column_int(0);
        }
    }

    auto stmt = conn.prepare(R"SQL(
        SELECT grantee_type, grantee_value
        FROM issue_security_level_grants
        WHERE level_id = ?;
    )SQL");
    stmt.bind_int(1, security_level_id);

    bool has_grants = false;
    while (stmt.step() == SQLITE_ROW) {
        has_grants = true;
        std::string g_type = stmt.column_text(0);
        std::string g_val  = stmt.column_text(1);
        if (user_matches_grant(conn, user_id, user_role, g_type, g_val, project_id, reporter_id, assignee_id, lead_id, work_item_id)) {
            return true;
        }
    }

    return !has_grants;
}

inline std::string build_security_filter(db::sqlite_db &conn, int user_id, const std::string &item_alias = "w") {
    if (user_id <= 0) {
        return "(1=0)";
    }

    std::string user_role;
    std::string user_type = "INTERNAL";
    std::string username;
    {
        auto ustmt = conn.prepare("SELECT role, COALESCE(user_type, 'INTERNAL'), COALESCE(username, '') FROM users WHERE id = ?;");
        ustmt.bind_int(1, user_id);
        if (ustmt.step() == SQLITE_ROW) {
            user_role = ustmt.column_text(0);
            user_type = ustmt.column_text(1);
            username  = ustmt.column_text(2);
        }
    }

    if (user_type == "PORTAL_CUSTOMER") {
        return "(1=0)";
    }

    if (user_is_admin(conn, user_id, user_role)) {
        return "1=1";
    }

    std::string uid_str = std::to_string(user_id);
    std::string u_role_esc;
    for (char c : user_role) {
        if (c == '\'') u_role_esc += "''";
        else u_role_esc += c;
    }
    std::string u_name_esc;
    for (char c : username) {
        if (c == '\'') u_name_esc += "''";
        else u_name_esc += c;
    }

    std::ostringstream sql;
    // 1. Project BROWSE_PROJECT check
    sql << "((" << item_alias << ".project_id IN (SELECT p.id FROM projects p WHERE p.permission_scheme_id IS NULL OR p.permission_scheme_id IN ("
        << "SELECT psg.scheme_id FROM permission_scheme_grants psg WHERE psg.permission_key = 'BROWSE_PROJECT' AND ("
        << "psg.grantee_type = 'anyone' "
        << "OR (psg.grantee_type = 'group' AND psg.grantee_value IN (SELECT g.name FROM user_groups g JOIN user_group_members ugm ON g.id = ugm.group_id WHERE ugm.user_id = " << uid_str << ")) "
        << "OR (psg.grantee_type = 'role' AND ("
        << "psg.grantee_value IN (SELECT role_name FROM project_role_members WHERE user_id = " << uid_str << " AND project_id = p.id) "
        << "OR lower(psg.grantee_value) = lower('" << u_role_esc << "') "
        << "OR (lower(psg.grantee_value) IN ('administrators','admin') AND lower('" << u_role_esc << "') IN ('admin','administrator')) "
        << "OR (lower(psg.grantee_value) IN ('developers','developer') AND lower('" << u_role_esc << "') = 'developer') "
        << "OR (lower(psg.grantee_value) IN ('qa','qa engineer') AND lower('" << u_role_esc << "') IN ('qa','qa engineer')) "
        << "OR (lower(psg.grantee_value) IN ('product managers','project manager') AND lower('" << u_role_esc << "') IN ('project manager','product manager')) "
        << "OR (lower(psg.grantee_value) IN ('viewers','viewer') AND lower('" << u_role_esc << "') = 'viewer')"
        << ")) "
        << "OR (psg.grantee_type = 'user' AND (psg.grantee_value = '" << uid_str << "' OR lower(psg.grantee_value) = lower('" << u_name_esc << "'))) "
        << "OR ((psg.grantee_type = 'reporter' OR (psg.grantee_type = 'dynamic' AND lower(psg.grantee_value) = 'reporter')) AND " << item_alias << ".reporter_id = " << uid_str << ") "
        << "OR ((psg.grantee_type = 'assignee' OR (psg.grantee_type = 'dynamic' AND lower(psg.grantee_value) = 'assignee')) AND " << item_alias << ".assignee_id = " << uid_str << ") "
        << "OR ((psg.grantee_type = 'lead' OR (psg.grantee_type = 'dynamic' AND lower(psg.grantee_value) = 'lead')) AND p.lead_user_id = " << uid_str << ")"
        << ")))) OR EXISTS (SELECT 1 FROM permission_scheme_grants psg JOIN projects p ON p.id = " << item_alias << ".project_id AND (p.permission_scheme_id = psg.scheme_id OR (p.permission_scheme_id IS NULL AND psg.scheme_id = 1)) JOIN custom_fields cf ON (CAST(cf.id AS TEXT) = psg.grantee_value OR lower(cf.name) = lower(psg.grantee_value) OR lower(cf.field_key) = lower(psg.grantee_value)) JOIN custom_field_values cfv ON cfv.custom_field_id = cf.id AND cfv.work_item_id = " << item_alias << ".id WHERE psg.permission_key = 'BROWSE_PROJECT' AND psg.grantee_type = 'custom_field' AND (cfv.user_value_id = " << uid_str << " OR cfv.text_value = '" << uid_str << "' OR ('" << u_name_esc << "' <> '' AND lower(cfv.text_value) = lower('" << u_name_esc << "'))))) AND ";

    // 2. Issue Security Level check
    sql << "("
        << item_alias << ".security_level_id IS NULL OR "
        << item_alias << ".security_level_id = 0 OR "
        << item_alias << ".security_level_id IN ("
        << "SELECT slg.level_id FROM issue_security_level_grants slg WHERE "
        << "slg.grantee_type = 'anyone' "
        << "OR (slg.grantee_type = 'group' AND slg.grantee_value IN (SELECT g.name FROM user_groups g JOIN user_group_members ugm ON g.id = ugm.group_id WHERE ugm.user_id = " << uid_str << ")) "
        << "OR (slg.grantee_type = 'role' AND ("
        << "slg.grantee_value IN (SELECT role_name FROM project_role_members WHERE user_id = " << uid_str << " AND project_id = " << item_alias << ".project_id) "
        << "OR lower(slg.grantee_value) = lower('" << u_role_esc << "') "
        << "OR (lower(slg.grantee_value) IN ('administrators','admin') AND lower('" << u_role_esc << "') IN ('admin','administrator')) "
        << "OR (lower(slg.grantee_value) IN ('developers','developer') AND lower('" << u_role_esc << "') = 'developer') "
        << "OR (lower(slg.grantee_value) IN ('qa','qa engineer') AND lower('" << u_role_esc << "') IN ('qa','qa engineer')) "
        << "OR (lower(slg.grantee_value) IN ('product managers','project manager') AND lower('" << u_role_esc << "') IN ('project manager','product manager')) "
        << "OR (lower(slg.grantee_value) IN ('viewers','viewer') AND lower('" << u_role_esc << "') = 'viewer')"
        << ")) "
        << "OR (slg.grantee_type = 'user' AND (slg.grantee_value = '" << uid_str << "' OR lower(slg.grantee_value) = lower('" << u_name_esc << "'))) "
        << "OR ((slg.grantee_type = 'reporter' OR (slg.grantee_type = 'dynamic' AND lower(slg.grantee_value) = 'reporter')) AND " << item_alias << ".reporter_id = " << uid_str << ") "
        << "OR ((slg.grantee_type = 'assignee' OR (slg.grantee_type = 'dynamic' AND lower(slg.grantee_value) = 'assignee')) AND " << item_alias << ".assignee_id = " << uid_str << ") "
        << "OR ((slg.grantee_type = 'lead' OR (slg.grantee_type = 'dynamic' AND lower(slg.grantee_value) = 'lead')) AND EXISTS (SELECT 1 FROM projects prj WHERE prj.id = " << item_alias << ".project_id AND prj.lead_user_id = " << uid_str << ")) "
        << "OR (slg.grantee_type = 'custom_field' AND EXISTS (SELECT 1 FROM custom_field_values cfv JOIN custom_fields cf ON cf.id = cfv.custom_field_id WHERE cfv.work_item_id = " << item_alias << ".id AND (CAST(cf.id AS TEXT) = slg.grantee_value OR lower(cf.name) = lower(slg.grantee_value) OR lower(cf.field_key) = lower(slg.grantee_value)) AND (cfv.user_value_id = " << uid_str << " OR cfv.text_value = '" << uid_str << "' OR ('" << u_name_esc << "' <> '' AND lower(cfv.text_value) = lower('" << u_name_esc << "')))))"
        << "))";

    return sql.str();
}

} // namespace security
