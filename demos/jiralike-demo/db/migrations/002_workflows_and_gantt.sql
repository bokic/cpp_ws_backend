-- Migration 002: Work items (Epics, Stories, Tasks, Subtasks, Bugs, Issues) and workflow state history

CREATE TABLE IF NOT EXISTS work_items (
    id                 INTEGER PRIMARY KEY AUTOINCREMENT,
    project_id         INTEGER NOT NULL,
    key                TEXT NOT NULL UNIQUE,
    type               TEXT NOT NULL DEFAULT 'Task',
    status             TEXT NOT NULL DEFAULT 'To Do',
    priority           TEXT NOT NULL DEFAULT 'Medium',
    title              TEXT NOT NULL,
    description        TEXT DEFAULT '',
    parent_id          INTEGER DEFAULT NULL,
    sprint_id          INTEGER DEFAULT NULL,
    assignee_id        INTEGER DEFAULT NULL,
    reporter_id        INTEGER DEFAULT NULL,
    story_points        INTEGER NOT NULL DEFAULT 0,
    progress           INTEGER NOT NULL DEFAULT 0,
    start_date         TEXT DEFAULT '',
    due_date           TEXT DEFAULT '',
    severity           TEXT DEFAULT '',
    steps_to_reproduce TEXT DEFAULT '',
    labels             TEXT DEFAULT '',
    board_order        INTEGER NOT NULL DEFAULT 0,
    created_at         TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at         TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (project_id) REFERENCES projects(id) ON DELETE CASCADE,
    FOREIGN KEY (parent_id) REFERENCES work_items(id) ON DELETE SET NULL,
    FOREIGN KEY (sprint_id) REFERENCES sprints(id) ON DELETE SET NULL,
    FOREIGN KEY (assignee_id) REFERENCES users(id) ON DELETE SET NULL,
    FOREIGN KEY (reporter_id) REFERENCES users(id) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS idx_work_items_project ON work_items(project_id);
CREATE INDEX IF NOT EXISTS idx_work_items_status ON work_items(status);
CREATE INDEX IF NOT EXISTS idx_work_items_type ON work_items(type);
CREATE INDEX IF NOT EXISTS idx_work_items_parent ON work_items(parent_id);

CREATE TABLE IF NOT EXISTS workflow_history (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    work_item_id INTEGER NOT NULL,
    from_status  TEXT NOT NULL,
    to_status    TEXT NOT NULL,
    changed_by   INTEGER,
    comment      TEXT DEFAULT '',
    created_at   TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (work_item_id) REFERENCES work_items(id) ON DELETE CASCADE,
    FOREIGN KEY (changed_by) REFERENCES users(id) ON DELETE SET NULL
);
