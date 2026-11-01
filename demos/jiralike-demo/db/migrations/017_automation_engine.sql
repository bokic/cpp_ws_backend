-- Migration 017: No-Code Automation Engine (Automation for Jira Rules Runner)

-- 1. Automation Rules Table
CREATE TABLE IF NOT EXISTS automation_rules (
    id                INTEGER PRIMARY KEY AUTOINCREMENT,
    project_id        INTEGER DEFAULT NULL, -- NULL indicates global rule across all projects
    name              TEXT NOT NULL,
    description       TEXT DEFAULT '',
    is_enabled        INTEGER NOT NULL DEFAULT 1,
    trigger_type      TEXT NOT NULL, -- 'status_changed', 'issue_created', 'field_value_changed', 'comment_added', 'scheduled_cron', 'incoming_webhook', 'manual'
    trigger_config    TEXT NOT NULL DEFAULT '{}', -- JSON configuration for the trigger
    conditions_json   TEXT NOT NULL DEFAULT '[]', -- JSON array of conditions
    actions_json      TEXT NOT NULL DEFAULT '[]', -- JSON array of actions & branches
    created_by        INTEGER DEFAULT NULL,
    created_at        TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at        TEXT NOT NULL DEFAULT (datetime('now')),
    execution_count   INTEGER NOT NULL DEFAULT 0,
    last_executed_at  TEXT DEFAULT '',
    FOREIGN KEY (project_id) REFERENCES projects(id) ON DELETE CASCADE,
    FOREIGN KEY (created_by) REFERENCES users(id) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS idx_automation_rules_project ON automation_rules(project_id);
CREATE INDEX IF NOT EXISTS idx_automation_rules_trigger ON automation_rules(trigger_type, is_enabled);

-- 2. Automation Triggers (Normalized)
CREATE TABLE IF NOT EXISTS automation_triggers (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    rule_id      INTEGER NOT NULL,
    trigger_type TEXT NOT NULL,
    config_json  TEXT NOT NULL DEFAULT '{}',
    created_at   TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (rule_id) REFERENCES automation_rules(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_automation_triggers_rule ON automation_triggers(rule_id);
CREATE INDEX IF NOT EXISTS idx_automation_triggers_type ON automation_triggers(trigger_type);

-- 3. Automation Conditions (Normalized)
CREATE TABLE IF NOT EXISTS automation_conditions (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    rule_id        INTEGER NOT NULL,
    condition_type TEXT NOT NULL, -- 'jql', 'issue_field', 'user_condition', 'has_subtasks'
    config_json    TEXT NOT NULL DEFAULT '{}',
    sort_order     INTEGER NOT NULL DEFAULT 0,
    FOREIGN KEY (rule_id) REFERENCES automation_rules(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_automation_conditions_rule ON automation_conditions(rule_id);

-- 4. Automation Actions (Normalized)
CREATE TABLE IF NOT EXISTS automation_actions (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    rule_id     INTEGER NOT NULL,
    action_type TEXT NOT NULL, -- 'transition_issue', 'edit_field', 'create_subtask', 'create_issue', 'add_comment', 'send_notification', 'http_request', 'branch'
    config_json TEXT NOT NULL DEFAULT '{}',
    sort_order  INTEGER NOT NULL DEFAULT 0,
    FOREIGN KEY (rule_id) REFERENCES automation_rules(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_automation_actions_rule ON automation_actions(rule_id);

-- 5. Automation Audit Logs
CREATE TABLE IF NOT EXISTS automation_logs (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    rule_id      INTEGER DEFAULT NULL,
    rule_name    TEXT NOT NULL,
    trigger_type TEXT NOT NULL,
    issue_id     INTEGER DEFAULT NULL,
    issue_key    TEXT DEFAULT '',
    status       TEXT NOT NULL, -- 'SUCCESS', 'FAILED', 'NO_ACTIONS', 'ERROR'
    duration_ms  REAL NOT NULL DEFAULT 0.0,
    log_trace    TEXT DEFAULT '',
    created_at   TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (rule_id) REFERENCES automation_rules(id) ON DELETE SET NULL,
    FOREIGN KEY (issue_id) REFERENCES work_items(id) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS idx_automation_logs_rule ON automation_logs(rule_id);
CREATE INDEX IF NOT EXISTS idx_automation_logs_issue ON automation_logs(issue_id);
CREATE INDEX IF NOT EXISTS idx_automation_logs_status ON automation_logs(status);
CREATE INDEX IF NOT EXISTS idx_automation_logs_created ON automation_logs(created_at DESC);

-- Seed Default Rules

-- Rule 1: Auto-close subtasks when parent is Done
INSERT OR IGNORE INTO automation_rules (
    id, project_id, name, description, is_enabled, trigger_type, trigger_config,
    conditions_json, actions_json, created_by
) VALUES (
    1,
    NULL,
    'Auto-close Subtasks when Issue is Done',
    'When an issue transitions to Done, automatically transition all child subtasks to Done.',
    1,
    'status_changed',
    '{"to_status": "Done"}',
    '[{"type": "has_subtasks"}]',
    '[{"type": "branch", "target": "subtasks", "actions": [{"type": "transition_issue", "status": "Done"}]}]',
    NULL
);

INSERT OR IGNORE INTO automation_triggers (id, rule_id, trigger_type, config_json)
VALUES (1, 1, 'status_changed', '{"to_status": "Done"}');

INSERT OR IGNORE INTO automation_conditions (id, rule_id, condition_type, config_json, sort_order)
VALUES (1, 1, 'has_subtasks', '{}', 1);

INSERT OR IGNORE INTO automation_actions (id, rule_id, action_type, config_json, sort_order)
VALUES (1, 1, 'branch', '{"target": "subtasks", "actions": [{"type": "transition_issue", "status": "Done"}]}', 1);

-- Rule 2: Critical Bug Alert & Triage
INSERT OR IGNORE INTO automation_rules (
    id, project_id, name, description, is_enabled, trigger_type, trigger_config,
    conditions_json, actions_json, created_by
) VALUES (
    2,
    NULL,
    'Critical Bug Auto-Triage',
    'When a Critical or Highest priority Bug is submitted, add an automated priority triage comment.',
    1,
    'issue_created',
    '{}',
    '[{"type": "jql", "jql": "type = ''Bug'' AND priority IN (''High'', ''Highest'')"}, {"type": "issue_field", "field": "priority", "operator": "in", "value": "High,Highest"}]',
    '[{"type": "add_comment", "body": "⚠️ Critical Bug {{issue.key}} created by {{issue.reporter}}! Priority is {{issue.priority}}. Auto-alerting QA and Dev teams."}]',
    NULL
);

INSERT OR IGNORE INTO automation_triggers (id, rule_id, trigger_type, config_json)
VALUES (2, 2, 'issue_created', '{}');

INSERT OR IGNORE INTO automation_conditions (id, rule_id, condition_type, config_json, sort_order)
VALUES (2, 2, 'jql', '{"jql": "type = ''Bug'' AND priority IN (''High'', ''Highest'')"}', 1);

INSERT OR IGNORE INTO automation_actions (id, rule_id, action_type, config_json, sort_order)
VALUES (2, 2, 'add_comment', '{"body": "⚠️ Critical Bug {{issue.key}} created by {{issue.reporter}}! Priority is {{issue.priority}}. Auto-alerting QA and Dev teams."}', 1);
