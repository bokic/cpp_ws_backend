-- Migration 020: ITSM & Service Desk Capabilities (Customer Portal & SLA Engine)

CREATE TABLE IF NOT EXISTS service_desks (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    project_id     INTEGER NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
    name           TEXT NOT NULL,
    description    TEXT DEFAULT '',
    portal_key     TEXT NOT NULL UNIQUE,
    is_open_access INTEGER NOT NULL DEFAULT 1,
    created_at     TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at     TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS request_types (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    service_desk_id INTEGER NOT NULL REFERENCES service_desks(id) ON DELETE CASCADE,
    issue_type_id   INTEGER REFERENCES issue_types(id) ON DELETE SET NULL,
    name            TEXT NOT NULL,
    description     TEXT DEFAULT '',
    icon            TEXT DEFAULT 'help_outline',
    help_text       TEXT DEFAULT '',
    display_order   INTEGER NOT NULL DEFAULT 0,
    created_at      TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS sla_calendars (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    name          TEXT NOT NULL,
    timezone      TEXT NOT NULL DEFAULT 'UTC',
    days_of_week  TEXT NOT NULL DEFAULT '1,2,3,4,5',
    start_time    TEXT NOT NULL DEFAULT '09:00',
    end_time      TEXT NOT NULL DEFAULT '17:00',
    holidays_json TEXT NOT NULL DEFAULT '[]',
    is_24x7       INTEGER NOT NULL DEFAULT 0,
    created_at    TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS sla_metrics (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    service_desk_id INTEGER NOT NULL REFERENCES service_desks(id) ON DELETE CASCADE,
    name            TEXT NOT NULL,
    metric_key      TEXT NOT NULL,
    description     TEXT DEFAULT '',
    calendar_id     INTEGER REFERENCES sla_calendars(id) ON DELETE SET NULL,
    start_condition TEXT DEFAULT 'issue_created',
    pause_condition TEXT DEFAULT 'waiting_for_customer',
    stop_condition  TEXT DEFAULT 'first_response',
    created_at      TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS sla_goals (
    id                       INTEGER PRIMARY KEY AUTOINCREMENT,
    metric_id                INTEGER NOT NULL REFERENCES sla_metrics(id) ON DELETE CASCADE,
    priority                 TEXT DEFAULT 'All',
    target_duration_seconds  INTEGER NOT NULL DEFAULT 14400,
    warning_duration_seconds INTEGER NOT NULL DEFAULT 3600,
    created_at               TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS work_item_slas (
    id                      INTEGER PRIMARY KEY AUTOINCREMENT,
    work_item_id            INTEGER NOT NULL REFERENCES work_items(id) ON DELETE CASCADE,
    metric_id               INTEGER NOT NULL REFERENCES sla_metrics(id) ON DELETE CASCADE,
    goal_id                 INTEGER REFERENCES sla_goals(id) ON DELETE SET NULL,
    status                  TEXT NOT NULL DEFAULT 'running',
    target_duration_seconds INTEGER NOT NULL DEFAULT 14400,
    elapsed_work_seconds    INTEGER NOT NULL DEFAULT 0,
    remaining_work_seconds  INTEGER NOT NULL DEFAULT 14400,
    is_paused               INTEGER NOT NULL DEFAULT 0,
    is_completed            INTEGER NOT NULL DEFAULT 0,
    is_breached             INTEGER NOT NULL DEFAULT 0,
    started_at              TEXT NOT NULL DEFAULT (datetime('now')),
    paused_at               TEXT DEFAULT NULL,
    completed_at            TEXT DEFAULT NULL,
    breached_at             TEXT DEFAULT NULL,
    last_calculated_at      TEXT NOT NULL DEFAULT (datetime('now')),
    UNIQUE(work_item_id, metric_id)
);

CREATE TABLE IF NOT EXISTS service_desk_queues (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    service_desk_id INTEGER NOT NULL REFERENCES service_desks(id) ON DELETE CASCADE,
    name            TEXT NOT NULL,
    jql_filter      TEXT NOT NULL DEFAULT '',
    filter_type     TEXT NOT NULL DEFAULT 'all',
    display_order   INTEGER NOT NULL DEFAULT 0,
    icon            TEXT DEFAULT 'inbox',
    created_at      TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS canned_responses (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    service_desk_id INTEGER REFERENCES service_desks(id) ON DELETE CASCADE,
    title           TEXT NOT NULL,
    shortcut        TEXT NOT NULL DEFAULT '',
    content         TEXT NOT NULL,
    created_by      INTEGER REFERENCES users(id) ON DELETE SET NULL,
    created_at      TEXT NOT NULL DEFAULT (datetime('now'))
);

-- Extend work_items with ITSM & Customer Portal fields
ALTER TABLE work_items ADD COLUMN service_desk_id INTEGER DEFAULT NULL REFERENCES service_desks(id) ON DELETE SET NULL;
ALTER TABLE work_items ADD COLUMN request_type_id INTEGER DEFAULT NULL REFERENCES request_types(id) ON DELETE SET NULL;
ALTER TABLE work_items ADD COLUMN customer_reporter_name TEXT DEFAULT '';
ALTER TABLE work_items ADD COLUMN customer_reporter_email TEXT DEFAULT '';
ALTER TABLE work_items ADD COLUMN first_response_at TEXT DEFAULT NULL;
ALTER TABLE work_items ADD COLUMN is_customer_request INTEGER NOT NULL DEFAULT 0;

-- Default Seed: SLA Calendars
INSERT OR IGNORE INTO sla_calendars (id, name, timezone, days_of_week, start_time, end_time, holidays_json, is_24x7)
VALUES (1, 'Standard Business Hours (9:00 - 17:00 Mon-Fri)', 'UTC', '1,2,3,4,5', '09:00', '17:00', '[]', 0);

INSERT OR IGNORE INTO sla_calendars (id, name, timezone, days_of_week, start_time, end_time, holidays_json, is_24x7)
VALUES (2, '24x7 Continuous Support', 'UTC', '1,2,3,4,5,6,7', '00:00', '24:00', '[]', 1);

-- Directed Workflow Status & Transitions for Service Desk: 'Waiting for customer'
INSERT OR IGNORE INTO workflow_statuses (workflow_id, name, category, color, pos_x, pos_y, sort_order, is_initial)
VALUES (1, 'Waiting for customer', 'In Progress', '#FFAB00', 495, 360, 80, 0);

INSERT OR IGNORE INTO workflow_statuses (workflow_id, name, category, color, pos_x, pos_y, sort_order, is_initial)
VALUES (2, 'Waiting for customer', 'In Progress', '#FFAB00', 500, 360, 80, 0);

INSERT OR IGNORE INTO workflow_transitions (workflow_id, name, from_status, to_status, screen_id, sort_order)
VALUES (1, 'Wait for Customer Response', '*', 'Waiting for customer', NULL, 85);

INSERT OR IGNORE INTO workflow_transitions (workflow_id, name, from_status, to_status, screen_id, sort_order)
VALUES (2, 'Wait for Customer Response', '*', 'Waiting for customer', NULL, 85);

INSERT OR IGNORE INTO workflow_transitions (workflow_id, name, from_status, to_status, screen_id, sort_order)
VALUES (1, 'Customer Replied', 'Waiting for customer', 'In Progress', NULL, 90);

INSERT OR IGNORE INTO workflow_transitions (workflow_id, name, from_status, to_status, screen_id, sort_order)
VALUES (2, 'Customer Replied', 'Waiting for customer', 'In Progress', NULL, 90);

-- Default Seed: Canned Responses
INSERT OR IGNORE INTO canned_responses (id, service_desk_id, title, shortcut, content)
VALUES (1, NULL, 'Greeting & In-Progress', '/greet', 'Hi {{customer_name}},

Thank you for contacting our support team. We have received your request regarding "{{issue_summary}}" ({{issue_key}}) and an agent is currently investigating.

Best regards,
{{agent_name}}');

INSERT OR IGNORE INTO canned_responses (id, service_desk_id, title, shortcut, content)
VALUES (2, NULL, 'Request More Information', '/info', 'Hi {{customer_name}},

To help us resolve {{issue_key}}, could you please provide additional details or screenshots regarding what occurred?

Thank you for your assistance,
{{agent_name}}');

INSERT OR IGNORE INTO canned_responses (id, service_desk_id, title, shortcut, content)
VALUES (3, NULL, 'Resolution & Closure', '/resolved', 'Hi {{customer_name}},

We have completed work on ticket {{issue_key}} ("{{issue_summary}}"). The fix or resolution has been applied.

Please let us know if you need any further assistance!

Best regards,
{{agent_name}}');
