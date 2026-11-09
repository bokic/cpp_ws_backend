#pragma once

#include "raii_sqlite.h"
#include "embedded_sql.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace db {

static constexpr int LATEST_SCHEMA_VERSION = 27;

struct migration_step {
    int version;
    const char *filename;
    const char *description;
    const char *fallback_sql = "";
};

inline const std::vector<migration_step> &get_migrations() {
    static std::vector<migration_step> steps = {
        {
            1,
            "db/migrations/001_init.sql",
            "Initial schema for app_config, users, sessions, projects, and sprints"
        },
        {
            2,
            "db/migrations/002_workflows_and_gantt.sql",
            "Work items (Epics, Stories, Tasks, Subtasks, Bugs, Issues) and workflow state history"
        },
        {
            3,
            "db/migrations/003_attachments_and_links.sql",
            "Comments, work item links, and blob attachments metadata"
        },
        {
            4,
            "db/migrations/004_new_feature_and_improvement.sql",
            "Add New Feature and Improvement work item types"
        },
        {
            5,
            "db/migrations/005_current_sprint.sql",
            "Add is_current flag to sprints table to define the Current Sprint"
        },
        {
            6,
            "db/migrations/006_work_item_category.sql",
            "Separate work item Hierarchy Type (Epic, Story, Task, Subtask) and Category (New Feature, Improvement, Bug, Issue)"
        },
        {
            7,
            "db/migrations/007_core_domain_entities.sql",
            "Core Jira domain entities: resolutions, issue_types, issue_type_schemes, components, versions, and work item associations"
        },
        {
            8,
            "db/migrations/008_custom_fields_and_screens.sql",
            "Dynamic Custom Fields Engine, Field Configurations, Screens, Screen Tabs & Screen Schemes"
        },
        {
            9,
            "db/migrations/009_directed_workflows.sql",
            "Directed Graph Workflow Engine: workflows, workflow_statuses, workflow_transitions, workflow_transition_rules, workflow_schemes"
        },
        {
            10,
            "db/migrations/010_jql_and_fts5.sql",
            "JQL Saved Filters, Subscriptions, and SQLite FTS5 Full-Text Search virtual table with triggers"
        },
        {
            11,
            "db/migrations/011_multi_boards_and_backlog.sql",
            "Scrum and Kanban Multi-Board Engine, Column Status Mappings, WIP Limits, Swimlanes, Quick Filters, and Sprint Lifecycle Velocity"
        },
        {
            12,
            "db/migrations/012_time_tracking_and_worklogs.sql",
            "Time Tracking, Estimation Triad (Original, Remaining, Time Spent) & Worklogs Subsystem"
        },
        {
            13,
            "db/migrations/013_agile_analytics_and_reporting.sql",
            "Agile Analytics, Sprint Events & Burndown/Burnup/CFD/Cycle Time Reporting Engine"
        },
        {
            14,
            "db/migrations/014_dashboards_and_gadgets.sql",
            "Configurable Dashboards & Gadgets Framework (Step 9 & Epic 7)"
        },
        {
            15,
            "db/migrations/015_rich_text_mentions_and_collaboration.sql",
            "Rich text audit changelog, user mentions, watchers, and voting tables"
        },
        {
            16,
            "db/migrations/016_permissions_and_security_schemes.sql",
            "Granular permission schemes, issue security schemes, user groups, and personal access tokens"
        },
        {
            17,
            "db/migrations/017_automation_engine.sql",
            "No-Code Automation Engine: automation_rules, triggers, conditions, actions, and audit logs"
        },
        {
            18,
            "db/migrations/018_notifications_and_webhooks.sql",
            "Notifications, SMTP Delivery & Outbound Webhooks: schemes, in_app_notifications, outbound_emails, webhooks, deliveries"
        },
        {
            19,
            "db/migrations/019_vcs_and_smart_commits.sql",
            "Developer Tooling & Git Integration: git_branches, git_commits, git_pull_requests, git_builds"
        },
        {
            20,
            "db/migrations/020_itsm_and_service_desk.sql",
            "ITSM & Service Desk Capabilities: service_desks, request_types, sla_metrics, sla_goals, sla_calendars"
        },
        {
            21,
            "db/migrations/021_bulk_operations_and_trash.sql",
            "Bulk Operations & Trash Bin (Soft Delete & 30-Day Retention)"
        },
        {
            22,
            "db/migrations/022_advanced_roadmaps_and_gantt.sql",
            "Advanced Roadmaps, Gantt Dependencies, Critical Path & Capacity Planning"
        },
        {
            23,
            "db/migrations/023_enterprise_security_and_mail.sql",
            "Enterprise SSO (SAML/OIDC), RFC 6238 TOTP 2FA, and Inbound Mail Processing"
        },
        {
            24,
            "db/migrations/024_user_preferences_and_governance.sql",
            "User Preferences, Localization & Governance"
        },
        {
            25,
            "db/migrations/025_component_lead_and_project_default_assignee.sql",
            "Component Lead and Project Default Assignee Routing"
        },
        {
            26,
            "db/migrations/026_customer_portal_identity_and_organizations.sql",
            "Dedicated Portal-Only Customer Identity, Customer Organizations, and Comment Privacy"
        },
        {
            27,
            "db/migrations/027_custom_field_user_picker_grantees.sql",
            "Custom Field User Picker Grantees and work_item_custom_fields view"
        },
    };
    static const bool initialized = []() {
        for (auto &step : steps) {
            auto sql = embedded::get_sql(step.filename);
            if (!sql.empty()) {
                step.fallback_sql = sql.data();
            }
        }
        return true;
    }();
    (void)initialized;
    return steps;
}

inline std::string load_migration_sql(const migration_step &step) {
    // 1. Retrieve embedded SQL compiled directly into binary
    auto embedded = embedded::get_sql(step.filename);
    if (!embedded.empty()) {
        return std::string(embedded);
    }

    // 2. Fall back to external file on disk if available (e.g. for development override)
    std::ifstream ifs(step.filename);
    if (ifs.is_open()) {
        std::ostringstream oss;
        oss << ifs.rdbuf();
        std::string content = oss.str();
        if (!content.empty()) {
            return content;
        }
    }

    // 3. Fall back to step.fallback_sql if set
    if (step.fallback_sql && *step.fallback_sql) {
        return step.fallback_sql;
    }

    throw std::runtime_error(std::string("Missing migration SQL for: ") + step.filename);
}

inline void ensure_storage_and_migrate(const std::string &db_path, const std::string &blobs_dir) {
    namespace fs = std::filesystem;

    fs::path db_fs_path(db_path);
    if (db_fs_path.has_parent_path()) {
        fs::create_directories(db_fs_path.parent_path());
    }
    fs::create_directories(blobs_dir);

    bool db_existed = fs::exists(db_fs_path) && fs::file_size(db_fs_path) > 0;
    if (!db_existed) {
        std::cout << "[jiralike-demo] Database '" << db_path
                  << "' does not exist. Creating blank database and enabling first-time webapp setup page..."
                  << std::endl;
    }

    sqlite_db conn(db_path);

    conn.exec(R"SQL(
        CREATE TABLE IF NOT EXISTS schema_version (
            version     INTEGER PRIMARY KEY,
            applied_at  TEXT NOT NULL DEFAULT (datetime('now')),
            description TEXT
        );
    )SQL");

    int current_version = 0;
    {
        auto stmt = conn.prepare("SELECT COALESCE(MAX(version), 0) FROM schema_version;");
        if (stmt.step() == SQLITE_ROW) {
            current_version = stmt.column_int(0);
        }
    }

    std::cout << "[jiralike-demo] Current DB schema version: " << current_version
              << " (latest target: " << LATEST_SCHEMA_VERSION << ")" << std::endl;

    if (current_version < LATEST_SCHEMA_VERSION) {
        const auto &migrations = get_migrations();
        for (const auto &step : migrations) {
            if (step.version > current_version) {
                std::cout << "[jiralike-demo] Upgrading database schema to v" << step.version
                          << ": " << step.description << "..." << std::endl;
                std::string sql = load_migration_sql(step);

                sqlite_transaction tx(conn);
                conn.exec(sql);
                auto ins = conn.prepare("INSERT INTO schema_version (version, description) VALUES (?, ?);");
                ins.bind_int(1, step.version);
                ins.bind_text(2, step.description);
                ins.step_done();
                tx.commit();
                current_version = step.version;
            }
        }
        std::cout << "[jiralike-demo] Database schema successfully upgraded to v"
                  << current_version << "." << std::endl;
    }

    // Ensure added_at column exists on customer_organization_members if migrated on an earlier v26 snapshot
    try {
        bool has_added_at = false;
        auto ti = conn.prepare("PRAGMA table_info(customer_organization_members);");
        while (ti.step() == SQLITE_ROW) {
            if (ti.column_str(1) == "added_at") {
                has_added_at = true;
                break;
            }
        }
        if (!has_added_at) {
            conn.exec("ALTER TABLE customer_organization_members ADD COLUMN added_at TEXT NOT NULL DEFAULT '';");
            conn.exec("UPDATE customer_organization_members SET added_at = datetime('now') WHERE added_at = '';");
        }
    } catch (...) {
    }

    // Ensure work_item_custom_fields view exists
    try {
        conn.exec("CREATE VIEW IF NOT EXISTS work_item_custom_fields AS SELECT id, work_item_id, custom_field_id, CASE WHEN user_value_id IS NOT NULL AND user_value_id > 0 THEN CAST(user_value_id AS TEXT) ELSE COALESCE(text_value, '') END AS value FROM custom_field_values;");
    } catch (...) {
    }
}

} // namespace db
