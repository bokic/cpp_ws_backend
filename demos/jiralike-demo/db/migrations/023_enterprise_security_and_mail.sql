-- Migration 023: Enterprise Security (SSO & 2FA/MFA) & Inbound Email Processing

-- 1. Enterprise SSO (SAML 2.0 / OIDC) Configuration Table
CREATE TABLE IF NOT EXISTS sso_configurations (
    id                    INTEGER PRIMARY KEY CHECK (id = 1),
    is_enabled            INTEGER NOT NULL DEFAULT 0,
    protocol              TEXT NOT NULL DEFAULT 'SAML2', -- 'SAML2' or 'OIDC'
    provider_name         TEXT NOT NULL DEFAULT 'Enterprise IdP',
    entity_id             TEXT NOT NULL DEFAULT 'urn:jiralike:sp',
    sso_url               TEXT NOT NULL DEFAULT 'https://idp.example.com/sso/login',
    issuer                TEXT NOT NULL DEFAULT 'https://idp.example.com',
    client_id             TEXT NOT NULL DEFAULT 'jiralike-enterprise-client',
    client_secret         TEXT NOT NULL DEFAULT '',
    certificate           TEXT NOT NULL DEFAULT '',
    email_domain          TEXT NOT NULL DEFAULT 'company.com',
    auto_provision        INTEGER NOT NULL DEFAULT 1, -- JIT provisioning
    default_role          TEXT NOT NULL DEFAULT 'Developer',
    created_at            TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at            TEXT NOT NULL DEFAULT (datetime('now'))
);

INSERT OR IGNORE INTO sso_configurations (id, is_enabled, protocol, provider_name, entity_id, sso_url, issuer, client_id, email_domain, auto_provision, default_role)
VALUES (1, 0, 'SAML2', 'Corporate Okta / Azure AD', 'urn:jiralike:sp', 'https://idp.example.com/sso/login', 'https://idp.example.com', 'jiralike-enterprise-client', 'example.com', 1, 'Developer');

-- 2. Two-Factor Authentication (RFC 6238 TOTP) User Credentials Table
CREATE TABLE IF NOT EXISTS user_2fa (
    user_id         INTEGER PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
    secret          TEXT NOT NULL,
    is_enabled      INTEGER NOT NULL DEFAULT 0,
    recovery_codes  TEXT NOT NULL DEFAULT '[]', -- JSON array of hashed recovery codes
    created_at      TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at      TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE INDEX IF NOT EXISTS idx_user_2fa_status ON user_2fa(user_id, is_enabled);

-- 3. MFA Pending Temporary Challenge Tokens for 2-step Login Flow
CREATE TABLE IF NOT EXISTS mfa_pending_logins (
    token           TEXT PRIMARY KEY,
    user_id         INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    expires_at      TEXT NOT NULL,
    created_at      TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE INDEX IF NOT EXISTS idx_mfa_pending_user ON mfa_pending_logins(user_id);

-- 4. Inbound Email Handlers Table (Routing, Filtering, Fallback Reporter)
CREATE TABLE IF NOT EXISTS inbound_mail_handlers (
    id                    INTEGER PRIMARY KEY AUTOINCREMENT,
    name                  TEXT NOT NULL,
    incoming_address      TEXT NOT NULL UNIQUE,
    target_project_id     INTEGER NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
    issue_type_id         INTEGER REFERENCES issue_types(id) ON DELETE SET NULL,
    default_reporter_id   INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    handler_type          TEXT NOT NULL DEFAULT 'CREATE_OR_COMMENT', -- 'CREATE_OR_COMMENT', 'COMMENT_ONLY', 'CREATE_ONLY'
    spam_keywords         TEXT NOT NULL DEFAULT 'out of office,auto-reply,unsubscribe,mailer-daemon,delivery status notification',
    is_active             INTEGER NOT NULL DEFAULT 1,
    created_at            TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at            TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE INDEX IF NOT EXISTS idx_inbound_mail_addr ON inbound_mail_handlers(incoming_address);

-- 5. Inbound Mail Processing & Audit Log Table
CREATE TABLE IF NOT EXISTS inbound_mail_logs (
    id                    INTEGER PRIMARY KEY AUTOINCREMENT,
    handler_id           INTEGER REFERENCES inbound_mail_handlers(id) ON DELETE SET NULL,
    from_address          TEXT NOT NULL,
    to_address            TEXT NOT NULL,
    subject               TEXT NOT NULL,
    status                TEXT NOT NULL, -- 'PROCESSED', 'REJECTED_SPAM', 'REJECTED_UNKNOWN', 'ERROR'
    work_item_id          INTEGER REFERENCES work_items(id) ON DELETE SET NULL,
    comment_id            INTEGER REFERENCES comments(id) ON DELETE SET NULL,
    error_message         TEXT DEFAULT '',
    created_at            TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE INDEX IF NOT EXISTS idx_inbound_mail_logs_status ON inbound_mail_logs(status);
CREATE INDEX IF NOT EXISTS idx_inbound_mail_logs_created ON inbound_mail_logs(created_at DESC);

-- Seed initial mail handler if projects and users exist
INSERT OR IGNORE INTO inbound_mail_handlers (name, incoming_address, target_project_id, default_reporter_id, handler_type)
SELECT 'Default Project Mailhook', 'support@example.com', id, (SELECT id FROM users ORDER BY id ASC LIMIT 1), 'CREATE_OR_COMMENT'
FROM projects ORDER BY id ASC LIMIT 1;
