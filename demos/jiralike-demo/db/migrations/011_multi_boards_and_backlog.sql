-- Migration v11: Scrum & Kanban Multi-Board Engine with Agile Backlog Planning

CREATE TABLE IF NOT EXISTS boards (
    id                   INTEGER PRIMARY KEY AUTOINCREMENT,
    project_id           INTEGER DEFAULT 1,
    name                 TEXT NOT NULL,
    board_type           TEXT NOT NULL DEFAULT 'scrum', -- 'scrum' | 'kanban'
    filter_jql           TEXT DEFAULT '',
    swimlane_strategy    TEXT NOT NULL DEFAULT 'none',  -- 'none' | 'epic' | 'assignee' | 'priority' | 'story' | 'custom_jql'
    card_extra_fields    TEXT DEFAULT 'story_points,due_date,components',
    card_color_strategy  TEXT NOT NULL DEFAULT 'priority', -- 'priority' | 'type' | 'category' | 'none'
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
    mapped_statuses TEXT NOT NULL, -- comma-separated workflow statuses mapped to this column
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

-- Expand sprints table for Sprint Lifecycle & Velocity tracking
ALTER TABLE sprints ADD COLUMN completed_at TEXT DEFAULT '';
ALTER TABLE sprints ADD COLUMN committed_points INTEGER NOT NULL DEFAULT 0;
ALTER TABLE sprints ADD COLUMN completed_points INTEGER NOT NULL DEFAULT 0;
ALTER TABLE sprints ADD COLUMN completed_items_count INTEGER NOT NULL DEFAULT 0;
ALTER TABLE sprints ADD COLUMN incomplete_items_count INTEGER NOT NULL DEFAULT 0;
ALTER TABLE sprints ADD COLUMN rollover_target_sprint_id INTEGER DEFAULT NULL;

-- Backfill committed_points and completed_points for existing sprints
UPDATE sprints
SET committed_points = COALESCE((SELECT SUM(w.story_points) FROM work_items w WHERE w.sprint_id = sprints.id), 0),
    completed_points = COALESCE((SELECT SUM(w.story_points) FROM work_items w WHERE w.sprint_id = sprints.id AND w.status = 'Done'), 0),
    completed_items_count = COALESCE((SELECT COUNT(*) FROM work_items w WHERE w.sprint_id = sprints.id AND w.status = 'Done'), 0),
    incomplete_items_count = COALESCE((SELECT COUNT(*) FROM work_items w WHERE w.sprint_id = sprints.id AND w.status != 'Done'), 0);

-- Ensure at least two unassigned backlog items (sprint_id IS NULL) exist when project 1 is already seeded
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

-- Seed Default Scrum & Kanban Boards
INSERT OR IGNORE INTO boards (id, project_id, name, board_type, filter_jql, swimlane_strategy, card_extra_fields, card_color_strategy, show_backlog_column, is_default) VALUES
    (1, 1, 'Core Platform Scrum Board',      'scrum',  '',                                                                    'none',     'story_points,due_date,components',   'priority', 0, 1),
    (2, 1, 'Continuous Flow Kanban Board',   'kanban', '',                                                                    'epic',     'due_date,components,fix_versions',   'priority', 1, 0),
    (3, 1, 'Bug Triage & QA Board',          'kanban', 'category = "Bug" OR status IN ("In Review", "QA / Testing", "Blocked")', 'priority', 'story_points,due_date,components', 'category', 1, 0);

-- Seed Columns for Board 1 (Core Platform Scrum Board - includes Many-to-One 'Review & QA' column)
INSERT OR IGNORE INTO board_columns (id, board_id, name, mapped_statuses, min_wip, max_wip, color, sort_order) VALUES
    (1,  1, 'To Do',                       'To Do',                  0, 0, '#42526E', 10),
    (2,  1, 'In Progress',                 'In Progress',            1, 5, '#0052CC', 20),
    (3,  1, 'Review & QA',                 'In Review,QA / Testing', 0, 4, '#00B8D9', 30),
    (4,  1, 'Blocked',                     'Blocked',                0, 2, '#DE350B', 40),
    (5,  1, 'Done',                        'Done',                   0, 0, '#00875A', 50);

-- Seed Columns for Board 2 (Continuous Flow Kanban Board with strict WIP limits)
INSERT OR IGNORE INTO board_columns (id, board_id, name, mapped_statuses, min_wip, max_wip, color, sort_order) VALUES
    (6,  2, 'Backlog',                     'Backlog',                0, 0, '#6B778C', 10),
    (7,  2, 'Selected for Development',    'To Do',                  0, 6, '#42526E', 20),
    (8,  2, 'Active Development',          'In Progress',            1, 3, '#0052CC', 30),
    (9,  2, 'Verification (Review + QA)',  'In Review,QA / Testing', 0, 3, '#6554C0', 40),
    (10, 2, 'Blocked',                     'Blocked',                0, 1, '#DE350B', 50),
    (11, 2, 'Released / Done',             'Done',                   0, 0, '#00875A', 60);

-- Seed Columns for Board 3 (Bug Triage & QA Board)
INSERT OR IGNORE INTO board_columns (id, board_id, name, mapped_statuses, min_wip, max_wip, color, sort_order) VALUES
    (12, 3, 'Incoming Triage',             'Backlog,To Do',          0, 8, '#42526E', 10),
    (13, 3, 'Fix In Progress',             'In Progress,Blocked',    0, 4, '#0052CC', 20),
    (14, 3, 'QA Verification',             'In Review,QA / Testing', 0, 4, '#00B8D9', 30),
    (15, 3, 'Verified & Closed',           'Done',                   0, 0, '#00875A', 40);

-- Seed Quick Filters
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

-- Seed Custom JQL Swimlanes
INSERT OR IGNORE INTO board_swimlanes (id, board_id, name, description, jql_query, sort_order) VALUES
    (1, 1, '🚨 Expedite (Critical & Highest)', 'Urgent items bypassing standard queue',      'priority IN ("Critical", "Highest")', 10),
    (2, 1, '🐛 Bugs & Regressions',            'Production and sprint defects',              'category = "Bug" AND priority NOT IN ("Critical", "Highest")', 20),
    (3, 1, '🚀 Standard Feature Delivery',     'Planned stories, tasks, and improvements',   'category != "Bug" AND priority NOT IN ("Critical", "Highest")', 30),
    (4, 2, '🚨 Expedite Lane',                 'Fast-track critical work items',             'priority IN ("Critical", "Highest")', 10),
    (5, 2, '⚡ Standard Continuous Flow',      'Regular flow items',                         'priority NOT IN ("Critical", "Highest")', 20);
