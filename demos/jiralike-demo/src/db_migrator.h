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

static constexpr int LATEST_SCHEMA_VERSION = 4;

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

                conn.exec("BEGIN IMMEDIATE;");
                try {
                    conn.exec(sql);
                    auto ins = conn.prepare("INSERT INTO schema_version (version, description) VALUES (?, ?);");
                    ins.bind_int(1, step.version);
                    ins.bind_text(2, step.description);
                    ins.step_done();
                    conn.exec("COMMIT;");
                } catch (...) {
                    try { conn.exec("ROLLBACK;"); } catch (...) {}
                    throw;
                }
                current_version = step.version;
            }
        }
        std::cout << "[jiralike-demo] Database schema successfully upgraded to v"
                  << current_version << "." << std::endl;
    }
}

} // namespace db
