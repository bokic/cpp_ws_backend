-- Migration 006: Separate work item dimension into Hierarchy Type (Epic, Story, Task, Subtask)
-- and Category (New Feature, Improvement, Bug, Issue)

ALTER TABLE work_items ADD COLUMN category TEXT NOT NULL DEFAULT 'New Feature';

-- Convert items whose 'type' was previously a category into structural types + category
UPDATE work_items SET category = 'Bug',         type = 'Task'  WHERE type = 'Bug';
UPDATE work_items SET category = 'New Feature', type = 'Story' WHERE type = 'New Feature';
UPDATE work_items SET category = 'Improvement', type = 'Task'  WHERE type = 'Improvement';
UPDATE work_items SET category = 'Issue',       type = 'Task'  WHERE type = 'Issue';

-- Enrich existing sample tasks/subtasks with varied categories
UPDATE work_items SET category = 'Improvement' WHERE type = 'Task' AND category = 'New Feature' AND (id % 2 = 0);
UPDATE work_items SET category = 'Improvement' WHERE type = 'Subtask' AND (id % 2 = 1);

CREATE INDEX IF NOT EXISTS idx_work_items_category ON work_items(category);
