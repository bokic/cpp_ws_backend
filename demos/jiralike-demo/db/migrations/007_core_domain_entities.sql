-- Migration 007: Core Jira Domain Model (Resolutions, Issue Types & Schemes, Components, Versions/Releases)

-- 1. Resolutions
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

-- 2. Configurable Issue Types & Issue Type Schemes
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
    (1, 1, 10),
    (1, 2, 20),
    (1, 3, 30),
    (1, 4, 40),
    (1, 5, 50);

ALTER TABLE projects ADD COLUMN issue_type_scheme_id INTEGER REFERENCES issue_type_schemes(id) ON DELETE SET NULL DEFAULT NULL;
UPDATE projects SET issue_type_scheme_id = 1 WHERE issue_type_scheme_id IS NULL;

-- 3. Project Components & Work Item Component Mappings
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

-- 4. Project Versions / Releases & Work Item Version Associations
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

-- 5. Add Resolution Columns to work_items
ALTER TABLE work_items ADD COLUMN resolution_id INTEGER DEFAULT NULL REFERENCES resolutions(id) ON DELETE SET NULL;
ALTER TABLE work_items ADD COLUMN resolution_date TEXT DEFAULT '';

CREATE INDEX IF NOT EXISTS idx_work_items_resolution ON work_items(resolution_id);

-- Backfill existing 'Done' work items with Fixed resolution
UPDATE work_items
SET resolution_id = 1,
    resolution_date = COALESCE(NULLIF(updated_at, ''), datetime('now'))
WHERE status = 'Done' AND resolution_id IS NULL;

-- 6. Seed Default Components & Versions for Project 1 if it exists
INSERT OR IGNORE INTO components (project_id, name, description, lead_user_id, default_assignee_type)
SELECT 1, 'FastCGI Core', 'C++23 FastCGI request routing, worker pool, and socket handling', lead_user_id, 'component_lead'
FROM projects WHERE id = 1;

INSERT OR IGNORE INTO components (project_id, name, description, lead_user_id, default_assignee_type)
SELECT 1, 'SQLite Storage Engine', 'Connection pooling, WAL migrations, and prepared statement cache', lead_user_id, 'component_lead'
FROM projects WHERE id = 1;

INSERT OR IGNORE INTO components (project_id, name, description, lead_user_id, default_assignee_type)
SELECT 1, 'Angular Board UI', 'Kanban drag-and-drop board, Gantt timeline, and SPA views', lead_user_id, 'project_lead'
FROM projects WHERE id = 1;

INSERT OR IGNORE INTO versions (project_id, name, description, start_date, release_date, status)
SELECT 1, 'v1.0.0', 'Initial foundation release with FastCGI, SQLite migrations, and Kanban board', '2026-10-01', '2026-10-25', 'released'
FROM projects WHERE id = 1;

INSERT OR IGNORE INTO versions (project_id, name, description, start_date, release_date, status)
SELECT 1, 'v1.1.0', 'Connection pooling, core Jira domain entities, components, and release management', '2026-10-26', '2026-11-10', 'unreleased'
FROM projects WHERE id = 1;

INSERT OR IGNORE INTO versions (project_id, name, description, start_date, release_date, status)
SELECT 1, 'v2.0.0', 'Custom fields engine, directed workflows, and JQL search compiler', '2026-11-11', '2026-12-01', 'unreleased'
FROM projects WHERE id = 1;

-- Associate existing demo work items with seeded components and versions
INSERT OR IGNORE INTO work_item_components (work_item_id, component_id)
SELECT w.id, c.id
FROM work_items w
JOIN components c ON c.project_id = w.project_id
WHERE (w.id % 3 = 1 AND c.name = 'FastCGI Core')
   OR (w.id % 3 = 2 AND c.name = 'SQLite Storage Engine')
   OR (w.id % 3 = 0 AND c.name = 'Angular Board UI');

INSERT OR IGNORE INTO work_item_versions (work_item_id, version_id, relation_type)
SELECT w.id, v.id, 'fix'
FROM work_items w
JOIN versions v ON v.project_id = w.project_id
WHERE (w.status = 'Done' AND v.name = 'v1.0.0')
   OR (w.status != 'Done' AND v.name = 'v1.1.0');

INSERT OR IGNORE INTO work_item_versions (work_item_id, version_id, relation_type)
SELECT w.id, v.id, 'affects'
FROM work_items w
JOIN versions v ON v.project_id = w.project_id
WHERE w.category = 'Bug' AND v.name = 'v1.0.0';
