-- Migration 016: Granular Permission Schemes, Issue Security Schemes, and User Groups

-- 1. User Groups & Group Members
CREATE TABLE IF NOT EXISTS user_groups (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL UNIQUE,
    description TEXT DEFAULT '',
    created_at  TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS user_group_members (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    group_id   INTEGER NOT NULL,
    user_id    INTEGER NOT NULL,
    added_at   TEXT NOT NULL DEFAULT (datetime('now')),
    UNIQUE(group_id, user_id),
    FOREIGN KEY (group_id) REFERENCES user_groups(id) ON DELETE CASCADE,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_group_members_group ON user_group_members(group_id);
CREATE INDEX IF NOT EXISTS idx_group_members_user ON user_group_members(user_id);

-- 2. Project Role Members (per-project membership)
CREATE TABLE IF NOT EXISTS project_role_members (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    project_id INTEGER NOT NULL,
    role_name  TEXT NOT NULL,
    user_id    INTEGER NOT NULL,
    created_at TEXT NOT NULL DEFAULT (datetime('now')),
    UNIQUE(project_id, role_name, user_id),
    FOREIGN KEY (project_id) REFERENCES projects(id) ON DELETE CASCADE,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_project_role_members_proj ON project_role_members(project_id);
CREATE INDEX IF NOT EXISTS idx_project_role_members_user ON project_role_members(user_id);

-- 3. Permission Schemes & Grants
CREATE TABLE IF NOT EXISTS permission_schemes (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL UNIQUE,
    description TEXT DEFAULT '',
    is_default  INTEGER NOT NULL DEFAULT 0,
    created_at  TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at  TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS permission_scheme_grants (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    scheme_id      INTEGER NOT NULL,
    permission_key TEXT NOT NULL,
    grantee_type   TEXT NOT NULL, -- 'role', 'group', 'dynamic', 'user', 'anyone'
    grantee_value  TEXT NOT NULL, -- role name, group name, dynamic identifier ('reporter', 'assignee', 'lead'), or user ID
    created_at     TEXT NOT NULL DEFAULT (datetime('now')),
    UNIQUE(scheme_id, permission_key, grantee_type, grantee_value),
    FOREIGN KEY (scheme_id) REFERENCES permission_schemes(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_perm_grants_scheme ON permission_scheme_grants(scheme_id, permission_key);

-- 4. Issue Security Schemes, Levels & Grants
CREATE TABLE IF NOT EXISTS issue_security_schemes (
    id               INTEGER PRIMARY KEY AUTOINCREMENT,
    name             TEXT NOT NULL UNIQUE,
    description      TEXT DEFAULT '',
    default_level_id INTEGER,
    created_at       TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at       TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS issue_security_levels (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    scheme_id   INTEGER NOT NULL,
    name        TEXT NOT NULL,
    description TEXT DEFAULT '',
    is_default  INTEGER NOT NULL DEFAULT 0,
    created_at  TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (scheme_id) REFERENCES issue_security_schemes(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_security_levels_scheme ON issue_security_levels(scheme_id);

CREATE TABLE IF NOT EXISTS issue_security_level_grants (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    level_id      INTEGER NOT NULL,
    grantee_type  TEXT NOT NULL, -- 'role', 'group', 'dynamic', 'user', 'anyone'
    grantee_value TEXT NOT NULL, -- role name, group name, dynamic identifier ('reporter', 'assignee', 'lead'), or user ID
    created_at    TEXT NOT NULL DEFAULT (datetime('now')),
    UNIQUE(level_id, grantee_type, grantee_value),
    FOREIGN KEY (level_id) REFERENCES issue_security_levels(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_sec_level_grants ON issue_security_level_grants(level_id);

-- 5. Personal Access Tokens (PAT)
CREATE TABLE IF NOT EXISTS personal_access_tokens (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id     INTEGER NOT NULL,
    token       TEXT NOT NULL UNIQUE,
    name        TEXT NOT NULL,
    scopes      TEXT NOT NULL DEFAULT 'read,write',
    expires_at  TEXT,
    created_at  TEXT NOT NULL DEFAULT (datetime('now')),
    last_used_at TEXT,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_pat_token ON personal_access_tokens(token);
CREATE INDEX IF NOT EXISTS idx_pat_user ON personal_access_tokens(user_id);

-- 6. Add Scheme & Security references to projects and work_items
ALTER TABLE projects ADD COLUMN permission_scheme_id INTEGER REFERENCES permission_schemes(id);
ALTER TABLE projects ADD COLUMN security_scheme_id INTEGER REFERENCES issue_security_schemes(id);
ALTER TABLE work_items ADD COLUMN security_level_id INTEGER REFERENCES issue_security_levels(id);

-- 7. Seed Default Groups
INSERT OR IGNORE INTO user_groups (id, name, description)
VALUES
    (1, 'jira-administrators', 'System administrators with unrestricted workspace access'),
    (2, 'jira-software-users', 'Standard Jira software developers, testers, and product managers');

-- Add user 1 (Admin) to groups if user 1 exists
INSERT OR IGNORE INTO user_group_members (group_id, user_id)
SELECT 1, 1 WHERE EXISTS (SELECT 1 FROM users WHERE id = 1);

INSERT OR IGNORE INTO user_group_members (group_id, user_id)
SELECT 2, 1 WHERE EXISTS (SELECT 1 FROM users WHERE id = 1);

-- 8. Seed Default Permission Scheme
INSERT OR IGNORE INTO permission_schemes (id, name, description, is_default)
VALUES (1, 'Default Permission Scheme', 'Default enterprise permission scheme for software projects', 1);

-- Seed Grants for Default Permission Scheme
INSERT OR IGNORE INTO permission_scheme_grants (scheme_id, permission_key, grantee_type, grantee_value)
VALUES
    -- Project Admin & Browse
    (1, 'ADMINISTER_PROJECTS', 'role', 'Administrators'),
    (1, 'ADMINISTER_PROJECTS', 'group', 'jira-administrators'),
    (1, 'BROWSE_PROJECT', 'role', 'Administrators'),
    (1, 'BROWSE_PROJECT', 'role', 'Developers'),
    (1, 'BROWSE_PROJECT', 'role', 'QA'),
    (1, 'BROWSE_PROJECT', 'role', 'Product Managers'),
    (1, 'BROWSE_PROJECT', 'role', 'Viewers'),
    (1, 'BROWSE_PROJECT', 'group', 'jira-software-users'),
    (1, 'BROWSE_PROJECT', 'group', 'jira-administrators'),

    -- Issues
    (1, 'CREATE_ISSUE', 'role', 'Administrators'),
    (1, 'CREATE_ISSUE', 'role', 'Developers'),
    (1, 'CREATE_ISSUE', 'role', 'QA'),
    (1, 'CREATE_ISSUE', 'role', 'Product Managers'),
    (1, 'CREATE_ISSUE', 'group', 'jira-software-users'),
    (1, 'CREATE_ISSUE', 'group', 'jira-administrators'),

    (1, 'EDIT_ISSUE', 'role', 'Administrators'),
    (1, 'EDIT_ISSUE', 'role', 'Developers'),
    (1, 'EDIT_ISSUE', 'role', 'Product Managers'),
    (1, 'EDIT_ISSUE', 'dynamic', 'assignee'),
    (1, 'EDIT_ISSUE', 'dynamic', 'reporter'),

    (1, 'ASSIGN_ISSUE', 'role', 'Administrators'),
    (1, 'ASSIGN_ISSUE', 'role', 'Developers'),
    (1, 'ASSIGN_ISSUE', 'role', 'Product Managers'),

    (1, 'RESOLVE_ISSUE', 'role', 'Administrators'),
    (1, 'RESOLVE_ISSUE', 'role', 'Developers'),
    (1, 'RESOLVE_ISSUE', 'role', 'QA'),
    (1, 'RESOLVE_ISSUE', 'role', 'Product Managers'),

    (1, 'CLOSE_ISSUE', 'role', 'Administrators'),
    (1, 'CLOSE_ISSUE', 'role', 'QA'),
    (1, 'CLOSE_ISSUE', 'role', 'Product Managers'),

    (1, 'DELETE_ISSUE', 'role', 'Administrators'),

    (1, 'MOVE_ISSUE', 'role', 'Administrators'),
    (1, 'MOVE_ISSUE', 'role', 'Product Managers'),

    (1, 'LINK_ISSUE', 'role', 'Administrators'),
    (1, 'LINK_ISSUE', 'role', 'Developers'),
    (1, 'LINK_ISSUE', 'role', 'QA'),
    (1, 'LINK_ISSUE', 'role', 'Product Managers'),

    -- Comments
    (1, 'ADD_COMMENTS', 'role', 'Administrators'),
    (1, 'ADD_COMMENTS', 'role', 'Developers'),
    (1, 'ADD_COMMENTS', 'role', 'QA'),
    (1, 'ADD_COMMENTS', 'role', 'Product Managers'),
    (1, 'ADD_COMMENTS', 'role', 'Viewers'),
    (1, 'ADD_COMMENTS', 'group', 'jira-software-users'),

    (1, 'EDIT_OWN_COMMENTS', 'role', 'Administrators'),
    (1, 'EDIT_OWN_COMMENTS', 'role', 'Developers'),
    (1, 'EDIT_OWN_COMMENTS', 'role', 'QA'),
    (1, 'EDIT_OWN_COMMENTS', 'role', 'Product Managers'),
    (1, 'EDIT_OWN_COMMENTS', 'group', 'jira-software-users'),

    (1, 'EDIT_ALL_COMMENTS', 'role', 'Administrators'),
    (1, 'DELETE_OWN_COMMENTS', 'role', 'Administrators'),
    (1, 'DELETE_OWN_COMMENTS', 'role', 'Developers'),
    (1, 'DELETE_ALL_COMMENTS', 'role', 'Administrators'),

    -- Attachments
    (1, 'CREATE_ATTACHMENTS', 'role', 'Administrators'),
    (1, 'CREATE_ATTACHMENTS', 'role', 'Developers'),
    (1, 'CREATE_ATTACHMENTS', 'role', 'QA'),
    (1, 'CREATE_ATTACHMENTS', 'role', 'Product Managers'),
    (1, 'CREATE_ATTACHMENTS', 'group', 'jira-software-users'),

    (1, 'DELETE_OWN_ATTACHMENTS', 'role', 'Administrators'),
    (1, 'DELETE_OWN_ATTACHMENTS', 'role', 'Developers'),
    (1, 'DELETE_ALL_ATTACHMENTS', 'role', 'Administrators'),

    -- Time Tracking & Worklogs
    (1, 'WORK_ON_ISSUES', 'role', 'Administrators'),
    (1, 'WORK_ON_ISSUES', 'role', 'Developers'),
    (1, 'WORK_ON_ISSUES', 'role', 'QA'),
    (1, 'WORK_ON_ISSUES', 'role', 'Product Managers'),
    (1, 'WORK_ON_ISSUES', 'dynamic', 'assignee'),

    (1, 'EDIT_OWN_WORKLOGS', 'role', 'Administrators'),
    (1, 'EDIT_OWN_WORKLOGS', 'role', 'Developers'),
    (1, 'EDIT_OWN_WORKLOGS', 'role', 'QA'),
    (1, 'EDIT_OWN_WORKLOGS', 'role', 'Product Managers'),

    (1, 'EDIT_ALL_WORKLOGS', 'role', 'Administrators'),
    (1, 'DELETE_WORKLOGS', 'role', 'Administrators'),

    -- Agile Sprints
    (1, 'MANAGE_SPRINTS', 'role', 'Administrators'),
    (1, 'MANAGE_SPRINTS', 'role', 'Product Managers');

-- 9. Seed Default Issue Security Scheme & Levels
INSERT OR IGNORE INTO issue_security_schemes (id, name, description, default_level_id)
VALUES (1, 'Default Security Scheme', 'Standard security scheme providing public, internal, and confidential tiers', 1);

INSERT OR IGNORE INTO issue_security_levels (id, scheme_id, name, description, is_default)
VALUES
    (1, 1, 'Public', 'Visible to all project members and software users', 1),
    (2, 1, 'Internal Only', 'Visible only to project developers, managers, QA, reporter and assignee', 0),
    (3, 1, 'Confidential / Security Vulnerability', 'Visible exclusively to administrators, project lead, reporter, and assignee', 0);

UPDATE issue_security_schemes SET default_level_id = 1 WHERE id = 1;

-- Security Level Grants:
-- Level 1: Public
INSERT OR IGNORE INTO issue_security_level_grants (level_id, grantee_type, grantee_value)
VALUES
    (1, 'role', 'Administrators'),
    (1, 'role', 'Developers'),
    (1, 'role', 'QA'),
    (1, 'role', 'Product Managers'),
    (1, 'role', 'Viewers'),
    (1, 'group', 'jira-software-users');

-- Level 2: Internal Only
INSERT OR IGNORE INTO issue_security_level_grants (level_id, grantee_type, grantee_value)
VALUES
    (2, 'role', 'Administrators'),
    (2, 'role', 'Developers'),
    (2, 'role', 'QA'),
    (2, 'role', 'Product Managers'),
    (2, 'dynamic', 'reporter'),
    (2, 'dynamic', 'assignee');

-- Level 3: Confidential / Security Vulnerability
INSERT OR IGNORE INTO issue_security_level_grants (level_id, grantee_type, grantee_value)
VALUES
    (3, 'role', 'Administrators'),
    (3, 'dynamic', 'lead'),
    (3, 'dynamic', 'reporter'),
    (3, 'dynamic', 'assignee');

-- 10. Update existing projects to link to default schemes
UPDATE projects
SET permission_scheme_id = 1
WHERE permission_scheme_id IS NULL;

UPDATE projects
SET security_scheme_id = 1
WHERE security_scheme_id IS NULL;
