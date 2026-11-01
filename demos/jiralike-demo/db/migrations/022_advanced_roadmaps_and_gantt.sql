-- Migration 022: Advanced Roadmaps, Gantt Dependencies, Critical Path & Capacity Planning

-- 1. Add lag_days to work_item_links for dependency scheduling (lead/lag offsets)
ALTER TABLE work_item_links ADD COLUMN lag_days INTEGER NOT NULL DEFAULT 0;

-- 2. Timeline Baselines Table for versioned schedule snapshots
CREATE TABLE IF NOT EXISTS timeline_baselines (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    project_id  INTEGER NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
    name        TEXT NOT NULL,
    description TEXT DEFAULT '',
    created_by  INTEGER REFERENCES users(id) ON DELETE SET NULL,
    created_at  TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE INDEX IF NOT EXISTS idx_timeline_baselines_proj ON timeline_baselines(project_id);

-- 3. Timeline Baseline Items Table capturing point-in-time schedule snapshots
CREATE TABLE IF NOT EXISTS timeline_baseline_items (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    baseline_id     INTEGER NOT NULL REFERENCES timeline_baselines(id) ON DELETE CASCADE,
    work_item_id    INTEGER NOT NULL REFERENCES work_items(id) ON DELETE CASCADE,
    item_key        TEXT NOT NULL,
    title           TEXT NOT NULL,
    start_date      TEXT DEFAULT '',
    due_date        TEXT DEFAULT '',
    duration_days   INTEGER NOT NULL DEFAULT 1,
    story_points    INTEGER NOT NULL DEFAULT 0,
    status          TEXT NOT NULL DEFAULT '',
    created_at      TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE INDEX IF NOT EXISTS idx_timeline_baseline_items_base ON timeline_baseline_items(baseline_id);
CREATE INDEX IF NOT EXISTS idx_timeline_baseline_items_item ON timeline_baseline_items(work_item_id);

-- 4. User Working Capacities Table for team capacity & workload balancing
CREATE TABLE IF NOT EXISTS user_capacities (
    id                    INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id               INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    project_id            INTEGER REFERENCES projects(id) ON DELETE CASCADE,
    weekly_capacity_hours REAL NOT NULL DEFAULT 40.0,
    created_at            TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at            TEXT NOT NULL DEFAULT (datetime('now')),
    UNIQUE(user_id, project_id)
);

CREATE INDEX IF NOT EXISTS idx_user_capacities_user ON user_capacities(user_id);

-- 5. Seed default capacities for existing active users (40h / week default)
INSERT OR IGNORE INTO user_capacities (user_id, project_id, weekly_capacity_hours)
SELECT id, NULL, 40.0 FROM users;
