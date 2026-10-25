-- Migration 005: Add is_current flag to sprints to define the Current Sprint

ALTER TABLE sprints ADD COLUMN is_current INTEGER NOT NULL DEFAULT 0;

UPDATE sprints
SET is_current = 1
WHERE id = (
    SELECT id FROM sprints WHERE status = 'active' ORDER BY id ASC LIMIT 1
)
AND NOT EXISTS (SELECT 1 FROM sprints WHERE is_current = 1);
