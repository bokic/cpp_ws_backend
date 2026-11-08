#pragma once

#include "raii_sqlite.h"
#include "raii_json.h"
#include "crypto_utils.h"
#include "security_engine.h"

#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <netdb.h>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>

namespace notifications {

struct NotificationEvent {
    std::string event_type; // issue_created, issue_updated, issue_assigned, issue_resolved, issue_commented, worklog_added, generic_event
    int project_id = 0;
    int issue_id = 0;
    std::string issue_key;
    std::string issue_title;
    std::string issue_status;
    std::string issue_priority;
    int actor_id = 0;
    std::string actor_name;
    std::string summary_text;
    std::string snippet;
};

inline std::string get_current_iso8601() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
    gmtime_r(&t, &tm_buf);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    return std::string(buf);
}

// HTTP POST Sender over POSIX socket with timeout
struct HttpResponse {
    int status_code = 0;
    std::string body;
    std::string error;
    double duration_ms = 0.0;
};

inline HttpResponse http_post_json(const std::string &url,
                                   const std::string &payload,
                                   const std::vector<std::pair<std::string, std::string>> &headers,
                                   int timeout_sec = 2) {
    HttpResponse resp;
    auto t0 = std::chrono::steady_clock::now();

    // Parse URL: http://host[:port]/path
    std::string prefix = "http://";
    std::string host, port_str = "80", path = "/";
    if (url.rfind(prefix, 0) == 0) {
        std::string rest = url.substr(prefix.size());
        auto slash_pos = rest.find('/');
        std::string host_port;
        if (slash_pos == std::string::npos) {
            host_port = rest;
            path = "/";
        } else {
            host_port = rest.substr(0, slash_pos);
            path = rest.substr(slash_pos);
        }
        auto colon_pos = host_port.find(':');
        if (colon_pos == std::string::npos) {
            host = host_port;
        } else {
            host = host_port.substr(0, colon_pos);
            port_str = host_port.substr(colon_pos + 1);
        }
    } else {
        resp.error = "Only http:// URLs are supported currently";
        auto t1 = std::chrono::steady_clock::now();
        resp.duration_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        return resp;
    }

    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res) != 0 || !res) {
        resp.error = "DNS resolution failed for host: " + host;
        auto t1 = std::chrono::steady_clock::now();
        resp.duration_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        return resp;
    }

    int sock = -1;
    for (struct addrinfo *p = res; p != nullptr; p = p->ai_next) {
        sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock < 0) continue;

        struct timeval tv{};
        tv.tv_sec = timeout_sec;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (connect(sock, p->ai_addr, p->ai_addrlen) == 0) {
            break;
        }
        close(sock);
        sock = -1;
    }
    freeaddrinfo(res);

    if (sock < 0) {
        resp.error = "Connection failed to " + host + ":" + port_str;
        auto t1 = std::chrono::steady_clock::now();
        resp.duration_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        return resp;
    }

    // Build HTTP request
    std::ostringstream req;
    req << "POST " << path << " HTTP/1.1\r\n";
    req << "Host: " << host << "\r\n";
    req << "User-Agent: JiraLike-Webhook-Engine/1.0\r\n";
    req << "Content-Type: application/json\r\n";
    req << "Content-Length: " << payload.size() << "\r\n";
    req << "Connection: close\r\n";
    for (const auto &h : headers) {
        req << h.first << ": " << h.second << "\r\n";
    }
    req << "\r\n";
    req << payload;

    std::string req_str = req.str();
    ssize_t sent = send(sock, req_str.c_str(), req_str.size(), 0);
    if (sent < 0) {
        resp.error = "Send failed: " + std::string(strerror(errno));
        close(sock);
        auto t1 = std::chrono::steady_clock::now();
        resp.duration_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        return resp;
    }

    // Read response
    char buffer[4096];
    std::string raw_resp;
    while (true) {
        ssize_t n = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) break;
        buffer[n] = '\0';
        raw_resp.append(buffer, n);
    }
    close(sock);

    auto t1 = std::chrono::steady_clock::now();
    resp.duration_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    // Parse status line: HTTP/1.1 200 OK
    if (!raw_resp.empty()) {
        auto first_line_end = raw_resp.find("\r\n");
        if (first_line_end != std::string::npos) {
            std::string status_line = raw_resp.substr(0, first_line_end);
            auto space1 = status_line.find(' ');
            if (space1 != std::string::npos) {
                auto space2 = status_line.find(' ', space1 + 1);
                std::string code_str = (space2 != std::string::npos) ?
                    status_line.substr(space1 + 1, space2 - space1 - 1) :
                    status_line.substr(space1 + 1);
                resp.status_code = std::atoi(code_str.c_str());
            }
        }
        auto body_start = raw_resp.find("\r\n\r\n");
        if (body_start != std::string::npos) {
            resp.body = raw_resp.substr(body_start + 4);
        } else {
            resp.body = raw_resp;
        }
    } else {
        resp.error = "Empty response received from webhook endpoint";
    }

    return resp;
}

// ----------------------------------------------------------------------------
// Notification Routing
// ----------------------------------------------------------------------------

inline void route_notification(db::sqlite_db &conn, const NotificationEvent &ev) {
    if (ev.issue_id <= 0) return;

    try {
        // 1. Resolve notification scheme for project
        int scheme_id = 1;
        {
            auto stmt = conn.prepare("SELECT COALESCE(notification_scheme_id, 1) FROM projects WHERE id = ?;");
            stmt.bind_int(1, ev.project_id);
            if (stmt.step() == SQLITE_ROW) {
                scheme_id = stmt.column_int(0);
                if (scheme_id <= 0) scheme_id = 1;
            }
        }

        // 2. Fetch issue metadata (assignee, reporter, key, title) if not filled
        int assignee_id = 0;
        int reporter_id = 0;
        int lead_user_id = 0;
        int security_level_id = 0;
        std::string current_key = ev.issue_key;
        std::string current_title = ev.issue_title;
        std::string current_status = ev.issue_status;
        std::string current_priority = ev.issue_priority;
        int project_id = ev.project_id;

        {
            auto stmt = conn.prepare("SELECT key, title, status, priority, project_id, COALESCE(assignee_id, 0), COALESCE(reporter_id, 0), COALESCE(security_level_id, 0) FROM work_items WHERE id = ?;");
            stmt.bind_int(1, ev.issue_id);
            if (stmt.step() == SQLITE_ROW) {
                if (current_key.empty()) current_key = stmt.column_str(0);
                if (current_title.empty()) current_title = stmt.column_str(1);
                if (current_status.empty()) current_status = stmt.column_str(2);
                if (current_priority.empty()) current_priority = stmt.column_str(3);
                if (project_id <= 0) project_id = stmt.column_int(4);
                assignee_id = stmt.column_int(5);
                reporter_id = stmt.column_int(6);
                security_level_id = stmt.column_int(7);
            }
        }

        if (project_id > 0) {
            auto stmt = conn.prepare("SELECT COALESCE(lead_user_id, 0) FROM projects WHERE id = ?;");
            stmt.bind_int(1, project_id);
            if (stmt.step() == SQLITE_ROW) {
                lead_user_id = stmt.column_int(0);
            }
        }

        // 3. Query notification scheme entries
        std::vector<std::string> matching_events = { ev.event_type, "generic_event" };
        if (ev.event_type == "comment_added") matching_events.push_back("issue_commented");
        else if (ev.event_type == "issue_commented") matching_events.push_back("comment_added");
        else if (ev.event_type == "status_changed") {
            matching_events.push_back("issue_updated");
            if (current_status == "Done" || current_status == "Resolved" || current_status == "Closed") {
                matching_events.push_back("issue_resolved");
            }
        } else if (ev.event_type == "field_value_changed") {
            matching_events.push_back("issue_updated");
        }

        std::string sql_entries = "SELECT recipient_type, recipient_parameter FROM notification_scheme_entries WHERE scheme_id = ? AND event_type IN (";
        for (size_t i = 0; i < matching_events.size(); ++i) {
            if (i > 0) sql_entries += ",";
            sql_entries += "?";
        }
        sql_entries += ");";

        auto stmt_entries = conn.prepare(sql_entries);
        stmt_entries.bind_int(1, scheme_id);
        for (size_t i = 0; i < matching_events.size(); ++i) {
            stmt_entries.bind_text(static_cast<int>(i + 2), matching_events[i]);
        }

        std::set<int> recipient_ids;
        while (stmt_entries.step() == SQLITE_ROW) {
            std::string r_type = stmt_entries.column_str(0);
            std::string r_param = stmt_entries.column_str(1);

            if (r_type == "assignee" && assignee_id > 0) {
                recipient_ids.insert(assignee_id);
            } else if (r_type == "reporter" && reporter_id > 0) {
                recipient_ids.insert(reporter_id);
            } else if (r_type == "project_lead" && lead_user_id > 0) {
                recipient_ids.insert(lead_user_id);
            } else if (r_type == "watchers") {
                auto stmt_w = conn.prepare("SELECT user_id FROM work_item_watchers WHERE work_item_id = ?;");
                stmt_w.bind_int(1, ev.issue_id);
                while (stmt_w.step() == SQLITE_ROW) {
                    recipient_ids.insert(stmt_w.column_int(0));
                }
            } else if (r_type == "user" && !r_param.empty()) {
                int uid = std::atoi(r_param.c_str());
                if (uid > 0) {
                    recipient_ids.insert(uid);
                } else {
                    auto stmt_u = conn.prepare("SELECT id FROM users WHERE username = ?;");
                    stmt_u.bind_text(1, r_param);
                    if (stmt_u.step() == SQLITE_ROW) {
                        recipient_ids.insert(stmt_u.column_int(0));
                    }
                }
            } else if (r_type == "role" && !r_param.empty() && project_id > 0) {
                auto stmt_r = conn.prepare(R"SQL(
                    SELECT user_id FROM project_role_members
                    WHERE project_id = ? AND lower(role_name) = lower(?) AND user_id IS NOT NULL
                    UNION
                    SELECT id FROM users WHERE lower(role) = lower(?);
                )SQL");
                stmt_r.bind_int(1, project_id);
                stmt_r.bind_text(2, r_param);
                stmt_r.bind_text(3, r_param);
                while (stmt_r.step() == SQLITE_ROW) {
                    recipient_ids.insert(stmt_r.column_int(0));
                }
            } else if (r_type == "group" && !r_param.empty()) {
                int gid = std::atoi(r_param.c_str());
                std::string sql_g = (gid > 0) ?
                    "SELECT user_id FROM user_group_members WHERE group_id = ?;" :
                    "SELECT ugm.user_id FROM user_group_members ugm JOIN user_groups ug ON ug.id = ugm.group_id WHERE lower(ug.name) = lower(?);";
                auto stmt_g = conn.prepare(sql_g);
                if (gid > 0) stmt_g.bind_int(1, gid);
                else stmt_g.bind_text(1, r_param);
                while (stmt_g.step() == SQLITE_ROW) {
                    recipient_ids.insert(stmt_g.column_int(0));
                }
            } else if (r_type == "custom_field" && !r_param.empty() && ev.issue_id > 0) {
                auto stmt_cf = conn.prepare(R"SQL(
                    SELECT COALESCE(cfv.user_value_id, 0), COALESCE(cfv.text_value, '')
                    FROM custom_field_values cfv
                    INNER JOIN custom_fields cf ON cf.id = cfv.custom_field_id
                    WHERE cfv.work_item_id = ?
                      AND (CAST(cf.id AS TEXT) = ? OR lower(cf.name) = lower(?) OR lower(cf.field_key) = lower(?))
                    LIMIT 1;
                )SQL");
                stmt_cf.bind_int(1, ev.issue_id);
                stmt_cf.bind_text(2, r_param);
                stmt_cf.bind_text(3, r_param);
                stmt_cf.bind_text(4, r_param);
                if (stmt_cf.step() == SQLITE_ROW) {
                    int cf_uid = stmt_cf.column_int(0);
                    std::string cf_txt = stmt_cf.column_str(1);
                    if (cf_uid > 0) {
                        recipient_ids.insert(cf_uid);
                    } else if (!cf_txt.empty()) {
                        int parsed_uid = std::atoi(cf_txt.c_str());
                        if (parsed_uid > 0) {
                            recipient_ids.insert(parsed_uid);
                        } else {
                            auto stmt_u = conn.prepare("SELECT id FROM users WHERE lower(username) = lower(?);");
                            stmt_u.bind_text(1, cf_txt);
                            if (stmt_u.step() == SQLITE_ROW) {
                                recipient_ids.insert(stmt_u.column_int(0));
                            }
                        }
                    }
                }
            }
        }

        if (recipient_ids.empty()) {
            return;
        }

        std::string title_line = "[" + current_key + "] " + (!ev.summary_text.empty() ? ev.summary_text : current_title);
        std::string link_url = "/items/" + current_key;
        std::string now_iso = get_current_iso8601();

        for (int uid : recipient_ids) {
            // Permission check: ensure user can browse project & view this issue (Step 11 security schemes)
            if (!security::has_permission(conn, uid, project_id, "BROWSE_PROJECT", reporter_id, assignee_id, ev.issue_id)) {
                continue;
            }
            if (!security::can_access_security_level(conn, uid, project_id, security_level_id, reporter_id, assignee_id, ev.issue_id)) {
                continue;
            }

            // Fetch user email & display name
            std::string user_email, user_display_name;
            {
                auto stmt_u = conn.prepare("SELECT email, display_name FROM users WHERE id = ?;");
                stmt_u.bind_int(1, uid);
                if (stmt_u.step() == SQLITE_ROW) {
                    user_email = stmt_u.column_str(0);
                    user_display_name = stmt_u.column_str(1);
                }
            }

            // 1. Insert In-App Notification
            {
                auto ins = conn.prepare(R"SQL(
                    INSERT INTO in_app_notifications (user_id, work_item_id, event_type, title, message, link, is_read)
                    VALUES (?, ?, ?, ?, ?, ?, 0);
                )SQL");
                ins.bind_int(1, uid);
                ins.bind_int(2, ev.issue_id);
                ins.bind_text(3, ev.event_type);
                ins.bind_text(4, title_line);
                ins.bind_text(5, ev.snippet);
                ins.bind_text(6, link_url);
                ins.step_done();
            }

            // 2. Queue Outbound Email with threading headers
            if (!user_email.empty()) {
                std::string email_subject = "[JIRALIKE] (" + current_key + ") " + current_title;
                std::string email_text = "Hi " + user_display_name + ",\n\n"
                    + "An event occurred on issue " + current_key + ":\n"
                    + "Event: " + ev.event_type + "\n"
                    + "Actor: " + ev.actor_name + "\n"
                    + "Summary: " + current_title + "\n"
                    + "Status: " + current_status + "\n"
                    + "Priority: " + current_priority + "\n"
                    + "Details: " + ev.snippet + "\n\n"
                    + "View issue: " + link_url + "\n\n"
                    + "--\nJiraLike Workspace Notification Engine";

                std::string email_html = R"HTML(
<div style="font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; max-width: 600px; margin: 0 auto; border: 1px solid #DFE1E6; border-radius: 4px; overflow: hidden;">
  <div style="background-color: #0052CC; color: #FFFFFF; padding: 14px 20px; font-weight: bold; font-size: 16px;">
    JiraLike Notification
  </div>
  <div style="padding: 20px; background-color: #FAFBFC;">
    <p style="margin-top: 0; color: #172B4D; font-size: 14px;">Hi <strong>)HTML" + user_display_name + R"HTML(</strong>,</p>
    <div style="background-color: #FFFFFF; border: 1px solid #EBECF0; border-radius: 4px; padding: 16px; margin: 16px 0;">
      <h3 style="margin-top: 0; color: #0052CC;">)HTML" + current_key + ": " + current_title + R"HTML(</h3>
      <p style="color: #5E6C84; font-size: 13px; margin: 4px 0;"><strong>Event:</strong> )HTML" + ev.event_type + R"HTML( | <strong>Actor:</strong> )HTML" + ev.actor_name + R"HTML(</p>
      <p style="color: #5E6C84; font-size: 13px; margin: 4px 0;"><strong>Status:</strong> )HTML" + current_status + R"HTML( | <strong>Priority:</strong> )HTML" + current_priority + R"HTML(</p>
      <div style="margin-top: 12px; padding: 10px; background-color: #F4F5F7; border-left: 3px solid #0052CC; font-size: 14px; color: #172B4D;">
        )HTML" + ev.snippet + R"HTML(
      </div>
    </div>
    <a href=")HTML" + link_url + R"HTML(" style="display: inline-block; background-color: #0052CC; color: #FFFFFF; text-decoration: none; padding: 8px 16px; border-radius: 3px; font-size: 14px; font-weight: 500;">View Issue</a>
  </div>
  <div style="padding: 12px 20px; background-color: #EBECF0; color: #6B778C; font-size: 11px; text-align: center;">
    This message was sent automatically by JiraLike Notification System.
  </div>
</div>
)HTML";

                // Threading headers: Message-ID, In-Reply-To, References
                auto h_obj = json::json_value::make_object();
                std::string msg_id = "<issue-" + std::to_string(ev.issue_id) + "." + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()) + "@jiralike.local>";
                std::string thread_ref = "<issue-" + std::to_string(ev.issue_id) + "@jiralike.local>";
                h_obj.add_str("Message-ID", msg_id);
                h_obj.add_str("In-Reply-To", thread_ref);
                h_obj.add_str("References", thread_ref);
                h_obj.add_str("X-Jira-Issue-Key", current_key);
                h_obj.add_str("X-Jira-Event", ev.event_type);

                auto ins_mail = conn.prepare(R"SQL(
                    INSERT INTO outbound_emails (to_email, to_name, subject, body_html, body_text, headers_json, status)
                    VALUES (?, ?, ?, ?, ?, ?, 'queued');
                )SQL");
                ins_mail.bind_text(1, user_email);
                ins_mail.bind_text(2, user_display_name);
                ins_mail.bind_text(3, email_subject);
                ins_mail.bind_text(4, email_html);
                ins_mail.bind_text(5, email_text);
                ins_mail.bind_text(6, h_obj.to_string());
                ins_mail.step_done();
            }
        }
    } catch (const std::exception &ex) {
        std::cerr << "[notification_engine] Exception in route_notification: " << ex.what() << std::endl;
    }
}

// ----------------------------------------------------------------------------
// Outbound Webhook Dispatching
// ----------------------------------------------------------------------------

inline void dispatch_webhooks(db::sqlite_db &conn, const NotificationEvent &ev) {
    try {
        // Query active webhooks
        auto stmt = conn.prepare("SELECT id, name, url, secret, events FROM webhooks WHERE is_active = 1;");

        struct WebhookInfo {
            int id;
            std::string name;
            std::string url;
            std::string secret;
            std::string events_json;
        };
        std::vector<WebhookInfo> webhooks;
        while (stmt.step() == SQLITE_ROW) {
            WebhookInfo w;
            w.id = stmt.column_int(0);
            w.name = stmt.column_str(1);
            w.url = stmt.column_str(2);
            w.secret = stmt.column_str(3);
            w.events_json = stmt.column_str(4);
            webhooks.push_back(std::move(w));
        }

        if (webhooks.empty()) return;

        // Construct standard Jira-compatible Webhook JSON payload
        auto payload = json::json_value::make_object();
        payload.add_str("event", ev.event_type);
        payload.add_str("timestamp", get_current_iso8601());

        auto issue_obj = json::json_value::make_object();
        issue_obj.add_int("id", ev.issue_id);
        issue_obj.add_str("key", ev.issue_key);
        issue_obj.add_str("title", ev.issue_title);
        issue_obj.add_str("status", ev.issue_status);
        issue_obj.add_str("priority", ev.issue_priority);
        issue_obj.add_int("project_id", ev.project_id);
        payload.add("issue", std::move(issue_obj));

        auto actor_obj = json::json_value::make_object();
        actor_obj.add_int("id", ev.actor_id);
        actor_obj.add_str("name", ev.actor_name);
        payload.add("user", std::move(actor_obj));

        payload.add_str("summary", ev.summary_text);
        payload.add_str("snippet", ev.snippet);

        std::string payload_str = payload.to_string();

        for (const auto &wh : webhooks) {
            // Check if webhook subscribes to this event
            bool subscribed = false;
            if (wh.events_json.find("\"*\"") != std::string::npos ||
                wh.events_json.find("'*'") != std::string::npos ||
                wh.events_json == "*" || wh.events_json.empty()) {
                subscribed = true;
            } else if (wh.events_json.find("\"" + ev.event_type + "\"") != std::string::npos ||
                       wh.events_json.find(ev.event_type) != std::string::npos) {
                subscribed = true;
            } else if (ev.event_type == "comment_added" && (wh.events_json.find("issue_commented") != std::string::npos)) {
                subscribed = true;
            } else if (ev.event_type == "issue_commented" && (wh.events_json.find("comment_added") != std::string::npos)) {
                subscribed = true;
            } else if (ev.event_type == "status_changed" && (wh.events_json.find("issue_updated") != std::string::npos || wh.events_json.find("issue_resolved") != std::string::npos)) {
                subscribed = true;
            }

            if (!subscribed) continue;

            // Compute HMAC-SHA256 signature
            std::string signature = utils::hmac_sha256_hex(wh.secret, payload_str);
            std::vector<std::pair<std::string, std::string>> headers = {
                {"X-Hub-Signature-256", "sha256=" + signature},
                {"X-Event-Type", ev.event_type},
                {"X-Issue-Key", ev.issue_key}
            };

            // Attempt delivery
            HttpResponse resp = http_post_json(wh.url, payload_str, headers, /*timeout_sec=*/2);

            std::string delivery_status = (resp.status_code >= 200 && resp.status_code < 300) ? "success" : "failed";
            std::string response_body_snippet = resp.body.substr(0, 500);

            // Record delivery
            auto ins = conn.prepare(R"SQL(
                INSERT INTO webhook_deliveries (webhook_id, event_type, payload, response_status, response_body, duration_ms, attempt, status, error_message)
                VALUES (?, ?, ?, ?, ?, ?, 1, ?, ?);
            )SQL");
            ins.bind_int(1, wh.id);
            ins.bind_text(2, ev.event_type);
            ins.bind_text(3, payload_str);
            ins.bind_int(4, resp.status_code);
            ins.bind_text(5, response_body_snippet);
            ins.bind_double(6, resp.duration_ms);
            ins.bind_text(7, delivery_status);
            ins.bind_text(8, resp.error);
            ins.step_done();
        }
    } catch (const std::exception &ex) {
        std::cerr << "[notification_engine] Exception in dispatch_webhooks: " << ex.what() << std::endl;
    }
}

// Unified event handler called on state transitions, comments, creates, updates
inline void handle_event(db::sqlite_db &conn, NotificationEvent ev) {
    if (ev.issue_id > 0 && (ev.issue_title.empty() || ev.issue_key.empty() || ev.project_id <= 0)) {
        try {
            auto stmt = conn.prepare("SELECT key, title, status, priority, project_id FROM work_items WHERE id = ?;");
            stmt.bind_int(1, ev.issue_id);
            if (stmt.step() == SQLITE_ROW) {
                if (ev.issue_key.empty()) ev.issue_key = stmt.column_str(0);
                if (ev.issue_title.empty()) ev.issue_title = stmt.column_str(1);
                if (ev.issue_status.empty()) ev.issue_status = stmt.column_str(2);
                if (ev.issue_priority.empty()) ev.issue_priority = stmt.column_str(3);
                if (ev.project_id <= 0) ev.project_id = stmt.column_int(4);
            }
        } catch (...) {}
    }
    route_notification(conn, ev);
    dispatch_webhooks(conn, ev);
}

} // namespace notifications
