#pragma once

#include "raii_sqlite.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace db {

static constexpr int LATEST_SCHEMA_VERSION = 7;

struct migration_step {
    int version;
    const char *filename;
    const char *description;
    const char *fallback_sql;
};

inline const std::vector<migration_step> &get_migrations() {
    static const std::vector<migration_step> steps = {
        {
            1,
            "db/migrations/001_init.sql",
            "Initial schema for app_config, users, sessions, projects, and sprints",
            R"SQL(
CREATE TABLE IF NOT EXISTS app_config (
    id                INTEGER PRIMARY KEY CHECK (id = 1),
    is_setup_complete INTEGER NOT NULL DEFAULT 0,
    instance_name     TEXT NOT NULL DEFAULT 'JiraLike Workspace',
    created_at        TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at        TEXT NOT NULL DEFAULT (datetime('now'))
);
INSERT OR IGNORE INTO app_config (id, is_setup_complete, instance_name)
VALUES (1, 0, 'JiraLike Workspace');

CREATE TABLE IF NOT EXISTS users (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    username      TEXT NOT NULL UNIQUE,
    email         TEXT NOT NULL UNIQUE,
    password_hash TEXT NOT NULL,
    display_name  TEXT NOT NULL,
    role          TEXT NOT NULL DEFAULT 'Developer',
    avatar_color  TEXT NOT NULL DEFAULT '#0052CC',
    avatar_blob   TEXT DEFAULT '',
    created_at    TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS sessions (
    token      TEXT PRIMARY KEY,
    user_id    INTEGER NOT NULL,
    created_at TEXT NOT NULL DEFAULT (datetime('now')),
    expires_at TEXT NOT NULL,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS projects (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    key          TEXT NOT NULL UNIQUE,
    name         TEXT NOT NULL,
    description  TEXT DEFAULT '',
    lead_user_id INTEGER,
    issue_seq    INTEGER NOT NULL DEFAULT 0,
    created_at   TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (lead_user_id) REFERENCES users(id) ON DELETE SET NULL
);

CREATE TABLE IF NOT EXISTS sprints (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    project_id INTEGER NOT NULL,
    name       TEXT NOT NULL,
    goal       TEXT DEFAULT '',
    status     TEXT NOT NULL DEFAULT 'active',
    start_date TEXT DEFAULT '',
    end_date   TEXT DEFAULT '',
    created_at TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (project_id) REFERENCES projects(id) ON DELETE CASCADE
);
)SQL"
        },
        {
            2,
            "db/migrations/002_workflows_and_gantt.sql",
            "Work items (Epics, Stories, Tasks, Subtasks, Bugs, Issues) and workflow state history",
            R"SQL(
CREATE TABLE IF NOT EXISTS work_items (
    id                 INTEGER PRIMARY KEY AUTOINCREMENT,
    project_id         INTEGER NOT NULL,
    key                TEXT NOT NULL UNIQUE,
    type               TEXT NOT NULL DEFAULT 'Task',
    status             TEXT NOT NULL DEFAULT 'To Do',
    priority           TEXT NOT NULL DEFAULT 'Medium',
    title              TEXT NOT NULL,
    description        TEXT DEFAULT '',
    parent_id          INTEGER DEFAULT NULL,
    sprint_id          INTEGER DEFAULT NULL,
    assignee_id        INTEGER DEFAULT NULL,
    reporter_id        INTEGER DEFAULT NULL,
    story_points       INTEGER NOT NULL DEFAULT 0,
    progress           INTEGER NOT NULL DEFAULT 0,
    start_date         TEXT DEFAULT '',
    due_date           TEXT DEFAULT '',
    severity           TEXT DEFAULT '',
    steps_to_reproduce TEXT DEFAULT '',
    labels             TEXT DEFAULT '',
    board_order        INTEGER NOT NULL DEFAULT 0,
    created_at         TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at         TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (project_id) REFERENCES projects(id) ON DELETE CASCADE,
    FOREIGN KEY (parent_id) REFERENCES work_items(id) ON DELETE SET NULL,
    FOREIGN KEY (sprint_id) REFERENCES sprints(id) ON DELETE SET NULL,
    FOREIGN KEY (assignee_id) REFERENCES users(id) ON DELETE SET NULL,
    FOREIGN KEY (reporter_id) REFERENCES users(id) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS idx_work_items_project ON work_items(project_id);
CREATE INDEX IF NOT EXISTS idx_work_items_status ON work_items(status);
CREATE INDEX IF NOT EXISTS idx_work_items_type ON work_items(type);
CREATE INDEX IF NOT EXISTS idx_work_items_parent ON work_items(parent_id);

CREATE TABLE IF NOT EXISTS workflow_history (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    work_item_id INTEGER NOT NULL,
    from_status  TEXT NOT NULL,
    to_status    TEXT NOT NULL,
    changed_by   INTEGER,
    comment      TEXT DEFAULT '',
    created_at   TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (work_item_id) REFERENCES work_items(id) ON DELETE CASCADE,
    FOREIGN KEY (changed_by) REFERENCES users(id) ON DELETE SET NULL
);
)SQL"
        },
        {
            3,
            "db/migrations/003_attachments_and_links.sql",
            "Comments, work item links, and blob attachments metadata",
            R"SQL(
CREATE TABLE IF NOT EXISTS comments (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    work_item_id INTEGER NOT NULL,
    author_id    INTEGER,
    body         TEXT NOT NULL,
    created_at   TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (work_item_id) REFERENCES work_items(id) ON DELETE CASCADE,
    FOREIGN KEY (author_id) REFERENCES users(id) ON DELETE SET NULL
);

CREATE TABLE IF NOT EXISTS work_item_links (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    source_item_id INTEGER NOT NULL,
    target_item_id INTEGER NOT NULL,
    link_type      TEXT NOT NULL DEFAULT 'relates_to',
    created_at     TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (source_item_id) REFERENCES work_items(id) ON DELETE CASCADE,
    FOREIGN KEY (target_item_id) REFERENCES work_items(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS attachments (
    id                INTEGER PRIMARY KEY AUTOINCREMENT,
    work_item_id      INTEGER NOT NULL,
    uploader_id       INTEGER,
    original_filename TEXT NOT NULL,
    mime_type         TEXT NOT NULL DEFAULT 'application/octet-stream',
    size_bytes        INTEGER NOT NULL DEFAULT 0,
    blob_filename     TEXT NOT NULL UNIQUE,
    created_at        TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (work_item_id) REFERENCES work_items(id) ON DELETE CASCADE,
    FOREIGN KEY (uploader_id) REFERENCES users(id) ON DELETE SET NULL
);
)SQL"
        },
        {
            4,
            "db/migrations/004_new_feature_and_improvement.sql",
            "Add New Feature and Improvement work item types",
            R"SQL(
INSERT INTO work_items (
    project_id, key, type, status, priority, title, description,
    parent_id, sprint_id, assignee_id, reporter_id, story_points,
    progress, start_date, due_date, labels, board_order
)
SELECT
    p.id,
    p.key || '-' || (p.issue_seq + 1),
    'New Feature',
    'In Progress',
    'High',
    'Real-time Kanban & Gantt filter presets',
    'Add ability to filter work items by New Feature and Improvement types across Kanban and Gantt boards.',
    1, 1, 1, 1, 5, 55, '2026-10-24', '2026-11-01', 'feature,board,ux', 120
FROM projects p
WHERE p.id = 1
  AND NOT EXISTS (SELECT 1 FROM work_items WHERE type = 'New Feature');

UPDATE projects
SET issue_seq = (SELECT COUNT(*) FROM work_items WHERE project_id = projects.id)
WHERE id = 1;

INSERT INTO work_items (
    project_id, key, type, status, priority, title, description,
    parent_id, sprint_id, assignee_id, reporter_id, story_points,
    progress, start_date, due_date, labels, board_order
)
SELECT
    p.id,
    p.key || '-' || (p.issue_seq + 1),
    'Improvement',
    'To Do',
    'Medium',
    'Optimize SQLite WAL checkpointing and prepared statement caching',
    'Enhance C++23 FastCGI SQLite connection settings for faster concurrent read/write throughput.',
    1, 1, 1, 1, 3, 25, '2026-10-26', '2026-11-03', 'improvement,performance,sqlite', 130
FROM projects p
WHERE p.id = 1
  AND NOT EXISTS (SELECT 1 FROM work_items WHERE type = 'Improvement');

UPDATE projects
SET issue_seq = (SELECT COUNT(*) FROM work_items WHERE project_id = projects.id)
WHERE id = 1;
)SQL"
        },
        {
            5,
            "db/migrations/005_current_sprint.sql",
            "Add is_current flag to sprints table to define the Current Sprint",
            R"SQL(
ALTER TABLE sprints ADD COLUMN is_current INTEGER NOT NULL DEFAULT 0;

UPDATE sprints
SET is_current = 1
WHERE id = (
    SELECT id FROM sprints WHERE status = 'active' ORDER BY id ASC LIMIT 1
)
AND NOT EXISTS (SELECT 1 FROM sprints WHERE is_current = 1);
)SQL"
        },
        {
            6,
            "db/migrations/006_work_item_category.sql",
            "Separate work item Hierarchy Type (Epic, Story, Task, Subtask) and Category (New Feature, Improvement, Bug, Issue)",
            R"SQL(
ALTER TABLE work_items ADD COLUMN category TEXT NOT NULL DEFAULT 'New Feature';

UPDATE work_items SET category = 'Bug',         type = 'Task'  WHERE type = 'Bug';
UPDATE work_items SET category = 'New Feature', type = 'Story' WHERE type = 'New Feature';
UPDATE work_items SET category = 'Improvement', type = 'Task'  WHERE type = 'Improvement';
UPDATE work_items SET category = 'Issue',       type = 'Task'  WHERE type = 'Issue';

UPDATE work_items SET category = 'Improvement' WHERE type = 'Task' AND category = 'New Feature' AND (id % 2 = 0);
UPDATE work_items SET category = 'Improvement' WHERE type = 'Subtask' AND (id % 2 = 1);

CREATE INDEX IF NOT EXISTS idx_work_items_category ON work_items(category);
)SQL"
        },
        {
            7,
            "db/migrations/007_core_domain_entities.sql",
            "Core Jira domain entities: resolutions, issue_types, issue_type_schemes, components, versions, and work item associations",
            R"SQL(
CREATE TABLE IF NOT EXISTS resolutions (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL UNIQUE,
    description TEXT DEFAULT '',
    sort_order  INTEGER NOT NULL DEFAULT 0,
    is_default  INTEGER NOT NULL DEFAULT 0,
    created_at  TEXT NOT NULL DEFAULT (datetime('now'))
);

INSERT OR IGNORE INTO resolutions (id, name, description, sort_order, is_default) VALUES
    (1, 'Fixed',            'A fix for this issue is checked into the tree and tested.', 10, 1),
    (2, 'Won''t Fix',       'The problem described is an issue which will never be fixed.', 20, 0),
    (3, 'Duplicate',        'The problem is a duplicate of an existing issue.', 30, 0),
    (4, 'Incomplete',       'The problem is not completely described.', 40, 0),
    (5, 'Cannot Reproduce', 'Attempts at reproducing this issue failed, or not enough information was available.', 50, 0),
    (6, 'Declined',         'The work item was reviewed and declined.', 60, 0);

CREATE TABLE IF NOT EXISTS issue_types (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    name            TEXT NOT NULL UNIQUE,
    description     TEXT DEFAULT '',
    icon_color      TEXT NOT NULL DEFAULT '#0052CC',
    is_subtask      INTEGER NOT NULL DEFAULT 0,
    hierarchy_level INTEGER NOT NULL DEFAULT 0,
    created_at      TEXT NOT NULL DEFAULT (datetime('now'))
);

INSERT OR IGNORE INTO issue_types (id, name, description, icon_color, is_subtask, hierarchy_level) VALUES
    (1, 'Initiative', 'High-level strategic portfolio initiative spanning multiple epics', '#403294', 0, 2),
    (2, 'Epic',       'Large body of work that can be broken down into stories and tasks', '#6554C0', 0, 1),
    (3, 'Story',      'User-centric feature specification and requirement',                '#36B37E', 0, 0),
    (4, 'Task',       'General engineering or operational work item',                      '#0052CC', 0, 0),
    (5, 'Subtask',    'Granular child item belonging to a parent Story or Task',           '#00B8D9', 1, -1);

CREATE TABLE IF NOT EXISTS issue_type_schemes (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL UNIQUE,
    description TEXT DEFAULT '',
    is_default  INTEGER NOT NULL DEFAULT 0,
    created_at  TEXT NOT NULL DEFAULT (datetime('now'))
);

INSERT OR IGNORE INTO issue_type_schemes (id, name, description, is_default) VALUES
    (1, 'Default Agile Software Scheme', 'Standard Jira Software hierarchy supporting Initiatives, Epics, Stories, Tasks, and Subtasks.', 1);

CREATE TABLE IF NOT EXISTS issue_type_scheme_items (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    scheme_id     INTEGER NOT NULL,
    issue_type_id INTEGER NOT NULL,
    sort_order    INTEGER NOT NULL DEFAULT 0,
    FOREIGN KEY (scheme_id) REFERENCES issue_type_schemes(id) ON DELETE CASCADE,
    FOREIGN KEY (issue_type_id) REFERENCES issue_types(id) ON DELETE CASCADE,
    UNIQUE (scheme_id, issue_type_id)
);

INSERT OR IGNORE INTO issue_type_scheme_items (scheme_id, issue_type_id, sort_order) VALUES
    (1, 1, 10), (1, 2, 20), (1, 3, 30), (1, 4, 40), (1, 5, 50);

ALTER TABLE projects ADD COLUMN issue_type_scheme_id INTEGER DEFAULT 1 REFERENCES issue_type_schemes(id) ON DELETE SET NULL;

CREATE TABLE IF NOT EXISTS components (
    id                    INTEGER PRIMARY KEY AUTOINCREMENT,
    project_id            INTEGER NOT NULL,
    name                  TEXT NOT NULL,
    description           TEXT DEFAULT '',
    lead_user_id          INTEGER DEFAULT NULL,
    default_assignee_type TEXT NOT NULL DEFAULT 'project_lead',
    created_at            TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (project_id) REFERENCES projects(id) ON DELETE CASCADE,
    FOREIGN KEY (lead_user_id) REFERENCES users(id) ON DELETE SET NULL,
    UNIQUE (project_id, name)
);

CREATE TABLE IF NOT EXISTS work_item_components (
    work_item_id INTEGER NOT NULL,
    component_id INTEGER NOT NULL,
    PRIMARY KEY (work_item_id, component_id),
    FOREIGN KEY (work_item_id) REFERENCES work_items(id) ON DELETE CASCADE,
    FOREIGN KEY (component_id) REFERENCES components(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_wic_item ON work_item_components(work_item_id);
CREATE INDEX IF NOT EXISTS idx_wic_comp ON work_item_components(component_id);

CREATE TABLE IF NOT EXISTS versions (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    project_id   INTEGER NOT NULL,
    name         TEXT NOT NULL,
    description  TEXT DEFAULT '',
    start_date   TEXT DEFAULT '',
    release_date TEXT DEFAULT '',
    status       TEXT NOT NULL DEFAULT 'unreleased',
    created_at   TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (project_id) REFERENCES projects(id) ON DELETE CASCADE,
    UNIQUE (project_id, name)
);

CREATE TABLE IF NOT EXISTS work_item_versions (
    work_item_id  INTEGER NOT NULL,
    version_id    INTEGER NOT NULL,
    relation_type TEXT NOT NULL DEFAULT 'fix',
    PRIMARY KEY (work_item_id, version_id, relation_type),
    FOREIGN KEY (work_item_id) REFERENCES work_items(id) ON DELETE CASCADE,
    FOREIGN KEY (version_id) REFERENCES versions(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_wiv_item ON work_item_versions(work_item_id);
CREATE INDEX IF NOT EXISTS idx_wiv_ver ON work_item_versions(version_id);

ALTER TABLE work_items ADD COLUMN resolution_id INTEGER DEFAULT NULL REFERENCES resolutions(id) ON DELETE SET NULL;
ALTER TABLE work_items ADD COLUMN resolution_date TEXT DEFAULT '';

CREATE INDEX IF NOT EXISTS idx_work_items_resolution ON work_items(resolution_id);

UPDATE work_items
SET resolution_id = 1,
    resolution_date = COALESCE(NULLIF(updated_at, ''), datetime('now'))
WHERE status = 'Done' AND resolution_id IS NULL;
)SQL"
        }
    };
    return steps;
}

inline std::string load_migration_sql(const migration_step &step) {
    std::ifstream ifs(step.filename);
    if (ifs.is_open()) {
        std::ostringstream oss;
        oss << ifs.rdbuf();
        std::string content = oss.str();
        if (!content.empty()) {
            return content;
        }
    }
    return step.fallback_sql;
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
}

} // namespace db
