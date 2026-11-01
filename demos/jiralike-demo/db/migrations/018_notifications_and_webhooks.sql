-- Migration 018: Notifications, SMTP Delivery & Outbound Webhooks

CREATE TABLE IF NOT EXISTS notification_schemes (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL UNIQUE,
    description TEXT DEFAULT '',
    is_default  INTEGER NOT NULL DEFAULT 0,
    created_at  TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at  TEXT NOT NULL DEFAULT (datetime('now'))
);

ALTER TABLE projects ADD COLUMN notification_scheme_id INTEGER REFERENCES notification_schemes(id);

CREATE TABLE IF NOT EXISTS notification_scheme_entries (
    id                  INTEGER PRIMARY KEY AUTOINCREMENT,
    scheme_id           INTEGER NOT NULL,
    event_type          TEXT NOT NULL,
    recipient_type      TEXT NOT NULL,
    recipient_parameter TEXT DEFAULT '',
    created_at          TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (scheme_id) REFERENCES notification_schemes(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_notif_entries_scheme ON notification_scheme_entries(scheme_id, event_type);

-- Seed Default Notification Scheme
INSERT OR IGNORE INTO notification_schemes (id, name, description, is_default)
VALUES (1, 'Default Notification Scheme', 'Standard Jira notification scheme routing events to assignees, reporters, watchers and project lead', 1);

INSERT OR IGNORE INTO notification_scheme_entries (scheme_id, event_type, recipient_type, recipient_parameter) VALUES
    (1, 'issue_created', 'assignee', ''),
    (1, 'issue_created', 'reporter', ''),
    (1, 'issue_created', 'watchers', ''),
    (1, 'issue_created', 'project_lead', ''),
    (1, 'issue_updated', 'assignee', ''),
    (1, 'issue_updated', 'reporter', ''),
    (1, 'issue_updated', 'watchers', ''),
    (1, 'issue_assigned', 'assignee', ''),
    (1, 'issue_assigned', 'reporter', ''),
    (1, 'issue_assigned', 'watchers', ''),
    (1, 'issue_resolved', 'assignee', ''),
    (1, 'issue_resolved', 'reporter', ''),
    (1, 'issue_resolved', 'watchers', ''),
    (1, 'issue_commented', 'assignee', ''),
    (1, 'issue_commented', 'reporter', ''),
    (1, 'issue_commented', 'watchers', ''),
    (1, 'worklog_added', 'watchers', ''),
    (1, 'generic_event', 'assignee', ''),
    (1, 'generic_event', 'reporter', ''),
    (1, 'generic_event', 'watchers', '');

UPDATE projects SET notification_scheme_id = 1 WHERE notification_scheme_id IS NULL;

-- In-App Notifications
CREATE TABLE IF NOT EXISTS in_app_notifications (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id      INTEGER NOT NULL,
    work_item_id INTEGER,
    event_type   TEXT NOT NULL,
    title        TEXT NOT NULL,
    message      TEXT NOT NULL DEFAULT '',
    link         TEXT NOT NULL DEFAULT '',
    is_read      INTEGER NOT NULL DEFAULT 0,
    created_at   TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,
    FOREIGN KEY (work_item_id) REFERENCES work_items(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_in_app_notif_user ON in_app_notifications(user_id, is_read, id DESC);
CREATE INDEX IF NOT EXISTS idx_in_app_notif_item ON in_app_notifications(work_item_id);

-- Outbound Emails
CREATE TABLE IF NOT EXISTS outbound_emails (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    to_email      TEXT NOT NULL,
    to_name       TEXT DEFAULT '',
    subject       TEXT NOT NULL,
    body_html     TEXT NOT NULL,
    body_text     TEXT NOT NULL,
    headers_json  TEXT DEFAULT '{}',
    status        TEXT NOT NULL DEFAULT 'queued',
    error_message TEXT DEFAULT '',
    sent_at       TEXT,
    created_at    TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE INDEX IF NOT EXISTS idx_outbound_emails_status ON outbound_emails(status, id ASC);

-- SMTP Configuration
CREATE TABLE IF NOT EXISTS smtp_config (
    id          INTEGER PRIMARY KEY CHECK (id = 1),
    host        TEXT NOT NULL DEFAULT 'localhost',
    port        INTEGER NOT NULL DEFAULT 25,
    username    TEXT DEFAULT '',
    password    TEXT DEFAULT '',
    from_email  TEXT NOT NULL DEFAULT 'jira@example.com',
    from_name   TEXT NOT NULL DEFAULT 'JiraLike Notifications',
    tls_mode    TEXT NOT NULL DEFAULT 'none',
    is_enabled  INTEGER NOT NULL DEFAULT 0,
    updated_at  TEXT NOT NULL DEFAULT (datetime('now'))
);

INSERT OR IGNORE INTO smtp_config (id, host, port, username, password, from_email, from_name, tls_mode, is_enabled)
VALUES (1, 'localhost', 25, '', '', 'jira@example.com', 'JiraLike Notifications', 'none', 0);

-- Webhooks
CREATE TABLE IF NOT EXISTS webhooks (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    name          TEXT NOT NULL,
    url           TEXT NOT NULL,
    secret        TEXT DEFAULT '',
    events        TEXT NOT NULL DEFAULT '["issue_created","issue_updated","issue_commented"]',
    is_active     INTEGER NOT NULL DEFAULT 1,
    created_by_id INTEGER,
    created_at    TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at    TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (created_by_id) REFERENCES users(id) ON DELETE SET NULL
);

-- Webhook Deliveries
CREATE TABLE IF NOT EXISTS webhook_deliveries (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    webhook_id      INTEGER NOT NULL,
    event_type      TEXT NOT NULL,
    payload         TEXT NOT NULL,
    response_status INTEGER DEFAULT 0,
    response_body   TEXT DEFAULT '',
    duration_ms     REAL DEFAULT 0.0,
    attempt         INTEGER NOT NULL DEFAULT 1,
    status          TEXT NOT NULL DEFAULT 'pending',
    error_message   TEXT DEFAULT '',
    created_at      TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (webhook_id) REFERENCES webhooks(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_webhook_deliveries_wh ON webhook_deliveries(webhook_id, id DESC);
