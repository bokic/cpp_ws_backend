-- Migration 024: User Preferences & Localization
CREATE TABLE IF NOT EXISTS user_preferences (
    user_id     INTEGER PRIMARY KEY,
    language    TEXT NOT NULL DEFAULT 'en',
    timezone    TEXT NOT NULL DEFAULT 'UTC',
    date_format TEXT NOT NULL DEFAULT 'YYYY-MM-DD',
    time_format TEXT NOT NULL DEFAULT '24h',
    created_at  TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at  TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);

-- Pre-seed user preferences for all existing users
INSERT OR IGNORE INTO user_preferences (user_id, language, timezone, date_format, time_format)
SELECT id, 'en', 'UTC', 'YYYY-MM-DD', '24h' FROM users;
