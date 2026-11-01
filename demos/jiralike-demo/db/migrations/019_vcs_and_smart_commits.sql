-- Migration 019: Developer Tooling & Git Integration (Smart Commits & Dev Panel)

CREATE TABLE IF NOT EXISTS git_repositories (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    name           TEXT NOT NULL,
    provider       TEXT NOT NULL DEFAULT 'github', -- 'github', 'gitlab', 'bitbucket', 'gitea', 'custom'
    repo_url       TEXT NOT NULL DEFAULT '',
    webhook_secret TEXT DEFAULT '',
    is_active      INTEGER NOT NULL DEFAULT 1,
    created_at     TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at     TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS git_branches (
    id                INTEGER PRIMARY KEY AUTOINCREMENT,
    work_item_id      INTEGER NOT NULL,
    repository_id     INTEGER,
    repository_name   TEXT NOT NULL DEFAULT '',
    branch_name       TEXT NOT NULL,
    branch_url        TEXT DEFAULT '',
    last_commit_hash  TEXT DEFAULT '',
    created_at        TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at        TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (work_item_id) REFERENCES work_items(id) ON DELETE CASCADE,
    FOREIGN KEY (repository_id) REFERENCES git_repositories(id) ON DELETE SET NULL,
    UNIQUE(work_item_id, repository_name, branch_name)
);

CREATE INDEX IF NOT EXISTS idx_git_branches_item ON git_branches(work_item_id);

CREATE TABLE IF NOT EXISTS git_commits (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    work_item_id    INTEGER NOT NULL,
    repository_id   INTEGER,
    repository_name TEXT NOT NULL DEFAULT '',
    commit_hash     TEXT NOT NULL,
    short_hash      TEXT NOT NULL,
    message         TEXT NOT NULL,
    author_name     TEXT NOT NULL DEFAULT '',
    author_email    TEXT NOT NULL DEFAULT '',
    commit_url      TEXT DEFAULT '',
    committed_at    TEXT NOT NULL DEFAULT (datetime('now')),
    created_at      TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (work_item_id) REFERENCES work_items(id) ON DELETE CASCADE,
    FOREIGN KEY (repository_id) REFERENCES git_repositories(id) ON DELETE SET NULL,
    UNIQUE(work_item_id, commit_hash)
);

CREATE INDEX IF NOT EXISTS idx_git_commits_item ON git_commits(work_item_id);
CREATE INDEX IF NOT EXISTS idx_git_commits_hash ON git_commits(commit_hash);

CREATE TABLE IF NOT EXISTS git_pull_requests (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    work_item_id    INTEGER NOT NULL,
    repository_id   INTEGER,
    repository_name TEXT NOT NULL DEFAULT '',
    pr_number       INTEGER NOT NULL,
    title           TEXT NOT NULL,
    source_branch   TEXT NOT NULL DEFAULT '',
    target_branch   TEXT NOT NULL DEFAULT 'main',
    status          TEXT NOT NULL DEFAULT 'OPEN', -- 'OPEN', 'MERGED', 'DECLINED', 'CLOSED'
    author_name     TEXT NOT NULL DEFAULT '',
    author_email    TEXT NOT NULL DEFAULT '',
    pr_url          TEXT DEFAULT '',
    created_at      TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at      TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (work_item_id) REFERENCES work_items(id) ON DELETE CASCADE,
    FOREIGN KEY (repository_id) REFERENCES git_repositories(id) ON DELETE SET NULL,
    UNIQUE(work_item_id, repository_name, pr_number)
);

CREATE INDEX IF NOT EXISTS idx_git_prs_item ON git_pull_requests(work_item_id);

CREATE TABLE IF NOT EXISTS git_builds (
    id               INTEGER PRIMARY KEY AUTOINCREMENT,
    work_item_id     INTEGER NOT NULL,
    repository_id    INTEGER,
    repository_name  TEXT NOT NULL DEFAULT '',
    pipeline_id      TEXT NOT NULL,
    build_number     TEXT NOT NULL DEFAULT '',
    status           TEXT NOT NULL DEFAULT 'SUCCESS', -- 'SUCCESS', 'FAILED', 'IN_PROGRESS', 'CANCELLED'
    commit_hash      TEXT DEFAULT '',
    branch_name      TEXT DEFAULT '',
    build_url        TEXT DEFAULT '',
    duration_seconds INTEGER DEFAULT 0,
    started_at       TEXT NOT NULL DEFAULT (datetime('now')),
    finished_at      TEXT DEFAULT '',
    created_at       TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (work_item_id) REFERENCES work_items(id) ON DELETE CASCADE,
    FOREIGN KEY (repository_id) REFERENCES git_repositories(id) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS idx_git_builds_item ON git_builds(work_item_id);
