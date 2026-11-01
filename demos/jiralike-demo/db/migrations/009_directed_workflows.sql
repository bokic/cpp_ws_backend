-- Migration v9: Directed Graph Workflow Engine, Statuses, Transitions, Conditions/Validators/Post-Functions & Workflow Schemes

CREATE TABLE IF NOT EXISTS workflows (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL UNIQUE,
    description TEXT NOT NULL DEFAULT '',
    is_default  INTEGER NOT NULL DEFAULT 0,
    is_system   INTEGER NOT NULL DEFAULT 0,
    created_at  TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS workflow_statuses (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    workflow_id INTEGER NOT NULL,
    name        TEXT NOT NULL,
    category    TEXT NOT NULL DEFAULT 'To Do',
    color       TEXT NOT NULL DEFAULT '#0052CC',
    pos_x       INTEGER NOT NULL DEFAULT 100,
    pos_y       INTEGER NOT NULL DEFAULT 140,
    sort_order  INTEGER NOT NULL DEFAULT 0,
    is_initial  INTEGER NOT NULL DEFAULT 0,
    FOREIGN KEY (workflow_id) REFERENCES workflows(id) ON DELETE CASCADE,
    UNIQUE(workflow_id, name)
);

CREATE INDEX IF NOT EXISTS idx_wf_statuses_workflow ON workflow_statuses(workflow_id);

CREATE TABLE IF NOT EXISTS workflow_transitions (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    workflow_id INTEGER NOT NULL,
    name        TEXT NOT NULL,
    from_status TEXT NOT NULL DEFAULT '*',
    to_status   TEXT NOT NULL,
    screen_id   INTEGER DEFAULT NULL,
    sort_order  INTEGER NOT NULL DEFAULT 0,
    created_at  TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (workflow_id) REFERENCES workflows(id) ON DELETE CASCADE,
    FOREIGN KEY (screen_id) REFERENCES screens(id) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS idx_wf_transitions_workflow ON workflow_transitions(workflow_id);

CREATE TABLE IF NOT EXISTS workflow_transition_rules (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    transition_id INTEGER NOT NULL,
    rule_category TEXT NOT NULL,
    rule_type     TEXT NOT NULL,
    field_name    TEXT NOT NULL DEFAULT '',
    config_value  TEXT NOT NULL DEFAULT '',
    description   TEXT NOT NULL DEFAULT '',
    sort_order    INTEGER NOT NULL DEFAULT 0,
    FOREIGN KEY (transition_id) REFERENCES workflow_transitions(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_wf_rules_transition ON workflow_transition_rules(transition_id);

CREATE TABLE IF NOT EXISTS workflow_schemes (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL,
    description TEXT NOT NULL DEFAULT '',
    project_id  INTEGER DEFAULT NULL,
    issue_type  TEXT NOT NULL DEFAULT '*',
    workflow_id INTEGER NOT NULL,
    is_default  INTEGER NOT NULL DEFAULT 0,
    created_at  TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (project_id) REFERENCES projects(id) ON DELETE CASCADE,
    FOREIGN KEY (workflow_id) REFERENCES workflows(id) ON DELETE CASCADE,
    UNIQUE(project_id, issue_type)
);

-- Expand workflow_history audit trail
ALTER TABLE workflow_history ADD COLUMN transition_id INTEGER DEFAULT NULL;
ALTER TABLE workflow_history ADD COLUMN transition_name TEXT NOT NULL DEFAULT '';
ALTER TABLE workflow_history ADD COLUMN executed_post_functions TEXT NOT NULL DEFAULT '';

-- Seed Workflow 1: Default Agile 7-State Workflow
INSERT OR IGNORE INTO workflows (id, name, description, is_default, is_system) VALUES
    (1, 'Default Agile 7-State Workflow', 'Standard Jira Software directed workflow with Backlog, Sprint Execution, Code Review, QA Verification, Resolution Screen, and Subtask Guard.', 1, 1),
    (2, 'Strict Bug & QA Verification Workflow', 'Enforces strict step-by-step Bug triage: Assignee-only execution, mandatory reproduction steps, QA Engineer verification gate, and resolution post-functions.', 0, 1);

-- Statuses for Workflow 1 (Default Agile 7-State Workflow)
INSERT OR IGNORE INTO workflow_statuses (workflow_id, name, category, color, pos_x, pos_y, sort_order, is_initial) VALUES
    (1, 'Backlog',      'To Do',       '#42526E',  70, 130, 10, 1),
    (1, 'To Do',        'To Do',       '#0052CC', 240, 130, 20, 0),
    (1, 'In Progress',  'In Progress', '#FF991F', 410, 130, 30, 0),
    (1, 'In Review',    'In Progress', '#6554C0', 580, 130, 40, 0),
    (1, 'QA / Testing', 'In Progress', '#00B8D9', 750, 130, 50, 0),
    (1, 'Done',         'Done',        '#00875A', 920, 130, 60, 0),
    (1, 'Blocked',      'In Progress', '#DE350B', 495, 290, 70, 0);

-- Directed Transitions for Workflow 1
INSERT OR IGNORE INTO workflow_transitions (id, workflow_id, name, from_status, to_status, screen_id, sort_order) VALUES
    (1,  1, 'Schedule to To Do',   'Backlog',      'To Do',        NULL, 10),
    (2,  1, 'Send to Backlog',     'To Do',        'Backlog',      NULL, 15),
    (3,  1, 'Start Progress',      '*',            'In Progress',  NULL, 20),
    (4,  1, 'Stop Progress',       'In Progress',  'To Do',        NULL, 25),
    (5,  1, 'Submit for Review',   'In Progress',  'In Review',    NULL, 30),
    (6,  1, 'Request Changes',     'In Review',    'In Progress',  NULL, 35),
    (7,  1, 'Promote to QA',       'In Review',    'QA / Testing', NULL, 40),
    (8,  1, 'QA Failed',           'QA / Testing', 'In Progress',  NULL, 45),
    (9,  1, 'Resolve & Close',     '*',            'Done',         3,    50),
    (10, 1, 'Reopen Issue',        'Done',         'To Do',        NULL, 55),
    (11, 1, 'Flag Blocked',        '*',            'Blocked',      NULL, 60),
    (12, 1, 'Unblock to Progress', 'Blocked',      'In Progress',  NULL, 65),
    (13, 1, 'Move to To Do',       '*',            'To Do',        NULL, 70),
    (14, 1, 'Move to In Review',   '*',            'In Review',    NULL, 75),
    (15, 1, 'Move to QA / Testing','*',            'QA / Testing', NULL, 80);

-- Rules for Workflow 1 Transitions
-- Transition 3: Start Progress -> Set progress=40
INSERT OR IGNORE INTO workflow_transition_rules (id, transition_id, rule_category, rule_type, field_name, config_value, description, sort_order) VALUES
    (1, 3, 'post_function', 'set_field_value', 'progress', '40', 'Automatically advance progress to 40% when work starts', 10),
    (2, 5, 'post_function', 'set_field_value', 'progress', '80', 'Automatically advance progress to 80% on code review submission', 10),
    (3, 7, 'post_function', 'set_field_value', 'progress', '90', 'Automatically advance progress to 90% when promoted to QA', 10),
    (4, 8, 'validator',     'comment_required', 'comment', 'true', 'Require QA failure explanation comment when returning to In Progress', 10);

-- Transition 9: Resolve & Close (* -> Done, Screen 3)
INSERT OR IGNORE INTO workflow_transition_rules (id, transition_id, rule_category, rule_type, field_name, config_value, description, sort_order) VALUES
    (5, 9, 'condition',     'subtasks_completed', '',              'Done',           'Prevent closing parent work item until all child subtasks are Done', 10),
    (6, 9, 'validator',     'field_required',     'resolution_id', 'required',       'Require Resolution outcome (e.g., Fixed, Won''t Fix) when closing issue', 20),
    (7, 9, 'post_function', 'set_field_value',    'progress',      '100',            'Set work item completion progress to 100%', 30),
    (8, 9, 'post_function', 'update_parent_status', 'status',      'Done',           'Auto-transition parent Story/Task to Done when all sibling subtasks reach Done', 40),
    (9, 9, 'post_function', 'dispatch_event',     'event',         'issue_resolved', 'Emit issue_resolved workflow event to audit log', 50);

-- Transition 10: Reopen Issue (Done -> To Do)
INSERT OR IGNORE INTO workflow_transition_rules (id, transition_id, rule_category, rule_type, field_name, config_value, description, sort_order) VALUES
    (10, 10, 'validator',     'comment_required',  'comment',    'true',           'Require reason comment when reopening a closed issue', 10),
    (11, 10, 'post_function', 'clear_field_value', 'resolution', '',               'Automatically clear Resolution and Resolution Date when reopening issue', 20),
    (12, 10, 'post_function', 'set_field_value',   'progress',   '0',              'Reset progress to 0% upon reopening', 30),
    (13, 10, 'post_function', 'dispatch_event',    'event',      'issue_reopened', 'Emit issue_reopened workflow event to audit log', 40);

-- Transition 11: Flag Blocked (* -> Blocked)
INSERT OR IGNORE INTO workflow_transition_rules (id, transition_id, rule_category, rule_type, field_name, config_value, description, sort_order) VALUES
    (14, 11, 'validator',     'comment_required', 'comment', 'true',          'Require explanation comment describing the blocker', 10),
    (15, 11, 'post_function', 'dispatch_event',   'event',   'issue_blocked', 'Emit issue_blocked event to audit log', 20);

-- Statuses for Workflow 2 (Strict Bug & QA Verification Workflow)
INSERT OR IGNORE INTO workflow_statuses (workflow_id, name, category, color, pos_x, pos_y, sort_order, is_initial) VALUES
    (2, 'To Do',        'To Do',       '#0052CC', 100, 140, 10, 1),
    (2, 'In Progress',  'In Progress', '#FF991F', 300, 140, 20, 0),
    (2, 'In Review',    'In Progress', '#6554C0', 500, 140, 30, 0),
    (2, 'QA / Testing', 'In Progress', '#00B8D9', 700, 140, 40, 0),
    (2, 'Done',         'Done',        '#00875A', 900, 140, 50, 0),
    (2, 'Blocked',      'In Progress', '#DE350B', 500, 290, 60, 0);

-- Directed Transitions for Workflow 2
INSERT OR IGNORE INTO workflow_transitions (id, workflow_id, name, from_status, to_status, screen_id, sort_order) VALUES
    (20, 2, 'Start Bug Fix',          'To Do',        'In Progress',  NULL, 10),
    (21, 2, 'Submit Fix for Review',  'In Progress',  'In Review',    NULL, 20),
    (22, 2, 'Send to QA Verification','In Review',    'QA / Testing', NULL, 30),
    (23, 2, 'QA Verify & Close',      'QA / Testing', 'Done',         3,    40),
    (24, 2, 'QA Reject Fix',          'QA / Testing', 'In Progress',  NULL, 50),
    (25, 2, 'Reopen Bug',             'Done',         'To Do',        NULL, 60),
    (26, 2, 'Flag Bug Blocked',       '*',            'Blocked',      NULL, 70),
    (27, 2, 'Unblock Bug',            'Blocked',      'In Progress',  NULL, 80);

-- Rules for Workflow 2 Transitions
INSERT OR IGNORE INTO workflow_transition_rules (id, transition_id, rule_category, rule_type, field_name, config_value, description, sort_order) VALUES
    (20, 20, 'condition',     'only_assignee',       '',                   'true',                              'Only the assigned developer (or Admin) can start fixing the bug', 10),
    (21, 20, 'validator',     'field_required',      'steps_to_reproduce', 'required',                          'Steps to Reproduce must be documented before starting bug fix', 20),
    (22, 20, 'post_function', 'set_field_value',     'progress',           '35',                                'Set progress to 35% on bug fix start', 30),
    (23, 21, 'validator',     'comment_required',    'comment',            'true',                              'Require root-cause & PR summary comment when submitting for review', 10),
    (24, 21, 'post_function', 'set_field_value',     'progress',           '75',                                'Set progress to 75% on review submission', 20),
    (25, 22, 'post_function', 'assign_to_user',      'assignee_id',        'role:QA Engineer',                  'Automatically reassign bug to QA Engineer upon entering QA / Testing', 10),
    (26, 22, 'post_function', 'set_field_value',     'progress',           '90',                                'Set progress to 90% on QA handoff', 20),
    (27, 23, 'condition',     'user_in_role',        'role',               'QA Engineer,Admin,Project Manager', 'Only QA Engineers, Project Managers, or Admins can verify & close bugs', 10),
    (28, 23, 'condition',     'subtasks_completed',  '',                   'Done',                              'All subtasks must be Done before closing bug', 20),
    (29, 23, 'validator',     'field_required',      'resolution_id',      'required',                          'Resolution is mandatory when closing a verified bug', 30),
    (30, 23, 'validator',     'comment_required',    'comment',            'true',                              'QA verification sign-off comment is required', 40),
    (31, 23, 'post_function', 'set_field_value',     'progress',           '100',                               'Set progress to 100% upon QA verification', 50),
    (32, 24, 'validator',     'comment_required',    'comment',            'true',                              'Require QA rejection details', 10),
    (33, 24, 'post_function', 'assign_to_user',      'assignee_id',        'reporter',                          'Reassign rejected bug back to Reporter', 20),
    (34, 25, 'validator',     'comment_required',    'comment',            'true',                              'Require reopen justification comment', 10),
    (35, 25, 'post_function', 'clear_field_value',   'resolution',         '',                                  'Clear resolution on bug reopen', 20);

-- Default Workflow Scheme Bindings
INSERT OR IGNORE INTO workflow_schemes (id, name, description, project_id, issue_type, workflow_id, is_default) VALUES
    (1, 'Default Agile Workflow Scheme', 'Maps all standard project issue types (*) to the Default Agile 7-State Workflow', NULL, '*', 1, 1);
