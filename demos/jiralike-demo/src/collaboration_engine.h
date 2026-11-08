#pragma once

#include "crypto_utils.h"
#include "raii_json.h"
#include <algorithm>
#include <cctype>
#include <regex>
#include <string>
#include <vector>

namespace collaboration {

inline void record_changelog(db::sqlite_db &conn,
                             int work_item_id,
                             int changed_by_id,
                             const std::string &field_name,
                             const std::string &old_value,
                             const std::string &new_value) {
    if (old_value == new_value) {
        return;
    }
    auto ins = conn.prepare(R"SQL(
        INSERT INTO work_item_changelog (work_item_id, changed_by_id, field_name, old_value, new_value)
        VALUES (?, ?, ?, ?, ?);
    )SQL");
    ins.bind_int(1, work_item_id);
    if (changed_by_id > 0) {
        ins.bind_int(2, changed_by_id);
    } else {
        ins.bind_null(2);
    }
    ins.bind_text(3, field_name);
    ins.bind_text(4, old_value);
    ins.bind_text(5, new_value);
    ins.step_done();
}

inline std::string extract_snippet_around(const std::string &text, size_t pos, size_t length, size_t context = 60) {
    size_t start = (pos > context) ? (pos - context) : 0;
    size_t end = std::min(text.size(), pos + length + context);
    std::string snippet = text.substr(start, end - start);
    // Replace newlines with spaces for a single-line summary
    for (char &c : snippet) {
        if (c == '\r' || c == '\n') c = ' ';
    }
    if (start > 0) snippet = "..." + snippet;
    if (end < text.size()) snippet = snippet + "...";
    return snippet;
}

inline void process_mentions_and_links(db::sqlite_db &conn,
                                       int work_item_id,
                                       int comment_id,
                                       int author_id,
                                       const std::string &text,
                                       bool is_internal = false) {
    if (text.empty() || work_item_id <= 0) {
        return;
    }

    // 1. Process User Mentions: @username
    // Regex for @([a-zA-Z0-9_.-]+)
    static const std::regex mention_regex(R"((?:^|\s)@([a-zA-Z0-9_.-]+))");
    auto mention_begin = std::sregex_iterator(text.begin(), text.end(), mention_regex);
    auto mention_end = std::sregex_iterator();

    for (std::sregex_iterator i = mention_begin; i != mention_end; ++i) {
        std::smatch match = *i;
        std::string uname = match[1].str();
        if (uname.empty()) continue;

        auto u_stmt = conn.prepare(R"SQL(
            SELECT id, display_name, COALESCE(user_type, 'INTERNAL') FROM users WHERE LOWER(username) = LOWER(?) OR LOWER(display_name) = LOWER(?);
        )SQL");
        u_stmt.bind_text(1, uname);
        u_stmt.bind_text(2, uname);
        if (u_stmt.step() == SQLITE_ROW) {
            int mentioned_id = u_stmt.column_int(0);
            std::string mentioned_type = u_stmt.column_str(2);
            if (is_internal && mentioned_type == "PORTAL_CUSTOMER") {
                continue;
            }
            if (mentioned_id > 0 && mentioned_id != author_id) {
                // Check if mention already recorded for this comment or item
                bool exists = false;
                if (comment_id > 0) {
                    auto chk = conn.prepare("SELECT 1 FROM work_item_mentions WHERE comment_id = ? AND mentioned_user_id = ?;");
                    chk.bind_int(1, comment_id);
                    chk.bind_int(2, mentioned_id);
                    exists = (chk.step() == SQLITE_ROW);
                } else {
                    auto chk = conn.prepare("SELECT 1 FROM work_item_mentions WHERE work_item_id = ? AND comment_id IS NULL AND mentioned_user_id = ?;");
                    chk.bind_int(1, work_item_id);
                    chk.bind_int(2, mentioned_id);
                    exists = (chk.step() == SQLITE_ROW);
                }

                if (!exists) {
                    std::string snippet = extract_snippet_around(text, match.position(0), match.length(0));
                    auto mins = conn.prepare(R"SQL(
                        INSERT INTO work_item_mentions (work_item_id, comment_id, author_id, mentioned_user_id, snippet)
                        VALUES (?, ?, ?, ?, ?);
                    )SQL");
                    mins.bind_int(1, work_item_id);
                    if (comment_id > 0) {
                        mins.bind_int(2, comment_id);
                    } else {
                        mins.bind_null(2);
                    }
                    mins.bind_int(3, author_id > 0 ? author_id : 1);
                    mins.bind_int(4, mentioned_id);
                    mins.bind_text(5, snippet);
                    mins.step_done();

                    // Auto-subscribe mentioned user as a watcher
                    auto wins = conn.prepare("INSERT OR IGNORE INTO work_item_watchers (work_item_id, user_id) VALUES (?, ?);");
                    wins.bind_int(1, work_item_id);
                    wins.bind_int(2, mentioned_id);
                    wins.step_done();
                }
            }
        }
    }

    // 2. Process Issue Key Auto-Links: [A-Z]+-[0-9]+
    static const std::regex key_regex(R"(\b([A-Za-z][A-Za-z0-9]+-[0-9]+)\b)");
    auto key_begin = std::sregex_iterator(text.begin(), text.end(), key_regex);
    auto key_end = std::sregex_iterator();

    for (std::sregex_iterator i = key_begin; i != key_end; ++i) {
        std::smatch match = *i;
        std::string target_key = match[1].str();
        if (target_key.empty()) continue;

        auto k_stmt = conn.prepare("SELECT id FROM work_items WHERE UPPER(key) = UPPER(?);");
        k_stmt.bind_text(1, target_key);
        if (k_stmt.step() == SQLITE_ROW) {
            int target_id = k_stmt.column_int(0);
            if (target_id > 0 && target_id != work_item_id) {
                // Check if link already exists in either direction
                auto chk = conn.prepare(R"SQL(
                    SELECT 1 FROM work_item_links
                    WHERE (source_item_id = ? AND target_item_id = ?)
                       OR (source_item_id = ? AND target_item_id = ?);
                )SQL");
                chk.bind_int(1, work_item_id);
                chk.bind_int(2, target_id);
                chk.bind_int(3, target_id);
                chk.bind_int(4, work_item_id);
                if (chk.step() != SQLITE_ROW) {
                    auto lins = conn.prepare(R"SQL(
                        INSERT INTO work_item_links (source_item_id, target_item_id, link_type)
                        VALUES (?, ?, 'mentions');
                    )SQL");
                    lins.bind_int(1, work_item_id);
                    lins.bind_int(2, target_id);
                    lins.step_done();
                }
            }
        }
    }
}

inline json::json_value get_item_changelog(db::sqlite_db &conn, int work_item_id) {
    auto stmt = conn.prepare(R"SQL(
        SELECT c.id, c.work_item_id, c.changed_by_id, COALESCE(u.display_name, 'System'),
               COALESCE(u.avatar_color, '#6B778C'), c.field_name, COALESCE(c.old_value, ''),
               COALESCE(c.new_value, ''), c.created_at
        FROM work_item_changelog c
        LEFT JOIN users u ON u.id = c.changed_by_id
        WHERE c.work_item_id = ?
        ORDER BY c.id DESC;
    )SQL");
    stmt.bind_int(1, work_item_id);

    auto list = json::json_value::make_array();
    while (stmt.step() == SQLITE_ROW) {
        auto row = json::json_value::make_object();
        row.add_int("id", stmt.column_int(0));
        row.add_int("work_item_id", stmt.column_int(1));
        row.add_nullable_int("changed_by_id", stmt.column_int(2));
        row.add_str("changed_by_name", stmt.column_str(3));
        row.add_str("changed_by_color", stmt.column_str(4));
        row.add_str("field_name", stmt.column_str(5));
        row.add_str("old_value", stmt.column_str(6));
        row.add_str("new_value", stmt.column_str(7));
        row.add_str("created_at", stmt.column_str(8));
        list.add(std::move(row));
    }
    return list;
}

inline json::json_value get_user_mentions(db::sqlite_db &conn, int user_id, bool unread_only = false) {
    std::string sql = R"SQL(
        SELECT m.id, m.work_item_id, w.key, w.title, m.comment_id, m.author_id,
               COALESCE(u.display_name, 'User'), COALESCE(u.avatar_color, '#0052CC'),
               m.snippet, m.is_read, m.created_at
        FROM work_item_mentions m
        INNER JOIN work_items w ON w.id = m.work_item_id
        LEFT JOIN users u ON u.id = m.author_id
        WHERE m.mentioned_user_id = ?
    )SQL";
    if (unread_only) {
        sql += " AND m.is_read = 0 ";
    }
    sql += " ORDER BY m.id DESC LIMIT 50;";

    auto stmt = conn.prepare(sql);
    stmt.bind_int(1, user_id);

    auto list = json::json_value::make_array();
    while (stmt.step() == SQLITE_ROW) {
        auto row = json::json_value::make_object();
        row.add_int("id", stmt.column_int(0));
        row.add_int("work_item_id", stmt.column_int(1));
        row.add_str("item_key", stmt.column_str(2));
        row.add_str("item_title", stmt.column_str(3));
        row.add_nullable_int("comment_id", stmt.column_int(4));
        row.add_int("author_id", stmt.column_int(5));
        row.add_str("author_name", stmt.column_str(6));
        row.add_str("author_color", stmt.column_str(7));
        row.add_str("snippet", stmt.column_str(8));
        row.add_bool("is_read", stmt.column_int(9) == 1);
        row.add_str("created_at", stmt.column_str(10));
        list.add(std::move(row));
    }
    return list;
}

inline json::json_value get_item_watchers(db::sqlite_db &conn, int work_item_id, int current_user_id) {
    auto stmt = conn.prepare(R"SQL(
        SELECT w.user_id, u.username, u.display_name, u.avatar_color, w.created_at
        FROM work_item_watchers w
        INNER JOIN users u ON u.id = w.user_id
        WHERE w.work_item_id = ?
        ORDER BY u.display_name ASC;
    )SQL");
    stmt.bind_int(1, work_item_id);

    bool is_watching = false;
    int watchers_count = 0;
    auto watchers = json::json_value::make_array();
    while (stmt.step() == SQLITE_ROW) {
        int uid = stmt.column_int(0);
        if (uid == current_user_id) {
            is_watching = true;
        }
        auto w = json::json_value::make_object();
        w.add_int("user_id", uid);
        w.add_str("username", stmt.column_str(1));
        w.add_str("display_name", stmt.column_str(2));
        w.add_str("avatar_color", stmt.column_str(3));
        w.add_str("created_at", stmt.column_str(4));
        watchers.add(std::move(w));
        ++watchers_count;
    }

    auto res = json::json_value::make_object();
    res.add_int("watchers_count", watchers_count);
    res.add_bool("is_watching", is_watching);
    res.add("watchers", std::move(watchers));
    return res;
}

inline json::json_value get_item_votes(db::sqlite_db &conn, int work_item_id, int current_user_id) {
    auto stmt = conn.prepare(R"SQL(
        SELECT v.user_id, u.username, u.display_name, v.created_at
        FROM work_item_votes v
        INNER JOIN users u ON u.id = v.user_id
        WHERE v.work_item_id = ?
        ORDER BY v.created_at DESC;
    )SQL");
    stmt.bind_int(1, work_item_id);

    bool has_voted = false;
    int votes_count = 0;
    auto voters = json::json_value::make_array();
    while (stmt.step() == SQLITE_ROW) {
        int uid = stmt.column_int(0);
        if (uid == current_user_id) {
            has_voted = true;
        }
        auto v = json::json_value::make_object();
        v.add_int("user_id", uid);
        v.add_str("username", stmt.column_str(1));
        v.add_str("display_name", stmt.column_str(2));
        v.add_str("created_at", stmt.column_str(3));
        voters.add(std::move(v));
        ++votes_count;
    }

    auto res = json::json_value::make_object();
    res.add_int("votes_count", votes_count);
    res.add_bool("has_voted", has_voted);
    res.add("voters", std::move(voters));
    return res;
}

} // namespace collaboration
