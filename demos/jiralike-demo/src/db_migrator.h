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

static constexpr int LATEST_SCHEMA_VERSION = 16;

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
        },
        {
            8,
            "db/migrations/008_custom_fields_and_screens.sql",
            "Dynamic Custom Fields Engine, Field Configurations, Screens, Screen Tabs & Screen Schemes",
            R"SQL(
CREATE TABLE IF NOT EXISTS custom_fields (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    name          TEXT NOT NULL,
    field_key     TEXT NOT NULL UNIQUE,
    field_type    TEXT NOT NULL DEFAULT 'short_text',
    description   TEXT DEFAULT '',
    default_value TEXT DEFAULT '',
    is_required   INTEGER NOT NULL DEFAULT 0,
    is_searchable INTEGER NOT NULL DEFAULT 1,
    min_value     REAL DEFAULT NULL,
    max_value     REAL DEFAULT NULL,
    formula_expr  TEXT DEFAULT '',
    sort_order    INTEGER NOT NULL DEFAULT 0,
    created_at    TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE INDEX IF NOT EXISTS idx_custom_fields_key ON custom_fields(field_key);

INSERT OR IGNORE INTO custom_fields (
    id, name, field_key, field_type, description, default_value, is_required, is_searchable, min_value, max_value, formula_expr, sort_order
) VALUES
    (1, 'Target Environment',      'environment',         'select_single',      'Deployment or test environment where the issue occurs or is targeted.', 'Production', 0, 1, NULL, NULL, '', 10),
    (2, 'Acceptance Criteria',     'acceptance_criteria', 'long_text',          'Testable Definition of Done (DoD) criteria in Markdown checklist format.', '', 0, 1, NULL, NULL, '', 20),
    (3, 'Customer Impact Tier',    'customer_impact',     'select_single',      'Business and customer SLA severity tier impacted by this work item.', 'Tier 3 - Moderate', 0, 1, NULL, NULL, '', 30),
    (4, 'Technical Risk Score',    'risk_score',          'number',             'Numeric risk assessment score from 0 (low risk) to 100 (high complexity/risk).', '15', 0, 1, 0, 100, '', 40),
    (5, 'Target Deployment Date',  'target_deploy_date',  'date',               'Scheduled production rollout or maintenance window date.', '', 0, 1, NULL, NULL, '', 50),
    (6, 'QA Sign-off Reviewer',    'qa_reviewer',         'user_picker_single', 'Designated QA engineer or peer reviewer responsible for verification.', '', 0, 1, NULL, NULL, '', 60),
    (7, 'Design / Spec URL',       'spec_url',            'url',                'External link to RFC, architecture specification, or Figma design document.', '', 0, 1, NULL, NULL, '', 70),
    (8, 'Security Audit Passed',   'security_reviewed',   'checkbox',           'Indicates whether security review and threat modeling are complete.', 'false', 0, 1, NULL, NULL, '', 80),
    (9, 'Affected Platforms',      'affected_platforms',  'select_multi',       'Operating systems, browsers, or runtime platforms impacted.', '["Linux x86_64"]', 0, 1, NULL, NULL, '', 90),
    (10,'Estimated Effort Hours',  'effort_hours_formula','formula',            'Calculated engineering hours derived automatically from Story Points (story_points * 4).', '', 0, 0, NULL, NULL, 'story_points * 4', 100);

CREATE TABLE IF NOT EXISTS custom_field_options (
    id               INTEGER PRIMARY KEY AUTOINCREMENT,
    custom_field_id  INTEGER NOT NULL,
    parent_option_id INTEGER DEFAULT NULL,
    value            TEXT NOT NULL,
    color            TEXT NOT NULL DEFAULT '#0052CC',
    sort_order       INTEGER NOT NULL DEFAULT 0,
    is_active        INTEGER NOT NULL DEFAULT 1,
    is_default       INTEGER NOT NULL DEFAULT 0,
    FOREIGN KEY (custom_field_id) REFERENCES custom_fields(id) ON DELETE CASCADE,
    FOREIGN KEY (parent_option_id) REFERENCES custom_field_options(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_cfo_field ON custom_field_options(custom_field_id, sort_order);

INSERT OR IGNORE INTO custom_field_options (id, custom_field_id, value, color, sort_order, is_active, is_default) VALUES
    (1,  1, 'Production',               '#DE350B', 10, 1, 1),
    (2,  1, 'Staging',                  '#FF991F', 20, 1, 0),
    (3,  1, 'QA Cluster',               '#00B8D9', 30, 1, 0),
    (4,  1, 'Local Dev',                '#36B37E', 40, 1, 0),
    (5,  3, 'Tier 1 - Enterprise Critical', '#DE350B', 10, 1, 0),
    (6,  3, 'Tier 2 - High',                '#FF991F', 20, 1, 0),
    (7,  3, 'Tier 3 - Moderate',            '#0052CC', 30, 1, 1),
    (8,  3, 'Internal Only',                '#6B778C', 40, 1, 0),
    (9,  9, 'Linux x86_64',             '#0052CC', 10, 1, 1),
    (10, 9, 'Linux ARM64',              '#00B8D9', 20, 1, 0),
    (11, 9, 'Docker / Kubernetes',      '#6554C0', 30, 1, 0),
    (12, 9, 'Web Browser (SPA)',        '#36B37E', 40, 1, 0);

CREATE TABLE IF NOT EXISTS custom_field_values (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    work_item_id    INTEGER NOT NULL,
    custom_field_id INTEGER NOT NULL,
    text_value      TEXT DEFAULT '',
    numeric_value   REAL DEFAULT NULL,
    date_value      TEXT DEFAULT '',
    user_value_id   INTEGER DEFAULT NULL,
    json_value      TEXT DEFAULT '',
    updated_at      TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (work_item_id) REFERENCES work_items(id) ON DELETE CASCADE,
    FOREIGN KEY (custom_field_id) REFERENCES custom_fields(id) ON DELETE CASCADE,
    FOREIGN KEY (user_value_id) REFERENCES users(id) ON DELETE SET NULL,
    UNIQUE (work_item_id, custom_field_id)
);

CREATE UNIQUE INDEX IF NOT EXISTS idx_cfv_item_field ON custom_field_values(work_item_id, custom_field_id);
CREATE INDEX IF NOT EXISTS idx_cfv_field ON custom_field_values(custom_field_id);

CREATE TABLE IF NOT EXISTS field_configurations (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL UNIQUE,
    description TEXT DEFAULT '',
    is_default  INTEGER NOT NULL DEFAULT 0,
    created_at  TEXT NOT NULL DEFAULT (datetime('now'))
);

INSERT OR IGNORE INTO field_configurations (id, name, description, is_default) VALUES
    (1, 'Default Software Field Configuration', 'Standard field visibility, required rules, and markdown renderers for Agile projects.', 1);

CREATE TABLE IF NOT EXISTS field_configuration_items (
    id                   INTEGER PRIMARY KEY AUTOINCREMENT,
    configuration_id     INTEGER NOT NULL,
    custom_field_id      INTEGER NOT NULL,
    issue_type           TEXT NOT NULL DEFAULT '*',
    is_required          INTEGER NOT NULL DEFAULT 0,
    is_hidden            INTEGER NOT NULL DEFAULT 0,
    renderer             TEXT NOT NULL DEFAULT 'plain',
    default_value        TEXT DEFAULT '',
    description_override TEXT DEFAULT '',
    FOREIGN KEY (configuration_id) REFERENCES field_configurations(id) ON DELETE CASCADE,
    FOREIGN KEY (custom_field_id) REFERENCES custom_fields(id) ON DELETE CASCADE,
    UNIQUE (configuration_id, custom_field_id, issue_type)
);

INSERT OR IGNORE INTO field_configuration_items (
    configuration_id, custom_field_id, issue_type, is_required, is_hidden, renderer, default_value, description_override
) VALUES
    (1, 1, '*', 0, 0, 'plain',    'Production',        ''),
    (1, 2, '*', 0, 0, 'markdown', '',                  'Markdown checklist of acceptance criteria'),
    (1, 3, '*', 0, 0, 'plain',    'Tier 3 - Moderate', ''),
    (1, 4, '*', 0, 0, 'plain',    '15',                ''),
    (1, 5, '*', 0, 0, 'plain',    '',                  ''),
    (1, 6, '*', 0, 0, 'plain',    '',                  ''),
    (1, 7, '*', 0, 0, 'plain',    '',                  ''),
    (1, 8, '*', 0, 0, 'plain',    'false',             ''),
    (1, 9, '*', 0, 0, 'plain',    '["Linux x86_64"]',  ''),
    (1, 10,'*', 0, 0, 'plain',    '',                  '');

CREATE TABLE IF NOT EXISTS field_configuration_schemes (
    id               INTEGER PRIMARY KEY AUTOINCREMENT,
    name             TEXT NOT NULL UNIQUE,
    description      TEXT DEFAULT '',
    project_id       INTEGER DEFAULT NULL,
    configuration_id INTEGER NOT NULL DEFAULT 1,
    is_default       INTEGER NOT NULL DEFAULT 0,
    created_at       TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (project_id) REFERENCES projects(id) ON DELETE CASCADE,
    FOREIGN KEY (configuration_id) REFERENCES field_configurations(id) ON DELETE CASCADE
);

INSERT OR IGNORE INTO field_configuration_schemes (id, name, description, project_id, configuration_id, is_default) VALUES
    (1, 'Default Field Configuration Scheme', 'Binds the Default Software Field Configuration to all issue types.', NULL, 1, 1);

CREATE TABLE IF NOT EXISTS screens (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL UNIQUE,
    description TEXT DEFAULT '',
    screen_type TEXT NOT NULL DEFAULT 'default',
    created_at  TEXT NOT NULL DEFAULT (datetime('now'))
);

INSERT OR IGNORE INTO screens (id, name, description, screen_type) VALUES
    (1, 'Default Create Issue Screen',        'Organized tabs capturing essential fields and custom metadata when creating a new issue.', 'create'),
    (2, 'Default Edit & View Issue Screen',   'Comprehensive tabbed layout for viewing and inline editing of work item custom fields.',  'edit'),
    (3, 'Resolve / Close Transition Screen',  'Popup screen presented when transitioning an issue to Done or Resolved.',                  'transition');

CREATE TABLE IF NOT EXISTS screen_tabs (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    screen_id  INTEGER NOT NULL,
    name       TEXT NOT NULL,
    sort_order INTEGER NOT NULL DEFAULT 0,
    FOREIGN KEY (screen_id) REFERENCES screens(id) ON DELETE CASCADE,
    UNIQUE (screen_id, name)
);

INSERT OR IGNORE INTO screen_tabs (id, screen_id, name, sort_order) VALUES
    (1, 1, 'General & Acceptance',    10),
    (2, 1, 'Engineering & QA',        20),
    (3, 1, 'Release & Compliance',    30),
    (4, 2, 'General & Acceptance',    10),
    (5, 2, 'Engineering & QA',        20),
    (6, 2, 'Release & Compliance',    30),
    (7, 3, 'Resolution & Sign-off',   10);

CREATE TABLE IF NOT EXISTS screen_tab_fields (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    tab_id          INTEGER NOT NULL,
    field_key       TEXT NOT NULL,
    custom_field_id INTEGER DEFAULT NULL,
    sort_order      INTEGER NOT NULL DEFAULT 0,
    FOREIGN KEY (tab_id) REFERENCES screen_tabs(id) ON DELETE CASCADE,
    FOREIGN KEY (custom_field_id) REFERENCES custom_fields(id) ON DELETE CASCADE,
    UNIQUE (tab_id, field_key)
);

CREATE INDEX IF NOT EXISTS idx_stf_tab ON screen_tab_fields(tab_id, sort_order);

INSERT OR IGNORE INTO screen_tab_fields (tab_id, field_key, custom_field_id, sort_order) VALUES
    (1, 'environment',          1, 10),
    (1, 'acceptance_criteria',  2, 20),
    (1, 'customer_impact',      3, 30),
    (2, 'risk_score',           4, 10),
    (2, 'qa_reviewer',          6, 20),
    (2, 'affected_platforms',   9, 30),
    (3, 'target_deploy_date',   5, 10),
    (3, 'spec_url',             7, 20),
    (3, 'security_reviewed',    8, 30),
    (4, 'environment',          1, 10),
    (4, 'acceptance_criteria',  2, 20),
    (4, 'customer_impact',      3, 30),
    (4, 'effort_hours_formula', 10,40),
    (5, 'risk_score',           4, 10),
    (5, 'qa_reviewer',          6, 20),
    (5, 'affected_platforms',   9, 30),
    (6, 'target_deploy_date',   5, 10),
    (6, 'spec_url',             7, 20),
    (6, 'security_reviewed',    8, 30),
    (7, 'environment',          1, 10),
    (7, 'qa_reviewer',          6, 20),
    (7, 'security_reviewed',    8, 30);

CREATE TABLE IF NOT EXISTS screen_schemes (
    id                   INTEGER PRIMARY KEY AUTOINCREMENT,
    name                 TEXT NOT NULL UNIQUE,
    description          TEXT DEFAULT '',
    project_id           INTEGER DEFAULT NULL,
    is_default           INTEGER NOT NULL DEFAULT 0,
    create_screen_id     INTEGER NOT NULL DEFAULT 1,
    edit_screen_id       INTEGER NOT NULL DEFAULT 2,
    view_screen_id       INTEGER NOT NULL DEFAULT 2,
    transition_screen_id INTEGER NOT NULL DEFAULT 3,
    created_at           TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (project_id) REFERENCES projects(id) ON DELETE CASCADE,
    FOREIGN KEY (create_screen_id) REFERENCES screens(id) ON DELETE SET DEFAULT,
    FOREIGN KEY (edit_screen_id) REFERENCES screens(id) ON DELETE SET DEFAULT,
    FOREIGN KEY (view_screen_id) REFERENCES screens(id) ON DELETE SET DEFAULT,
    FOREIGN KEY (transition_screen_id) REFERENCES screens(id) ON DELETE SET DEFAULT
);

INSERT OR IGNORE INTO screen_schemes (
    id, name, description, project_id, is_default, create_screen_id, edit_screen_id, view_screen_id, transition_screen_id
) VALUES
    (1, 'Default Agile Screen Scheme', 'Maps Create, Edit, View, and Transition operations to the standard tabbed screens.', NULL, 1, 1, 2, 2, 3);

INSERT OR IGNORE INTO custom_field_values (work_item_id, custom_field_id, text_value)
SELECT w.id, 1, CASE WHEN w.category = 'Bug' THEN 'Production' ELSE 'Staging' END
FROM work_items w;

INSERT OR IGNORE INTO custom_field_values (work_item_id, custom_field_id, text_value)
SELECT w.id, 2, '- [x] Unit & integration tests passing' || char(10) || '- [x] Zero SQLite lock contention under load'
FROM work_items w
WHERE w.type IN ('Story', 'Epic');

INSERT OR IGNORE INTO custom_field_values (work_item_id, custom_field_id, text_value)
SELECT w.id, 3, CASE WHEN w.priority IN ('Critical', 'Highest', 'High') THEN 'Tier 1 - Enterprise Critical' ELSE 'Tier 2 - High' END
FROM work_items w;

INSERT OR IGNORE INTO custom_field_values (work_item_id, custom_field_id, numeric_value, text_value)
SELECT w.id, 4, (w.story_points * 8.0), CAST(w.story_points * 8 AS TEXT)
FROM work_items w
WHERE w.story_points > 0;

INSERT OR IGNORE INTO custom_field_values (work_item_id, custom_field_id, date_value, text_value)
SELECT w.id, 5, w.due_date, w.due_date
FROM work_items w
WHERE w.due_date != '';

INSERT OR IGNORE INTO custom_field_values (work_item_id, custom_field_id, user_value_id, text_value)
SELECT w.id, 6, COALESCE(w.assignee_id, 1), CAST(COALESCE(w.assignee_id, 1) AS TEXT)
FROM work_items w
WHERE w.assignee_id IS NOT NULL;

INSERT OR IGNORE INTO custom_field_values (work_item_id, custom_field_id, text_value)
SELECT w.id, 7, 'https://docs.jiralike.local/specs/' || lower(w.key)
FROM work_items w
WHERE w.type IN ('Epic', 'Story');

INSERT OR IGNORE INTO custom_field_values (work_item_id, custom_field_id, text_value, numeric_value)
SELECT w.id, 8, CASE WHEN w.status = 'Done' THEN 'true' ELSE 'false' END, CASE WHEN w.status = 'Done' THEN 1 ELSE 0 END
FROM work_items w;

INSERT OR IGNORE INTO custom_field_values (work_item_id, custom_field_id, json_value, text_value)
SELECT w.id, 9, '["Linux x86_64","Docker / Kubernetes"]', 'Linux x86_64, Docker / Kubernetes'
FROM work_items w;
)SQL"
        },
        {
            9,
            "db/migrations/009_directed_workflows.sql",
            "Directed Graph Workflow Engine: workflows, workflow_statuses, workflow_transitions, workflow_transition_rules, workflow_schemes",
            R"SQL(
CREATE TABLE IF NOT EXISTS workflows (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL UNIQUE,
    description TEXT NOT NULL DEFAULT '',
    is_default  INTEGER NOT NULL DEFAULT 0,
    is_system   INTEGER NOT NULL DEFAULT 0,
    created_at  TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS workflow_statuses (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    workflow_id INTEGER NOT NULL,
    name        TEXT NOT NULL,
    category    TEXT NOT NULL DEFAULT 'To Do',
    color       TEXT NOT NULL DEFAULT '#0052CC',
    pos_x       INTEGER NOT NULL DEFAULT 100,
    pos_y       INTEGER NOT NULL DEFAULT 140,
    sort_order  INTEGER NOT NULL DEFAULT 0,
    is_initial  INTEGER NOT NULL DEFAULT 0,
    FOREIGN KEY (workflow_id) REFERENCES workflows(id) ON DELETE CASCADE,
    UNIQUE(workflow_id, name)
);

CREATE INDEX IF NOT EXISTS idx_wf_statuses_workflow ON workflow_statuses(workflow_id);

CREATE TABLE IF NOT EXISTS workflow_transitions (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    workflow_id INTEGER NOT NULL,
    name        TEXT NOT NULL,
    from_status TEXT NOT NULL DEFAULT '*',
    to_status   TEXT NOT NULL,
    screen_id   INTEGER DEFAULT NULL,
    sort_order  INTEGER NOT NULL DEFAULT 0,
    created_at  TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (workflow_id) REFERENCES workflows(id) ON DELETE CASCADE,
    FOREIGN KEY (screen_id) REFERENCES screens(id) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS idx_wf_transitions_workflow ON workflow_transitions(workflow_id);

CREATE TABLE IF NOT EXISTS workflow_transition_rules (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    transition_id INTEGER NOT NULL,
    rule_category TEXT NOT NULL,
    rule_type     TEXT NOT NULL,
    field_name    TEXT NOT NULL DEFAULT '',
    config_value  TEXT NOT NULL DEFAULT '',
    description   TEXT NOT NULL DEFAULT '',
    sort_order    INTEGER NOT NULL DEFAULT 0,
    FOREIGN KEY (transition_id) REFERENCES workflow_transitions(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_wf_rules_transition ON workflow_transition_rules(transition_id);

CREATE TABLE IF NOT EXISTS workflow_schemes (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL,
    description TEXT NOT NULL DEFAULT '',
    project_id  INTEGER DEFAULT NULL,
    issue_type  TEXT NOT NULL DEFAULT '*',
    workflow_id INTEGER NOT NULL,
    is_default  INTEGER NOT NULL DEFAULT 0,
    created_at  TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (project_id) REFERENCES projects(id) ON DELETE CASCADE,
    FOREIGN KEY (workflow_id) REFERENCES workflows(id) ON DELETE CASCADE,
    UNIQUE(project_id, issue_type)
);

ALTER TABLE workflow_history ADD COLUMN transition_id INTEGER DEFAULT NULL;
ALTER TABLE workflow_history ADD COLUMN transition_name TEXT NOT NULL DEFAULT '';
ALTER TABLE workflow_history ADD COLUMN executed_post_functions TEXT NOT NULL DEFAULT '';

INSERT OR IGNORE INTO workflows (id, name, description, is_default, is_system) VALUES
    (1, 'Default Agile 7-State Workflow', 'Standard Jira Software directed workflow with Backlog, Sprint Execution, Code Review, QA Verification, Resolution Screen, and Subtask Guard.', 1, 1),
    (2, 'Strict Bug & QA Verification Workflow', 'Enforces strict step-by-step Bug triage: Assignee-only execution, mandatory reproduction steps, QA Engineer verification gate, and resolution post-functions.', 0, 1);

INSERT OR IGNORE INTO workflow_statuses (workflow_id, name, category, color, pos_x, pos_y, sort_order, is_initial) VALUES
    (1, 'Backlog',      'To Do',       '#42526E',  70, 130, 10, 1),
    (1, 'To Do',        'To Do',       '#0052CC', 240, 130, 20, 0),
    (1, 'In Progress',  'In Progress', '#FF991F', 410, 130, 30, 0),
    (1, 'In Review',    'In Progress', '#6554C0', 580, 130, 40, 0),
    (1, 'QA / Testing', 'In Progress', '#00B8D9', 750, 130, 50, 0),
    (1, 'Done',         'Done',        '#00875A', 920, 130, 60, 0),
    (1, 'Blocked',      'In Progress', '#DE350B', 495, 290, 70, 0);

INSERT OR IGNORE INTO workflow_transitions (id, workflow_id, name, from_status, to_status, screen_id, sort_order) VALUES
    (1,  1, 'Schedule to To Do',   'Backlog',      'To Do',        NULL, 10),
    (2,  1, 'Send to Backlog',     'To Do',        'Backlog',      NULL, 15),
    (3,  1, 'Start Progress',      '*',            'In Progress',  NULL, 20),
    (4,  1, 'Stop Progress',       'In Progress',  'To Do',        NULL, 25),
    (5,  1, 'Submit for Review',   'In Progress',  'In Review',    NULL, 30),
    (6,  1, 'Request Changes',     'In Review',    'In Progress',  NULL, 35),
    (7,  1, 'Promote to QA',       'In Review',    'QA / Testing', NULL, 40),
    (8,  1, 'QA Failed',           'QA / Testing', 'In Progress',  NULL, 45),
    (9,  1, 'Resolve & Close',     '*',            'Done',         3,    50),
    (10, 1, 'Reopen Issue',        'Done',         'To Do',        NULL, 55),
    (11, 1, 'Flag Blocked',        '*',            'Blocked',      NULL, 60),
    (12, 1, 'Unblock to Progress', 'Blocked',      'In Progress',  NULL, 65),
    (13, 1, 'Move to To Do',       '*',            'To Do',        NULL, 70),
    (14, 1, 'Move to In Review',   '*',            'In Review',    NULL, 75),
    (15, 1, 'Move to QA / Testing','*',            'QA / Testing', NULL, 80);

INSERT OR IGNORE INTO workflow_transition_rules (id, transition_id, rule_category, rule_type, field_name, config_value, description, sort_order) VALUES
    (1,  3,  'post_function', 'set_field_value',      'progress',      '40',             'Automatically advance progress to 40% when work starts', 10),
    (2,  5,  'post_function', 'set_field_value',      'progress',      '80',             'Automatically advance progress to 80% on code review submission', 10),
    (3,  7,  'post_function', 'set_field_value',      'progress',      '90',             'Automatically advance progress to 90% when promoted to QA', 10),
    (4,  8,  'validator',     'comment_required',     'comment',       'true',           'Require QA failure explanation comment when returning to In Progress', 10),
    (5,  9,  'condition',     'subtasks_completed',   '',              'Done',           'Prevent closing parent work item until all child subtasks are Done', 10),
    (6,  9,  'validator',     'field_required',       'resolution_id', 'required',       'Require Resolution outcome (e.g., Fixed, Won''t Fix) when closing issue', 20),
    (7,  9,  'post_function', 'set_field_value',      'progress',      '100',            'Set work item completion progress to 100%', 30),
    (8,  9,  'post_function', 'update_parent_status', 'status',        'Done',           'Auto-transition parent Story/Task to Done when all sibling subtasks reach Done', 40),
    (9,  9,  'post_function', 'dispatch_event',       'event',         'issue_resolved', 'Emit issue_resolved workflow event to audit log', 50),
    (10, 10, 'validator',     'comment_required',     'comment',       'true',           'Require reason comment when reopening a closed issue', 10),
    (11, 10, 'post_function', 'clear_field_value',    'resolution',    '',               'Automatically clear Resolution and Resolution Date when reopening issue', 20),
    (12, 10, 'post_function', 'set_field_value',      'progress',      '0',              'Reset progress to 0% upon reopening', 30),
    (13, 10, 'post_function', 'dispatch_event',       'event',         'issue_reopened', 'Emit issue_reopened workflow event to audit log', 40),
    (14, 11, 'validator',     'comment_required',     'comment',       'true',           'Require explanation comment describing the blocker', 10),
    (15, 11, 'post_function', 'dispatch_event',       'event',         'issue_blocked',  'Emit issue_blocked event to audit log', 20);

INSERT OR IGNORE INTO workflow_statuses (workflow_id, name, category, color, pos_x, pos_y, sort_order, is_initial) VALUES
    (2, 'To Do',        'To Do',       '#0052CC', 100, 140, 10, 1),
    (2, 'In Progress',  'In Progress', '#FF991F', 300, 140, 20, 0),
    (2, 'In Review',    'In Progress', '#6554C0', 500, 140, 30, 0),
    (2, 'QA / Testing', 'In Progress', '#00B8D9', 700, 140, 40, 0),
    (2, 'Done',         'Done',        '#00875A', 900, 140, 50, 0),
    (2, 'Blocked',      'In Progress', '#DE350B', 500, 290, 60, 0);

INSERT OR IGNORE INTO workflow_transitions (id, workflow_id, name, from_status, to_status, screen_id, sort_order) VALUES
    (20, 2, 'Start Bug Fix',          'To Do',        'In Progress',  NULL, 10),
    (21, 2, 'Submit Fix for Review',  'In Progress',  'In Review',    NULL, 20),
    (22, 2, 'Send to QA Verification','In Review',    'QA / Testing', NULL, 30),
    (23, 2, 'QA Verify & Close',      'QA / Testing', 'Done',         3,    40),
    (24, 2, 'QA Reject Fix',          'QA / Testing', 'In Progress',  NULL, 50),
    (25, 2, 'Reopen Bug',             'Done',         'To Do',        NULL, 60),
    (26, 2, 'Flag Bug Blocked',       '*',            'Blocked',      NULL, 70),
    (27, 2, 'Unblock Bug',            'Blocked',      'In Progress',  NULL, 80);

INSERT OR IGNORE INTO workflow_transition_rules (id, transition_id, rule_category, rule_type, field_name, config_value, description, sort_order) VALUES
    (20, 20, 'condition',     'only_assignee',       '',                   'true',                              'Only the assigned developer (or Admin) can start fixing the bug', 10),
    (21, 20, 'validator',     'field_required',      'steps_to_reproduce', 'required',                          'Steps to Reproduce must be documented before starting bug fix', 20),
    (22, 20, 'post_function', 'set_field_value',     'progress',           '35',                                'Set progress to 35% on bug fix start', 30),
    (23, 21, 'validator',     'comment_required',    'comment',            'true',                              'Require root-cause & PR summary comment when submitting for review', 10),
    (24, 21, 'post_function', 'set_field_value',     'progress',           '75',                                'Set progress to 75% on review submission', 20),
    (25, 22, 'post_function', 'assign_to_user',      'assignee_id',        'role:QA Engineer',                  'Automatically reassign bug to QA Engineer upon entering QA / Testing', 10),
    (26, 22, 'post_function', 'set_field_value',     'progress',           '90',                                'Set progress to 90% on QA handoff', 20),
    (27, 23, 'condition',     'user_in_role',        'role',               'QA Engineer,Admin,Project Manager', 'Only QA Engineers, Project Managers, or Admins can verify & close bugs', 10),
    (28, 23, 'condition',     'subtasks_completed',  '',                   'Done',                              'All subtasks must be Done before closing bug', 20),
    (29, 23, 'validator',     'field_required',      'resolution_id',      'required',                          'Resolution is mandatory when closing a verified bug', 30),
    (30, 23, 'validator',     'comment_required',    'comment',            'true',                              'QA verification sign-off comment is required', 40),
    (31, 23, 'post_function', 'set_field_value',     'progress',           '100',                               'Set progress to 100% upon QA verification', 50),
    (32, 24, 'validator',     'comment_required',    'comment',            'true',                              'Require QA rejection details', 10),
    (33, 24, 'post_function', 'assign_to_user',      'assignee_id',        'reporter',                          'Reassign rejected bug back to Reporter', 20),
    (34, 25, 'validator',     'comment_required',    'comment',            'true',                              'Require reopen justification comment', 10),
    (35, 25, 'post_function', 'clear_field_value',   'resolution',         '',                                  'Clear resolution on bug reopen', 20);

INSERT OR IGNORE INTO workflow_schemes (id, name, description, project_id, issue_type, workflow_id, is_default) VALUES
    (1, 'Default Agile Workflow Scheme', 'Maps all standard project issue types (*) to the Default Agile 7-State Workflow', NULL, '*', 1, 1);
)SQL"
        },
        {
            10,
            "db/migrations/010_jql_and_fts5.sql",
            "JQL Saved Filters, Subscriptions, and SQLite FTS5 Full-Text Search virtual table with triggers",
            R"SQL(
CREATE VIRTUAL TABLE IF NOT EXISTS work_items_fts USING fts5(
    work_item_id UNINDEXED,
    key,
    title,
    description,
    steps_to_reproduce,
    labels,
    comments_text,
    tokenize = 'unicode61 remove_diacritics 2'
);

DELETE FROM work_items_fts;
INSERT INTO work_items_fts (work_item_id, key, title, description, steps_to_reproduce, labels, comments_text)
SELECT
    w.id,
    COALESCE(w.key, ''),
    COALESCE(w.title, ''),
    COALESCE(w.description, ''),
    COALESCE(w.steps_to_reproduce, ''),
    COALESCE(w.labels, ''),
    COALESCE((SELECT group_concat(c.body, ' ') FROM comments c WHERE c.work_item_id = w.id), '')
FROM work_items w;

CREATE TRIGGER IF NOT EXISTS trg_work_items_fts_ai
AFTER INSERT ON work_items
BEGIN
    INSERT INTO work_items_fts (work_item_id, key, title, description, steps_to_reproduce, labels, comments_text)
    VALUES (
        NEW.id,
        COALESCE(NEW.key, ''),
        COALESCE(NEW.title, ''),
        COALESCE(NEW.description, ''),
        COALESCE(NEW.steps_to_reproduce, ''),
        COALESCE(NEW.labels, ''),
        COALESCE((SELECT group_concat(c.body, ' ') FROM comments c WHERE c.work_item_id = NEW.id), '')
    );
END;

CREATE TRIGGER IF NOT EXISTS trg_work_items_fts_au
AFTER UPDATE ON work_items
BEGIN
    DELETE FROM work_items_fts WHERE CAST(work_item_id AS INTEGER) = OLD.id;
    INSERT INTO work_items_fts (work_item_id, key, title, description, steps_to_reproduce, labels, comments_text)
    VALUES (
        NEW.id,
        COALESCE(NEW.key, ''),
        COALESCE(NEW.title, ''),
        COALESCE(NEW.description, ''),
        COALESCE(NEW.steps_to_reproduce, ''),
        COALESCE(NEW.labels, ''),
        COALESCE((SELECT group_concat(c.body, ' ') FROM comments c WHERE c.work_item_id = NEW.id), '')
    );
END;

CREATE TRIGGER IF NOT EXISTS trg_work_items_fts_ad
AFTER DELETE ON work_items
BEGIN
    DELETE FROM work_items_fts WHERE CAST(work_item_id AS INTEGER) = OLD.id;
END;

CREATE TRIGGER IF NOT EXISTS trg_comments_fts_ai
AFTER INSERT ON comments
BEGIN
    DELETE FROM work_items_fts WHERE CAST(work_item_id AS INTEGER) = NEW.work_item_id;
    INSERT INTO work_items_fts (work_item_id, key, title, description, steps_to_reproduce, labels, comments_text)
    SELECT
        w.id,
        COALESCE(w.key, ''),
        COALESCE(w.title, ''),
        COALESCE(w.description, ''),
        COALESCE(w.steps_to_reproduce, ''),
        COALESCE(w.labels, ''),
        COALESCE((SELECT group_concat(c.body, ' ') FROM comments c WHERE c.work_item_id = w.id), '')
    FROM work_items w
    WHERE w.id = NEW.work_item_id;
END;

CREATE TRIGGER IF NOT EXISTS trg_comments_fts_au
AFTER UPDATE ON comments
BEGIN
    DELETE FROM work_items_fts WHERE CAST(work_item_id AS INTEGER) = NEW.work_item_id;
    INSERT INTO work_items_fts (work_item_id, key, title, description, steps_to_reproduce, labels, comments_text)
    SELECT
        w.id,
        COALESCE(w.key, ''),
        COALESCE(w.title, ''),
        COALESCE(w.description, ''),
        COALESCE(w.steps_to_reproduce, ''),
        COALESCE(w.labels, ''),
        COALESCE((SELECT group_concat(c.body, ' ') FROM comments c WHERE c.work_item_id = w.id), '')
    FROM work_items w
    WHERE w.id = NEW.work_item_id;
END;

CREATE TRIGGER IF NOT EXISTS trg_comments_fts_ad
AFTER DELETE ON comments
BEGIN
    DELETE FROM work_items_fts WHERE CAST(work_item_id AS INTEGER) = OLD.work_item_id;
    INSERT INTO work_items_fts (work_item_id, key, title, description, steps_to_reproduce, labels, comments_text)
    SELECT
        w.id,
        COALESCE(w.key, ''),
        COALESCE(w.title, ''),
        COALESCE(w.description, ''),
        COALESCE(w.steps_to_reproduce, ''),
        COALESCE(w.labels, ''),
        COALESCE((SELECT group_concat(c.body, ' ') FROM comments c WHERE c.work_item_id = w.id), '')
    FROM work_items w
    WHERE w.id = OLD.work_item_id;
END;

CREATE TABLE IF NOT EXISTS saved_filters (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    owner_id     INTEGER DEFAULT NULL,
    name         TEXT NOT NULL,
    description  TEXT DEFAULT '',
    jql_query    TEXT NOT NULL,
    is_favorite  INTEGER NOT NULL DEFAULT 0,
    is_system    INTEGER NOT NULL DEFAULT 0,
    share_type   TEXT NOT NULL DEFAULT 'public',
    share_target TEXT DEFAULT '*',
    view_columns TEXT DEFAULT 'key,type,title,status,priority,assignee,sprint,story_points,updated_at',
    sort_order   INTEGER NOT NULL DEFAULT 100,
    created_at   TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at   TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (owner_id) REFERENCES users(id) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS idx_saved_filters_owner ON saved_filters(owner_id);
CREATE INDEX IF NOT EXISTS idx_saved_filters_fav ON saved_filters(is_favorite);

CREATE TABLE IF NOT EXISTS filter_stars (
    filter_id  INTEGER NOT NULL,
    user_id    INTEGER NOT NULL,
    created_at TEXT NOT NULL DEFAULT (datetime('now')),
    PRIMARY KEY (filter_id, user_id),
    FOREIGN KEY (filter_id) REFERENCES saved_filters(id) ON DELETE CASCADE,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS filter_subscriptions (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    filter_id       INTEGER NOT NULL,
    user_id         INTEGER DEFAULT NULL,
    cron_schedule   TEXT NOT NULL DEFAULT '0 8 * * 1-5',
    frequency_label TEXT NOT NULL DEFAULT 'Daily (Weekdays 08:00)',
    recipient_scope TEXT NOT NULL DEFAULT 'self',
    email_empty     INTEGER NOT NULL DEFAULT 0,
    is_active       INTEGER NOT NULL DEFAULT 1,
    last_run_at     TEXT DEFAULT '',
    created_at      TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (filter_id) REFERENCES saved_filters(id) ON DELETE CASCADE,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);

INSERT OR IGNORE INTO saved_filters (id, owner_id, name, description, jql_query, is_favorite, is_system, share_type, share_target, sort_order) VALUES
    (1, NULL, 'My Open Issues',               'Unresolved work items currently assigned to the logged-in user',                               'assignee = currentUser() AND resolution IS EMPTY ORDER BY priority DESC, updated DESC', 1, 1, 'public', '*', 10),
    (2, NULL, 'Reported by Me',               'All issues created or reported by the logged-in user',                                         'reporter = currentUser() ORDER BY created DESC',                                        1, 1, 'public', '*', 20),
    (3, NULL, 'Active Sprint Work',           'All unresolved items in currently active sprints ordered by priority',                         'sprint IN openSprints() AND status != "Done" ORDER BY priority DESC, key ASC',         1, 1, 'public', '*', 30),
    (4, NULL, 'High Priority & Critical Bugs','Open defects with High, Highest, or Critical priority requiring immediate triage',             'category = "Bug" AND priority IN ("Critical", "Highest", "High") AND status != "Done" ORDER BY priority DESC', 1, 1, 'public', '*', 40),
    (5, NULL, 'Ready for QA Verification',    'Work items currently in In Review or QA / Testing awaiting verification',                      'status IN ("In Review", "QA / Testing") ORDER BY priority DESC, updated DESC',          0, 1, 'public', '*', 50),
    (6, NULL, 'Recently Updated',             'Issues updated since the start of the current week',                                           'updated >= startOfWeek() ORDER BY updated DESC',                                        0, 1, 'public', '*', 60),
    (7, NULL, 'Done & Resolved Issues',       'Completed work items with a resolution set',                                                   'status = "Done" OR resolution IS NOT EMPTY ORDER BY updated DESC',                      0, 1, 'public', '*', 70),
    (8, NULL, 'Unassigned Backlog',           'Items in Backlog or To Do that do not yet have an owner',                                      'assignee IS EMPTY AND status IN ("Backlog", "To Do") ORDER BY priority DESC, id ASC',   0, 1, 'public', '*', 80);
)SQL"
        },
        {
            11,
            "db/migrations/011_multi_boards_and_backlog.sql",
            "Scrum and Kanban Multi-Board Engine, Column Status Mappings, WIP Limits, Swimlanes, Quick Filters, and Sprint Lifecycle Velocity",
            R"SQL(
CREATE TABLE IF NOT EXISTS boards (
    id                   INTEGER PRIMARY KEY AUTOINCREMENT,
    project_id           INTEGER DEFAULT 1,
    name                 TEXT NOT NULL,
    board_type           TEXT NOT NULL DEFAULT 'scrum',
    filter_jql           TEXT DEFAULT '',
    swimlane_strategy    TEXT NOT NULL DEFAULT 'none',
    card_extra_fields    TEXT DEFAULT 'story_points,due_date,components',
    card_color_strategy  TEXT NOT NULL DEFAULT 'priority',
    show_backlog_column  INTEGER NOT NULL DEFAULT 0,
    is_default           INTEGER NOT NULL DEFAULT 0,
    created_at           TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at           TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE INDEX IF NOT EXISTS idx_boards_project ON boards(project_id);

CREATE TABLE IF NOT EXISTS board_columns (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    board_id        INTEGER NOT NULL,
    name            TEXT NOT NULL,
    mapped_statuses TEXT NOT NULL,
    min_wip         INTEGER NOT NULL DEFAULT 0,
    max_wip         INTEGER NOT NULL DEFAULT 0,
    color           TEXT NOT NULL DEFAULT '#42526E',
    sort_order      INTEGER NOT NULL DEFAULT 100,
    FOREIGN KEY (board_id) REFERENCES boards(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_board_columns_board ON board_columns(board_id, sort_order);

CREATE TABLE IF NOT EXISTS board_swimlanes (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    board_id    INTEGER NOT NULL,
    name        TEXT NOT NULL,
    description TEXT DEFAULT '',
    jql_query   TEXT NOT NULL,
    sort_order  INTEGER NOT NULL DEFAULT 100,
    FOREIGN KEY (board_id) REFERENCES boards(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_board_swimlanes_board ON board_swimlanes(board_id, sort_order);

CREATE TABLE IF NOT EXISTS board_quick_filters (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    board_id    INTEGER NOT NULL,
    name        TEXT NOT NULL,
    jql_query   TEXT NOT NULL,
    description TEXT DEFAULT '',
    sort_order  INTEGER NOT NULL DEFAULT 100,
    FOREIGN KEY (board_id) REFERENCES boards(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_board_quick_filters_board ON board_quick_filters(board_id, sort_order);

ALTER TABLE sprints ADD COLUMN completed_at TEXT DEFAULT '';
ALTER TABLE sprints ADD COLUMN committed_points INTEGER NOT NULL DEFAULT 0;
ALTER TABLE sprints ADD COLUMN completed_points INTEGER NOT NULL DEFAULT 0;
ALTER TABLE sprints ADD COLUMN completed_items_count INTEGER NOT NULL DEFAULT 0;
ALTER TABLE sprints ADD COLUMN incomplete_items_count INTEGER NOT NULL DEFAULT 0;
ALTER TABLE sprints ADD COLUMN rollover_target_sprint_id INTEGER DEFAULT NULL;

UPDATE sprints
SET committed_points = COALESCE((SELECT SUM(w.story_points) FROM work_items w WHERE w.sprint_id = sprints.id), 0),
    completed_points = COALESCE((SELECT SUM(w.story_points) FROM work_items w WHERE w.sprint_id = sprints.id AND w.status = 'Done'), 0),
    completed_items_count = COALESCE((SELECT COUNT(*) FROM work_items w WHERE w.sprint_id = sprints.id AND w.status = 'Done'), 0),
    incomplete_items_count = COALESCE((SELECT COUNT(*) FROM work_items w WHERE w.sprint_id = sprints.id AND w.status != 'Done'), 0);

INSERT INTO work_items (
    project_id, key, type, category, status, priority, title, description,
    parent_id, sprint_id, assignee_id, reporter_id, story_points,
    progress, start_date, due_date, labels, board_order
)
SELECT
    p.id,
    p.key || '-' || (p.issue_seq + 1),
    'Story',
    'New Feature',
    'Backlog',
    'High',
    'Multi-Board Scrum & Kanban Swimlane Engine',
    'Support configurable column-to-status mappings, WIP limits, JQL quick filters, and Agile Backlog sprint planning.',
    1, NULL, 1, 1, 8, 0, '2026-11-10', '2026-11-24', 'agile,board,scrum,kanban', 200
FROM projects p
WHERE p.id = 1
  AND NOT EXISTS (SELECT 1 FROM work_items WHERE sprint_id IS NULL);

UPDATE projects
SET issue_seq = (SELECT COUNT(*) FROM work_items WHERE project_id = projects.id)
WHERE id = 1;

INSERT INTO work_items (
    project_id, key, type, category, status, priority, title, description,
    parent_id, sprint_id, assignee_id, reporter_id, story_points,
    progress, start_date, due_date, labels, board_order
)
SELECT
    p.id,
    p.key || '-' || (p.issue_seq + 1),
    'Task',
    'Improvement',
    'Backlog',
    'Medium',
    'Automated Sprint Velocity & Incomplete Issue Rollover',
    'Calculate committed vs completed story points when completing a sprint and roll over unfinished work items.',
    2, NULL, NULL, 1, 5, 0, '2026-11-12', '2026-11-26', 'sprint,velocity,backlog', 210
FROM projects p
WHERE p.id = 1
  AND (SELECT COUNT(*) FROM work_items WHERE sprint_id IS NULL) < 2;

UPDATE projects
SET issue_seq = (SELECT COUNT(*) FROM work_items WHERE project_id = projects.id)
WHERE id = 1;

INSERT OR IGNORE INTO boards (id, project_id, name, board_type, filter_jql, swimlane_strategy, card_extra_fields, card_color_strategy, show_backlog_column, is_default) VALUES
    (1, 1, 'Core Platform Scrum Board',      'scrum',  '',                                                                    'none',     'story_points,due_date,components',   'priority', 0, 1),
    (2, 1, 'Continuous Flow Kanban Board',   'kanban', '',                                                                    'epic',     'due_date,components,fix_versions',   'priority', 1, 0),
    (3, 1, 'Bug Triage & QA Board',          'kanban', 'category = "Bug" OR status IN ("In Review", "QA / Testing", "Blocked")', 'priority', 'story_points,due_date,components', 'category', 1, 0);

INSERT OR IGNORE INTO board_columns (id, board_id, name, mapped_statuses, min_wip, max_wip, color, sort_order) VALUES
    (1,  1, 'To Do',                       'To Do',                  0, 0, '#42526E', 10),
    (2,  1, 'In Progress',                 'In Progress',            1, 5, '#0052CC', 20),
    (3,  1, 'Review & QA',                 'In Review,QA / Testing', 0, 4, '#00B8D9', 30),
    (4,  1, 'Blocked',                     'Blocked',                0, 2, '#DE350B', 40),
    (5,  1, 'Done',                        'Done',                   0, 0, '#00875A', 50),
    (6,  2, 'Backlog',                     'Backlog',                0, 0, '#6B778C', 10),
    (7,  2, 'Selected for Development',    'To Do',                  0, 6, '#42526E', 20),
    (8,  2, 'Active Development',          'In Progress',            1, 3, '#0052CC', 30),
    (9,  2, 'Verification (Review + QA)',  'In Review,QA / Testing', 0, 3, '#6554C0', 40),
    (10, 2, 'Blocked',                     'Blocked',                0, 1, '#DE350B', 50),
    (11, 2, 'Released / Done',             'Done',                   0, 0, '#00875A', 60),
    (12, 3, 'Incoming Triage',             'Backlog,To Do',          0, 8, '#42526E', 10),
    (13, 3, 'Fix In Progress',             'In Progress,Blocked',    0, 4, '#0052CC', 20),
    (14, 3, 'QA Verification',             'In Review,QA / Testing', 0, 4, '#00B8D9', 30),
    (15, 3, 'Verified & Closed',           'Done',                   0, 0, '#00875A', 40);

INSERT OR IGNORE INTO board_quick_filters (id, board_id, name, jql_query, description, sort_order) VALUES
    (1,  1, 'Only My Issues',           'assignee = currentUser()',                           'Show items assigned to the logged-in user', 10),
    (2,  1, 'High & Critical Priority', 'priority IN ("Critical", "Highest", "High")',        'Focus on high-priority sprint deliverables', 20),
    (3,  1, 'Bugs & Defects',           'category = "Bug"',                                   'Filter to defects only', 30),
    (4,  1, 'Recently Updated',         'updated >= startOfWeek()',                           'Updated since the start of this week', 40),
    (5,  1, 'Blocked',                  'status = "Blocked"',                                 'Show blocked sprint items', 50),
    (6,  2, 'Only My Issues',           'assignee = currentUser()',                           'Show items assigned to the logged-in user', 10),
    (7,  2, 'Expedite / Critical',      'priority IN ("Critical", "Highest")',                'Urgent flow items', 20),
    (8,  2, 'Bugs Only',                'category = "Bug"',                                   'Defects in continuous flow', 30),
    (9,  2, 'Unassigned',               'assignee IS EMPTY',                                  'Items needing an owner', 40),
    (10, 3, 'Critical Severity',        'priority IN ("Critical", "Highest")',                'Critical and highest priority bugs', 10),
    (11, 3, 'Assigned to Me',           'assignee = currentUser()',                           'My assigned QA/Bug items', 20);

INSERT OR IGNORE INTO board_swimlanes (id, board_id, name, description, jql_query, sort_order) VALUES
    (1, 1, '🚨 Expedite (Critical & Highest)', 'Urgent items bypassing standard queue',      'priority IN ("Critical", "Highest")', 10),
    (2, 1, '🐛 Bugs & Regressions',            'Production and sprint defects',              'category = "Bug" AND priority NOT IN ("Critical", "Highest")', 20),
    (3, 1, '🚀 Standard Feature Delivery',     'Planned stories, tasks, and improvements',   'category != "Bug" AND priority NOT IN ("Critical", "Highest")', 30),
    (4, 2, '🚨 Expedite Lane',                 'Fast-track critical work items',             'priority IN ("Critical", "Highest")', 10),
    (5, 2, '⚡ Standard Continuous Flow',      'Regular flow items',                         'priority NOT IN ("Critical", "Highest")', 20);
)SQL"
        },
        {
            12,
            "db/migrations/012_time_tracking_and_worklogs.sql",
            "Time Tracking, Estimation Triad (Original, Remaining, Time Spent) & Worklogs Subsystem",
            R"SQL(
ALTER TABLE work_items ADD COLUMN original_estimate_seconds INTEGER NOT NULL DEFAULT 0;
ALTER TABLE work_items ADD COLUMN remaining_estimate_seconds INTEGER NOT NULL DEFAULT 0;
ALTER TABLE work_items ADD COLUMN time_spent_seconds INTEGER NOT NULL DEFAULT 0;
ALTER TABLE work_items ADD COLUMN tshirt_size TEXT NOT NULL DEFAULT '';

CREATE TABLE IF NOT EXISTS worklogs (
    id                  INTEGER PRIMARY KEY AUTOINCREMENT,
    work_item_id        INTEGER NOT NULL,
    author_id           INTEGER DEFAULT NULL,
    time_spent_seconds  INTEGER NOT NULL,
    started_at          TEXT NOT NULL DEFAULT (datetime('now')),
    description         TEXT NOT NULL DEFAULT '',
    adjustment_mode     TEXT NOT NULL DEFAULT 'auto',
    adjustment_seconds  INTEGER NOT NULL DEFAULT 0,
    remaining_before    INTEGER NOT NULL DEFAULT 0,
    remaining_after     INTEGER NOT NULL DEFAULT 0,
    created_at          TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at          TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (work_item_id) REFERENCES work_items(id) ON DELETE CASCADE,
    FOREIGN KEY (author_id) REFERENCES users(id) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS idx_worklogs_item ON worklogs(work_item_id, started_at DESC);
CREATE INDEX IF NOT EXISTS idx_worklogs_author ON worklogs(author_id, started_at DESC);

UPDATE work_items
SET original_estimate_seconds = CASE
        WHEN story_points > 0 THEN story_points * 14400
        ELSE 28800
    END,
    tshirt_size = CASE
        WHEN story_points >= 13 THEN 'XL'
        WHEN story_points >= 8  THEN 'L'
        WHEN story_points >= 5  THEN 'M'
        WHEN story_points >= 3  THEN 'S'
        ELSE 'XS'
    END
WHERE original_estimate_seconds = 0;

INSERT INTO worklogs (
    work_item_id, author_id, time_spent_seconds, started_at, description,
    adjustment_mode, adjustment_seconds, remaining_before, remaining_after
)
SELECT
    w.id,
    COALESCE(w.assignee_id, w.reporter_id),
    CASE
        WHEN w.status = 'Done' THEN w.original_estimate_seconds
        WHEN w.progress > 0 THEN CAST((w.original_estimate_seconds * w.progress) / 100 AS INTEGER)
        ELSE 7200
    END,
    COALESCE(NULLIF(w.start_date, '') || ' 09:30:00', datetime('now', '-2 days')),
    'Implemented core deliverables and verified acceptance criteria for ' || w.key,
    'auto',
    0,
    w.original_estimate_seconds,
    CASE
        WHEN w.status = 'Done' THEN 0
        WHEN w.progress > 0 THEN MAX(0, w.original_estimate_seconds - CAST((w.original_estimate_seconds * w.progress) / 100 AS INTEGER))
        ELSE MAX(0, w.original_estimate_seconds - 7200)
    END
FROM work_items w
WHERE w.status IN ('Done', 'In Review', 'QA / Testing', 'In Progress')
  AND NOT EXISTS (SELECT 1 FROM worklogs wl WHERE wl.work_item_id = w.id);

UPDATE work_items
SET time_spent_seconds = COALESCE((SELECT SUM(wl.time_spent_seconds) FROM worklogs wl WHERE wl.work_item_id = work_items.id), 0),
    remaining_estimate_seconds = CASE
        WHEN status = 'Done' THEN 0
        ELSE MAX(0, original_estimate_seconds - COALESCE((SELECT SUM(wl.time_spent_seconds) FROM worklogs wl WHERE wl.work_item_id = work_items.id), 0))
    END;
)SQL"
        },
        {
            13,
            "db/migrations/013_agile_analytics_and_reporting.sql",
            "Agile Analytics, Sprint Events & Burndown/Burnup/CFD/Cycle Time Reporting Engine",
            R"SQL(
CREATE TABLE IF NOT EXISTS sprint_events (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    sprint_id    INTEGER NOT NULL,
    work_item_id INTEGER NOT NULL,
    event_type   TEXT NOT NULL,
    old_points   INTEGER NOT NULL DEFAULT 0,
    new_points   INTEGER NOT NULL DEFAULT 0,
    created_at   TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (sprint_id) REFERENCES sprints(id) ON DELETE CASCADE,
    FOREIGN KEY (work_item_id) REFERENCES work_items(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_sprint_events_sprint ON sprint_events(sprint_id, created_at ASC);
CREATE INDEX IF NOT EXISTS idx_sprint_events_item ON sprint_events(work_item_id);

INSERT INTO sprint_events (sprint_id, work_item_id, event_type, old_points, new_points, created_at)
SELECT
    w.sprint_id,
    w.id,
    'committed',
    0,
    w.story_points,
    COALESCE(s.start_date || ' 09:00:00', w.created_at)
FROM work_items w
JOIN sprints s ON s.id = w.sprint_id
WHERE w.sprint_id IS NOT NULL;

CREATE TRIGGER IF NOT EXISTS trg_work_items_sprint_update
AFTER UPDATE OF sprint_id, story_points ON work_items
FOR EACH ROW
BEGIN
    INSERT INTO sprint_events (sprint_id, work_item_id, event_type, old_points, new_points, created_at)
    SELECT OLD.sprint_id, OLD.id, 'removed', OLD.story_points, 0, datetime('now')
    WHERE OLD.sprint_id IS NOT NULL AND (NEW.sprint_id IS NULL OR OLD.sprint_id != NEW.sprint_id);

    INSERT INTO sprint_events (sprint_id, work_item_id, event_type, old_points, new_points, created_at)
    SELECT NEW.sprint_id, NEW.id, 'added', 0, NEW.story_points, datetime('now')
    WHERE NEW.sprint_id IS NOT NULL AND (OLD.sprint_id IS NULL OR OLD.sprint_id != NEW.sprint_id);

    INSERT INTO sprint_events (sprint_id, work_item_id, event_type, old_points, new_points, created_at)
    SELECT NEW.sprint_id, NEW.id, 'points_changed', OLD.story_points, NEW.story_points, datetime('now')
    WHERE NEW.sprint_id IS NOT NULL AND OLD.sprint_id = NEW.sprint_id AND OLD.story_points != NEW.story_points;
END;

CREATE TRIGGER IF NOT EXISTS trg_work_items_sprint_insert
AFTER INSERT ON work_items
FOR EACH ROW
WHEN NEW.sprint_id IS NOT NULL
BEGIN
    INSERT INTO sprint_events (sprint_id, work_item_id, event_type, old_points, new_points, created_at)
    VALUES (NEW.sprint_id, NEW.id, 'added', 0, NEW.story_points, datetime('now'));
END;

INSERT OR IGNORE INTO sprints (id, project_id, name, goal, status, start_date, end_date, is_current, completed_at, committed_points, completed_points, completed_items_count, incomplete_items_count)
SELECT
    99,
    1,
    'Sprint 0 - Foundations & FastCGI Setup',
    'Initial project setup, database migrations schema, and basic HTTP routing.',
    'closed',
    '2026-10-01',
    '2026-10-14',
    0,
    '2026-10-14 17:30:00',
    24,
    21,
    5,
    1
WHERE EXISTS (SELECT 1 FROM projects WHERE id = 1);
)SQL"
        },
        {
            14,
            "db/migrations/014_dashboards_and_gadgets.sql",
            "Configurable Dashboards & Gadgets Framework (Step 9 & Epic 7)",
            R"SQL(
CREATE TABLE IF NOT EXISTS dashboards (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    owner_id     INTEGER NOT NULL,
    name         TEXT NOT NULL,
    description  TEXT DEFAULT '',
    layout       TEXT NOT NULL DEFAULT '2_column_equal',
    is_favorite  INTEGER NOT NULL DEFAULT 0,
    share_type   TEXT NOT NULL DEFAULT 'private',
    project_id   INTEGER DEFAULT NULL,
    created_at   TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at   TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (owner_id) REFERENCES users(id) ON DELETE CASCADE,
    FOREIGN KEY (project_id) REFERENCES projects(id) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS idx_dashboards_owner ON dashboards(owner_id);
CREATE INDEX IF NOT EXISTS idx_dashboards_fav ON dashboards(is_favorite);
CREATE INDEX IF NOT EXISTS idx_dashboards_project ON dashboards(project_id);

CREATE TABLE IF NOT EXISTS dashboard_gadgets (
    id               INTEGER PRIMARY KEY AUTOINCREMENT,
    dashboard_id     INTEGER NOT NULL,
    gadget_type      TEXT NOT NULL,
    title            TEXT NOT NULL,
    column_index     INTEGER NOT NULL DEFAULT 0,
    row_index        INTEGER NOT NULL DEFAULT 0,
    preferences_json TEXT NOT NULL DEFAULT '{}',
    created_at       TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at       TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (dashboard_id) REFERENCES dashboards(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_dashboard_gadgets_dash ON dashboard_gadgets(dashboard_id, column_index, row_index);

INSERT OR IGNORE INTO dashboards (id, owner_id, name, description, layout, is_favorite, share_type, project_id, created_at, updated_at)
SELECT
    1,
    u.id,
    'Engineering Operations & Agile Overview',
    'Default team executive dashboard featuring active sprint burndown, personal work queue, and defect distribution.',
    '2_column_equal',
    1,
    'public',
    1,
    datetime('now', '-3 days'),
    datetime('now')
FROM users u
WHERE u.id = 1
  AND EXISTS (SELECT 1 FROM projects WHERE id = 1);

INSERT OR IGNORE INTO dashboard_gadgets (id, dashboard_id, gadget_type, title, column_index, row_index, preferences_json, created_at)
SELECT 1, 1, 'assigned_to_me', 'Assigned to Me', 0, 0, '{"limit":10,"include_resolved":false}', datetime('now', '-3 days')
WHERE EXISTS (SELECT 1 FROM dashboards WHERE id = 1);

INSERT OR IGNORE INTO dashboard_gadgets (id, dashboard_id, gadget_type, title, column_index, row_index, preferences_json, created_at)
SELECT 2, 1, 'sprint_burndown', 'Active Sprint Burndown', 1, 0, '{"sprint_id":0,"unit":"points"}', datetime('now', '-3 days')
WHERE EXISTS (SELECT 1 FROM dashboards WHERE id = 1);

INSERT OR IGNORE INTO dashboard_gadgets (id, dashboard_id, gadget_type, title, column_index, row_index, preferences_json, created_at)
SELECT 3, 1, 'pie_chart', 'Issues by Status', 0, 1, '{"statistic_type":"status"}', datetime('now', '-3 days')
WHERE EXISTS (SELECT 1 FROM dashboards WHERE id = 1);

INSERT OR IGNORE INTO dashboard_gadgets (id, dashboard_id, gadget_type, title, column_index, row_index, preferences_json, created_at)
SELECT 4, 1, 'activity_stream', 'Recent Activity Stream', 1, 1, '{"limit":15}', datetime('now', '-3 days')
WHERE EXISTS (SELECT 1 FROM dashboards WHERE id = 1);

INSERT OR IGNORE INTO dashboard_gadgets (id, dashboard_id, gadget_type, title, column_index, row_index, preferences_json, created_at)
SELECT 5, 1, 'two_dimensional_stats', 'Assignee vs Status Matrix', 0, 2, '{"x_axis":"status","y_axis":"assignee"}', datetime('now', '-3 days')
WHERE EXISTS (SELECT 1 FROM dashboards WHERE id = 1);

INSERT OR IGNORE INTO dashboard_gadgets (id, dashboard_id, gadget_type, title, column_index, row_index, preferences_json, created_at)
SELECT 6, 1, 'created_vs_resolved', 'Created vs Resolved Trend', 1, 2, '{"days":30}', datetime('now', '-3 days')
WHERE EXISTS (SELECT 1 FROM dashboards WHERE id = 1);
)SQL"
        },
        {
            15,
            "db/migrations/015_rich_text_mentions_and_collaboration.sql",
            "Rich text audit changelog, user mentions, watchers, and voting tables",
            R"SQL(
CREATE TABLE IF NOT EXISTS work_item_changelog (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    work_item_id  INTEGER NOT NULL,
    changed_by_id INTEGER,
    field_name    TEXT NOT NULL,
    old_value     TEXT,
    new_value     TEXT,
    created_at    TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (work_item_id) REFERENCES work_items(id) ON DELETE CASCADE,
    FOREIGN KEY (changed_by_id) REFERENCES users(id) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS idx_changelog_work_item ON work_item_changelog(work_item_id, id DESC);

CREATE TABLE IF NOT EXISTS work_item_mentions (
    id                INTEGER PRIMARY KEY AUTOINCREMENT,
    work_item_id      INTEGER NOT NULL,
    comment_id        INTEGER,
    author_id         INTEGER NOT NULL,
    mentioned_user_id INTEGER NOT NULL,
    snippet           TEXT NOT NULL DEFAULT '',
    is_read           INTEGER NOT NULL DEFAULT 0,
    created_at        TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (work_item_id) REFERENCES work_items(id) ON DELETE CASCADE,
    FOREIGN KEY (comment_id) REFERENCES comments(id) ON DELETE CASCADE,
    FOREIGN KEY (author_id) REFERENCES users(id) ON DELETE CASCADE,
    FOREIGN KEY (mentioned_user_id) REFERENCES users(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_mentions_user ON work_item_mentions(mentioned_user_id, is_read, id DESC);
CREATE INDEX IF NOT EXISTS idx_mentions_item ON work_item_mentions(work_item_id);

CREATE TABLE IF NOT EXISTS work_item_watchers (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    work_item_id INTEGER NOT NULL,
    user_id      INTEGER NOT NULL,
    created_at   TEXT NOT NULL DEFAULT (datetime('now')),
    UNIQUE(work_item_id, user_id),
    FOREIGN KEY (work_item_id) REFERENCES work_items(id) ON DELETE CASCADE,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_watchers_item ON work_item_watchers(work_item_id);
CREATE INDEX IF NOT EXISTS idx_watchers_user ON work_item_watchers(user_id);

CREATE TABLE IF NOT EXISTS work_item_votes (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    work_item_id INTEGER NOT NULL,
    user_id      INTEGER NOT NULL,
    created_at   TEXT NOT NULL DEFAULT (datetime('now')),
    UNIQUE(work_item_id, user_id),
    FOREIGN KEY (work_item_id) REFERENCES work_items(id) ON DELETE CASCADE,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_votes_item ON work_item_votes(work_item_id);
CREATE INDEX IF NOT EXISTS idx_votes_user ON work_item_votes(user_id);
)SQL"
        },
        {
            16,
            "db/migrations/016_permissions_and_security_schemes.sql",
            "Granular permission schemes, issue security schemes, user groups, and personal access tokens",
            R"SQL(
CREATE TABLE IF NOT EXISTS user_groups (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL UNIQUE,
    description TEXT DEFAULT '',
    created_at  TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS user_group_members (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    group_id   INTEGER NOT NULL,
    user_id    INTEGER NOT NULL,
    added_at   TEXT NOT NULL DEFAULT (datetime('now')),
    UNIQUE(group_id, user_id),
    FOREIGN KEY (group_id) REFERENCES user_groups(id) ON DELETE CASCADE,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_group_members_group ON user_group_members(group_id);
CREATE INDEX IF NOT EXISTS idx_group_members_user ON user_group_members(user_id);

CREATE TABLE IF NOT EXISTS project_role_members (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    project_id INTEGER NOT NULL,
    role_name  TEXT NOT NULL,
    user_id    INTEGER NOT NULL,
    created_at TEXT NOT NULL DEFAULT (datetime('now')),
    UNIQUE(project_id, role_name, user_id),
    FOREIGN KEY (project_id) REFERENCES projects(id) ON DELETE CASCADE,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_project_role_members_proj ON project_role_members(project_id);
CREATE INDEX IF NOT EXISTS idx_project_role_members_user ON project_role_members(user_id);

CREATE TABLE IF NOT EXISTS permission_schemes (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL UNIQUE,
    description TEXT DEFAULT '',
    is_default  INTEGER NOT NULL DEFAULT 0,
    created_at  TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at  TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS permission_scheme_grants (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    scheme_id      INTEGER NOT NULL,
    permission_key TEXT NOT NULL,
    grantee_type   TEXT NOT NULL,
    grantee_value  TEXT NOT NULL,
    created_at     TEXT NOT NULL DEFAULT (datetime('now')),
    UNIQUE(scheme_id, permission_key, grantee_type, grantee_value),
    FOREIGN KEY (scheme_id) REFERENCES permission_schemes(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_perm_grants_scheme ON permission_scheme_grants(scheme_id, permission_key);

CREATE TABLE IF NOT EXISTS issue_security_schemes (
    id               INTEGER PRIMARY KEY AUTOINCREMENT,
    name             TEXT NOT NULL UNIQUE,
    description      TEXT DEFAULT '',
    default_level_id INTEGER,
    created_at       TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at       TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS issue_security_levels (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    scheme_id   INTEGER NOT NULL,
    name        TEXT NOT NULL,
    description TEXT DEFAULT '',
    is_default  INTEGER NOT NULL DEFAULT 0,
    created_at  TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (scheme_id) REFERENCES issue_security_schemes(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_security_levels_scheme ON issue_security_levels(scheme_id);

CREATE TABLE IF NOT EXISTS issue_security_level_grants (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    level_id      INTEGER NOT NULL,
    grantee_type  TEXT NOT NULL,
    grantee_value TEXT NOT NULL,
    created_at    TEXT NOT NULL DEFAULT (datetime('now')),
    UNIQUE(level_id, grantee_type, grantee_value),
    FOREIGN KEY (level_id) REFERENCES issue_security_levels(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_sec_level_grants ON issue_security_level_grants(level_id);

CREATE TABLE IF NOT EXISTS personal_access_tokens (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id     INTEGER NOT NULL,
    token       TEXT NOT NULL UNIQUE,
    name        TEXT NOT NULL,
    scopes      TEXT NOT NULL DEFAULT 'read,write',
    expires_at  TEXT,
    created_at  TEXT NOT NULL DEFAULT (datetime('now')),
    last_used_at TEXT,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_pat_token ON personal_access_tokens(token);
CREATE INDEX IF NOT EXISTS idx_pat_user ON personal_access_tokens(user_id);

ALTER TABLE projects ADD COLUMN permission_scheme_id INTEGER REFERENCES permission_schemes(id);
ALTER TABLE projects ADD COLUMN security_scheme_id INTEGER REFERENCES issue_security_schemes(id);
ALTER TABLE work_items ADD COLUMN security_level_id INTEGER REFERENCES issue_security_levels(id);

INSERT OR IGNORE INTO user_groups (id, name, description)
VALUES
    (1, 'jira-administrators', 'System administrators with unrestricted workspace access'),
    (2, 'jira-software-users', 'Standard Jira software developers, testers, and product managers');

INSERT OR IGNORE INTO user_group_members (group_id, user_id)
SELECT 1, 1 WHERE EXISTS (SELECT 1 FROM users WHERE id = 1);

INSERT OR IGNORE INTO user_group_members (group_id, user_id)
SELECT 2, 1 WHERE EXISTS (SELECT 1 FROM users WHERE id = 1);

INSERT OR IGNORE INTO permission_schemes (id, name, description, is_default)
VALUES (1, 'Default Permission Scheme', 'Default enterprise permission scheme for software projects', 1);

INSERT OR IGNORE INTO permission_scheme_grants (scheme_id, permission_key, grantee_type, grantee_value)
VALUES
    (1, 'ADMINISTER_PROJECTS', 'role', 'Administrators'),
    (1, 'ADMINISTER_PROJECTS', 'group', 'jira-administrators'),
    (1, 'BROWSE_PROJECT', 'role', 'Administrators'),
    (1, 'BROWSE_PROJECT', 'role', 'Developers'),
    (1, 'BROWSE_PROJECT', 'role', 'QA'),
    (1, 'BROWSE_PROJECT', 'role', 'Product Managers'),
    (1, 'BROWSE_PROJECT', 'role', 'Viewers'),
    (1, 'BROWSE_PROJECT', 'group', 'jira-software-users'),
    (1, 'BROWSE_PROJECT', 'group', 'jira-administrators'),
    (1, 'CREATE_ISSUE', 'role', 'Administrators'),
    (1, 'CREATE_ISSUE', 'role', 'Developers'),
    (1, 'CREATE_ISSUE', 'role', 'QA'),
    (1, 'CREATE_ISSUE', 'role', 'Product Managers'),
    (1, 'CREATE_ISSUE', 'group', 'jira-software-users'),
    (1, 'CREATE_ISSUE', 'group', 'jira-administrators'),
    (1, 'EDIT_ISSUE', 'role', 'Administrators'),
    (1, 'EDIT_ISSUE', 'role', 'Developers'),
    (1, 'EDIT_ISSUE', 'role', 'Product Managers'),
    (1, 'EDIT_ISSUE', 'dynamic', 'assignee'),
    (1, 'EDIT_ISSUE', 'dynamic', 'reporter'),
    (1, 'ASSIGN_ISSUE', 'role', 'Administrators'),
    (1, 'ASSIGN_ISSUE', 'role', 'Developers'),
    (1, 'ASSIGN_ISSUE', 'role', 'Product Managers'),
    (1, 'RESOLVE_ISSUE', 'role', 'Administrators'),
    (1, 'RESOLVE_ISSUE', 'role', 'Developers'),
    (1, 'RESOLVE_ISSUE', 'role', 'QA'),
    (1, 'RESOLVE_ISSUE', 'role', 'Product Managers'),
    (1, 'CLOSE_ISSUE', 'role', 'Administrators'),
    (1, 'CLOSE_ISSUE', 'role', 'QA'),
    (1, 'CLOSE_ISSUE', 'role', 'Product Managers'),
    (1, 'DELETE_ISSUE', 'role', 'Administrators'),
    (1, 'MOVE_ISSUE', 'role', 'Administrators'),
    (1, 'MOVE_ISSUE', 'role', 'Product Managers'),
    (1, 'LINK_ISSUE', 'role', 'Administrators'),
    (1, 'LINK_ISSUE', 'role', 'Developers'),
    (1, 'LINK_ISSUE', 'role', 'QA'),
    (1, 'LINK_ISSUE', 'role', 'Product Managers'),
    (1, 'ADD_COMMENTS', 'role', 'Administrators'),
    (1, 'ADD_COMMENTS', 'role', 'Developers'),
    (1, 'ADD_COMMENTS', 'role', 'QA'),
    (1, 'ADD_COMMENTS', 'role', 'Product Managers'),
    (1, 'ADD_COMMENTS', 'role', 'Viewers'),
    (1, 'ADD_COMMENTS', 'group', 'jira-software-users'),
    (1, 'EDIT_OWN_COMMENTS', 'role', 'Administrators'),
    (1, 'EDIT_OWN_COMMENTS', 'role', 'Developers'),
    (1, 'EDIT_OWN_COMMENTS', 'role', 'QA'),
    (1, 'EDIT_OWN_COMMENTS', 'role', 'Product Managers'),
    (1, 'EDIT_OWN_COMMENTS', 'group', 'jira-software-users'),
    (1, 'EDIT_ALL_COMMENTS', 'role', 'Administrators'),
    (1, 'DELETE_OWN_COMMENTS', 'role', 'Administrators'),
    (1, 'DELETE_OWN_COMMENTS', 'role', 'Developers'),
    (1, 'DELETE_ALL_COMMENTS', 'role', 'Administrators'),
    (1, 'CREATE_ATTACHMENTS', 'role', 'Administrators'),
    (1, 'CREATE_ATTACHMENTS', 'role', 'Developers'),
    (1, 'CREATE_ATTACHMENTS', 'role', 'QA'),
    (1, 'CREATE_ATTACHMENTS', 'role', 'Product Managers'),
    (1, 'CREATE_ATTACHMENTS', 'group', 'jira-software-users'),
    (1, 'DELETE_OWN_ATTACHMENTS', 'role', 'Administrators'),
    (1, 'DELETE_OWN_ATTACHMENTS', 'role', 'Developers'),
    (1, 'DELETE_ALL_ATTACHMENTS', 'role', 'Administrators'),
    (1, 'WORK_ON_ISSUES', 'role', 'Administrators'),
    (1, 'WORK_ON_ISSUES', 'role', 'Developers'),
    (1, 'WORK_ON_ISSUES', 'role', 'QA'),
    (1, 'WORK_ON_ISSUES', 'role', 'Product Managers'),
    (1, 'WORK_ON_ISSUES', 'dynamic', 'assignee'),
    (1, 'EDIT_OWN_WORKLOGS', 'role', 'Administrators'),
    (1, 'EDIT_OWN_WORKLOGS', 'role', 'Developers'),
    (1, 'EDIT_OWN_WORKLOGS', 'role', 'QA'),
    (1, 'EDIT_OWN_WORKLOGS', 'role', 'Product Managers'),
    (1, 'EDIT_ALL_WORKLOGS', 'role', 'Administrators'),
    (1, 'DELETE_WORKLOGS', 'role', 'Administrators'),
    (1, 'MANAGE_SPRINTS', 'role', 'Administrators'),
    (1, 'MANAGE_SPRINTS', 'role', 'Product Managers');

INSERT OR IGNORE INTO issue_security_schemes (id, name, description, default_level_id)
VALUES (1, 'Default Security Scheme', 'Standard security scheme providing public, internal, and confidential tiers', 1);

INSERT OR IGNORE INTO issue_security_levels (id, scheme_id, name, description, is_default)
VALUES
    (1, 1, 'Public', 'Visible to all project members and software users', 1),
    (2, 1, 'Internal Only', 'Visible only to project developers, managers, QA, reporter and assignee', 0),
    (3, 1, 'Confidential / Security Vulnerability', 'Visible exclusively to administrators, project lead, reporter, and assignee', 0);

UPDATE issue_security_schemes SET default_level_id = 1 WHERE id = 1;

INSERT OR IGNORE INTO issue_security_level_grants (level_id, grantee_type, grantee_value)
VALUES
    (1, 'role', 'Administrators'),
    (1, 'role', 'Developers'),
    (1, 'role', 'QA'),
    (1, 'role', 'Product Managers'),
    (1, 'role', 'Viewers'),
    (1, 'group', 'jira-software-users'),
    (2, 'role', 'Administrators'),
    (2, 'role', 'Developers'),
    (2, 'role', 'QA'),
    (2, 'role', 'Product Managers'),
    (2, 'dynamic', 'reporter'),
    (2, 'dynamic', 'assignee'),
    (3, 'role', 'Administrators'),
    (3, 'dynamic', 'lead'),
    (3, 'dynamic', 'reporter'),
    (3, 'dynamic', 'assignee');

UPDATE projects SET permission_scheme_id = 1 WHERE permission_scheme_id IS NULL;
UPDATE projects SET security_scheme_id = 1 WHERE security_scheme_id IS NULL;
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
