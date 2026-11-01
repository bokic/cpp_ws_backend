#pragma once

#include "crypto_utils.h"
#include "raii_json.h"
#include "raii_sqlite.h"
#include "time_tracking.h"
#include "collaboration_engine.h"
#include "automation_engine.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace vcs {

enum class CommandType {
    Comment,
    Time,
    Transition
};

struct SmartCommitCommand {
    CommandType type;
    std::string raw_command;
    std::string comment_body;
    int64_t time_seconds = 0;
    std::string time_comment;
    std::string target_status;
};

struct SmartCommitExecutionResult {
    std::vector<std::string> matched_keys;
    std::vector<SmartCommitCommand> commands;
    std::vector<std::string> applied_actions;
    std::vector<std::string> errors;
    int user_id = 1;
    std::string user_name;
};

// Extracts Jira issue keys like PROJ-101, DEMO-12, TEST-456
inline std::vector<std::string> extract_issue_keys(const std::string &text) {
    std::vector<std::string> keys;
    if (text.empty()) return keys;

    static const std::regex key_regex(R"(\b([A-Za-z][A-Za-z0-9]+-[0-9]+)\b)");
    auto words_begin = std::sregex_iterator(text.begin(), text.end(), key_regex);
    auto words_end = std::sregex_iterator();

    for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
        std::smatch match = *i;
        std::string key = match.str(1);
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });
        if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
            keys.push_back(key);
        }
    }
    return keys;
}

// Parses smart commit commands (#comment, #time, #<transition>)
inline std::vector<SmartCommitCommand> parse_smart_commands(const std::string &message) {
    std::vector<SmartCommitCommand> commands;
    if (message.empty()) return commands;

    // Split text into tokens by '#' while preserving delimiters
    size_t pos = 0;
    while ((pos = message.find('#', pos)) != std::string::npos) {
        size_t cmd_start = pos + 1;
        if (cmd_start >= message.size() || std::isspace(static_cast<unsigned char>(message[cmd_start]))) {
            pos++;
            continue;
        }

        // Find end of command name
        size_t cmd_name_end = cmd_start;
        while (cmd_name_end < message.size() &&
               (std::isalnum(static_cast<unsigned char>(message[cmd_name_end])) ||
                message[cmd_name_end] == '-' || message[cmd_name_end] == '_')) {
            cmd_name_end++;
        }

        std::string cmd_name = message.substr(cmd_start, cmd_name_end - cmd_start);
        std::string lower_cmd = cmd_name;
        std::transform(lower_cmd.begin(), lower_cmd.end(), lower_cmd.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

        // Find boundary of arguments (next '#' or end of text)
        size_t next_hash = message.find('#', cmd_name_end);
        std::string raw_args;
        if (next_hash == std::string::npos) {
            raw_args = message.substr(cmd_name_end);
            pos = message.size();
        } else {
            raw_args = message.substr(cmd_name_end, next_hash - cmd_name_end);
            pos = next_hash;
        }

        // Trim leading and trailing whitespace from raw_args
        size_t a_start = 0;
        while (a_start < raw_args.size() && std::isspace(static_cast<unsigned char>(raw_args[a_start]))) {
            a_start++;
        }
        size_t a_end = raw_args.size();
        while (a_end > a_start && std::isspace(static_cast<unsigned char>(raw_args[a_end - 1]))) {
            a_end--;
        }
        std::string args = (a_start < a_end) ? raw_args.substr(a_start, a_end - a_start) : "";

        if (lower_cmd == "comment") {
            if (!args.empty()) {
                SmartCommitCommand cmd;
                cmd.type = CommandType::Comment;
                cmd.raw_command = "#" + cmd_name + " " + args;
                cmd.comment_body = args;
                commands.push_back(cmd);
            }
        } else if (lower_cmd == "time") {
            if (!args.empty()) {
                // Parse duration part and optional worklog comment
                // e.g. "1h 30m worked on bug fix" or "45m" or "2d 4h"
                std::istringstream iss(args);
                std::string token;
                std::string duration_str;
                std::string time_comment;
                bool reading_duration = true;

                while (iss >> token) {
                    if (reading_duration) {
                        // Check if token looks like duration (e.g. "1h", "30m", "2d", "1.5h", "90s", "1w")
                        bool has_digit = false;
                        bool has_unit = false;
                        for (char c : token) {
                            if (std::isdigit(static_cast<unsigned char>(c))) has_digit = true;
                            if (c == 'w' || c == 'W' || c == 'd' || c == 'D' ||
                                c == 'h' || c == 'H' || c == 'm' || c == 'M' ||
                                c == 's' || c == 'S') has_unit = true;
                        }
                        if (has_digit) {
                            if (!duration_str.empty()) duration_str += " ";
                            duration_str += token;
                        } else {
                            reading_duration = false;
                            time_comment += token;
                        }
                    } else {
                        if (!time_comment.empty()) time_comment += " ";
                        time_comment += token;
                    }
                }

                int64_t dur_seconds = 0;
                std::string parse_err;
                if (time_tracking::parse_duration_to_seconds(duration_str, dur_seconds, parse_err) && dur_seconds > 0) {
                    SmartCommitCommand cmd;
                    cmd.type = CommandType::Time;
                    cmd.raw_command = "#" + cmd_name + " " + args;
                    cmd.time_seconds = dur_seconds;
                    cmd.time_comment = time_comment;
                    commands.push_back(cmd);
                }
            }
        } else {
            // Transition command (#resolve, #close, #done, #in-progress, #reopen, #start, etc.)
            SmartCommitCommand cmd;
            cmd.type = CommandType::Transition;
            cmd.raw_command = "#" + cmd_name;

            std::string normalized_status;
            if (lower_cmd == "resolve" || lower_cmd == "resolved" || lower_cmd == "done" || lower_cmd == "finish") {
                normalized_status = "Done";
            } else if (lower_cmd == "close" || lower_cmd == "closed") {
                normalized_status = "Closed";
            } else if (lower_cmd == "in-progress" || lower_cmd == "inprogress" || lower_cmd == "start") {
                normalized_status = "In Progress";
            } else if (lower_cmd == "reopen" || lower_cmd == "reopened") {
                normalized_status = "Reopened";
            } else if (lower_cmd == "to-do" || lower_cmd == "todo" || lower_cmd == "stop") {
                normalized_status = "To Do";
            } else if (lower_cmd == "transition" && !args.empty()) {
                normalized_status = args;
            } else {
                // General hyphen-to-space capitalized status
                std::string status_candidate = cmd_name;
                for (char &c : status_candidate) {
                    if (c == '-' || c == '_') c = ' ';
                }
                // Capitalize words
                bool cap = true;
                for (char &c : status_candidate) {
                    if (std::isspace(static_cast<unsigned char>(c))) {
                        cap = true;
                    } else if (cap) {
                        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                        cap = false;
                    } else {
                        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    }
                }
                normalized_status = status_candidate;
            }

            cmd.target_status = normalized_status;
            commands.push_back(cmd);
        }
    }

    return commands;
}

// Looks up user ID matching committer email or username, defaulting to admin user (1)
inline int find_user_by_author(db::sqlite_db &conn, const std::string &author_email, const std::string &author_name) {
    if (!author_email.empty()) {
        auto stmt = conn.prepare("SELECT id FROM users WHERE LOWER(email) = LOWER(?);");
        stmt.bind_text(1, author_email);
        if (stmt.step() == SQLITE_ROW) {
            return stmt.column_int(0);
        }
    }
    if (!author_name.empty()) {
        auto stmt = conn.prepare("SELECT id FROM users WHERE LOWER(username) = LOWER(?) OR LOWER(display_name) = LOWER(?);");
        stmt.bind_text(1, author_name);
        stmt.bind_text(2, author_name);
        if (stmt.step() == SQLITE_ROW) {
            return stmt.column_int(0);
        }
    }
    auto stmt = conn.prepare("SELECT id FROM users ORDER BY id ASC LIMIT 1;");
    if (stmt.step() == SQLITE_ROW) {
        return stmt.column_int(0);
    }
    return 1;
}

// Executes smart commit actions for an individual work item
inline bool apply_smart_commit(db::sqlite_db &conn,
                               int item_id,
                               const std::string &item_key,
                               int user_id,
                               const std::string &short_hash,
                               const std::vector<SmartCommitCommand> &commands,
                               std::vector<std::string> &actions_log,
                               std::vector<std::string> &errors) {
    std::string old_status;
    int project_id = 0;
    std::string summary;
    {
        auto q = conn.prepare("SELECT status, project_id, title FROM work_items WHERE id = ?;");
        q.bind_int(1, item_id);
        if (q.step() == SQLITE_ROW) {
            old_status = q.column_str(0);
            project_id = q.column_int(1);
            summary = q.column_str(2);
        } else {
            errors.push_back("Work item " + item_key + " not found");
            return false;
        }
    }

    std::string user_display_name = "System";
    std::string user_username = "system";
    {
        auto uq = conn.prepare("SELECT display_name, username FROM users WHERE id = ?;");
        uq.bind_int(1, user_id);
        if (uq.step() == SQLITE_ROW) {
            user_display_name = uq.column_str(0);
            user_username = uq.column_str(1);
        }
    }

    for (const auto &cmd : commands) {
        if (cmd.type == CommandType::Comment) {
            int cid = 0;
            {
                auto cins = conn.prepare("INSERT INTO comments (work_item_id, author_id, body) VALUES (?, ?, ?);");
                cins.bind_int(1, item_id);
                cins.bind_int(2, user_id);
                cins.bind_text(3, cmd.comment_body);
                cins.step_done();
                cid = static_cast<int>(conn.last_insert_rowid());
            }

            collaboration::process_mentions_and_links(conn, item_id, cid, user_id, cmd.comment_body);
            collaboration::record_changelog(conn, item_id, user_id, "comment", "", "Added comment via commit " + short_hash);

            auto wstmt = conn.prepare("INSERT OR IGNORE INTO work_item_watchers (work_item_id, user_id) VALUES (?, ?);");
            wstmt.bind_int(1, item_id);
            wstmt.bind_int(2, user_id);
            wstmt.step_done();

            automation::AutomationEvent a_ev;
            a_ev.type = automation::EventType::CommentAdded;
            a_ev.project_id = project_id;
            a_ev.issue_id = item_id;
            a_ev.issue_key = item_key;
            a_ev.user_id = user_id;
            a_ev.user_username = user_username;
            a_ev.user_display_name = user_display_name;
            a_ev.comment_id = cid;
            a_ev.comment_body = cmd.comment_body;
            automation::dispatch_event(conn, a_ev);

            actions_log.push_back("Added comment to " + item_key + " via #" + short_hash);
        } else if (cmd.type == CommandType::Time) {
            std::string desc = cmd.time_comment.empty() ? ("Logged via smart commit " + short_hash) : cmd.time_comment;
            int wid = 0;
            {
                auto wins = conn.prepare(R"SQL(
                    INSERT INTO worklogs (
                        work_item_id, author_id, time_spent_seconds,
                        started_at, description, adjustment_mode, adjustment_seconds,
                        remaining_before, remaining_after
                    ) VALUES (
                        ?, ?, ?,
                        datetime('now'), ?, 'auto', 0,
                        0, 0
                    );
                )SQL");
                wins.bind_int(1, item_id);
                wins.bind_int(2, user_id);
                wins.bind_int(3, static_cast<int>(cmd.time_seconds));
                wins.bind_text(4, desc);
                wins.step_done();
                wid = static_cast<int>(conn.last_insert_rowid());
            }

            {
                auto upd = conn.prepare(R"SQL(
                    UPDATE work_items
                    SET time_spent_seconds = time_spent_seconds + ?,
                        remaining_estimate_seconds = MAX(0, remaining_estimate_seconds - ?),
                        updated_at = datetime('now')
                    WHERE id = ?;
                )SQL");
                upd.bind_int(1, static_cast<int>(cmd.time_seconds));
                upd.bind_int(2, static_cast<int>(cmd.time_seconds));
                upd.bind_int(3, item_id);
                upd.step_done();
            }

            collaboration::record_changelog(conn, item_id, user_id, "worklog", "",
                "Logged " + time_tracking::format_seconds_to_duration(cmd.time_seconds) + " via commit " + short_hash);

            automation::AutomationEvent a_ev;
            a_ev.type = automation::EventType::WorklogAdded;
            a_ev.project_id = project_id;
            a_ev.issue_id = item_id;
            a_ev.issue_key = item_key;
            a_ev.user_id = user_id;
            a_ev.user_username = user_username;
            a_ev.user_display_name = user_display_name;
            automation::dispatch_event(conn, a_ev);

            actions_log.push_back("Logged " + time_tracking::format_seconds_to_duration(cmd.time_seconds) + " on " + item_key + " via #" + short_hash);
        } else if (cmd.type == CommandType::Transition) {
            std::string target_status = cmd.target_status;
            // Check if status is known or map to default status
            {
                auto sq = conn.prepare("SELECT name FROM workflow_statuses WHERE LOWER(name) = LOWER(?) LIMIT 1;");
                sq.bind_text(1, target_status);
                if (sq.step() == SQLITE_ROW) {
                    target_status = sq.column_str(0);
                }
            }

            if (target_status != old_status) {
                int resolution_id = 0;
                bool is_done_transition = (target_status == "Done" || target_status == "Closed" || target_status == "Resolved");
                if (is_done_transition) {
                    auto res_q = conn.prepare("SELECT id FROM resolutions WHERE is_default = 1 LIMIT 1;");
                    if (res_q.step() == SQLITE_ROW) {
                        resolution_id = res_q.column_int(0);
                    }
                    if (resolution_id <= 0) {
                        auto res_fb = conn.prepare("SELECT id FROM resolutions ORDER BY id ASC LIMIT 1;");
                        if (res_fb.step() == SQLITE_ROW) {
                            resolution_id = res_fb.column_int(0);
                        }
                    }
                }

                auto upd = conn.prepare(R"SQL(
                    UPDATE work_items
                    SET status = ?,
                        progress = CASE WHEN ? = 'Done' THEN 100 ELSE progress END,
                        resolution_id = CASE WHEN ? = 1 THEN COALESCE(?, resolution_id) ELSE resolution_id END,
                        resolution_date = CASE WHEN ? = 1 THEN datetime('now') ELSE resolution_date END,
                        updated_at = datetime('now')
                    WHERE id = ?;
                )SQL");
                upd.bind_text(1, target_status);
                upd.bind_text(2, target_status);
                upd.bind_int(3, is_done_transition ? 1 : 0);
                if (resolution_id > 0) upd.bind_int(4, resolution_id);
                else upd.bind_null(4);
                upd.bind_int(5, is_done_transition ? 1 : 0);
                upd.bind_int(6, item_id);
                upd.step_done();

                auto hist = conn.prepare(R"SQL(
                    INSERT INTO workflow_history (work_item_id, from_status, to_status, changed_by, comment)
                    VALUES (?, ?, ?, ?, 'Smart Commit ' || ? || ': Transitioned via ' || ?);
                )SQL");
                hist.bind_int(1, item_id);
                hist.bind_text(2, old_status);
                hist.bind_text(3, target_status);
                hist.bind_int(4, user_id);
                hist.bind_text(5, short_hash);
                hist.bind_text(6, cmd.raw_command);
                hist.step_done();

                collaboration::record_changelog(conn, item_id, user_id, "status", old_status, target_status);

                automation::AutomationEvent a_ev;
                a_ev.type = automation::EventType::StatusChanged;
                a_ev.project_id = project_id;
                a_ev.issue_id = item_id;
                a_ev.issue_key = item_key;
                a_ev.user_id = user_id;
                a_ev.user_username = user_username;
                a_ev.user_display_name = user_display_name;
                a_ev.from_status = old_status;
                a_ev.to_status = target_status;
                automation::dispatch_event(conn, a_ev);

                actions_log.push_back("Transitioned " + item_key + " from '" + old_status + "' to '" + target_status + "' via " + cmd.raw_command);
                old_status = target_status;
            }
        }
    }
    return true;
}

// Records a git commit link and executes smart commits if present
inline bool record_commit(db::sqlite_db &conn,
                          int item_id,
                          int repository_id,
                          const std::string &repository_name,
                          const std::string &commit_hash,
                          const std::string &short_hash,
                          const std::string &message,
                          const std::string &author_name,
                          const std::string &author_email,
                          const std::string &commit_url,
                          const std::string &committed_at) {
    auto ins = conn.prepare(R"SQL(
        INSERT INTO git_commits (
            work_item_id, repository_id, repository_name, commit_hash, short_hash,
            message, author_name, author_email, commit_url, committed_at
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, CASE WHEN ? != '' THEN ? ELSE datetime('now') END)
        ON CONFLICT(work_item_id, commit_hash) DO UPDATE SET
            message = excluded.message,
            commit_url = excluded.commit_url;
    )SQL");
    ins.bind_int(1, item_id);
    if (repository_id > 0) ins.bind_int(2, repository_id);
    else ins.bind_null(2);
    ins.bind_text(3, repository_name);
    ins.bind_text(4, commit_hash);
    ins.bind_text(5, short_hash.empty() ? commit_hash.substr(0, 7) : short_hash);
    ins.bind_text(6, message);
    ins.bind_text(7, author_name);
    ins.bind_text(8, author_email);
    ins.bind_text(9, commit_url);
    ins.bind_text(10, committed_at);
    ins.bind_text(11, committed_at);
    ins.step_done();
    return true;
}

// Records a git branch link
inline bool record_branch(db::sqlite_db &conn,
                          int item_id,
                          int repository_id,
                          const std::string &repository_name,
                          const std::string &branch_name,
                          const std::string &branch_url,
                          const std::string &last_commit_hash) {
    auto ins = conn.prepare(R"SQL(
        INSERT INTO git_branches (
            work_item_id, repository_id, repository_name, branch_name, branch_url, last_commit_hash, updated_at
        ) VALUES (?, ?, ?, ?, ?, ?, datetime('now'))
        ON CONFLICT(work_item_id, repository_name, branch_name) DO UPDATE SET
            branch_url = CASE WHEN excluded.branch_url != '' THEN excluded.branch_url ELSE git_branches.branch_url END,
            last_commit_hash = CASE WHEN excluded.last_commit_hash != '' THEN excluded.last_commit_hash ELSE git_branches.last_commit_hash END,
            updated_at = datetime('now');
    )SQL");
    ins.bind_int(1, item_id);
    if (repository_id > 0) ins.bind_int(2, repository_id);
    else ins.bind_null(2);
    ins.bind_text(3, repository_name);
    ins.bind_text(4, branch_name);
    ins.bind_text(5, branch_url);
    ins.bind_text(6, last_commit_hash);
    ins.step_done();
    return true;
}

// Records a pull request link
inline bool record_pull_request(db::sqlite_db &conn,
                                int item_id,
                                int repository_id,
                                const std::string &repository_name,
                                int pr_number,
                                const std::string &title,
                                const std::string &source_branch,
                                const std::string &target_branch,
                                const std::string &status,
                                const std::string &author_name,
                                const std::string &author_email,
                                const std::string &pr_url) {
    auto ins = conn.prepare(R"SQL(
        INSERT INTO git_pull_requests (
            work_item_id, repository_id, repository_name, pr_number, title,
            source_branch, target_branch, status, author_name, author_email, pr_url, updated_at
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, datetime('now'))
        ON CONFLICT(work_item_id, repository_name, pr_number) DO UPDATE SET
            title = excluded.title,
            source_branch = excluded.source_branch,
            target_branch = excluded.target_branch,
            status = excluded.status,
            author_name = excluded.author_name,
            author_email = excluded.author_email,
            pr_url = excluded.pr_url,
            updated_at = datetime('now');
    )SQL");
    ins.bind_int(1, item_id);
    if (repository_id > 0) ins.bind_int(2, repository_id);
    else ins.bind_null(2);
    ins.bind_text(3, repository_name);
    ins.bind_int(4, pr_number);
    ins.bind_text(5, title);
    ins.bind_text(6, source_branch);
    ins.bind_text(7, target_branch);
    ins.bind_text(8, status);
    ins.bind_text(9, author_name);
    ins.bind_text(10, author_email);
    ins.bind_text(11, pr_url);
    ins.step_done();
    return true;
}

// Records a CI/CD build run
inline bool record_build(db::sqlite_db &conn,
                         int item_id,
                         int repository_id,
                         const std::string &repository_name,
                         const std::string &pipeline_id,
                         const std::string &build_number,
                         const std::string &status,
                         const std::string &commit_hash,
                         const std::string &branch_name,
                         const std::string &build_url,
                         int duration_seconds) {
    auto ins = conn.prepare(R"SQL(
        INSERT INTO git_builds (
            work_item_id, repository_id, repository_name, pipeline_id, build_number,
            status, commit_hash, branch_name, build_url, duration_seconds, started_at, finished_at
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, datetime('now'), datetime('now'));
    )SQL");
    ins.bind_int(1, item_id);
    if (repository_id > 0) ins.bind_int(2, repository_id);
    else ins.bind_null(2);
    ins.bind_text(3, repository_name);
    ins.bind_text(4, pipeline_id);
    ins.bind_text(5, build_number);
    ins.bind_text(6, status);
    ins.bind_text(7, commit_hash);
    ins.bind_text(8, branch_name);
    ins.bind_text(9, build_url);
    ins.bind_int(10, duration_seconds);
    ins.step_done();
    return true;
}

// Process a commit: extract keys, link commit, execute smart commands
inline SmartCommitExecutionResult process_commit_event(db::sqlite_db &conn,
                                                       const std::string &repository_name,
                                                       int repository_id,
                                                       const std::string &commit_hash,
                                                       const std::string &message,
                                                       const std::string &author_name,
                                                       const std::string &author_email,
                                                       const std::string &commit_url,
                                                       const std::string &committed_at) {
    SmartCommitExecutionResult res;
    res.matched_keys = extract_issue_keys(message);
    res.commands = parse_smart_commands(message);
    res.user_id = find_user_by_author(conn, author_email, author_name);

    {
        auto uq = conn.prepare("SELECT display_name FROM users WHERE id = ?;");
        uq.bind_int(1, res.user_id);
        if (uq.step() == SQLITE_ROW) {
            res.user_name = uq.column_str(0);
        }
    }

    std::string short_hash = commit_hash.size() > 7 ? commit_hash.substr(0, 7) : commit_hash;

    for (const auto &key : res.matched_keys) {
        int item_id = 0;
        {
            auto q = conn.prepare("SELECT id FROM work_items WHERE UPPER(key) = UPPER(?);");
            q.bind_text(1, key);
            if (q.step() == SQLITE_ROW) {
                item_id = q.column_int(0);
            }
        }
        if (item_id <= 0) {
            res.errors.push_back("Issue key " + key + " not found in database");
            continue;
        }

        // Link commit
        record_commit(conn, item_id, repository_id, repository_name, commit_hash, short_hash,
                      message, author_name, author_email, commit_url, committed_at);
        res.applied_actions.push_back("Linked commit " + short_hash + " to " + key);

        // Execute smart commit commands if any
        if (!res.commands.empty()) {
            apply_smart_commit(conn, item_id, key, res.user_id, short_hash, res.commands,
                               res.applied_actions, res.errors);
        }
    }

    return res;
}

// Fetches the full development panel payload for a work item
inline json::json_value get_item_development_summary(db::sqlite_db &conn, int item_id) {
    auto res = json::json_value::make_object();
    res.add_int("work_item_id", item_id);

    // Branches
    auto branches_arr = json::json_value::make_array();
    int b_count = 0;
    {
        auto q = conn.prepare(R"SQL(
            SELECT id, repository_name, branch_name, branch_url, last_commit_hash, created_at, updated_at
            FROM git_branches
            WHERE work_item_id = ?
            ORDER BY id DESC;
        )SQL");
        q.bind_int(1, item_id);
        while (q.step() == SQLITE_ROW) {
            b_count++;
            auto b = json::json_value::make_object();
            b.add_int("id", q.column_int(0));
            b.add_str("repository_name", q.column_str(1));
            b.add_str("branch_name", q.column_str(2));
            b.add_str("branch_url", q.column_str(3));
            b.add_str("last_commit_hash", q.column_str(4));
            b.add_str("created_at", q.column_str(5));
            b.add_str("updated_at", q.column_str(6));
            branches_arr.add(std::move(b));
        }
    }

    // Commits
    auto commits_arr = json::json_value::make_array();
    int c_count = 0;
    {
        auto q = conn.prepare(R"SQL(
            SELECT id, repository_name, commit_hash, short_hash, message, author_name, author_email, commit_url, committed_at, created_at
            FROM git_commits
            WHERE work_item_id = ?
            ORDER BY id DESC;
        )SQL");
        q.bind_int(1, item_id);
        while (q.step() == SQLITE_ROW) {
            c_count++;
            auto c = json::json_value::make_object();
            c.add_int("id", q.column_int(0));
            c.add_str("repository_name", q.column_str(1));
            c.add_str("commit_hash", q.column_str(2));
            c.add_str("short_hash", q.column_str(3));
            c.add_str("message", q.column_str(4));
            c.add_str("author_name", q.column_str(5));
            c.add_str("author_email", q.column_str(6));
            c.add_str("commit_url", q.column_str(7));
            c.add_str("committed_at", q.column_str(8));
            c.add_str("created_at", q.column_str(9));
            commits_arr.add(std::move(c));
        }
    }

    // Pull requests
    auto prs_arr = json::json_value::make_array();
    int pr_count = 0;
    {
        auto q = conn.prepare(R"SQL(
            SELECT id, repository_name, pr_number, title, source_branch, target_branch, status, author_name, author_email, pr_url, created_at, updated_at
            FROM git_pull_requests
            WHERE work_item_id = ?
            ORDER BY id DESC;
        )SQL");
        q.bind_int(1, item_id);
        while (q.step() == SQLITE_ROW) {
            pr_count++;
            auto pr = json::json_value::make_object();
            pr.add_int("id", q.column_int(0));
            pr.add_str("repository_name", q.column_str(1));
            pr.add_int("pr_number", q.column_int(2));
            pr.add_str("title", q.column_str(3));
            pr.add_str("source_branch", q.column_str(4));
            pr.add_str("target_branch", q.column_str(5));
            pr.add_str("status", q.column_str(6));
            pr.add_str("author_name", q.column_str(7));
            pr.add_str("author_email", q.column_str(8));
            pr.add_str("pr_url", q.column_str(9));
            pr.add_str("created_at", q.column_str(10));
            pr.add_str("updated_at", q.column_str(11));
            prs_arr.add(std::move(pr));
        }
    }

    // Builds
    auto builds_arr = json::json_value::make_array();
    int bd_count = 0;
    {
        auto q = conn.prepare(R"SQL(
            SELECT id, repository_name, pipeline_id, build_number, status, commit_hash, branch_name, build_url, duration_seconds, started_at, finished_at, created_at
            FROM git_builds
            WHERE work_item_id = ?
            ORDER BY id DESC;
        )SQL");
        q.bind_int(1, item_id);
        while (q.step() == SQLITE_ROW) {
            bd_count++;
            auto bd = json::json_value::make_object();
            bd.add_int("id", q.column_int(0));
            bd.add_str("repository_name", q.column_str(1));
            bd.add_str("pipeline_id", q.column_str(2));
            bd.add_str("build_number", q.column_str(3));
            bd.add_str("status", q.column_str(4));
            bd.add_str("commit_hash", q.column_str(5));
            bd.add_str("branch_name", q.column_str(6));
            bd.add_str("build_url", q.column_str(7));
            bd.add_int("duration_seconds", q.column_int(8));
            bd.add_str("started_at", q.column_str(9));
            bd.add_str("finished_at", q.column_str(10));
            bd.add_str("created_at", q.column_str(11));
            builds_arr.add(std::move(bd));
        }
    }

    res.add_int("branches_count", b_count);
    res.add_int("commits_count", c_count);
    res.add_int("pull_requests_count", pr_count);
    res.add_int("builds_count", bd_count);

    res.add("branches", std::move(branches_arr));
    res.add("commits", std::move(commits_arr));
    res.add("pull_requests", std::move(prs_arr));
    res.add("builds", std::move(builds_arr));

    return res;
}

} // namespace vcs
