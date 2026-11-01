-- ============================================================================
-- Migration 014: Configurable Dashboards & Gadgets Framework (Step 9 & Epic 7)
-- ============================================================================

-- 1. Dashboards Table: Stores user and shared dashboards
CREATE TABLE IF NOT EXISTS dashboards (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    owner_id     INTEGER NOT NULL,
    name         TEXT NOT NULL,
    description  TEXT DEFAULT '',
    layout       TEXT NOT NULL DEFAULT '2_column_equal', -- '1_column', '2_column_equal', '2_column_split', '3_column'
    is_favorite  INTEGER NOT NULL DEFAULT 0,
    share_type   TEXT NOT NULL DEFAULT 'private',       -- 'private', 'project', 'public'
    project_id   INTEGER DEFAULT NULL,
    created_at   TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at   TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (owner_id) REFERENCES users(id) ON DELETE CASCADE,
    FOREIGN KEY (project_id) REFERENCES projects(id) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS idx_dashboards_owner ON dashboards(owner_id);
CREATE INDEX IF NOT EXISTS idx_dashboards_fav ON dashboards(is_favorite);
CREATE INDEX IF NOT EXISTS idx_dashboards_project ON dashboards(project_id);

-- 2. Dashboard Gadgets Table: Stores gadget instances placed on a dashboard
CREATE TABLE IF NOT EXISTS dashboard_gadgets (
    id               INTEGER PRIMARY KEY AUTOINCREMENT,
    dashboard_id     INTEGER NOT NULL,
    gadget_type      TEXT NOT NULL, -- 'filter_results', 'pie_chart', 'created_vs_resolved', 'sprint_burndown', 'two_dimensional_stats', 'assigned_to_me', 'activity_stream', 'roadmap_summary'
    title            TEXT NOT NULL,
    column_index     INTEGER NOT NULL DEFAULT 0,
    row_index        INTEGER NOT NULL DEFAULT 0,
    preferences_json TEXT NOT NULL DEFAULT '{}',
    created_at       TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at       TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (dashboard_id) REFERENCES dashboards(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_dashboard_gadgets_dash ON dashboard_gadgets(dashboard_id, column_index, row_index);

-- 3. Seed Default System Starter Dashboard (if admin and project 1 exist)
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

-- Seed default gadgets on Dashboard 1
INSERT OR IGNORE INTO dashboard_gadgets (id, dashboard_id, gadget_type, title, column_index, row_index, preferences_json, created_at)
SELECT
    1,
    1,
    'assigned_to_me',
    'Assigned to Me',
    0,
    0,
    '{"limit":10,"include_resolved":false}',
    datetime('now', '-3 days')
WHERE EXISTS (SELECT 1 FROM dashboards WHERE id = 1);

INSERT OR IGNORE INTO dashboard_gadgets (id, dashboard_id, gadget_type, title, column_index, row_index, preferences_json, created_at)
SELECT
    2,
    1,
    'sprint_burndown',
    'Active Sprint Burndown',
    1,
    0,
    '{"sprint_id":0,"unit":"points"}',
    datetime('now', '-3 days')
WHERE EXISTS (SELECT 1 FROM dashboards WHERE id = 1);

INSERT OR IGNORE INTO dashboard_gadgets (id, dashboard_id, gadget_type, title, column_index, row_index, preferences_json, created_at)
SELECT
    3,
    1,
    'pie_chart',
    'Issues by Status',
    0,
    1,
    '{"statistic_type":"status"}',
    datetime('now', '-3 days')
WHERE EXISTS (SELECT 1 FROM dashboards WHERE id = 1);

INSERT OR IGNORE INTO dashboard_gadgets (id, dashboard_id, gadget_type, title, column_index, row_index, preferences_json, created_at)
SELECT
    4,
    1,
    'activity_stream',
    'Recent Activity Stream',
    1,
    1,
    '{"limit":15}',
    datetime('now', '-3 days')
WHERE EXISTS (SELECT 1 FROM dashboards WHERE id = 1);

INSERT OR IGNORE INTO dashboard_gadgets (id, dashboard_id, gadget_type, title, column_index, row_index, preferences_json, created_at)
SELECT
    5,
    1,
    'two_dimensional_stats',
    'Assignee vs Status Matrix',
    0,
    2,
    '{"x_axis":"status","y_axis":"assignee"}',
    datetime('now', '-3 days')
WHERE EXISTS (SELECT 1 FROM dashboards WHERE id = 1);

INSERT OR IGNORE INTO dashboard_gadgets (id, dashboard_id, gadget_type, title, column_index, row_index, preferences_json, created_at)
SELECT
    6,
    1,
    'created_vs_resolved',
    'Created vs Resolved Trend',
    1,
    2,
    '{"days":30}',
    datetime('now', '-3 days')
WHERE EXISTS (SELECT 1 FROM dashboards WHERE id = 1);
