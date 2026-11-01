-- Migration 021: Bulk Operations & Trash Bin (Soft Delete & Retention)
-- Stores deleted work items and associated metadata for 30-day retention countdown

CREATE TABLE IF NOT EXISTS trash_bin (
    id               INTEGER PRIMARY KEY AUTOINCREMENT,
    original_id      INTEGER NOT NULL,
    item_key         TEXT NOT NULL,
    project_id       INTEGER NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
    project_key      TEXT NOT NULL,
    item_type        TEXT NOT NULL,
    title            TEXT NOT NULL,
    item_data_json   TEXT NOT NULL,
    deleted_by       INTEGER REFERENCES users(id) ON DELETE SET NULL,
    deleted_at       TEXT NOT NULL DEFAULT (datetime('now')),
    purge_at         TEXT NOT NULL DEFAULT (datetime('now', '+30 days')),
    restored_at      TEXT DEFAULT NULL
);

CREATE INDEX IF NOT EXISTS idx_trash_bin_purge ON trash_bin(purge_at);
CREATE INDEX IF NOT EXISTS idx_trash_bin_project ON trash_bin(project_id);
