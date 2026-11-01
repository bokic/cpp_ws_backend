-- ============================================================================
-- Migration 012: Time Tracking, Estimation Triad & Worklogs Subsystem (Step 7)
-- ============================================================================

-- 1. Add Estimation Triad & Alternative Estimation Unit fields to work_items
ALTER TABLE work_items ADD COLUMN original_estimate_seconds INTEGER NOT NULL DEFAULT 0;
ALTER TABLE work_items ADD COLUMN remaining_estimate_seconds INTEGER NOT NULL DEFAULT 0;
ALTER TABLE work_items ADD COLUMN time_spent_seconds INTEGER NOT NULL DEFAULT 0;
ALTER TABLE work_items ADD COLUMN tshirt_size TEXT NOT NULL DEFAULT '';

-- 2. Create Worklogs Table
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

-- 3. Backfill realistic estimates and t-shirt sizes on existing demo work items
-- (1 story point = 4 hours = 14,400 seconds baseline estimate)
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

-- 4. Seed realistic worklog entries for existing work items (when database is already populated)
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

-- Sync work_items.time_spent_seconds and remaining_estimate_seconds with seeded worklogs
UPDATE work_items
SET time_spent_seconds = COALESCE((SELECT SUM(wl.time_spent_seconds) FROM worklogs wl WHERE wl.work_item_id = work_items.id), 0),
    remaining_estimate_seconds = CASE
        WHEN status = 'Done' THEN 0
        ELSE MAX(0, original_estimate_seconds - COALESCE((SELECT SUM(wl.time_spent_seconds) FROM worklogs wl WHERE wl.work_item_id = work_items.id), 0))
    END;
