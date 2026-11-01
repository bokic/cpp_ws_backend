-- Migration v10: JQL (Jira Query Language) Saved Filters, Subscriptions & SQLite FTS5 Full-Text Search
-- Epic 3: JQL Parser, AST Execution Engine, FTS5 Full-Text Search, and Saved Filters

-- 1. SQLite FTS5 Full-Text Search Virtual Table on Work Items & Comments
CREATE VIRTUAL TABLE IF NOT EXISTS work_items_fts USING fts5(
    work_item_id UNINDEXED,
    key,
    title,
    description,
    steps_to_reproduce,
    labels,
    comments_text,
    tokenize = 'unicode61 remove_diacritics 2'
);

-- Rebuild / Backfill FTS5 index from existing work_items and comments
DELETE FROM work_items_fts;
INSERT INTO work_items_fts (work_item_id, key, title, description, steps_to_reproduce, labels, comments_text)
SELECT
    w.id,
    COALESCE(w.key, ''),
    COALESCE(w.title, ''),
    COALESCE(w.description, ''),
    COALESCE(w.steps_to_reproduce, ''),
    COALESCE(w.labels, ''),
    COALESCE((SELECT group_concat(c.body, ' ') FROM comments c WHERE c.work_item_id = w.id), '')
FROM work_items w;

-- Synchronization Triggers on work_items
CREATE TRIGGER IF NOT EXISTS trg_work_items_fts_ai
AFTER INSERT ON work_items
BEGIN
    INSERT INTO work_items_fts (work_item_id, key, title, description, steps_to_reproduce, labels, comments_text)
    VALUES (
        NEW.id,
        COALESCE(NEW.key, ''),
        COALESCE(NEW.title, ''),
        COALESCE(NEW.description, ''),
        COALESCE(NEW.steps_to_reproduce, ''),
        COALESCE(NEW.labels, ''),
        COALESCE((SELECT group_concat(c.body, ' ') FROM comments c WHERE c.work_item_id = NEW.id), '')
    );
END;

CREATE TRIGGER IF NOT EXISTS trg_work_items_fts_au
AFTER UPDATE ON work_items
BEGIN
    DELETE FROM work_items_fts WHERE CAST(work_item_id AS INTEGER) = OLD.id;
    INSERT INTO work_items_fts (work_item_id, key, title, description, steps_to_reproduce, labels, comments_text)
    VALUES (
        NEW.id,
        COALESCE(NEW.key, ''),
        COALESCE(NEW.title, ''),
        COALESCE(NEW.description, ''),
        COALESCE(NEW.steps_to_reproduce, ''),
        COALESCE(NEW.labels, ''),
        COALESCE((SELECT group_concat(c.body, ' ') FROM comments c WHERE c.work_item_id = NEW.id), '')
    );
END;

CREATE TRIGGER IF NOT EXISTS trg_work_items_fts_ad
AFTER DELETE ON work_items
BEGIN
    DELETE FROM work_items_fts WHERE CAST(work_item_id AS INTEGER) = OLD.id;
END;

-- Synchronization Triggers on comments
CREATE TRIGGER IF NOT EXISTS trg_comments_fts_ai
AFTER INSERT ON comments
BEGIN
    DELETE FROM work_items_fts WHERE CAST(work_item_id AS INTEGER) = NEW.work_item_id;
    INSERT INTO work_items_fts (work_item_id, key, title, description, steps_to_reproduce, labels, comments_text)
    SELECT
        w.id,
        COALESCE(w.key, ''),
        COALESCE(w.title, ''),
        COALESCE(w.description, ''),
        COALESCE(w.steps_to_reproduce, ''),
        COALESCE(w.labels, ''),
        COALESCE((SELECT group_concat(c.body, ' ') FROM comments c WHERE c.work_item_id = w.id), '')
    FROM work_items w
    WHERE w.id = NEW.work_item_id;
END;

CREATE TRIGGER IF NOT EXISTS trg_comments_fts_au
AFTER UPDATE ON comments
BEGIN
    DELETE FROM work_items_fts WHERE CAST(work_item_id AS INTEGER) = NEW.work_item_id;
    INSERT INTO work_items_fts (work_item_id, key, title, description, steps_to_reproduce, labels, comments_text)
    SELECT
        w.id,
        COALESCE(w.key, ''),
        COALESCE(w.title, ''),
        COALESCE(w.description, ''),
        COALESCE(w.steps_to_reproduce, ''),
        COALESCE(w.labels, ''),
        COALESCE((SELECT group_concat(c.body, ' ') FROM comments c WHERE c.work_item_id = w.id), '')
    FROM work_items w
    WHERE w.id = NEW.work_item_id;
END;

CREATE TRIGGER IF NOT EXISTS trg_comments_fts_ad
AFTER DELETE ON comments
BEGIN
    DELETE FROM work_items_fts WHERE CAST(work_item_id AS INTEGER) = OLD.work_item_id;
    INSERT INTO work_items_fts (work_item_id, key, title, description, steps_to_reproduce, labels, comments_text)
    SELECT
        w.id,
        COALESCE(w.key, ''),
        COALESCE(w.title, ''),
        COALESCE(w.description, ''),
        COALESCE(w.steps_to_reproduce, ''),
        COALESCE(w.labels, ''),
        COALESCE((SELECT group_concat(c.body, ' ') FROM comments c WHERE c.work_item_id = w.id), '')
    FROM work_items w
    WHERE w.id = OLD.work_item_id;
END;

-- 2. Saved JQL Filters & Sharing Permissions
CREATE TABLE IF NOT EXISTS saved_filters (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    owner_id     INTEGER DEFAULT NULL,
    name         TEXT NOT NULL,
    description  TEXT DEFAULT '',
    jql_query    TEXT NOT NULL,
    is_favorite  INTEGER NOT NULL DEFAULT 0,
    is_system    INTEGER NOT NULL DEFAULT 0,
    share_type   TEXT NOT NULL DEFAULT 'public', -- 'private', 'public', 'project', 'role'
    share_target TEXT DEFAULT '*',
    view_columns TEXT DEFAULT 'key,type,title,status,priority,assignee,sprint,story_points,updated_at',
    sort_order   INTEGER NOT NULL DEFAULT 100,
    created_at   TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at   TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (owner_id) REFERENCES users(id) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS idx_saved_filters_owner ON saved_filters(owner_id);
CREATE INDEX IF NOT EXISTS idx_saved_filters_fav ON saved_filters(is_favorite);

-- Per-user favorite filter stars
CREATE TABLE IF NOT EXISTS filter_stars (
    filter_id  INTEGER NOT NULL,
    user_id    INTEGER NOT NULL,
    created_at TEXT NOT NULL DEFAULT (datetime('now')),
    PRIMARY KEY (filter_id, user_id),
    FOREIGN KEY (filter_id) REFERENCES saved_filters(id) ON DELETE CASCADE,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);

-- 3. Filter Subscriptions (Scheduled Digest Runner)
CREATE TABLE IF NOT EXISTS filter_subscriptions (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    filter_id       INTEGER NOT NULL,
    user_id         INTEGER DEFAULT NULL,
    cron_schedule   TEXT NOT NULL DEFAULT '0 8 * * 1-5', -- Weekdays at 08:00
    frequency_label TEXT NOT NULL DEFAULT 'Daily (Weekdays 08:00)',
    recipient_scope TEXT NOT NULL DEFAULT 'self',        -- 'self', 'role:Developer', 'role:QA Engineer', 'all'
    email_empty     INTEGER NOT NULL DEFAULT 0,
    is_active       INTEGER NOT NULL DEFAULT 1,
    last_run_at     TEXT DEFAULT '',
    created_at      TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (filter_id) REFERENCES saved_filters(id) ON DELETE CASCADE,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);

-- 4. Seed Default System Saved Filters
INSERT OR IGNORE INTO saved_filters (id, owner_id, name, description, jql_query, is_favorite, is_system, share_type, share_target, sort_order) VALUES
    (1, NULL, 'My Open Issues',               'Unresolved work items currently assigned to the logged-in user',                               'assignee = currentUser() AND resolution IS EMPTY ORDER BY priority DESC, updated DESC', 1, 1, 'public', '*', 10),
    (2, NULL, 'Reported by Me',               'All issues created or reported by the logged-in user',                                         'reporter = currentUser() ORDER BY created DESC',                                        1, 1, 'public', '*', 20),
    (3, NULL, 'Active Sprint Work',           'All unresolved items in currently active sprints ordered by priority',                         'sprint IN openSprints() AND status != "Done" ORDER BY priority DESC, key ASC',         1, 1, 'public', '*', 30),
    (4, NULL, 'High Priority & Critical Bugs','Open defects with High, Highest, or Critical priority requiring immediate triage',             'category = "Bug" AND priority IN ("Critical", "Highest", "High") AND status != "Done" ORDER BY priority DESC', 1, 1, 'public', '*', 40),
    (5, NULL, 'Ready for QA Verification',    'Work items currently in In Review or QA / Testing awaiting verification',                      'status IN ("In Review", "QA / Testing") ORDER BY priority DESC, updated DESC',          0, 1, 'public', '*', 50),
    (6, NULL, 'Recently Updated',             'Issues updated since the start of the current week',                                           'updated >= startOfWeek() ORDER BY updated DESC',                                        0, 1, 'public', '*', 60),
    (7, NULL, 'Done & Resolved Issues',       'Completed work items with a resolution set',                                                   'status = "Done" OR resolution IS NOT EMPTY ORDER BY updated DESC',                      0, 1, 'public', '*', 70),
    (8, NULL, 'Unassigned Backlog',           'Items in Backlog or To Do that do not yet have an owner',                                      'assignee IS EMPTY AND status IN ("Backlog", "To Do") ORDER BY priority DESC, id ASC',   0, 1, 'public', '*', 80);
