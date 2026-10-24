-- Consolidated reference schema for jiralike-demo (Versions 1..3)
-- Note: On startup, jiralike-demo automatically creates the database if missing
-- and runs sequential migrations from db/migrations/.

CREATE TABLE IF NOT EXISTS schema_version (
    version     INTEGER PRIMARY KEY,
    applied_at  TEXT NOT NULL DEFAULT (datetime('now')),
    description TEXT
);

-- Includes 001_init.sql, 002_workflows_and_gantt.sql, 003_attachments_and_links.sql
