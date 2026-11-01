-- Migration 015: Rich text changelog, user mentions, watchers, and votes

CREATE TABLE IF NOT EXISTS work_item_changelog (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    work_item_id  INTEGER NOT NULL,
    changed_by_id INTEGER,
    field_name    TEXT NOT NULL,
    old_value     TEXT,
    new_value     TEXT,
    created_at    TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (work_item_id) REFERENCES work_items(id) ON DELETE CASCADE,
    FOREIGN KEY (changed_by_id) REFERENCES users(id) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS idx_changelog_work_item ON work_item_changelog(work_item_id, id DESC);

CREATE TABLE IF NOT EXISTS work_item_mentions (
    id                INTEGER PRIMARY KEY AUTOINCREMENT,
    work_item_id      INTEGER NOT NULL,
    comment_id        INTEGER,
    author_id         INTEGER NOT NULL,
    mentioned_user_id INTEGER NOT NULL,
    snippet           TEXT NOT NULL DEFAULT '',
    is_read           INTEGER NOT NULL DEFAULT 0,
    created_at        TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (work_item_id) REFERENCES work_items(id) ON DELETE CASCADE,
    FOREIGN KEY (comment_id) REFERENCES comments(id) ON DELETE CASCADE,
    FOREIGN KEY (author_id) REFERENCES users(id) ON DELETE CASCADE,
    FOREIGN KEY (mentioned_user_id) REFERENCES users(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_mentions_user ON work_item_mentions(mentioned_user_id, is_read, id DESC);
CREATE INDEX IF NOT EXISTS idx_mentions_item ON work_item_mentions(work_item_id);

CREATE TABLE IF NOT EXISTS work_item_watchers (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    work_item_id INTEGER NOT NULL,
    user_id      INTEGER NOT NULL,
    created_at   TEXT NOT NULL DEFAULT (datetime('now')),
    UNIQUE(work_item_id, user_id),
    FOREIGN KEY (work_item_id) REFERENCES work_items(id) ON DELETE CASCADE,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_watchers_item ON work_item_watchers(work_item_id);
CREATE INDEX IF NOT EXISTS idx_watchers_user ON work_item_watchers(user_id);

CREATE TABLE IF NOT EXISTS work_item_votes (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    work_item_id INTEGER NOT NULL,
    user_id      INTEGER NOT NULL,
    created_at   TEXT NOT NULL DEFAULT (datetime('now')),
    UNIQUE(work_item_id, user_id),
    FOREIGN KEY (work_item_id) REFERENCES work_items(id) ON DELETE CASCADE,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_votes_item ON work_item_votes(work_item_id);
CREATE INDEX IF NOT EXISTS idx_votes_user ON work_item_votes(user_id);
