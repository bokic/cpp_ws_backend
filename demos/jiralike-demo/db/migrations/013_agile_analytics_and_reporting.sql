-- ============================================================================
-- Migration 013: Agile Analytics & Reporting Engine (Step 8 & Epic 5)
-- ============================================================================

-- 1. Sprint Events table: tracks sprint commitment, scope changes, point changes
CREATE TABLE IF NOT EXISTS sprint_events (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    sprint_id    INTEGER NOT NULL,
    work_item_id INTEGER NOT NULL,
    event_type   TEXT NOT NULL, -- 'committed', 'added', 'removed', 'points_changed'
    old_points   INTEGER NOT NULL DEFAULT 0,
    new_points   INTEGER NOT NULL DEFAULT 0,
    created_at   TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (sprint_id) REFERENCES sprints(id) ON DELETE CASCADE,
    FOREIGN KEY (work_item_id) REFERENCES work_items(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_sprint_events_sprint ON sprint_events(sprint_id, created_at ASC);
CREATE INDEX IF NOT EXISTS idx_sprint_events_item ON sprint_events(work_item_id);

-- 2. Populate initial committed events for all current work items in sprints
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

-- 3. Triggers to automatically record sprint scope changes
CREATE TRIGGER IF NOT EXISTS trg_work_items_sprint_update
AFTER UPDATE OF sprint_id, story_points ON work_items
FOR EACH ROW
BEGIN
    -- Scope removed from old sprint
    INSERT INTO sprint_events (sprint_id, work_item_id, event_type, old_points, new_points, created_at)
    SELECT OLD.sprint_id, OLD.id, 'removed', OLD.story_points, 0, datetime('now')
    WHERE OLD.sprint_id IS NOT NULL AND (NEW.sprint_id IS NULL OR OLD.sprint_id != NEW.sprint_id);

    -- Scope added to new sprint
    INSERT INTO sprint_events (sprint_id, work_item_id, event_type, old_points, new_points, created_at)
    SELECT NEW.sprint_id, NEW.id, 'added', 0, NEW.story_points, datetime('now')
    WHERE NEW.sprint_id IS NOT NULL AND (OLD.sprint_id IS NULL OR OLD.sprint_id != NEW.sprint_id);

    -- Story points changed while remaining in sprint
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

-- 4. Ensure historical sprint data exists for multi-sprint velocity tracking (if project 1 exists)
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
