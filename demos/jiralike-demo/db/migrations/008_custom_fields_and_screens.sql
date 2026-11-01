-- =============================================================================
-- Migration v8: Dynamic Custom Fields Engine, Field Configurations & Screen Schemes
-- =============================================================================

-- 1. Custom Field Definitions (custom_fields)
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

-- Seed standard Jira custom fields across types
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

-- 2. Custom Field Selectable Options (custom_field_options)
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
    -- Options for custom_field_id = 1 (Target Environment)
    (1,  1, 'Production',               '#DE350B', 10, 1, 1),
    (2,  1, 'Staging',                  '#FF991F', 20, 1, 0),
    (3,  1, 'QA Cluster',               '#00B8D9', 30, 1, 0),
    (4,  1, 'Local Dev',                '#36B37E', 40, 1, 0),
    -- Options for custom_field_id = 3 (Customer Impact Tier)
    (5,  3, 'Tier 1 - Enterprise Critical', '#DE350B', 10, 1, 0),
    (6,  3, 'Tier 2 - High',                '#FF991F', 20, 1, 0),
    (7,  3, 'Tier 3 - Moderate',            '#0052CC', 30, 1, 1),
    (8,  3, 'Internal Only',                '#6B778C', 40, 1, 0),
    -- Options for custom_field_id = 9 (Affected Platforms)
    (9,  9, 'Linux x86_64',             '#0052CC', 10, 1, 1),
    (10, 9, 'Linux ARM64',              '#00B8D9', 20, 1, 0),
    (11, 9, 'Docker / Kubernetes',      '#6554C0', 30, 1, 0),
    (12, 9, 'Web Browser (SPA)',        '#36B37E', 40, 1, 0);

-- 3. Normalized EAV Storage for Custom Field Values (custom_field_values)
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

-- 4. Field Configurations & Schemes (field_configurations, field_configuration_items, field_configuration_schemes)
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

-- 5. Screens, Screen Tabs, Tab Fields & Screen Schemes
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
    -- Screen 1: Default Create Issue Screen
    (1, 1, 'General & Acceptance',    10),
    (2, 1, 'Engineering & QA',        20),
    (3, 1, 'Release & Compliance',    30),
    -- Screen 2: Default Edit & View Issue Screen
    (4, 2, 'General & Acceptance',    10),
    (5, 2, 'Engineering & QA',        20),
    (6, 2, 'Release & Compliance',    30),
    -- Screen 3: Resolve / Close Transition Screen
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
    -- Screen 1 (Create) Tabs
    (1, 'environment',          1, 10),
    (1, 'acceptance_criteria',  2, 20),
    (1, 'customer_impact',      3, 30),
    (2, 'risk_score',           4, 10),
    (2, 'qa_reviewer',          6, 20),
    (2, 'affected_platforms',   9, 30),
    (3, 'target_deploy_date',   5, 10),
    (3, 'spec_url',             7, 20),
    (3, 'security_reviewed',    8, 30),
    -- Screen 2 (Edit & View) Tabs
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
    -- Screen 3 (Transition) Tab
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

-- Backfill sample custom field values for existing work items
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
