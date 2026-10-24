-- Migration 004: Add support & seed examples for 'New Feature' and 'Improvement' work item types

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
    1,
    1,
    1,
    1,
    5,
    55,
    '2026-10-24',
    '2026-11-01',
    'feature,board,ux',
    120
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
    1,
    1,
    1,
    1,
    3,
    25,
    '2026-10-26',
    '2026-11-03',
    'improvement,performance,sqlite',
    130
FROM projects p
WHERE p.id = 1
  AND NOT EXISTS (SELECT 1 FROM work_items WHERE type = 'Improvement');

UPDATE projects
SET issue_seq = (SELECT COUNT(*) FROM work_items WHERE project_id = projects.id)
WHERE id = 1;
