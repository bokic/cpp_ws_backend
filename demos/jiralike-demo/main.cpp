#include "wsserver.h"
#include "wsworker.h"
#include "wsroute.h"

#include "src/raii_sqlite.h"
#include "src/connection_pool.h"
#include "src/raii_json.h"
#include "src/crypto_utils.h"
#include "src/db_migrator.h"
#include "src/jql_lexer.h"
#include "src/jql_parser.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>
#include <fcgio.h>

static std::string g_db_path = "db/jiralike.db";
static std::string g_blobs_dir = "blobs";
static std::shared_ptr<db::connection_pool<db::sqlite_db>> g_db_pool;

static void init_db_pool(int worker_num) {
    size_t workers = worker_num > 0 ? static_cast<size_t>(worker_num) : 4;
    db::pool_options opts;
    opts.min_size = std::max<size_t>(4, workers);
    opts.max_size = std::max<size_t>(32, workers * 4);
    opts.acquire_timeout = std::chrono::milliseconds(5000);
    opts.idle_timeout = std::chrono::milliseconds(60000);
    opts.ping_on_acquire = true;

    g_db_pool = std::make_shared<db::connection_pool<db::sqlite_db>>(
        []() { return std::make_unique<db::sqlite_db>(g_db_path); },
        opts
    );
}

// Acquires a leased SQLite database connection from the thread-safe connection pool.
// When the returned pooled_connection leaves scope, its state is automatically reset
// and the underlying handle is returned to the pool.
static db::pooled_connection<db::sqlite_db> acquire_db_connection() {
    if (!g_db_pool) {
        init_db_pool(4);
    }
    return g_db_pool->acquire();
}

static const std::string &get_header(const std::map<std::string, std::string> &header, const std::string &key) {
    static const std::string empty_str;
    auto it = header.find(key);
    return it != header.end() ? it->second : empty_str;
}

static void send_json(backend::wsworker *worker,
                      const std::map<std::string, std::string> &header,
                      const std::string &status,
                      const json::json_value &payload) {
    std::string proto = get_header(header, "SERVER_PROTOCOL");
    if (proto.empty()) proto = "HTTP/1.1";
    const char *json_str = payload.to_string();
    int len = static_cast<int>(std::strlen(json_str));
    FCGX_FPrintF(worker->out(),
                 "%s %s\r\n"
                 "Status: %s\r\n"
                 "Content-Type: application/json; charset=utf-8\r\n"
                 "X-Content-Type-Options: nosniff\r\n"
                 "Cache-Control: no-store\r\n"
                 "Content-Length: %d\r\n\r\n%s",
                 proto.c_str(), status.c_str(), status.c_str(), len, json_str);
}

static void send_error(backend::wsworker *worker,
                       const std::map<std::string, std::string> &header,
                       const std::string &status,
                       const std::string &message) {
    auto err = json::json_value::make_object();
    err.add_str("error", message);
    send_json(worker, header, status, err);
}

struct auth_user {
    int id = 0;
    std::string username;
    std::string email;
    std::string display_name;
    std::string role;
    std::string avatar_color;
    bool authenticated = false;
};

static std::string extract_token(backend::wsworker *worker, const std::map<std::string, std::string> &header) {
    std::string auth = get_header(header, "HTTP_AUTHORIZATION");
    if (auth.starts_with("Bearer ") || auth.starts_with("bearer ")) {
        return auth.substr(7);
    }
    std::string x_token = get_header(header, "HTTP_X_AUTH_TOKEN");
    if (!x_token.empty()) {
        return x_token;
    }
    auto args = worker->parse_args(get_header(header, "QUERY_STRING"));
    if (!args["token"].empty()) {
        return args["token"];
    }
    return "";
}

static auth_user get_authenticated_user(db::sqlite_db &conn,
                                        backend::wsworker *worker,
                                        const std::map<std::string, std::string> &header) {
    auth_user u;
    std::string token = extract_token(worker, header);
    if (token.empty()) return u;

    auto stmt = conn.prepare(R"SQL(
        SELECT u.id, u.username, u.email, u.display_name, u.role, u.avatar_color
        FROM sessions s
        INNER JOIN users u ON u.id = s.user_id
        WHERE s.token = ? AND s.expires_at > datetime('now');
    )SQL");
    stmt.bind_text(1, token);
    if (stmt.step() == SQLITE_ROW) {
        u.id = stmt.column_int(0);
        u.username = stmt.column_str(1);
        u.email = stmt.column_str(2);
        u.display_name = stmt.column_str(3);
        u.role = stmt.column_str(4);
        u.avatar_color = stmt.column_str(5);
        u.authenticated = true;
    }
    return u;
}

static json::json_value user_to_json(const auth_user &u) {
    auto obj = json::json_value::make_object();
    obj.add_int("id", u.id);
    obj.add_str("username", u.username);
    obj.add_str("email", u.email);
    obj.add_str("display_name", u.display_name);
    obj.add_str("role", u.role);
    obj.add_str("avatar_color", u.avatar_color);
    return obj;
}

static json::json_value pool_stats_to_json() {
    auto obj = json::json_value::make_object();
    if (!g_db_pool) return obj;
    db::pool_stats st = g_db_pool->stats();
    obj.add_int("total_connections", static_cast<int>(st.total_connections));
    obj.add_int("idle_connections", static_cast<int>(st.idle_connections));
    obj.add_int("active_leases", static_cast<int>(st.active_leases));
    obj.add_int("min_size", static_cast<int>(st.min_size));
    obj.add_int("max_size", static_cast<int>(st.max_size));
    obj.add_int64("total_acquisitions", static_cast<int64_t>(st.total_acquisitions));
    obj.add_int64("total_timeouts", static_cast<int64_t>(st.total_timeouts));
    obj.add_int64("total_health_failures", static_cast<int64_t>(st.total_health_failures));
    obj.add_int64("stmt_cache_hits", static_cast<int64_t>(st.total_stmt_cache_hits));
    obj.add_int64("stmt_cache_misses", static_cast<int64_t>(st.total_stmt_cache_misses));
    obj.add_int("cached_statements", static_cast<int>(st.total_cached_statements));
    return obj;
}

// GET /api/health
static void handle_health(backend::wsworker *worker,
                          const std::map<std::string, std::string> &header,
                          const std::list<std::string> &) {
    auto conn = acquire_db_connection();
    bool db_ok = conn.ping();

    auto res = json::json_value::make_object();
    res.add_str("status", db_ok ? "ok" : "degraded");
    res.add_bool("db_healthy", db_ok);
    res.add("db_pool", pool_stats_to_json());
    send_json(worker, header, db_ok ? "200 OK" : "503 Service Unavailable", res);
}

// GET /api/setup/status
static void handle_setup_status(backend::wsworker *worker,
                                const std::map<std::string, std::string> &header,
                                const std::list<std::string> &) {
    auto conn = acquire_db_connection();

    bool is_setup = false;
    std::string instance_name = "JiraLike Workspace";
    {
        auto stmt = conn.prepare("SELECT is_setup_complete, instance_name FROM app_config WHERE id = 1;");
        if (stmt.step() == SQLITE_ROW) {
            is_setup = stmt.column_int(0) != 0;
            instance_name = stmt.column_str(1);
        }
    }

    int schema_ver = 0;
    auto migrations_arr = json::json_value::make_array();
    {
        auto stmt = conn.prepare("SELECT version, applied_at, description FROM schema_version ORDER BY version ASC;");
        while (stmt.step() == SQLITE_ROW) {
            schema_ver = stmt.column_int(0);
            auto m = json::json_value::make_object();
            m.add_int("version", stmt.column_int(0));
            m.add_str("applied_at", stmt.column_str(1));
            m.add_str("description", stmt.column_str(2));
            migrations_arr.add(std::move(m));
        }
    }

    int user_count = 0;
    {
        auto stmt = conn.prepare("SELECT COUNT(*) FROM users;");
        if (stmt.step() == SQLITE_ROW) user_count = stmt.column_int(0);
    }

    auto res = json::json_value::make_object();
    res.add_bool("setup_complete", is_setup && user_count > 0);
    res.add_str("instance_name", instance_name);
    res.add_int("schema_version", schema_ver);
    res.add_int("latest_schema_version", db::LATEST_SCHEMA_VERSION);
    res.add("migrations", std::move(migrations_arr));
    res.add("db_pool", pool_stats_to_json());
    send_json(worker, header, "200 OK", res);
}

static void seed_demo_workspace(db::sqlite_db &conn, int admin_id, int project_id, const std::string &proj_key) {
    // Create additional demo team members
    auto ins_user = conn.prepare(R"SQL(
        INSERT OR IGNORE INTO users (username, email, password_hash, display_name, role, avatar_color)
        VALUES (?, ?, ?, ?, ?, ?);
    )SQL");

    std::string default_pw = utils::hash_password("demo123");

    auto add_member = [&](const char *uname, const char *email, const char *dname, const char *role, const char *color) -> int {
        ins_user.clear_and_reset();
        ins_user.bind_text(1, uname);
        ins_user.bind_text(2, email);
        ins_user.bind_text(3, default_pw);
        ins_user.bind_text(4, dname);
        ins_user.bind_text(5, role);
        ins_user.bind_text(6, color);
        ins_user.step_done();
        int id = static_cast<int>(conn.last_insert_rowid());
        return id > 0 ? id : admin_id;
    };

    int sarah_id = add_member("sarah", "sarah@jiralike.local", "Sarah Chen", "Project Manager", "#00875A");
    int marcus_id = add_member("marcus", "marcus@jiralike.local", "Marcus Vance", "Developer", "#6554C0");
    int elena_id = add_member("elena", "elena@jiralike.local", "Elena Rostova", "QA Engineer", "#FF5630");

    // Create Sprints
    auto ins_sprint = conn.prepare(R"SQL(
        INSERT INTO sprints (project_id, name, goal, status, start_date, end_date, is_current)
        VALUES (?, ?, ?, ?, ?, ?, ?);
    )SQL");
    ins_sprint.bind_int(1, project_id);
    ins_sprint.bind_text(2, "Sprint 1 - Core Architecture & Board");
    ins_sprint.bind_text(3, "Deliver FastCGI SQLite migrations, Kanban drag-and-drop, and Gantt timeline.");
    ins_sprint.bind_text(4, "active");
    ins_sprint.bind_text(5, "2026-10-15");
    ins_sprint.bind_text(6, "2026-11-05");
    ins_sprint.bind_int(7, 1);
    ins_sprint.step_done();
    int sprint1_id = static_cast<int>(conn.last_insert_rowid());

    ins_sprint.clear_and_reset();
    ins_sprint.bind_int(1, project_id);
    ins_sprint.bind_text(2, "Sprint 2 - Reporting & Automation");
    ins_sprint.bind_text(3, "Add velocity charts, webhook integrations, and custom SLA notifications.");
    ins_sprint.bind_text(4, "planned");
    ins_sprint.bind_text(5, "2026-11-06");
    ins_sprint.bind_text(6, "2026-11-20");
    ins_sprint.bind_int(7, 0);
    ins_sprint.step_done();
    int sprint2_id = static_cast<int>(conn.last_insert_rowid());

    int seq = 0;
    auto ins_item = conn.prepare(R"SQL(
        INSERT INTO work_items (
            project_id, key, type, category, status, priority, title, description,
            parent_id, sprint_id, assignee_id, reporter_id, story_points,
            progress, start_date, due_date, severity, steps_to_reproduce, labels, board_order
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )SQL");

    auto ins_hist = conn.prepare(R"SQL(
        INSERT INTO workflow_history (work_item_id, from_status, to_status, changed_by, comment)
        VALUES (?, ?, ?, ?, ?);
    )SQL");

    auto create_item = [&](const char *type, const char *category, const char *status, const char *priority,
                           const char *title, const char *desc, int parent_id, int sprint_id,
                           int assignee_id, int reporter_id, int points, int progress,
                           const char *start_date, const char *due_date,
                           const char *severity, const char *steps, const char *labels) -> int {
        ++seq;
        std::string item_key = proj_key + "-" + std::to_string(seq);
        ins_item.clear_and_reset();
        ins_item.bind_int(1, project_id);
        ins_item.bind_text(2, item_key);
        ins_item.bind_text(3, type);
        ins_item.bind_text(4, category);
        ins_item.bind_text(5, status);
        ins_item.bind_text(6, priority);
        ins_item.bind_text(7, title);
        ins_item.bind_text(8, desc);
        ins_item.bind_optional_int(9, parent_id);
        ins_item.bind_optional_int(10, sprint_id);
        ins_item.bind_optional_int(11, assignee_id);
        ins_item.bind_optional_int(12, reporter_id);
        ins_item.bind_int(13, points);
        ins_item.bind_int(14, progress);
        ins_item.bind_text(15, start_date);
        ins_item.bind_text(16, due_date);
        ins_item.bind_text(17, severity);
        ins_item.bind_text(18, steps);
        ins_item.bind_text(19, labels);
        ins_item.bind_int(20, seq * 10);
        ins_item.step_done();
        int item_id = static_cast<int>(conn.last_insert_rowid());

        ins_hist.clear_and_reset();
        ins_hist.bind_int(1, item_id);
        ins_hist.bind_text(2, "Created");
        ins_hist.bind_text(3, status);
        ins_hist.bind_int(4, reporter_id);
        ins_hist.bind_text(5, "Initial creation during workspace setup");
        ins_hist.step_done();

        return item_id;
    };

    int epic1 = create_item(
        "Epic", "New Feature", "In Progress", "Highest",
        "FastCGI Backend & SQLite Schema Migration Engine",
        "Design and implement automatic schema version detection and atomic SQL upgrade execution on startup, plus blob storage for attachments.",
        0, sprint1_id, admin_id, sarah_id, 13, 75,
        "2026-10-15", "2026-10-30", "", "", "backend,sqlite,architecture"
    );

    int epic2 = create_item(
        "Epic", "New Feature", "In Progress", "High",
        "Interactive Angular Kanban & Gantt Workspace",
        "Single-page application with drag-and-drop state columns, Gantt timeline visualization, issue creation workflows, and attachment previews.",
        0, sprint1_id, marcus_id, sarah_id, 21, 60,
        "2026-10-18", "2026-11-05", "", "", "frontend,angular,ux"
    );

    int story1 = create_item(
        "Story", "New Feature", "Done", "Highest",
        "Startup SQLite version check and sequential migration runner",
        "As an administrator, when the backend starts, it should inspect schema_version and run pending migration scripts up to the latest schema version.",
        epic1, sprint1_id, admin_id, sarah_id, 5, 100,
        "2026-10-15", "2026-10-20", "", "", "database,migrations"
    );

    int story2 = create_item(
        "Story", "New Feature", "In Review", "High",
        "Blob storage upload & streaming API for issue attachments",
        "Allow users to attach screenshots, logs, and documents to any work item, persisted safely in the blobs/ directory.",
        epic1, sprint1_id, marcus_id, admin_id, 5, 85,
        "2026-10-20", "2026-10-26", "", "", "blobs,storage,api"
    );

    create_item(
        "Subtask", "Improvement", "Done", "Medium",
        "Sanitize uploaded filenames and generate unique content prefixes",
        "Prevent directory traversal and filename collisions in blobs/ directory.",
        story2, sprint1_id, marcus_id, marcus_id, 2, 100,
        "2026-10-20", "2026-10-22", "", "", "security,blobs"
    );

    create_item(
        "Subtask", "New Feature", "In Progress", "Medium",
        "Add attachment gallery and one-click download in Issue Detail view",
        "Display file size, MIME badge, uploader name, and download link.",
        story2, sprint1_id, elena_id, marcus_id, 3, 60,
        "2026-10-22", "2026-10-26", "", "", "frontend,attachments"
    );

    int story3 = create_item(
        "Story", "New Feature", "In Progress", "Highest",
        "Kanban & Gantt board with drag-and-drop state transitions",
        "Users can view items grouped by state and drag cards across columns or timeline rows to transition their workflow state.",
        epic2, sprint1_id, marcus_id, sarah_id, 8, 65,
        "2026-10-21", "2026-10-31", "", "", "kanban,gantt,drag-drop"
    );

    create_item(
        "Task", "Improvement", "QA / Testing", "High",
        "Package application for Docker, Ubuntu .deb, and Arch Linux AUR",
        "Provide multi-stage Dockerfile, Debian package scripts, and Arch PKGBUILD preserving SQLite database across upgrades.",
        epic1, sprint1_id, admin_id, sarah_id, 5, 90,
        "2026-10-23", "2026-10-29", "", "", "docker,debian,arch,packaging"
    );

    int bug1 = create_item(
        "Task", "Bug", "To Do", "Critical",
        "Gantt timeline bar overlaps when due_date is earlier than start_date",
        "When a user manually enters an inverted date range on an issue, the timeline bar width calculation should clamp to a minimum 1-day span.",
        epic2, sprint1_id, elena_id, elena_id, 3, 15,
        "2026-10-25", "2026-10-28", "Major",
        "1. Open Create Item page\n2. Select type 'Task' and category 'Bug'\n3. Set Start Date to 2026-10-30 and Due Date to 2026-10-25\n4. Open Gantt view and observe bar width.",
        "gantt,ui-bug"
    );

    int issue1 = create_item(
        "Task", "Issue", "Blocked", "High",
        "Define production Nginx rate-limiting policy for /api/blobs uploads",
        "Waiting on DevOps security review to finalize client_max_body_size and burst limits.",
        epic1, sprint1_id, sarah_id, admin_id, 2, 20,
        "2026-10-24", "2026-11-02", "", "", "nginx,ops,security"
    );

    create_item(
        "Story", "New Feature", "Backlog", "Medium",
        "Add Sprint burndown and cumulative flow analytics",
        "Track remaining story points per day across active sprints.",
        epic2, sprint2_id, sarah_id, sarah_id, 8, 0,
        "2026-11-06", "2026-11-15", "", "", "analytics,sprint"
    );

    create_item(
        "Story", "New Feature", "In Progress", "High",
        "Real-time Kanban & Gantt filter presets",
        "Add ability to filter work items by Hierarchy Type and Category across Kanban and Gantt boards.",
        epic2, sprint1_id, marcus_id, sarah_id, 5, 55,
        "2026-10-24", "2026-11-01", "", "", "feature,board,ux"
    );

    create_item(
        "Task", "Improvement", "To Do", "Medium",
        "Optimize SQLite WAL checkpointing and prepared statement caching",
        "Enhance C++23 FastCGI SQLite connection settings for faster concurrent read/write throughput.",
        epic1, sprint1_id, admin_id, admin_id, 3, 25,
        "2026-10-26", "2026-11-03", "", "", "improvement,performance,sqlite"
    );

    // Update project's issue_seq
    auto upd_proj = conn.prepare("UPDATE projects SET issue_seq = ? WHERE id = ?;");
    upd_proj.bind_int(1, seq);
    upd_proj.bind_int(2, project_id);
    upd_proj.step_done();

    // Add issue links
    auto ins_link = conn.prepare("INSERT INTO work_item_links (source_item_id, target_item_id, link_type) VALUES (?, ?, ?);");
    ins_link.bind_int(1, story1);
    ins_link.bind_int(2, story2);
    ins_link.bind_text(3, "blocks");
    ins_link.step_done();

    ins_link.clear_and_reset();
    ins_link.bind_int(1, bug1);
    ins_link.bind_int(2, story3);
    ins_link.bind_text(3, "relates_to");
    ins_link.step_done();

    ins_link.clear_and_reset();
    ins_link.bind_int(1, story2);
    ins_link.bind_int(2, issue1);
    ins_link.bind_text(3, "relates_to");
    ins_link.step_done();

    // Add sample comments
    auto ins_comment = conn.prepare("INSERT INTO comments (work_item_id, author_id, body) VALUES (?, ?, ?);");
    ins_comment.bind_int(1, story3);
    ins_comment.bind_int(2, sarah_id);
    ins_comment.bind_text(3, "Make sure both Kanban column drag-and-drop and Gantt state selector trigger PATCH /api/items/:id/state and log workflow_history.");
    ins_comment.step_done();

    ins_comment.clear_and_reset();
    ins_comment.bind_int(1, story2);
    ins_comment.bind_int(2, marcus_id);
    ins_comment.bind_text(3, "Blob storage handler is wired up and tested with both multipart uploads and base64 payloads.");
    ins_comment.step_done();

    // Create a sample attachment in blobs/
    std::string blob_name = "demo_architecture_spec.txt";
    std::string blob_path = g_blobs_dir + "/" + blob_name;
    std::string sample_content =
        "JiraLike Demo Architecture Specification\n"
        "========================================\n"
        "- Backend: C++23 FastCGI (cpp_ws_backend)\n"
        "- Database: SQLite with schema_version & automatic startup migrations\n"
        "- File Storage: blobs/ directory\n"
        "- Frontend: Angular Single-Page Application\n";
    {
        std::ofstream ofs(blob_path, std::ios::binary);
        ofs.write(sample_content.data(), static_cast<std::streamsize>(sample_content.size()));
    }
    auto ins_att = conn.prepare(R"SQL(
        INSERT OR IGNORE INTO attachments (work_item_id, uploader_id, original_filename, mime_type, size_bytes, blob_filename)
        VALUES (?, ?, ?, ?, ?, ?);
    )SQL");
    ins_att.bind_int(1, story2);
    ins_att.bind_int(2, admin_id);
    ins_att.bind_text(3, "architecture_spec.txt");
    ins_att.bind_text(4, "text/plain");
    ins_att.bind_int(5, static_cast<int>(sample_content.size()));
    ins_att.bind_text(6, blob_name);
    ins_att.step_done();

    // Seed project Components & Versions and associate with demo work items
    auto ins_comp = conn.prepare(R"SQL(
        INSERT OR IGNORE INTO components (project_id, name, description, lead_user_id, default_assignee_type)
        VALUES (?, ?, ?, ?, ?);
    )SQL");
    ins_comp.bind_int(1, project_id);
    ins_comp.bind_text(2, "FastCGI Core");
    ins_comp.bind_text(3, "C++23 FastCGI request routing, worker pool, and socket handling");
    ins_comp.bind_int(4, admin_id);
    ins_comp.bind_text(5, "component_lead");
    ins_comp.step_done();

    ins_comp.clear_and_reset();
    ins_comp.bind_int(1, project_id);
    ins_comp.bind_text(2, "SQLite Storage Engine");
    ins_comp.bind_text(3, "Connection pooling, WAL migrations, and prepared statement cache");
    ins_comp.bind_int(4, admin_id);
    ins_comp.bind_text(5, "component_lead");
    ins_comp.step_done();

    ins_comp.clear_and_reset();
    ins_comp.bind_int(1, project_id);
    ins_comp.bind_text(2, "Angular Board UI");
    ins_comp.bind_text(3, "Kanban drag-and-drop board, Gantt timeline, and SPA views");
    ins_comp.bind_int(4, marcus_id);
    ins_comp.bind_text(5, "component_lead");
    ins_comp.step_done();

    auto ins_ver = conn.prepare(R"SQL(
        INSERT OR IGNORE INTO versions (project_id, name, description, start_date, release_date, status)
        VALUES (?, ?, ?, ?, ?, ?);
    )SQL");
    ins_ver.bind_int(1, project_id);
    ins_ver.bind_text(2, "v1.0.0");
    ins_ver.bind_text(3, "Initial foundation release with FastCGI, SQLite migrations, and Kanban board");
    ins_ver.bind_text(4, "2026-10-01");
    ins_ver.bind_text(5, "2026-10-25");
    ins_ver.bind_text(6, "released");
    ins_ver.step_done();

    ins_ver.clear_and_reset();
    ins_ver.bind_int(1, project_id);
    ins_ver.bind_text(2, "v1.1.0");
    ins_ver.bind_text(3, "Connection pooling, core Jira domain entities, components, and release management");
    ins_ver.bind_text(4, "2026-10-26");
    ins_ver.bind_text(5, "2026-11-10");
    ins_ver.bind_text(6, "unreleased");
    ins_ver.step_done();

    ins_ver.clear_and_reset();
    ins_ver.bind_int(1, project_id);
    ins_ver.bind_text(2, "v2.0.0");
    ins_ver.bind_text(3, "Custom fields engine, directed workflows, and JQL search compiler");
    ins_ver.bind_text(4, "2026-11-11");
    ins_ver.bind_text(5, "2026-12-01");
    ins_ver.bind_text(6, "unreleased");
    ins_ver.step_done();

    conn.exec(R"SQL(
        UPDATE work_items
        SET resolution_id = 1,
            resolution_date = COALESCE(NULLIF(updated_at, ''), datetime('now'))
        WHERE status = 'Done' AND resolution_id IS NULL;

        INSERT OR IGNORE INTO work_item_components (work_item_id, component_id)
        SELECT w.id, c.id
        FROM work_items w
        JOIN components c ON c.project_id = w.project_id
        WHERE (w.id % 3 = 1 AND c.name = 'FastCGI Core')
           OR (w.id % 3 = 2 AND c.name = 'SQLite Storage Engine')
           OR (w.id % 3 = 0 AND c.name = 'Angular Board UI');

        INSERT OR IGNORE INTO work_item_versions (work_item_id, version_id, relation_type)
        SELECT w.id, v.id, 'fix'
        FROM work_items w
        JOIN versions v ON v.project_id = w.project_id
        WHERE (w.status = 'Done' AND v.name = 'v1.0.0')
           OR (w.status != 'Done' AND v.name = 'v1.1.0');

        INSERT OR IGNORE INTO work_item_versions (work_item_id, version_id, relation_type)
        SELECT w.id, v.id, 'affects'
        FROM work_items w
        JOIN versions v ON v.project_id = w.project_id
        WHERE w.category = 'Bug' AND v.name = 'v1.0.0';

        INSERT OR IGNORE INTO custom_field_values (work_item_id, custom_field_id, text_value)
        SELECT w.id, 1, CASE WHEN w.category = 'Bug' THEN 'Production' ELSE 'Staging' END
        FROM work_items w;

        INSERT OR IGNORE INTO custom_field_values (work_item_id, custom_field_id, text_value)
        SELECT w.id, 2, '- [x] Unit & integration tests passing' || char(10) || '- [x] Zero SQLite lock contention under load'
        FROM work_items w
        WHERE w.type IN ('Story', 'Epic');

        INSERT OR IGNORE INTO custom_field_values (work_item_id, custom_field_id, text_value)
        SELECT w.id, 3, CASE WHEN w.priority IN ('Critical', 'Highest', 'High') THEN 'Tier 1 - Enterprise Critical' ELSE 'Tier 2 - High' END
        FROM work_items w;

        INSERT OR IGNORE INTO custom_field_values (work_item_id, custom_field_id, numeric_value, text_value)
        SELECT w.id, 4, (w.story_points * 8.0), CAST(w.story_points * 8 AS TEXT)
        FROM work_items w
        WHERE w.story_points > 0;

        INSERT OR IGNORE INTO custom_field_values (work_item_id, custom_field_id, date_value, text_value)
        SELECT w.id, 5, w.due_date, w.due_date
        FROM work_items w
        WHERE w.due_date != '';

        INSERT OR IGNORE INTO custom_field_values (work_item_id, custom_field_id, user_value_id, text_value)
        SELECT w.id, 6, COALESCE(w.assignee_id, 1), CAST(COALESCE(w.assignee_id, 1) AS TEXT)
        FROM work_items w
        WHERE w.assignee_id IS NOT NULL;

        INSERT OR IGNORE INTO custom_field_values (work_item_id, custom_field_id, text_value)
        SELECT w.id, 7, 'https://docs.jiralike.local/specs/' || lower(w.key)
        FROM work_items w
        WHERE w.type IN ('Epic', 'Story');

        INSERT OR IGNORE INTO custom_field_values (work_item_id, custom_field_id, text_value, numeric_value)
        SELECT w.id, 8, CASE WHEN w.status = 'Done' THEN 'true' ELSE 'false' END, CASE WHEN w.status = 'Done' THEN 1 ELSE 0 END
        FROM work_items w;

        INSERT OR IGNORE INTO custom_field_values (work_item_id, custom_field_id, json_value, text_value)
        SELECT w.id, 9, '["Linux x86_64","Docker / Kubernetes"]', 'Linux x86_64, Docker / Kubernetes'
        FROM work_items w;
    )SQL");
}

// POST /api/setup/init
static void handle_setup_init(backend::wsworker *worker,
                              const std::map<std::string, std::string> &header,
                              const std::list<std::string> &) {
    auto body = json::json_value::parse(worker->body());
    if (!body) {
        send_error(worker, header, "400 Bad Request", "Invalid JSON body");
        return;
    }

    auto conn = acquire_db_connection();
    {
        auto chk = conn.prepare("SELECT is_setup_complete FROM app_config WHERE id = 1;");
        if (chk.step() == SQLITE_ROW && chk.column_int(0) != 0) {
            send_error(worker, header, "409 Conflict", "Workspace setup has already been completed.");
            return;
        }
    }

    std::string instance_name = body.get_string("instance_name", "JiraLike");
    std::string admin_username = body.get_string("admin_username", "admin");
    std::string admin_email = body.get_string("admin_email", "admin@jiralike.local");
    std::string admin_display_name = body.get_string("admin_display_name", "System Administrator");
    std::string admin_password = body.get_string("admin_password", "admin123");
    std::string project_key = body.get_string("project_key", "JIRA");
    std::string project_name = body.get_string("project_name", "Core Platform Development");
    std::string project_desc = body.get_string("project_description", "Primary issue tracking, Kanban board, and Gantt schedule.");
    bool seed_demo = body.get_bool("seed_demo_data", true);

    if (admin_username.empty() || admin_password.empty() || project_key.empty() || project_name.empty()) {
        send_error(worker, header, "400 Bad Request", "Admin credentials and project key/name are required.");
        return;
    }

    // Normalize project key to uppercase alphanumeric
    for (char &c : project_key) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    int admin_id = 0;
    int project_id = 0;
    std::string token;
    try {
        db::sqlite_transaction tx(conn);
        auto ins_admin = conn.prepare(R"SQL(
            INSERT INTO users (username, email, password_hash, display_name, role, avatar_color)
            VALUES (?, ?, ?, ?, 'Admin', '#0052CC');
        )SQL");
        ins_admin.bind_text(1, admin_username);
        ins_admin.bind_text(2, admin_email);
        ins_admin.bind_text(3, utils::hash_password(admin_password));
        ins_admin.bind_text(4, admin_display_name);
        ins_admin.step_done();
        admin_id = static_cast<int>(conn.last_insert_rowid());

        auto ins_proj = conn.prepare(R"SQL(
            INSERT INTO projects (key, name, description, lead_user_id, issue_seq)
            VALUES (?, ?, ?, ?, 0);
        )SQL");
        ins_proj.bind_text(1, project_key);
        ins_proj.bind_text(2, project_name);
        ins_proj.bind_text(3, project_desc);
        ins_proj.bind_int(4, admin_id);
        ins_proj.step_done();
        project_id = static_cast<int>(conn.last_insert_rowid());

        if (seed_demo) {
            seed_demo_workspace(conn, admin_id, project_id, project_key);
        } else {
            auto ins_sprint = conn.prepare(R"SQL(
                INSERT INTO sprints (project_id, name, goal, status, start_date, end_date)
                VALUES (?, 'Sprint 1', 'Initial project sprint', 'active', date('now'), date('now', '+14 days'));
            )SQL");
            ins_sprint.bind_int(1, project_id);
            ins_sprint.step_done();
        }

        auto upd_cfg = conn.prepare(R"SQL(
            UPDATE app_config
            SET is_setup_complete = 1, instance_name = ?, updated_at = datetime('now')
            WHERE id = 1;
        )SQL");
        upd_cfg.bind_text(1, instance_name);
        upd_cfg.step_done();

        token = utils::generate_random_hex(32);
        auto ins_sess = conn.prepare(R"SQL(
            INSERT INTO sessions (token, user_id, expires_at)
            VALUES (?, ?, datetime('now', '+7 days'));
        )SQL");
        ins_sess.bind_text(1, token);
        ins_sess.bind_int(2, admin_id);
        ins_sess.step_done();

        tx.commit();
    } catch (const std::exception &ex) {
        send_error(worker, header, "500 Internal Server Error", std::string("Setup failed: ") + ex.what());
        return;
    }

    auth_user u{admin_id, admin_username, admin_email, admin_display_name, "Admin", "#0052CC", true};
    auto res = json::json_value::make_object();
    res.add_bool("setup_complete", true);
    res.add_str("instance_name", instance_name);
    res.add_str("token", token);
    res.add("user", user_to_json(u));
    res.add_int("project_id", project_id);
    send_json(worker, header, "200 OK", res);
}

// POST /api/auth/login
static void handle_auth_login(backend::wsworker *worker,
                              const std::map<std::string, std::string> &header,
                              const std::list<std::string> &) {
    auto body = json::json_value::parse(worker->body());
    if (!body) {
        send_error(worker, header, "400 Bad Request", "Invalid JSON body");
        return;
    }

    std::string username = body.get_string("username");
    std::string password = body.get_string("password");
    if (username.empty() || password.empty()) {
        send_error(worker, header, "400 Bad Request", "Username and password are required");
        return;
    }

    auto conn = acquire_db_connection();
    auto stmt = conn.prepare(R"SQL(
        SELECT id, username, email, password_hash, display_name, role, avatar_color
        FROM users
        WHERE username = ? OR email = ?;
    )SQL");
    stmt.bind_text(1, username);
    stmt.bind_text(2, username);

    if (stmt.step() != SQLITE_ROW) {
        send_error(worker, header, "401 Unauthorized", "Invalid username or password");
        return;
    }

    std::string stored_hash = stmt.column_str(3);
    if (!utils::verify_password(password, stored_hash)) {
        send_error(worker, header, "401 Unauthorized", "Invalid username or password");
        return;
    }

    auth_user u;
    u.id = stmt.column_int(0);
    u.username = stmt.column_str(1);
    u.email = stmt.column_str(2);
    u.display_name = stmt.column_str(4);
    u.role = stmt.column_str(5);
    u.avatar_color = stmt.column_str(6);
    u.authenticated = true;
    stmt.reset();

    std::string token = utils::generate_random_hex(32);
    {
        db::sqlite_transaction tx(conn);
        auto ins = conn.prepare(R"SQL(
            INSERT INTO sessions (token, user_id, expires_at)
            VALUES (?, ?, datetime('now', '+7 days'));
        )SQL");
        ins.bind_text(1, token);
        ins.bind_int(2, u.id);
        ins.step_done();
        tx.commit();
    }

    auto res = json::json_value::make_object();
    res.add_str("token", token);
    res.add("user", user_to_json(u));
    send_json(worker, header, "200 OK", res);
}

// POST /api/auth/logout
static void handle_auth_logout(backend::wsworker *worker,
                               const std::map<std::string, std::string> &header,
                               const std::list<std::string> &) {
    std::string token = extract_token(worker, header);
    if (!token.empty()) {
        auto conn = acquire_db_connection();
        db::sqlite_transaction tx(conn);
        auto stmt = conn.prepare("DELETE FROM sessions WHERE token = ?;");
        stmt.bind_text(1, token);
        stmt.step_done();
        tx.commit();
    }
    auto res = json::json_value::make_object();
    res.add_bool("ok", true);
    send_json(worker, header, "200 OK", res);
}

// GET /api/auth/me
static void handle_auth_me(backend::wsworker *worker,
                           const std::map<std::string, std::string> &header,
                           const std::list<std::string> &) {
    auto conn = acquire_db_connection();
    auto u = get_authenticated_user(conn, worker, header);
    if (!u.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Not authenticated");
        return;
    }
    send_json(worker, header, "200 OK", user_to_json(u));
}

// GET /api/users & POST /api/users
static void handle_users(backend::wsworker *worker,
                         const std::map<std::string, std::string> &header,
                         const std::list<std::string> &) {
    auto conn = acquire_db_connection();
    std::string method = get_header(header, "REQUEST_METHOD");

    if (method == "GET") {
        auto stmt = conn.prepare(R"SQL(
            SELECT id, username, email, display_name, role, avatar_color, created_at
            FROM users ORDER BY display_name ASC;
        )SQL");
        auto arr = json::json_value::make_array();
        while (stmt.step() == SQLITE_ROW) {
            auto u = json::json_value::make_object();
            u.add_int("id", stmt.column_int(0));
            u.add_str("username", stmt.column_str(1));
            u.add_str("email", stmt.column_str(2));
            u.add_str("display_name", stmt.column_str(3));
            u.add_str("role", stmt.column_str(4));
            u.add_str("avatar_color", stmt.column_str(5));
            u.add_str("created_at", stmt.column_str(6));
            arr.add(std::move(u));
        }
        send_json(worker, header, "200 OK", arr);
        return;
    }

    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    auto body = json::json_value::parse(worker->body());
    if (!body) {
        send_error(worker, header, "400 Bad Request", "Invalid JSON body");
        return;
    }

    std::string uname = body.get_string("username");
    std::string email = body.get_string("email");
    std::string dname = body.get_string("display_name");
    std::string pw = body.get_string("password", "welcome123");
    std::string role = body.get_string("role", "Developer");
    std::string color = body.get_string("avatar_color", "#0052CC");

    if (uname.empty() || dname.empty()) {
        send_error(worker, header, "400 Bad Request", "username and display_name are required");
        return;
    }
    if (email.empty()) email = uname + "@jiralike.local";

    int new_id = 0;
    {
        db::sqlite_transaction tx(conn);
        auto ins = conn.prepare(R"SQL(
            INSERT INTO users (username, email, password_hash, display_name, role, avatar_color)
            VALUES (?, ?, ?, ?, ?, ?);
        )SQL");
        ins.bind_text(1, uname);
        ins.bind_text(2, email);
        ins.bind_text(3, utils::hash_password(pw));
        ins.bind_text(4, dname);
        ins.bind_text(5, role);
        ins.bind_text(6, color);
        ins.step_done();
        new_id = static_cast<int>(conn.last_insert_rowid());
        tx.commit();
    }

    auth_user created{new_id, uname, email, dname, role, color, true};
    send_json(worker, header, "201 Created", user_to_json(created));
}

// GET /api/projects & POST /api/projects
static void handle_projects(backend::wsworker *worker,
                            const std::map<std::string, std::string> &header,
                            const std::list<std::string> &) {
    auto conn = acquire_db_connection();
    std::string method = get_header(header, "REQUEST_METHOD");

    if (method == "GET") {
        auto stmt = conn.prepare(R"SQL(
            SELECT p.id, p.key, p.name, p.description, p.lead_user_id,
                   COALESCE(u.display_name, ''), p.issue_seq, p.created_at,
                   (SELECT COUNT(*) FROM work_items w WHERE w.project_id = p.id) AS total_items,
                   COALESCE(p.issue_type_scheme_id, 1),
                   (SELECT COUNT(*) FROM components c WHERE c.project_id = p.id) AS component_count,
                   (SELECT COUNT(*) FROM versions v WHERE v.project_id = p.id) AS version_count
            FROM projects p
            LEFT JOIN users u ON u.id = p.lead_user_id
            ORDER BY p.id ASC;
        )SQL");
        auto arr = json::json_value::make_array();
        while (stmt.step() == SQLITE_ROW) {
            auto p = json::json_value::make_object();
            p.add_int("id", stmt.column_int(0));
            p.add_str("key", stmt.column_str(1));
            p.add_str("name", stmt.column_str(2));
            p.add_str("description", stmt.column_str(3));
            p.add_nullable_int("lead_user_id", stmt.column_int(4));
            p.add_str("lead_name", stmt.column_str(5));
            p.add_int("issue_seq", stmt.column_int(6));
            p.add_str("created_at", stmt.column_str(7));
            p.add_int("total_items", stmt.column_int(8));
            p.add_int("issue_type_scheme_id", stmt.column_int(9));
            p.add_int("component_count", stmt.column_int(10));
            p.add_int("version_count", stmt.column_int(11));
            arr.add(std::move(p));
        }
        send_json(worker, header, "200 OK", arr);
        return;
    }

    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    auto body = json::json_value::parse(worker->body());
    if (!body) {
        send_error(worker, header, "400 Bad Request", "Invalid JSON body");
        return;
    }

    std::string key = body.get_string("key");
    std::string name = body.get_string("name");
    std::string desc = body.get_string("description");
    int scheme_id = body.get_int("issue_type_scheme_id", 1);
    if (key.empty() || name.empty()) {
        send_error(worker, header, "400 Bad Request", "Project key and name are required");
        return;
    }
    for (char &c : key) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    int pid = 0;
    {
        db::sqlite_transaction tx(conn);
        auto ins = conn.prepare(R"SQL(
            INSERT INTO projects (key, name, description, lead_user_id, issue_type_scheme_id)
            VALUES (?, ?, ?, ?, ?);
        )SQL");
        ins.bind_text(1, key);
        ins.bind_text(2, name);
        ins.bind_text(3, desc);
        ins.bind_int(4, u_auth.id);
        ins.bind_int(5, scheme_id > 0 ? scheme_id : 1);
        ins.step_done();
        pid = static_cast<int>(conn.last_insert_rowid());
        tx.commit();
    }

    auto res = json::json_value::make_object();
    res.add_int("id", pid);
    res.add_str("key", key);
    res.add_str("name", name);
    res.add_str("description", desc);
    res.add_int("issue_type_scheme_id", scheme_id > 0 ? scheme_id : 1);
    send_json(worker, header, "201 Created", res);
}

// GET / POST /api/resolutions
static void handle_resolutions(backend::wsworker *worker,
                               const std::map<std::string, std::string> &header,
                               const std::list<std::string> &) {
    auto conn = acquire_db_connection();
    std::string method = get_header(header, "REQUEST_METHOD");

    if (method == "GET") {
        auto stmt = conn.prepare(R"SQL(
            SELECT r.id, r.name, r.description, r.sort_order, r.is_default, r.created_at,
                   (SELECT COUNT(*) FROM work_items w WHERE w.resolution_id = r.id) AS item_count
            FROM resolutions r
            ORDER BY r.sort_order ASC, r.id ASC;
        )SQL");
        auto arr = json::json_value::make_array();
        while (stmt.step() == SQLITE_ROW) {
            auto r = json::json_value::make_object();
            r.add_int("id", stmt.column_int(0));
            r.add_str("name", stmt.column_str(1));
            r.add_str("description", stmt.column_str(2));
            r.add_int("sort_order", stmt.column_int(3));
            r.add_bool("is_default", stmt.column_int(4) != 0);
            r.add_str("created_at", stmt.column_str(5));
            r.add_int("item_count", stmt.column_int(6));
            arr.add(std::move(r));
        }
        send_json(worker, header, "200 OK", arr);
        return;
    }

    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    auto body = json::json_value::parse(worker->body());
    if (!body) {
        send_error(worker, header, "400 Bad Request", "Invalid JSON body");
        return;
    }

    std::string name = body.get_string("name");
    std::string description = body.get_string("description");
    int sort_order = body.get_int("sort_order", 100);
    bool is_default = body.get_bool("is_default", false);

    if (name.empty()) {
        send_error(worker, header, "400 Bad Request", "Resolution name is required");
        return;
    }

    int new_id = 0;
    try {
        db::sqlite_transaction tx(conn);
        if (is_default) {
            conn.exec("UPDATE resolutions SET is_default = 0;");
        }
        auto ins = conn.prepare(R"SQL(
            INSERT INTO resolutions (name, description, sort_order, is_default)
            VALUES (?, ?, ?, ?);
        )SQL");
        ins.bind_text(1, name);
        ins.bind_text(2, description);
        ins.bind_int(3, sort_order);
        ins.bind_int(4, is_default ? 1 : 0);
        ins.step_done();
        new_id = static_cast<int>(conn.last_insert_rowid());
        tx.commit();
    } catch (const std::exception &ex) {
        send_error(worker, header, "400 Bad Request", ex.what());
        return;
    }

    auto res = json::json_value::make_object();
    res.add_int("id", new_id);
    res.add_str("name", name);
    res.add_str("description", description);
    res.add_int("sort_order", sort_order);
    res.add_bool("is_default", is_default);
    res.add_int("item_count", 0);
    send_json(worker, header, "201 Created", res);
}

// GET / POST /api/issue-types
static void handle_issue_types(backend::wsworker *worker,
                               const std::map<std::string, std::string> &header,
                               const std::list<std::string> &) {
    auto conn = acquire_db_connection();
    std::string method = get_header(header, "REQUEST_METHOD");

    if (method == "GET") {
        auto args = worker->parse_args(get_header(header, "QUERY_STRING"));
        int project_id = std::atoi(args["project_id"].c_str());

        std::string sql;
        if (project_id > 0) {
            sql = R"SQL(
                SELECT it.id, it.name, it.description, it.icon_color, it.is_subtask, it.hierarchy_level, it.created_at
                FROM issue_types it
                INNER JOIN issue_type_scheme_items si ON si.issue_type_id = it.id
                INNER JOIN projects p ON p.issue_type_scheme_id = si.scheme_id
                WHERE p.id = ?
                ORDER BY si.sort_order ASC, it.hierarchy_level DESC, it.id ASC;
            )SQL";
        } else {
            sql = R"SQL(
                SELECT id, name, description, icon_color, is_subtask, hierarchy_level, created_at
                FROM issue_types
                ORDER BY hierarchy_level DESC, id ASC;
            )SQL";
        }

        auto stmt = conn.prepare(sql);
        if (project_id > 0) {
            stmt.bind_int(1, project_id);
        }

        auto arr = json::json_value::make_array();
        while (stmt.step() == SQLITE_ROW) {
            auto t = json::json_value::make_object();
            t.add_int("id", stmt.column_int(0));
            t.add_str("name", stmt.column_str(1));
            t.add_str("description", stmt.column_str(2));
            t.add_str("icon_color", stmt.column_str(3));
            t.add_str("color", stmt.column_str(3));
            t.add_bool("is_subtask", stmt.column_int(4) != 0);
            t.add_int("hierarchy_level", stmt.column_int(5));
            t.add_bool("is_system", stmt.column_int(0) <= 5);
            t.add_str("created_at", stmt.column_str(6));
            arr.add(std::move(t));
        }
        send_json(worker, header, "200 OK", arr);
        return;
    }

    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    auto body = json::json_value::parse(worker->body());
    if (!body) {
        send_error(worker, header, "400 Bad Request", "Invalid JSON body");
        return;
    }

    std::string name = body.get_string("name");
    std::string description = body.get_string("description");
    std::string icon_color = body.get_string("icon_color", "#0052CC");
    bool is_subtask = body.get_bool("is_subtask", false);
    int hierarchy_level = body.get_int("hierarchy_level", is_subtask ? -1 : 0);

    if (name.empty()) {
        send_error(worker, header, "400 Bad Request", "Issue type name is required");
        return;
    }

    int new_id = 0;
    try {
        db::sqlite_transaction tx(conn);
        auto ins = conn.prepare(R"SQL(
            INSERT INTO issue_types (name, description, icon_color, is_subtask, hierarchy_level)
            VALUES (?, ?, ?, ?, ?);
        )SQL");
        ins.bind_text(1, name);
        ins.bind_text(2, description);
        ins.bind_text(3, icon_color);
        ins.bind_int(4, is_subtask ? 1 : 0);
        ins.bind_int(5, hierarchy_level);
        ins.step_done();
        new_id = static_cast<int>(conn.last_insert_rowid());

        // Automatically attach new issue type to the Default scheme (scheme_id = 1)
        auto link_scheme = conn.prepare(R"SQL(
            INSERT OR IGNORE INTO issue_type_scheme_items (scheme_id, issue_type_id, sort_order)
            VALUES (1, ?, ?);
        )SQL");
        link_scheme.bind_int(1, new_id);
        link_scheme.bind_int(2, new_id * 10);
        link_scheme.step_done();

        tx.commit();
    } catch (const std::exception &ex) {
        send_error(worker, header, "400 Bad Request", ex.what());
        return;
    }

    auto res = json::json_value::make_object();
    res.add_int("id", new_id);
    res.add_str("name", name);
    res.add_str("description", description);
    res.add_str("icon_color", icon_color);
    res.add_bool("is_subtask", is_subtask);
    res.add_int("hierarchy_level", hierarchy_level);
    send_json(worker, header, "201 Created", res);
}

// GET / POST /api/issue-type-schemes
static void handle_issue_type_schemes(backend::wsworker *worker,
                                      const std::map<std::string, std::string> &header,
                                      const std::list<std::string> &) {
    auto conn = acquire_db_connection();
    std::string method = get_header(header, "REQUEST_METHOD");

    if (method == "GET") {
        auto stmt = conn.prepare("SELECT id, name, description, is_default, created_at FROM issue_type_schemes ORDER BY id ASC;");
        auto arr = json::json_value::make_array();
        while (stmt.step() == SQLITE_ROW) {
            int sid = stmt.column_int(0);
            auto s = json::json_value::make_object();
            s.add_int("id", sid);
            s.add_str("name", stmt.column_str(1));
            s.add_str("description", stmt.column_str(2));
            s.add_bool("is_default", stmt.column_int(3) != 0);
            s.add_str("created_at", stmt.column_str(4));

            auto it_stmt = conn.prepare(R"SQL(
                SELECT it.id, it.name, it.icon_color, it.is_subtask, it.hierarchy_level
                FROM issue_type_scheme_items si
                INNER JOIN issue_types it ON it.id = si.issue_type_id
                WHERE si.scheme_id = ?
                ORDER BY si.sort_order ASC, it.hierarchy_level DESC;
            )SQL");
            it_stmt.bind_int(1, sid);
            auto types_arr = json::json_value::make_array();
            while (it_stmt.step() == SQLITE_ROW) {
                auto tobj = json::json_value::make_object();
                tobj.add_int("id", it_stmt.column_int(0));
                tobj.add_str("name", it_stmt.column_str(1));
                tobj.add_str("icon_color", it_stmt.column_str(2));
                tobj.add_bool("is_subtask", it_stmt.column_int(3) != 0);
                tobj.add_int("hierarchy_level", it_stmt.column_int(4));
                types_arr.add(std::move(tobj));
            }
            s.add("issue_types", std::move(types_arr));
            arr.add(std::move(s));
        }
        send_json(worker, header, "200 OK", arr);
        return;
    }

    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    auto body = json::json_value::parse(worker->body());
    if (!body) {
        send_error(worker, header, "400 Bad Request", "Invalid JSON body");
        return;
    }

    std::string name = body.get_string("name");
    std::string description = body.get_string("description");
    auto type_ids = body.get_int_array("issue_type_ids");

    if (name.empty()) {
        send_error(worker, header, "400 Bad Request", "Scheme name is required");
        return;
    }

    int scheme_id = 0;
    try {
        db::sqlite_transaction tx(conn);
        auto ins = conn.prepare("INSERT INTO issue_type_schemes (name, description, is_default) VALUES (?, ?, 0);");
        ins.bind_text(1, name);
        ins.bind_text(2, description);
        ins.step_done();
        scheme_id = static_cast<int>(conn.last_insert_rowid());

        auto ins_item = conn.prepare("INSERT OR IGNORE INTO issue_type_scheme_items (scheme_id, issue_type_id, sort_order) VALUES (?, ?, ?);");
        int ord = 10;
        for (int tid : type_ids) {
            ins_item.clear_and_reset();
            ins_item.bind_int(1, scheme_id);
            ins_item.bind_int(2, tid);
            ins_item.bind_int(3, ord);
            ins_item.step_done();
            ord += 10;
        }
        tx.commit();
    } catch (const std::exception &ex) {
        send_error(worker, header, "400 Bad Request", ex.what());
        return;
    }

    auto res = json::json_value::make_object();
    res.add_int("id", scheme_id);
    res.add_str("name", name);
    res.add_str("description", description);
    res.add_bool("is_default", false);
    send_json(worker, header, "201 Created", res);
}

// GET / POST /api/projects/([0-9]+)/components and /api/components
static void handle_components(backend::wsworker *worker,
                              const std::map<std::string, std::string> &header,
                              const std::list<std::string> &uri_params) {
    auto conn = acquire_db_connection();
    std::string method = get_header(header, "REQUEST_METHOD");
    auto args = worker->parse_args(get_header(header, "QUERY_STRING"));

    int project_id = 0;
    if (!uri_params.empty()) {
        project_id = std::atoi(uri_params.front().c_str());
    } else if (!args["project_id"].empty()) {
        project_id = std::atoi(args["project_id"].c_str());
    }

    if (method == "GET") {
        std::string sql = R"SQL(
            SELECT c.id, c.project_id, c.name, c.description, c.lead_user_id,
                   COALESCE(u.display_name, ''), c.default_assignee_type, c.created_at,
                   (SELECT COUNT(*) FROM work_item_components wc WHERE wc.component_id = c.id) AS item_count
            FROM components c
            LEFT JOIN users u ON u.id = c.lead_user_id
        )SQL";
        if (project_id > 0) {
            sql += " WHERE c.project_id = ? ";
        }
        sql += " ORDER BY c.name ASC;";

        auto stmt = conn.prepare(sql);
        if (project_id > 0) {
            stmt.bind_int(1, project_id);
        }

        auto arr = json::json_value::make_array();
        while (stmt.step() == SQLITE_ROW) {
            auto c = json::json_value::make_object();
            c.add_int("id", stmt.column_int(0));
            c.add_int("project_id", stmt.column_int(1));
            c.add_str("name", stmt.column_str(2));
            c.add_str("description", stmt.column_str(3));
            c.add_nullable_int("lead_user_id", stmt.column_int(4));
            c.add_str("lead_name", stmt.column_str(5));
            c.add_str("default_assignee_type", stmt.column_str(6));
            c.add_str("created_at", stmt.column_str(7));
            c.add_int("item_count", stmt.column_int(8));
            arr.add(std::move(c));
        }
        send_json(worker, header, "200 OK", arr);
        return;
    }

    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    auto body = json::json_value::parse(worker->body());
    if (!body) {
        send_error(worker, header, "400 Bad Request", "Invalid JSON body");
        return;
    }

    if (project_id <= 0) {
        project_id = body.get_int("project_id", 1);
    }
    std::string name = body.get_string("name");
    std::string description = body.get_string("description");
    int lead_user_id = body.get_int("lead_user_id", 0);
    std::string default_assignee_type = body.get_string("default_assignee_type", "project_lead");

    if (name.empty()) {
        send_error(worker, header, "400 Bad Request", "Component name is required");
        return;
    }

    int comp_id = 0;
    try {
        db::sqlite_transaction tx(conn);
        auto ins = conn.prepare(R"SQL(
            INSERT INTO components (project_id, name, description, lead_user_id, default_assignee_type)
            VALUES (?, ?, ?, ?, ?);
        )SQL");
        ins.bind_int(1, project_id);
        ins.bind_text(2, name);
        ins.bind_text(3, description);
        ins.bind_optional_int(4, lead_user_id);
        ins.bind_text(5, default_assignee_type);
        ins.step_done();
        comp_id = static_cast<int>(conn.last_insert_rowid());
        tx.commit();
    } catch (const std::exception &ex) {
        send_error(worker, header, "400 Bad Request", ex.what());
        return;
    }

    std::string lead_name;
    if (lead_user_id > 0) {
        auto ustmt = conn.prepare("SELECT display_name FROM users WHERE id = ?;");
        ustmt.bind_int(1, lead_user_id);
        if (ustmt.step() == SQLITE_ROW) lead_name = ustmt.column_str(0);
    }

    auto res = json::json_value::make_object();
    res.add_int("id", comp_id);
    res.add_int("project_id", project_id);
    res.add_str("name", name);
    res.add_str("description", description);
    res.add_nullable_int("lead_user_id", lead_user_id);
    res.add_str("lead_name", lead_name);
    res.add_str("default_assignee_type", default_assignee_type);
    res.add_int("item_count", 0);
    send_json(worker, header, "201 Created", res);
}

// PUT / DELETE /api/components/([0-9]+)
static void handle_component_detail(backend::wsworker *worker,
                                    const std::map<std::string, std::string> &header,
                                    const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing component ID");
        return;
    }
    int comp_id = std::atoi(uri_params.front().c_str());

    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    std::string method = get_header(header, "REQUEST_METHOD");
    if (method == "DELETE") {
        db::sqlite_transaction tx(conn);
        auto del = conn.prepare("DELETE FROM components WHERE id = ?;");
        del.bind_int(1, comp_id);
        del.step_done();
        tx.commit();
        auto res = json::json_value::make_object();
        res.add_bool("deleted", true);
        send_json(worker, header, "200 OK", res);
        return;
    }

    auto body = json::json_value::parse(worker->body());
    if (!body) {
        send_error(worker, header, "400 Bad Request", "Invalid JSON body");
        return;
    }

    std::string name = body.get_string("name");
    std::string description = body.get_string("description");
    int lead_user_id = body.get_int("lead_user_id", 0);
    std::string default_assignee_type = body.get_string("default_assignee_type", "project_lead");

    if (name.empty()) {
        send_error(worker, header, "400 Bad Request", "Component name is required");
        return;
    }

    try {
        db::sqlite_transaction tx(conn);
        auto upd = conn.prepare(R"SQL(
            UPDATE components
            SET name = ?, description = ?, lead_user_id = ?, default_assignee_type = ?
            WHERE id = ?;
        )SQL");
        upd.bind_text(1, name);
        upd.bind_text(2, description);
        upd.bind_optional_int(3, lead_user_id);
        upd.bind_text(4, default_assignee_type);
        upd.bind_int(5, comp_id);
        upd.step_done();
        tx.commit();
    } catch (const std::exception &ex) {
        send_error(worker, header, "400 Bad Request", ex.what());
        return;
    }

    auto res = json::json_value::make_object();
    res.add_int("id", comp_id);
    res.add_str("name", name);
    res.add_str("description", description);
    res.add_nullable_int("lead_user_id", lead_user_id);
    res.add_str("default_assignee_type", default_assignee_type);
    send_json(worker, header, "200 OK", res);
}

// GET / POST /api/projects/([0-9]+)/versions and /api/versions
static void handle_versions(backend::wsworker *worker,
                            const std::map<std::string, std::string> &header,
                            const std::list<std::string> &uri_params) {
    auto conn = acquire_db_connection();
    std::string method = get_header(header, "REQUEST_METHOD");
    auto args = worker->parse_args(get_header(header, "QUERY_STRING"));

    int project_id = 0;
    if (!uri_params.empty()) {
        project_id = std::atoi(uri_params.front().c_str());
    } else if (!args["project_id"].empty()) {
        project_id = std::atoi(args["project_id"].c_str());
    }

    if (method == "GET") {
        std::string sql = R"SQL(
            SELECT v.id, v.project_id, v.name, v.description, v.start_date, v.release_date, v.status, v.created_at,
                   (SELECT COUNT(*) FROM work_item_versions wv WHERE wv.version_id = v.id AND wv.relation_type = 'fix') AS fix_count,
                   (SELECT COUNT(*) FROM work_item_versions wv INNER JOIN work_items w ON w.id = wv.work_item_id WHERE wv.version_id = v.id AND wv.relation_type = 'fix' AND w.status = 'Done') AS done_count,
                   (SELECT COUNT(*) FROM work_item_versions wv WHERE wv.version_id = v.id AND wv.relation_type = 'affects') AS affects_count
            FROM versions v
        )SQL";
        if (project_id > 0) {
            sql += " WHERE v.project_id = ? ";
        }
        sql += " ORDER BY v.id DESC;";

        auto stmt = conn.prepare(sql);
        if (project_id > 0) {
            stmt.bind_int(1, project_id);
        }

        auto arr = json::json_value::make_array();
        while (stmt.step() == SQLITE_ROW) {
            auto v = json::json_value::make_object();
            v.add_int("id", stmt.column_int(0));
            v.add_int("project_id", stmt.column_int(1));
            v.add_str("name", stmt.column_str(2));
            v.add_str("description", stmt.column_str(3));
            v.add_str("start_date", stmt.column_str(4));
            v.add_str("release_date", stmt.column_str(5));
            v.add_str("status", stmt.column_str(6));
            v.add_str("created_at", stmt.column_str(7));
            v.add_int("fix_item_count", stmt.column_int(8));
            v.add_int("done_item_count", stmt.column_int(9));
            v.add_int("affects_item_count", stmt.column_int(10));
            v.add_int("fix_count", stmt.column_int(8));
            v.add_int("fix_done_count", stmt.column_int(9));
            v.add_int("affects_count", stmt.column_int(10));
            arr.add(std::move(v));
        }
        send_json(worker, header, "200 OK", arr);
        return;
    }

    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    auto body = json::json_value::parse(worker->body());
    if (!body) {
        send_error(worker, header, "400 Bad Request", "Invalid JSON body");
        return;
    }

    if (project_id <= 0) {
        project_id = body.get_int("project_id", 1);
    }
    std::string name = body.get_string("name");
    std::string description = body.get_string("description");
    std::string start_date = body.get_string("start_date");
    std::string release_date = body.get_string("release_date");
    std::string status = body.get_string("status", "unreleased");

    if (name.empty()) {
        send_error(worker, header, "400 Bad Request", "Version name is required");
        return;
    }

    int ver_id = 0;
    try {
        db::sqlite_transaction tx(conn);
        auto ins = conn.prepare(R"SQL(
            INSERT INTO versions (project_id, name, description, start_date, release_date, status)
            VALUES (?, ?, ?, ?, ?, ?);
        )SQL");
        ins.bind_int(1, project_id);
        ins.bind_text(2, name);
        ins.bind_text(3, description);
        ins.bind_text(4, start_date);
        ins.bind_text(5, release_date);
        ins.bind_text(6, status);
        ins.step_done();
        ver_id = static_cast<int>(conn.last_insert_rowid());
        tx.commit();
    } catch (const std::exception &ex) {
        send_error(worker, header, "400 Bad Request", ex.what());
        return;
    }

    auto res = json::json_value::make_object();
    res.add_int("id", ver_id);
    res.add_int("project_id", project_id);
    res.add_str("name", name);
    res.add_str("description", description);
    res.add_str("start_date", start_date);
    res.add_str("release_date", release_date);
    res.add_str("status", status);
    res.add_int("fix_item_count", 0);
    res.add_int("done_item_count", 0);
    res.add_int("affects_item_count", 0);
    send_json(worker, header, "201 Created", res);
}

// PUT / DELETE /api/versions/([0-9]+)
static void handle_version_detail(backend::wsworker *worker,
                                  const std::map<std::string, std::string> &header,
                                  const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing version ID");
        return;
    }
    int ver_id = std::atoi(uri_params.front().c_str());

    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    std::string method = get_header(header, "REQUEST_METHOD");
    if (method == "DELETE") {
        db::sqlite_transaction tx(conn);
        auto del = conn.prepare("DELETE FROM versions WHERE id = ?;");
        del.bind_int(1, ver_id);
        del.step_done();
        tx.commit();
        auto res = json::json_value::make_object();
        res.add_bool("deleted", true);
        send_json(worker, header, "200 OK", res);
        return;
    }

    auto body = json::json_value::parse(worker->body());
    if (!body) {
        send_error(worker, header, "400 Bad Request", "Invalid JSON body");
        return;
    }

    std::string name = body.get_string("name");
    std::string description = body.get_string("description");
    std::string start_date = body.get_string("start_date");
    std::string release_date = body.get_string("release_date");
    std::string status = body.get_string("status", "unreleased");

    if (name.empty()) {
        send_error(worker, header, "400 Bad Request", "Version name is required");
        return;
    }

    try {
        db::sqlite_transaction tx(conn);
        auto upd = conn.prepare(R"SQL(
            UPDATE versions
            SET name = ?, description = ?, start_date = ?, release_date = ?, status = ?
            WHERE id = ?;
        )SQL");
        upd.bind_text(1, name);
        upd.bind_text(2, description);
        upd.bind_text(3, start_date);
        upd.bind_text(4, release_date);
        upd.bind_text(5, status);
        upd.bind_int(6, ver_id);
        upd.step_done();
        tx.commit();
    } catch (const std::exception &ex) {
        send_error(worker, header, "400 Bad Request", ex.what());
        return;
    }

    auto res = json::json_value::make_object();
    res.add_int("id", ver_id);
    res.add_str("name", name);
    res.add_str("description", description);
    res.add_str("start_date", start_date);
    res.add_str("release_date", release_date);
    res.add_str("status", status);
    send_json(worker, header, "200 OK", res);
}

// GET /api/versions/([0-9]+)/release-notes
static void handle_version_release_notes(backend::wsworker *worker,
                                         const std::map<std::string, std::string> &header,
                                         const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing version ID");
        return;
    }
    int ver_id = std::atoi(uri_params.front().c_str());

    auto conn = acquire_db_connection();
    std::string ver_name;
    std::string ver_desc;
    std::string ver_date;
    std::string ver_status;
    std::string proj_name;
    {
        auto vstmt = conn.prepare(R"SQL(
            SELECT v.name, v.description, v.release_date, v.status, p.name
            FROM versions v
            INNER JOIN projects p ON p.id = v.project_id
            WHERE v.id = ?;
        )SQL");
        vstmt.bind_int(1, ver_id);
        if (vstmt.step() != SQLITE_ROW) {
            send_error(worker, header, "404 Not Found", "Version not found");
            return;
        }
        ver_name = vstmt.column_str(0);
        ver_desc = vstmt.column_str(1);
        ver_date = vstmt.column_str(2);
        ver_status = vstmt.column_str(3);
        proj_name = vstmt.column_str(4);
    }

    auto istmt = conn.prepare(R"SQL(
        SELECT w.id, w.key, w.type, w.category, w.status, w.priority, w.title,
               COALESCE(r.name, 'Unresolved'), COALESCE(u.display_name, 'Unassigned')
        FROM work_item_versions wv
        INNER JOIN work_items w ON w.id = wv.work_item_id
        LEFT JOIN resolutions r ON r.id = w.resolution_id
        LEFT JOIN users u ON u.id = w.assignee_id
        WHERE wv.version_id = ? AND wv.relation_type = 'fix'
        ORDER BY w.category ASC, w.id ASC;
    )SQL");
    istmt.bind_int(1, ver_id);

    std::map<std::string, std::vector<std::pair<std::string, std::string>>> grouped;
    int total_items = 0;
    auto items_arr = json::json_value::make_array();
    while (istmt.step() == SQLITE_ROW) {
        ++total_items;
        std::string key = istmt.column_str(1);
        std::string type = istmt.column_str(2);
        std::string cat = istmt.column_str(3);
        std::string status = istmt.column_str(4);
        std::string priority = istmt.column_str(5);
        std::string title = istmt.column_str(6);
        std::string res_name = istmt.column_str(7);
        std::string assignee = istmt.column_str(8);

        grouped[cat].emplace_back(key, title + " (" + status + " - " + res_name + ")");

        auto it = json::json_value::make_object();
        it.add_int("id", istmt.column_int(0));
        it.add_str("key", key);
        it.add_str("type", type);
        it.add_str("category", cat);
        it.add_str("status", status);
        it.add_str("priority", priority);
        it.add_str("title", title);
        it.add_str("resolution_name", res_name);
        it.add_str("assignee_name", assignee);
        items_arr.add(std::move(it));
    }

    std::ostringstream md;
    md << "# Release Notes — " << proj_name << " " << ver_name << "\n\n";
    if (!ver_date.empty()) {
        md << "**Release Date:** " << ver_date << "  \n";
    }
    md << "**Status:** " << ver_status << "\n\n";
    if (!ver_desc.empty()) {
        md << ver_desc << "\n\n";
    }
    if (grouped.empty()) {
        md << "_No work items are currently assigned to this Fix Version._\n";
    } else {
        for (const auto &[cat, entries] : grouped) {
            md << "## " << cat << "s\n";
            for (const auto &[k, summary] : entries) {
                md << "- **[" << k << "]** " << summary << "\n";
            }
            md << "\n";
        }
    }

    auto res = json::json_value::make_object();
    res.add_int("version_id", ver_id);
    res.add_str("version_name", ver_name);
    res.add_str("project_name", proj_name);
    res.add_str("release_date", ver_date);
    res.add_str("status", ver_status);
    res.add_int("total_items", total_items);
    res.add_str("markdown", md.str());
    res.add("items", std::move(items_arr));
    send_json(worker, header, "200 OK", res);
}

// =============================================================================
// Step 3: Dynamic Custom Fields Engine, Field Configurations & Screen Schemes
// =============================================================================

static std::string normalize_field_key(const std::string &raw) {
    std::string out;
    out.reserve(raw.size());
    bool last_underscore = false;
    for (unsigned char c : raw) {
        if (std::isalnum(c)) {
            out.push_back(static_cast<char>(std::tolower(c)));
            last_underscore = false;
        } else if (!out.empty() && !last_underscore) {
            out.push_back('_');
            last_underscore = true;
        }
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    return out;
}

static json::json_value load_custom_field_options(db::sqlite_db &conn, int field_id) {
    auto arr = json::json_value::make_array();
    auto ostmt = conn.prepare(R"SQL(
        SELECT id, custom_field_id, COALESCE(parent_option_id, 0), value, color, sort_order, is_active, is_default
        FROM custom_field_options
        WHERE custom_field_id = ?
        ORDER BY sort_order ASC, id ASC;
    )SQL");
    ostmt.bind_int(1, field_id);
    while (ostmt.step() == SQLITE_ROW) {
        auto o = json::json_value::make_object();
        o.add_int("id", ostmt.column_int(0));
        o.add_int("custom_field_id", ostmt.column_int(1));
        o.add_nullable_int("parent_option_id", ostmt.column_int(2));
        o.add_str("value", ostmt.column_str(3));
        o.add_str("color", ostmt.column_str(4));
        o.add_int("sort_order", ostmt.column_int(5));
        o.add_bool("is_active", ostmt.column_int(6) != 0);
        o.add_bool("is_default", ostmt.column_int(7) != 0);
        arr.add(std::move(o));
    }
    return arr;
}

static json::json_value row_to_custom_field_json(db::sqlite_db &conn, db::sqlite_stmt &stmt) {
    int fid = stmt.column_int(0);
    auto cf = json::json_value::make_object();
    cf.add_int("id", fid);
    cf.add_str("name", stmt.column_str(1));
    cf.add_str("field_key", stmt.column_str(2));
    cf.add_str("field_type", stmt.column_str(3));
    cf.add_str("type", stmt.column_str(3));
    cf.add_str("description", stmt.column_str(4));
    cf.add_str("default_value", stmt.column_str(5));
    cf.add_bool("is_required", stmt.column_int(6) != 0);
    cf.add_bool("is_searchable", stmt.column_int(7) != 0);
    if (stmt.column_is_null(8)) {
        cf.add("min_value", json::json_value::make_null());
    } else {
        cf.add_double("min_value", stmt.column_double(8));
    }
    if (stmt.column_is_null(9)) {
        cf.add("max_value", json::json_value::make_null());
    } else {
        cf.add_double("max_value", stmt.column_double(9));
    }
    cf.add_str("formula_expr", stmt.column_str(10));
    cf.add_int("sort_order", stmt.column_int(11));
    cf.add_str("created_at", stmt.column_str(12));
    cf.add("options", load_custom_field_options(conn, fid));
    return cf;
}

// GET | POST /api/custom-fields
static void handle_custom_fields(backend::wsworker *worker,
                                 const std::map<std::string, std::string> &header,
                                 const std::list<std::string> &) {
    auto conn = acquire_db_connection();
    std::string method = get_header(header, "REQUEST_METHOD");

    if (method == "GET") {
        auto stmt = conn.prepare(R"SQL(
            SELECT id, name, field_key, field_type, description, default_value,
                   is_required, is_searchable, min_value, max_value, formula_expr,
                   sort_order, created_at
            FROM custom_fields
            ORDER BY sort_order ASC, id ASC;
        )SQL");
        auto arr = json::json_value::make_array();
        while (stmt.step() == SQLITE_ROW) {
            arr.add(row_to_custom_field_json(conn, stmt));
        }
        send_json(worker, header, "200 OK", arr);
        return;
    }

    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    auto body = json::json_value::parse(worker->body());
    if (!body) {
        send_error(worker, header, "400 Bad Request", "Invalid JSON body");
        return;
    }

    std::string name = body.get_string("name");
    std::string field_key = body.get_string("field_key");
    if (field_key.empty()) {
        field_key = normalize_field_key(name);
    } else {
        field_key = normalize_field_key(field_key);
    }
    std::string field_type = body.get_string("field_type", body.get_string("type", "short_text"));
    std::string description = body.get_string("description");
    std::string default_value = body.get_string("default_value");
    bool is_required = body.get_bool("is_required", false);
    bool is_searchable = body.get_bool("is_searchable", true);
    bool has_min = body.has_key("min_value") && !body.is_null("min_value") && !body.get_string("min_value").empty();
    double min_value = has_min ? body.get_double("min_value", 0.0) : 0.0;
    bool has_max = body.has_key("max_value") && !body.is_null("max_value") && !body.get_string("max_value").empty();
    double max_value = has_max ? body.get_double("max_value", 0.0) : 0.0;
    std::string formula_expr = body.get_string("formula_expr");
    int sort_order = body.get_int("sort_order", 100);
    bool add_to_screens = body.get_bool("add_to_default_screens", true);

    if (name.empty() || field_key.empty()) {
        send_error(worker, header, "400 Bad Request", "Custom field name and field_key are required");
        return;
    }

    int new_field_id = 0;
    try {
        db::sqlite_transaction tx(conn);
        auto ins = conn.prepare(R"SQL(
            INSERT INTO custom_fields (
                name, field_key, field_type, description, default_value,
                is_required, is_searchable, min_value, max_value, formula_expr, sort_order
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
        )SQL");
        ins.bind_text(1, name);
        ins.bind_text(2, field_key);
        ins.bind_text(3, field_type);
        ins.bind_text(4, description);
        ins.bind_text(5, default_value);
        ins.bind_int(6, is_required ? 1 : 0);
        ins.bind_int(7, is_searchable ? 1 : 0);
        if (has_min) ins.bind_double(8, min_value); else ins.bind_null(8);
        if (has_max) ins.bind_double(9, max_value); else ins.bind_null(9);
        ins.bind_text(10, formula_expr);
        ins.bind_int(11, sort_order);
        ins.step_done();
        new_field_id = static_cast<int>(conn.last_insert_rowid());

        // Insert options if provided as string array or object array
        if (body.has_key("options")) {
            json_object *opts_arr = nullptr;
            if (json_object_object_get_ex(body.get(), "options", &opts_arr) &&
                opts_arr && json_object_is_type(opts_arr, json_type_array)) {
                auto oins = conn.prepare(R"SQL(
                    INSERT INTO custom_field_options (custom_field_id, parent_option_id, value, color, sort_order, is_active, is_default)
                    VALUES (?, ?, ?, ?, ?, 1, ?);
                )SQL");
                size_t len = json_object_array_length(opts_arr);
                for (size_t i = 0; i < len; ++i) {
                    json_object *elem = json_object_array_get_idx(opts_arr, i);
                    if (!elem || json_object_is_type(elem, json_type_null)) continue;
                    std::string val;
                    std::string color = "#0052CC";
                    int parent_id = 0;
                    int ord = static_cast<int>((i + 1) * 10);
                    int is_def = (i == 0 ? 1 : 0);

                    if (json_object_is_type(elem, json_type_object)) {
                        json_object *vf = nullptr;
                        if (json_object_object_get_ex(elem, "value", &vf) && vf) {
                            const char *s = json_object_get_string(vf);
                            if (s) val = s;
                        }
                        if (json_object_object_get_ex(elem, "color", &vf) && vf) {
                            const char *s = json_object_get_string(vf);
                            if (s && *s) color = s;
                        }
                        if (json_object_object_get_ex(elem, "parent_option_id", &vf) && vf && !json_object_is_type(vf, json_type_null)) {
                            parent_id = json_object_get_int(vf);
                        }
                        if (json_object_object_get_ex(elem, "sort_order", &vf) && vf) {
                            ord = json_object_get_int(vf);
                        }
                        if (json_object_object_get_ex(elem, "is_default", &vf) && vf) {
                            is_def = json_object_get_boolean(vf) ? 1 : 0;
                        }
                    } else {
                        const char *s = json_object_get_string(elem);
                        if (s) val = s;
                    }

                    if (!val.empty()) {
                        oins.clear_and_reset();
                        oins.bind_int(1, new_field_id);
                        oins.bind_optional_int(2, parent_id);
                        oins.bind_text(3, val);
                        oins.bind_text(4, color);
                        oins.bind_int(5, ord);
                        oins.bind_int(6, is_def);
                        oins.step_done();
                    }
                }
            }
        }

        // Register in default field configuration
        auto fc_ins = conn.prepare(R"SQL(
            INSERT OR IGNORE INTO field_configuration_items (
                configuration_id, custom_field_id, issue_type, is_required, is_hidden, renderer, default_value
            ) VALUES (1, ?, '*', ?, 0, ?, ?);
        )SQL");
        fc_ins.bind_int(1, new_field_id);
        fc_ins.bind_int(2, is_required ? 1 : 0);
        fc_ins.bind_text(3, field_type == "long_text" ? "markdown" : "plain");
        fc_ins.bind_text(4, default_value);
        fc_ins.step_done();

        // Place onto requested screen tabs or default Create & Edit screens
        auto tab_ids = body.get_int_array("screen_tab_ids");
        if (tab_ids.empty() && add_to_screens) {
            // Find first tab of Create screen (1) and Edit/View screen (2)
            auto tstmt = conn.prepare(R"SQL(
                SELECT id FROM screen_tabs
                WHERE screen_id IN (1, 2)
                GROUP BY screen_id
                HAVING MIN(sort_order);
            )SQL");
            while (tstmt.step() == SQLITE_ROW) {
                tab_ids.push_back(tstmt.column_int(0));
            }
        }
        if (!tab_ids.empty()) {
            auto stf_ins = conn.prepare(R"SQL(
                INSERT OR IGNORE INTO screen_tab_fields (tab_id, field_key, custom_field_id, sort_order)
                VALUES (?, ?, ?, ?);
            )SQL");
            for (int tid : tab_ids) {
                stf_ins.clear_and_reset();
                stf_ins.bind_int(1, tid);
                stf_ins.bind_text(2, field_key);
                stf_ins.bind_int(3, new_field_id);
                stf_ins.bind_int(4, sort_order);
                stf_ins.step_done();
            }
        }

        tx.commit();
    } catch (const std::exception &ex) {
        send_error(worker, header, "400 Bad Request", ex.what());
        return;
    }

    auto q = conn.prepare(R"SQL(
        SELECT id, name, field_key, field_type, description, default_value,
               is_required, is_searchable, min_value, max_value, formula_expr,
               sort_order, created_at
        FROM custom_fields WHERE id = ?;
    )SQL");
    q.bind_int(1, new_field_id);
    if (q.step() == SQLITE_ROW) {
        send_json(worker, header, "201 Created", row_to_custom_field_json(conn, q));
    } else {
        send_error(worker, header, "500 Internal Server Error", "Failed to load created custom field");
    }
}

// PUT | DELETE /api/custom-fields/([0-9]+)
static void handle_custom_field_detail(backend::wsworker *worker,
                                       const std::map<std::string, std::string> &header,
                                       const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing custom field ID");
        return;
    }
    int field_id = std::atoi(uri_params.front().c_str());

    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    std::string method = get_header(header, "REQUEST_METHOD");
    if (method == "DELETE") {
        db::sqlite_transaction tx(conn);
        auto del = conn.prepare("DELETE FROM custom_fields WHERE id = ?;");
        del.bind_int(1, field_id);
        del.step_done();
        tx.commit();
        auto res = json::json_value::make_object();
        res.add_bool("deleted", true);
        send_json(worker, header, "200 OK", res);
        return;
    }

    auto body = json::json_value::parse(worker->body());
    if (!body) {
        send_error(worker, header, "400 Bad Request", "Invalid JSON body");
        return;
    }

    std::string cur_name, cur_desc, cur_def, cur_formula;
    int cur_req = 0, cur_search = 1, cur_order = 10;
    bool cur_has_min = false, cur_has_max = false;
    double cur_min = 0.0, cur_max = 0.0;
    {
        auto cur = conn.prepare(R"SQL(
            SELECT name, description, default_value, is_required, is_searchable,
                   min_value, max_value, formula_expr, sort_order
            FROM custom_fields WHERE id = ?;
        )SQL");
        cur.bind_int(1, field_id);
        if (cur.step() != SQLITE_ROW) {
            send_error(worker, header, "404 Not Found", "Custom field not found");
            return;
        }
        cur_name = cur.column_str(0);
        cur_desc = cur.column_str(1);
        cur_def = cur.column_str(2);
        cur_req = cur.column_int(3);
        cur_search = cur.column_int(4);
        cur_has_min = !cur.column_is_null(5);
        cur_min = cur.column_double(5);
        cur_has_max = !cur.column_is_null(6);
        cur_max = cur.column_double(6);
        cur_formula = cur.column_str(7);
        cur_order = cur.column_int(8);
    }

    std::string name = body.has_key("name") ? body.get_string("name") : cur_name;
    std::string desc = body.has_key("description") ? body.get_string("description") : cur_desc;
    std::string def_val = body.has_key("default_value") ? body.get_string("default_value") : cur_def;
    bool is_req = body.has_key("is_required") ? body.get_bool("is_required") : (cur_req != 0);
    bool is_search = body.has_key("is_searchable") ? body.get_bool("is_searchable") : (cur_search != 0);
    bool has_min = body.has_key("min_value") ? (!body.is_null("min_value") && !body.get_string("min_value").empty()) : cur_has_min;
    double min_val = body.has_key("min_value") ? body.get_double("min_value", 0.0) : cur_min;
    bool has_max = body.has_key("max_value") ? (!body.is_null("max_value") && !body.get_string("max_value").empty()) : cur_has_max;
    double max_val = body.has_key("max_value") ? body.get_double("max_value", 0.0) : cur_max;
    std::string formula = body.has_key("formula_expr") ? body.get_string("formula_expr") : cur_formula;
    int sort_order = body.has_key("sort_order") ? body.get_int("sort_order", cur_order) : cur_order;

    try {
        db::sqlite_transaction tx(conn);
        auto upd = conn.prepare(R"SQL(
            UPDATE custom_fields
            SET name = ?, description = ?, default_value = ?, is_required = ?,
                is_searchable = ?, min_value = ?, max_value = ?, formula_expr = ?, sort_order = ?
            WHERE id = ?;
        )SQL");
        upd.bind_text(1, name);
        upd.bind_text(2, desc);
        upd.bind_text(3, def_val);
        upd.bind_int(4, is_req ? 1 : 0);
        upd.bind_int(5, is_search ? 1 : 0);
        if (has_min) upd.bind_double(6, min_val); else upd.bind_null(6);
        if (has_max) upd.bind_double(7, max_val); else upd.bind_null(7);
        upd.bind_text(8, formula);
        upd.bind_int(9, sort_order);
        upd.bind_int(10, field_id);
        upd.step_done();

        if (body.has_key("is_required")) {
            auto fc_upd = conn.prepare(R"SQL(
                UPDATE field_configuration_items
                SET is_required = ?
                WHERE custom_field_id = ? AND issue_type = '*';
            )SQL");
            fc_upd.bind_int(1, is_req ? 1 : 0);
            fc_upd.bind_int(2, field_id);
            fc_upd.step_done();
        }

        tx.commit();
    } catch (const std::exception &ex) {
        send_error(worker, header, "400 Bad Request", ex.what());
        return;
    }

    auto q = conn.prepare(R"SQL(
        SELECT id, name, field_key, field_type, description, default_value,
               is_required, is_searchable, min_value, max_value, formula_expr,
               sort_order, created_at
        FROM custom_fields WHERE id = ?;
    )SQL");
    q.bind_int(1, field_id);
    if (q.step() == SQLITE_ROW) {
        send_json(worker, header, "200 OK", row_to_custom_field_json(conn, q));
    } else {
        send_error(worker, header, "404 Not Found", "Custom field not found");
    }
}

// POST /api/custom-fields/([0-9]+)/options
static void handle_custom_field_options(backend::wsworker *worker,
                                        const std::map<std::string, std::string> &header,
                                        const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing custom field ID");
        return;
    }
    int field_id = std::atoi(uri_params.front().c_str());

    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    auto body = json::json_value::parse(worker->body());
    if (!body) {
        send_error(worker, header, "400 Bad Request", "Invalid JSON body");
        return;
    }

    std::string val = body.get_string("value");
    std::string color = body.get_string("color", "#0052CC");
    int parent_id = body.get_int("parent_option_id", 0);
    int sort_order = body.get_int("sort_order", 10);
    bool is_default = body.get_bool("is_default", false);

    if (val.empty()) {
        send_error(worker, header, "400 Bad Request", "Option value is required");
        return;
    }

    int opt_id = 0;
    try {
        db::sqlite_transaction tx(conn);
        auto ins = conn.prepare(R"SQL(
            INSERT INTO custom_field_options (custom_field_id, parent_option_id, value, color, sort_order, is_active, is_default)
            VALUES (?, ?, ?, ?, ?, 1, ?);
        )SQL");
        ins.bind_int(1, field_id);
        ins.bind_optional_int(2, parent_id);
        ins.bind_text(3, val);
        ins.bind_text(4, color);
        ins.bind_int(5, sort_order);
        ins.bind_int(6, is_default ? 1 : 0);
        ins.step_done();
        opt_id = static_cast<int>(conn.last_insert_rowid());
        tx.commit();
    } catch (const std::exception &ex) {
        send_error(worker, header, "400 Bad Request", ex.what());
        return;
    }

    auto res = json::json_value::make_object();
    res.add_int("id", opt_id);
    res.add_int("custom_field_id", field_id);
    res.add_nullable_int("parent_option_id", parent_id);
    res.add_str("value", val);
    res.add_str("color", color);
    res.add_int("sort_order", sort_order);
    res.add_bool("is_active", true);
    res.add_bool("is_default", is_default);
    send_json(worker, header, "201 Created", res);
}

// DELETE /api/custom-field-options/([0-9]+)
static void handle_delete_custom_field_option(backend::wsworker *worker,
                                              const std::map<std::string, std::string> &header,
                                              const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing option ID");
        return;
    }
    int opt_id = std::atoi(uri_params.front().c_str());

    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    {
        db::sqlite_transaction tx(conn);
        auto del = conn.prepare("DELETE FROM custom_field_options WHERE id = ?;");
        del.bind_int(1, opt_id);
        del.step_done();
        tx.commit();
    }

    auto res = json::json_value::make_object();
    res.add_bool("deleted", true);
    send_json(worker, header, "200 OK", res);
}

// GET | POST /api/field-configurations
static void handle_field_configurations(backend::wsworker *worker,
                                        const std::map<std::string, std::string> &header,
                                        const std::list<std::string> &) {
    auto conn = acquire_db_connection();
    std::string method = get_header(header, "REQUEST_METHOD");

    if (method == "GET") {
        auto stmt = conn.prepare("SELECT id, name, description, is_default, created_at FROM field_configurations ORDER BY id ASC;");
        auto arr = json::json_value::make_array();
        while (stmt.step() == SQLITE_ROW) {
            int cfg_id = stmt.column_int(0);
            auto cfg = json::json_value::make_object();
            cfg.add_int("id", cfg_id);
            cfg.add_str("name", stmt.column_str(1));
            cfg.add_str("description", stmt.column_str(2));
            cfg.add_bool("is_default", stmt.column_int(3) != 0);
            cfg.add_str("created_at", stmt.column_str(4));

            auto istmt = conn.prepare(R"SQL(
                SELECT cf.id, cf.name, cf.field_key, cf.field_type,
                       COALESCE(fci.id, 0),
                       COALESCE(fci.issue_type, '*'),
                       COALESCE(fci.is_required, cf.is_required),
                       COALESCE(fci.is_hidden, 0),
                       COALESCE(fci.renderer, CASE WHEN cf.field_type = 'long_text' THEN 'markdown' ELSE 'plain' END),
                       COALESCE(NULLIF(fci.default_value, ''), cf.default_value),
                       COALESCE(fci.description_override, '')
                FROM custom_fields cf
                LEFT JOIN field_configuration_items fci
                  ON fci.custom_field_id = cf.id AND fci.configuration_id = ?
                ORDER BY cf.sort_order ASC, cf.id ASC;
            )SQL");
            istmt.bind_int(1, cfg_id);
            auto items = json::json_value::make_array();
            while (istmt.step() == SQLITE_ROW) {
                auto it = json::json_value::make_object();
                it.add_int("custom_field_id", istmt.column_int(0));
                it.add_str("field_name", istmt.column_str(1));
                it.add_str("field_key", istmt.column_str(2));
                it.add_str("field_type", istmt.column_str(3));
                it.add_int("id", istmt.column_int(4));
                it.add_int("configuration_id", cfg_id);
                it.add_str("issue_type", istmt.column_str(5));
                it.add_bool("is_required", istmt.column_int(6) != 0);
                it.add_bool("is_hidden", istmt.column_int(7) != 0);
                it.add_str("renderer", istmt.column_str(8));
                it.add_str("default_value", istmt.column_str(9));
                it.add_str("description_override", istmt.column_str(10));
                items.add(std::move(it));
            }
            cfg.add("items", std::move(items));
            arr.add(std::move(cfg));
        }
        send_json(worker, header, "200 OK", arr);
        return;
    }

    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    auto body = json::json_value::parse(worker->body());
    if (!body) {
        send_error(worker, header, "400 Bad Request", "Invalid JSON body");
        return;
    }

    // If custom_field_id is provided, upsert field_configuration_items rule
    if (body.has_key("custom_field_id")) {
        int cfg_id = body.get_int("configuration_id", 1);
        int cf_id = body.get_int("custom_field_id", 0);
        std::string issue_type = body.get_string("issue_type", "*");
        if (issue_type.empty()) issue_type = "*";
        bool is_required = body.get_bool("is_required", false);
        bool is_hidden = body.get_bool("is_hidden", false);
        std::string renderer = body.get_string("renderer", "plain");
        std::string def_val = body.get_string("default_value");
        std::string desc_override = body.get_string("description_override");

        try {
            db::sqlite_transaction tx(conn);
            auto ups = conn.prepare(R"SQL(
                INSERT INTO field_configuration_items (
                    configuration_id, custom_field_id, issue_type, is_required, is_hidden, renderer, default_value, description_override
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?)
                ON CONFLICT(configuration_id, custom_field_id, issue_type) DO UPDATE SET
                    is_required = excluded.is_required,
                    is_hidden = excluded.is_hidden,
                    renderer = excluded.renderer,
                    default_value = excluded.default_value,
                    description_override = excluded.description_override;
            )SQL");
            ups.bind_int(1, cfg_id);
            ups.bind_int(2, cf_id);
            ups.bind_text(3, issue_type);
            ups.bind_int(4, is_required ? 1 : 0);
            ups.bind_int(5, is_hidden ? 1 : 0);
            ups.bind_text(6, renderer);
            ups.bind_text(7, def_val);
            ups.bind_text(8, desc_override);
            ups.step_done();

            if (issue_type == "*") {
                auto cf_upd = conn.prepare("UPDATE custom_fields SET is_required = ? WHERE id = ?;");
                cf_upd.bind_int(1, is_required ? 1 : 0);
                cf_upd.bind_int(2, cf_id);
                cf_upd.step_done();
            }
            tx.commit();
        } catch (const std::exception &ex) {
            send_error(worker, header, "400 Bad Request", ex.what());
            return;
        }

        auto res = json::json_value::make_object();
        res.add_bool("ok", true);
        res.add_int("configuration_id", cfg_id);
        res.add_int("custom_field_id", cf_id);
        res.add_str("issue_type", issue_type);
        res.add_bool("is_required", is_required);
        res.add_bool("is_hidden", is_hidden);
        res.add_str("renderer", renderer);
        send_json(worker, header, "200 OK", res);
        return;
    }

    // Create a new Field Configuration container
    std::string name = body.get_string("name");
    std::string description = body.get_string("description");
    bool is_default = body.get_bool("is_default", false);
    if (name.empty()) {
        send_error(worker, header, "400 Bad Request", "Field configuration name is required");
        return;
    }

    int new_cfg_id = 0;
    try {
        db::sqlite_transaction tx(conn);
        auto ins = conn.prepare("INSERT INTO field_configurations (name, description, is_default) VALUES (?, ?, ?);");
        ins.bind_text(1, name);
        ins.bind_text(2, description);
        ins.bind_int(3, is_default ? 1 : 0);
        ins.step_done();
        new_cfg_id = static_cast<int>(conn.last_insert_rowid());
        tx.commit();
    } catch (const std::exception &ex) {
        send_error(worker, header, "400 Bad Request", ex.what());
        return;
    }

    auto res = json::json_value::make_object();
    res.add_int("id", new_cfg_id);
    res.add_str("name", name);
    res.add_str("description", description);
    res.add_bool("is_default", is_default);
    send_json(worker, header, "201 Created", res);
}

static json::json_value build_screen_json(db::sqlite_db &conn,
                                          int screen_id,
                                          int config_id = 1,
                                          const std::string &issue_type = "*",
                                          bool filter_hidden = false) {
    auto sstmt = conn.prepare("SELECT id, name, description, screen_type, created_at FROM screens WHERE id = ?;");
    sstmt.bind_int(1, screen_id);
    if (sstmt.step() != SQLITE_ROW) {
        return json::json_value::make_null();
    }

    auto sc = json::json_value::make_object();
    sc.add_int("id", sstmt.column_int(0));
    sc.add_str("name", sstmt.column_str(1));
    sc.add_str("description", sstmt.column_str(2));
    sc.add_str("screen_type", sstmt.column_str(3));
    sc.add_str("created_at", sstmt.column_str(4));

    auto tstmt = conn.prepare("SELECT id, name, sort_order FROM screen_tabs WHERE screen_id = ? ORDER BY sort_order ASC, id ASC;");
    tstmt.bind_int(1, screen_id);
    auto tabs_arr = json::json_value::make_array();

    while (tstmt.step() == SQLITE_ROW) {
        int tab_id = tstmt.column_int(0);
        auto tab = json::json_value::make_object();
        tab.add_int("id", tab_id);
        tab.add_int("screen_id", screen_id);
        tab.add_str("name", tstmt.column_str(1));
        tab.add_int("sort_order", tstmt.column_int(2));

        auto fstmt = conn.prepare(R"SQL(
            SELECT stf.id, stf.tab_id, stf.field_key, COALESCE(stf.custom_field_id, cf.id, 0), stf.sort_order,
                   COALESCE(cf.name, stf.field_key),
                   COALESCE(cf.field_type, 'short_text'),
                   COALESCE(NULLIF(fci.description_override, ''), cf.description, ''),
                   COALESCE(fci.is_required, cf.is_required, 0),
                   COALESCE(fci.is_hidden, 0),
                   COALESCE(fci.renderer, CASE WHEN cf.field_type = 'long_text' THEN 'markdown' ELSE 'plain' END),
                   COALESCE(NULLIF(fci.default_value, ''), cf.default_value, ''),
                   cf.min_value,
                   cf.max_value,
                   COALESCE(cf.formula_expr, '')
            FROM screen_tab_fields stf
            LEFT JOIN custom_fields cf ON (cf.id = stf.custom_field_id OR cf.field_key = stf.field_key)
            LEFT JOIN field_configuration_items fci
              ON fci.custom_field_id = cf.id
             AND fci.configuration_id = ?
             AND fci.issue_type = COALESCE(
                   (SELECT issue_type FROM field_configuration_items
                    WHERE configuration_id = ? AND custom_field_id = cf.id AND issue_type IN (?, '*')
                    ORDER BY CASE WHEN issue_type = ? THEN 0 ELSE 1 END LIMIT 1),
                   '*'
                 )
            WHERE stf.tab_id = ?
            ORDER BY stf.sort_order ASC, stf.id ASC;
        )SQL");
        fstmt.bind_int(1, config_id);
        fstmt.bind_int(2, config_id);
        fstmt.bind_text(3, issue_type);
        fstmt.bind_text(4, issue_type);
        fstmt.bind_int(5, tab_id);

        auto fields_arr = json::json_value::make_array();
        while (fstmt.step() == SQLITE_ROW) {
            bool is_hidden = fstmt.column_int(9) != 0;
            if (filter_hidden && is_hidden) continue;

            int cf_id = fstmt.column_int(3);
            auto fo = json::json_value::make_object();
            fo.add_int("id", fstmt.column_int(0));
            fo.add_int("tab_id", fstmt.column_int(1));
            fo.add_str("field_key", fstmt.column_str(2));
            fo.add_nullable_int("custom_field_id", cf_id);
            fo.add_int("sort_order", fstmt.column_int(4));
            fo.add_str("name", fstmt.column_str(5));
            fo.add_str("field_type", fstmt.column_str(6));
            fo.add_str("description", fstmt.column_str(7));
            fo.add_bool("is_required", fstmt.column_int(8) != 0);
            fo.add_bool("is_hidden", is_hidden);
            fo.add_str("renderer", fstmt.column_str(10));
            fo.add_str("default_value", fstmt.column_str(11));
            if (fstmt.column_is_null(12)) {
                fo.add("min_value", json::json_value::make_null());
            } else {
                fo.add_double("min_value", fstmt.column_double(12));
            }
            if (fstmt.column_is_null(13)) {
                fo.add("max_value", json::json_value::make_null());
            } else {
                fo.add_double("max_value", fstmt.column_double(13));
            }
            fo.add_str("formula_expr", fstmt.column_str(14));
            if (cf_id > 0) {
                fo.add("options", load_custom_field_options(conn, cf_id));
            } else {
                fo.add("options", json::json_value::make_array());
            }
            fields_arr.add(std::move(fo));
        }
        tab.add("fields", std::move(fields_arr));
        tabs_arr.add(std::move(tab));
    }

    sc.add("tabs", std::move(tabs_arr));
    return sc;
}

// GET | POST /api/screens
static void handle_screens(backend::wsworker *worker,
                           const std::map<std::string, std::string> &header,
                           const std::list<std::string> &) {
    auto conn = acquire_db_connection();
    std::string method = get_header(header, "REQUEST_METHOD");

    if (method == "GET") {
        auto stmt = conn.prepare("SELECT id FROM screens ORDER BY id ASC;");
        auto arr = json::json_value::make_array();
        while (stmt.step() == SQLITE_ROW) {
            arr.add(build_screen_json(conn, stmt.column_int(0)));
        }
        send_json(worker, header, "200 OK", arr);
        return;
    }

    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    auto body = json::json_value::parse(worker->body());
    if (!body) {
        send_error(worker, header, "400 Bad Request", "Invalid JSON body");
        return;
    }

    std::string name = body.get_string("name");
    std::string description = body.get_string("description");
    std::string screen_type = body.get_string("screen_type", "default");
    if (name.empty()) {
        send_error(worker, header, "400 Bad Request", "Screen name is required");
        return;
    }

    int screen_id = 0;
    try {
        db::sqlite_transaction tx(conn);
        auto ins = conn.prepare("INSERT INTO screens (name, description, screen_type) VALUES (?, ?, ?);");
        ins.bind_text(1, name);
        ins.bind_text(2, description);
        ins.bind_text(3, screen_type);
        ins.step_done();
        screen_id = static_cast<int>(conn.last_insert_rowid());

        auto tab_names = body.get_string_array("tab_names");
        if (tab_names.empty()) {
            tab_names.push_back("General");
        }
        auto tins = conn.prepare("INSERT INTO screen_tabs (screen_id, name, sort_order) VALUES (?, ?, ?);");
        int ord = 10;
        for (const auto &tname : tab_names) {
            if (tname.empty()) continue;
            tins.clear_and_reset();
            tins.bind_int(1, screen_id);
            tins.bind_text(2, tname);
            tins.bind_int(3, ord);
            tins.step_done();
            ord += 10;
        }
        tx.commit();
    } catch (const std::exception &ex) {
        send_error(worker, header, "400 Bad Request", ex.what());
        return;
    }

    send_json(worker, header, "201 Created", build_screen_json(conn, screen_id));
}

// PUT | DELETE /api/screens/([0-9]+)
static void handle_screen_detail(backend::wsworker *worker,
                                 const std::map<std::string, std::string> &header,
                                 const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing screen ID");
        return;
    }
    int screen_id = std::atoi(uri_params.front().c_str());

    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    std::string method = get_header(header, "REQUEST_METHOD");
    if (method == "DELETE") {
        db::sqlite_transaction tx(conn);
        auto del = conn.prepare("DELETE FROM screens WHERE id = ?;");
        del.bind_int(1, screen_id);
        del.step_done();
        tx.commit();
        auto res = json::json_value::make_object();
        res.add_bool("deleted", true);
        send_json(worker, header, "200 OK", res);
        return;
    }

    auto body = json::json_value::parse(worker->body());
    if (!body) {
        send_error(worker, header, "400 Bad Request", "Invalid JSON body");
        return;
    }

    std::string name = body.get_string("name");
    std::string description = body.get_string("description");
    std::string screen_type = body.get_string("screen_type", "default");
    if (name.empty()) {
        send_error(worker, header, "400 Bad Request", "Screen name is required");
        return;
    }

    try {
        db::sqlite_transaction tx(conn);
        auto upd = conn.prepare("UPDATE screens SET name = ?, description = ?, screen_type = ? WHERE id = ?;");
        upd.bind_text(1, name);
        upd.bind_text(2, description);
        upd.bind_text(3, screen_type);
        upd.bind_int(4, screen_id);
        upd.step_done();
        tx.commit();
    } catch (const std::exception &ex) {
        send_error(worker, header, "400 Bad Request", ex.what());
        return;
    }

    send_json(worker, header, "200 OK", build_screen_json(conn, screen_id));
}

// POST /api/screens/([0-9]+)/tabs
static void handle_screen_tabs(backend::wsworker *worker,
                               const std::map<std::string, std::string> &header,
                               const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing screen ID");
        return;
    }
    int screen_id = std::atoi(uri_params.front().c_str());

    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    auto body = json::json_value::parse(worker->body());
    if (!body) {
        send_error(worker, header, "400 Bad Request", "Invalid JSON body");
        return;
    }

    std::string name = body.get_string("name");
    int sort_order = body.get_int("sort_order", 50);
    if (name.empty()) {
        send_error(worker, header, "400 Bad Request", "Tab name is required");
        return;
    }

    int tab_id = 0;
    try {
        db::sqlite_transaction tx(conn);
        auto ins = conn.prepare("INSERT INTO screen_tabs (screen_id, name, sort_order) VALUES (?, ?, ?);");
        ins.bind_int(1, screen_id);
        ins.bind_text(2, name);
        ins.bind_int(3, sort_order);
        ins.step_done();
        tab_id = static_cast<int>(conn.last_insert_rowid());
        tx.commit();
    } catch (const std::exception &ex) {
        send_error(worker, header, "400 Bad Request", ex.what());
        return;
    }

    auto res = json::json_value::make_object();
    res.add_int("id", tab_id);
    res.add_int("screen_id", screen_id);
    res.add_str("name", name);
    res.add_int("sort_order", sort_order);
    res.add("fields", json::json_value::make_array());
    send_json(worker, header, "201 Created", res);
}

// DELETE /api/screen-tabs/([0-9]+)
static void handle_delete_screen_tab(backend::wsworker *worker,
                                     const std::map<std::string, std::string> &header,
                                     const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing tab ID");
        return;
    }
    int tab_id = std::atoi(uri_params.front().c_str());

    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    {
        db::sqlite_transaction tx(conn);
        auto del = conn.prepare("DELETE FROM screen_tabs WHERE id = ?;");
        del.bind_int(1, tab_id);
        del.step_done();
        tx.commit();
    }

    auto res = json::json_value::make_object();
    res.add_bool("deleted", true);
    send_json(worker, header, "200 OK", res);
}

// POST /api/screen-tabs/([0-9]+)/fields
static void handle_screen_tab_fields(backend::wsworker *worker,
                                     const std::map<std::string, std::string> &header,
                                     const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing tab ID");
        return;
    }
    int tab_id = std::atoi(uri_params.front().c_str());

    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    auto body = json::json_value::parse(worker->body());
    if (!body) {
        send_error(worker, header, "400 Bad Request", "Invalid JSON body");
        return;
    }

    int custom_field_id = body.get_int("custom_field_id", 0);
    std::string field_key = body.get_string("field_key");
    int sort_order = body.get_int("sort_order", 10);

    if (custom_field_id > 0 && field_key.empty()) {
        auto q = conn.prepare("SELECT field_key FROM custom_fields WHERE id = ?;");
        q.bind_int(1, custom_field_id);
        if (q.step() == SQLITE_ROW) {
            field_key = q.column_str(0);
        }
    } else if (custom_field_id <= 0 && !field_key.empty()) {
        auto q = conn.prepare("SELECT id FROM custom_fields WHERE field_key = ?;");
        q.bind_text(1, field_key);
        if (q.step() == SQLITE_ROW) {
            custom_field_id = q.column_int(0);
        }
    }

    if (field_key.empty()) {
        send_error(worker, header, "400 Bad Request", "custom_field_id or field_key is required");
        return;
    }

    int stf_id = 0;
    try {
        db::sqlite_transaction tx(conn);
        auto ins = conn.prepare(R"SQL(
            INSERT INTO screen_tab_fields (tab_id, field_key, custom_field_id, sort_order)
            VALUES (?, ?, ?, ?)
            ON CONFLICT(tab_id, field_key) DO UPDATE SET
                sort_order = excluded.sort_order;
        )SQL");
        ins.bind_int(1, tab_id);
        ins.bind_text(2, field_key);
        ins.bind_optional_int(3, custom_field_id);
        ins.bind_int(4, sort_order);
        ins.step_done();
        stf_id = static_cast<int>(conn.last_insert_rowid());
        tx.commit();
    } catch (const std::exception &ex) {
        send_error(worker, header, "400 Bad Request", ex.what());
        return;
    }

    auto res = json::json_value::make_object();
    res.add_int("id", stf_id);
    res.add_int("tab_id", tab_id);
    res.add_str("field_key", field_key);
    res.add_nullable_int("custom_field_id", custom_field_id);
    res.add_int("sort_order", sort_order);
    send_json(worker, header, "201 Created", res);
}

// DELETE /api/screen-tab-fields/([0-9]+)
static void handle_delete_screen_tab_field(backend::wsworker *worker,
                                           const std::map<std::string, std::string> &header,
                                           const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing screen tab field ID");
        return;
    }
    int stf_id = std::atoi(uri_params.front().c_str());

    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    {
        db::sqlite_transaction tx(conn);
        auto del = conn.prepare("DELETE FROM screen_tab_fields WHERE id = ?;");
        del.bind_int(1, stf_id);
        del.step_done();
        tx.commit();
    }

    auto res = json::json_value::make_object();
    res.add_bool("deleted", true);
    send_json(worker, header, "200 OK", res);
}

// GET | POST /api/screen-schemes
static void handle_screen_schemes(backend::wsworker *worker,
                                  const std::map<std::string, std::string> &header,
                                  const std::list<std::string> &) {
    auto conn = acquire_db_connection();
    std::string method = get_header(header, "REQUEST_METHOD");

    if (method == "GET") {
        auto stmt = conn.prepare(R"SQL(
            SELECT ss.id, ss.name, ss.description, COALESCE(ss.project_id, 0), ss.is_default,
                   ss.create_screen_id, COALESCE(s1.name, ''),
                   ss.edit_screen_id,   COALESCE(s2.name, ''),
                   ss.view_screen_id,   COALESCE(s3.name, ''),
                   ss.transition_screen_id, COALESCE(s4.name, ''),
                   ss.created_at
            FROM screen_schemes ss
            LEFT JOIN screens s1 ON s1.id = ss.create_screen_id
            LEFT JOIN screens s2 ON s2.id = ss.edit_screen_id
            LEFT JOIN screens s3 ON s3.id = ss.view_screen_id
            LEFT JOIN screens s4 ON s4.id = ss.transition_screen_id
            ORDER BY ss.is_default DESC, ss.id ASC;
        )SQL");
        auto arr = json::json_value::make_array();
        while (stmt.step() == SQLITE_ROW) {
            auto ss = json::json_value::make_object();
            ss.add_int("id", stmt.column_int(0));
            ss.add_str("name", stmt.column_str(1));
            ss.add_str("description", stmt.column_str(2));
            ss.add_nullable_int("project_id", stmt.column_int(3));
            ss.add_bool("is_default", stmt.column_int(4) != 0);
            ss.add_int("create_screen_id", stmt.column_int(5));
            ss.add_str("create_screen_name", stmt.column_str(6));
            ss.add_int("edit_screen_id", stmt.column_int(7));
            ss.add_str("edit_screen_name", stmt.column_str(8));
            ss.add_int("view_screen_id", stmt.column_int(9));
            ss.add_str("view_screen_name", stmt.column_str(10));
            ss.add_int("transition_screen_id", stmt.column_int(11));
            ss.add_str("transition_screen_name", stmt.column_str(12));
            ss.add_str("created_at", stmt.column_str(13));
            arr.add(std::move(ss));
        }
        send_json(worker, header, "200 OK", arr);
        return;
    }

    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    auto body = json::json_value::parse(worker->body());
    if (!body) {
        send_error(worker, header, "400 Bad Request", "Invalid JSON body");
        return;
    }

    int id = body.get_int("id", 0);
    std::string name = body.get_string("name");
    std::string description = body.get_string("description");
    int project_id = body.get_int("project_id", 0);
    bool is_default = body.get_bool("is_default", false);
    int create_screen_id = body.get_int("create_screen_id", 1);
    int edit_screen_id = body.get_int("edit_screen_id", 2);
    int view_screen_id = body.get_int("view_screen_id", 2);
    int transition_screen_id = body.get_int("transition_screen_id", 3);

    try {
        db::sqlite_transaction tx(conn);
        if (id > 0) {
            auto upd = conn.prepare(R"SQL(
                UPDATE screen_schemes
                SET name = COALESCE(NULLIF(?, ''), name),
                    description = ?,
                    project_id = ?,
                    create_screen_id = ?,
                    edit_screen_id = ?,
                    view_screen_id = ?,
                    transition_screen_id = ?
                WHERE id = ?;
            )SQL");
            upd.bind_text(1, name);
            upd.bind_text(2, description);
            upd.bind_optional_int(3, project_id);
            upd.bind_int(4, create_screen_id);
            upd.bind_int(5, edit_screen_id);
            upd.bind_int(6, view_screen_id);
            upd.bind_int(7, transition_screen_id);
            upd.bind_int(8, id);
            upd.step_done();
        } else {
            if (name.empty()) {
                send_error(worker, header, "400 Bad Request", "Screen scheme name is required");
                return;
            }
            auto ins = conn.prepare(R"SQL(
                INSERT INTO screen_schemes (
                    name, description, project_id, is_default,
                    create_screen_id, edit_screen_id, view_screen_id, transition_screen_id
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?);
            )SQL");
            ins.bind_text(1, name);
            ins.bind_text(2, description);
            ins.bind_optional_int(3, project_id);
            ins.bind_int(4, is_default ? 1 : 0);
            ins.bind_int(5, create_screen_id);
            ins.bind_int(6, edit_screen_id);
            ins.bind_int(7, view_screen_id);
            ins.bind_int(8, transition_screen_id);
            ins.step_done();
            id = static_cast<int>(conn.last_insert_rowid());
        }
        tx.commit();
    } catch (const std::exception &ex) {
        send_error(worker, header, "400 Bad Request", ex.what());
        return;
    }

    auto res = json::json_value::make_object();
    res.add_int("id", id);
    res.add_str("name", name);
    res.add_str("description", description);
    res.add_nullable_int("project_id", project_id);
    res.add_bool("is_default", is_default);
    res.add_int("create_screen_id", create_screen_id);
    res.add_int("edit_screen_id", edit_screen_id);
    res.add_int("view_screen_id", view_screen_id);
    res.add_int("transition_screen_id", transition_screen_id);
    send_json(worker, header, "200 OK", res);
}

// GET /api/screens/resolve?project_id=1&operation=create|edit|view|transition&issue_type=Story
static void handle_resolve_screen(backend::wsworker *worker,
                                  const std::map<std::string, std::string> &header,
                                  const std::list<std::string> &) {
    auto conn = acquire_db_connection();
    auto args = worker->parse_args(get_header(header, "QUERY_STRING"));
    int project_id = std::atoi(args["project_id"].c_str());
    if (project_id <= 0) project_id = 1;
    std::string operation = args["operation"];
    if (operation.empty()) operation = "view";
    std::string issue_type = args["issue_type"];
    if (issue_type.empty()) issue_type = "*";
    bool include_hidden = (args["include_hidden"] == "1" || args["include_hidden"] == "true");

    int screen_id = 2;
    {
        auto ss = conn.prepare(R"SQL(
            SELECT create_screen_id, edit_screen_id, view_screen_id, transition_screen_id
            FROM screen_schemes
            WHERE project_id = ? OR is_default = 1
            ORDER BY CASE WHEN project_id = ? THEN 0 ELSE 1 END, id ASC
            LIMIT 1;
        )SQL");
        ss.bind_int(1, project_id);
        ss.bind_int(2, project_id);
        if (ss.step() == SQLITE_ROW) {
            if (operation == "create") screen_id = ss.column_int(0);
            else if (operation == "edit") screen_id = ss.column_int(1);
            else if (operation == "transition") screen_id = ss.column_int(3);
            else screen_id = ss.column_int(2);
        }
    }

    int config_id = 1;
    {
        auto fcs = conn.prepare(R"SQL(
            SELECT configuration_id
            FROM field_configuration_schemes
            WHERE project_id = ? OR is_default = 1
            ORDER BY CASE WHEN project_id = ? THEN 0 ELSE 1 END, id ASC
            LIMIT 1;
        )SQL");
        fcs.bind_int(1, project_id);
        fcs.bind_int(2, project_id);
        if (fcs.step() == SQLITE_ROW) {
            config_id = fcs.column_int(0);
        }
    }

    auto resolved = build_screen_json(conn, screen_id, config_id, issue_type, !include_hidden);
    if (!resolved) {
        send_error(worker, header, "404 Not Found", "Resolved screen not found");
        return;
    }
    resolved.add_str("operation", operation);
    resolved.add_int("project_id", project_id);
    resolved.add_str("issue_type", issue_type);
    resolved.add_int("configuration_id", config_id);
    send_json(worker, header, "200 OK", resolved);
}

// ============================================================================
// Step 4: Directed Graph Workflow Engine, Statuses, Transitions, Rules & Schemes
// ============================================================================

static json::json_value load_transition_rules_json(db::sqlite_db &conn,
                                                   int transition_id,
                                                   const std::string &category_filter = "") {
    std::string sql = R"SQL(
        SELECT id, transition_id, rule_category, rule_type, field_name, config_value, description, sort_order
        FROM workflow_transition_rules
        WHERE transition_id = ?
    )SQL";
    if (!category_filter.empty()) {
        sql += " AND rule_category = ? ";
    }
    sql += " ORDER BY sort_order ASC, id ASC;";

    auto stmt = conn.prepare(sql);
    stmt.bind_int(1, transition_id);
    if (!category_filter.empty()) {
        stmt.bind_text(2, category_filter);
    }

    auto arr = json::json_value::make_array();
    while (stmt.step() == SQLITE_ROW) {
        auto r = json::json_value::make_object();
        r.add_int("id", stmt.column_int(0));
        r.add_int("transition_id", stmt.column_int(1));
        r.add_str("rule_category", stmt.column_str(2));
        r.add_str("rule_type", stmt.column_str(3));
        r.add_str("field_name", stmt.column_str(4));
        r.add_str("config_value", stmt.column_str(5));
        r.add_str("description", stmt.column_str(6));
        r.add_int("sort_order", stmt.column_int(7));
        arr.add(std::move(r));
    }
    return arr;
}

static json::json_value build_workflow_json(db::sqlite_db &conn, int workflow_id) {
    auto wstmt = conn.prepare("SELECT id, name, description, is_default, is_system, created_at FROM workflows WHERE id = ?;");
    wstmt.bind_int(1, workflow_id);
    if (wstmt.step() != SQLITE_ROW) {
        return json::json_value::make_null();
    }

    auto wf = json::json_value::make_object();
    wf.add_int("id", wstmt.column_int(0));
    wf.add_str("name", wstmt.column_str(1));
    wf.add_str("description", wstmt.column_str(2));
    wf.add_bool("is_default", wstmt.column_int(3) != 0);
    wf.add_bool("is_system", wstmt.column_int(4) != 0);
    wf.add_str("created_at", wstmt.column_str(5));

    // Statuses
    auto sstmt = conn.prepare(R"SQL(
        SELECT ws.id, ws.workflow_id, ws.name, ws.category, ws.color, ws.pos_x, ws.pos_y, ws.sort_order, ws.is_initial,
               (SELECT COUNT(*) FROM work_items w WHERE w.status = ws.name) AS item_count
        FROM workflow_statuses ws
        WHERE ws.workflow_id = ?
        ORDER BY ws.sort_order ASC, ws.id ASC;
    )SQL");
    sstmt.bind_int(1, workflow_id);
    auto statuses_arr = json::json_value::make_array();
    while (sstmt.step() == SQLITE_ROW) {
        auto st = json::json_value::make_object();
        st.add_int("id", sstmt.column_int(0));
        st.add_int("workflow_id", sstmt.column_int(1));
        st.add_str("name", sstmt.column_str(2));
        st.add_str("category", sstmt.column_str(3));
        st.add_str("color", sstmt.column_str(4));
        st.add_int("pos_x", sstmt.column_int(5));
        st.add_int("pos_y", sstmt.column_int(6));
        st.add_int("sort_order", sstmt.column_int(7));
        st.add_bool("is_initial", sstmt.column_int(8) != 0);
        st.add_int("item_count", sstmt.column_int(9));
        statuses_arr.add(std::move(st));
    }
    wf.add("statuses", std::move(statuses_arr));

    // Transitions
    auto tstmt = conn.prepare(R"SQL(
        SELECT wt.id, wt.workflow_id, wt.name, wt.from_status, wt.to_status,
               COALESCE(wt.screen_id, 0), COALESCE(s.name, ''), wt.sort_order, wt.created_at
        FROM workflow_transitions wt
        LEFT JOIN screens s ON s.id = wt.screen_id
        WHERE wt.workflow_id = ?
        ORDER BY wt.sort_order ASC, wt.id ASC;
    )SQL");
    tstmt.bind_int(1, workflow_id);
    auto trans_arr = json::json_value::make_array();
    while (tstmt.step() == SQLITE_ROW) {
        int tid = tstmt.column_int(0);
        auto tr = json::json_value::make_object();
        tr.add_int("id", tid);
        tr.add_int("workflow_id", tstmt.column_int(1));
        tr.add_str("name", tstmt.column_str(2));
        tr.add_str("from_status", tstmt.column_str(3));
        tr.add_str("to_status", tstmt.column_str(4));
        tr.add_nullable_int("screen_id", tstmt.column_int(5));
        tr.add_str("screen_name", tstmt.column_str(6));
        tr.add_int("sort_order", tstmt.column_int(7));
        tr.add_str("created_at", tstmt.column_str(8));
        tr.add("rules", load_transition_rules_json(conn, tid));
        tr.add("conditions", load_transition_rules_json(conn, tid, "condition"));
        tr.add("validators", load_transition_rules_json(conn, tid, "validator"));
        tr.add("post_functions", load_transition_rules_json(conn, tid, "post_function"));
        trans_arr.add(std::move(tr));
    }
    wf.add("transitions", std::move(trans_arr));
    return wf;
}

static int resolve_workflow_id_for_item(db::sqlite_db &conn,
                                        int project_id,
                                        const std::string &type,
                                        const std::string &category) {
    auto stmt = conn.prepare(R"SQL(
        SELECT workflow_id
        FROM workflow_schemes
        WHERE (project_id = ? OR project_id IS NULL OR is_default = 1)
          AND issue_type IN (?, ?, '*')
        ORDER BY
          CASE WHEN project_id = ? THEN 0 ELSE 1 END,
          CASE WHEN issue_type = ? THEN 0 WHEN issue_type = ? THEN 1 ELSE 2 END,
          is_default DESC,
          id ASC
        LIMIT 1;
    )SQL");
    stmt.bind_int(1, project_id);
    stmt.bind_text(2, category);
    stmt.bind_text(3, type);
    stmt.bind_int(4, project_id);
    stmt.bind_text(5, category);
    stmt.bind_text(6, type);
    if (stmt.step() == SQLITE_ROW) {
        return stmt.column_int(0);
    }

    auto def = conn.prepare("SELECT id FROM workflows WHERE is_default = 1 ORDER BY id ASC LIMIT 1;");
    if (def.step() == SQLITE_ROW) {
        return def.column_int(0);
    }
    return 1;
}

// GET | POST /api/workflows
static void handle_workflows(backend::wsworker *worker,
                             const std::map<std::string, std::string> &header,
                             const std::list<std::string> &) {
    auto conn = acquire_db_connection();
    std::string method = get_header(header, "REQUEST_METHOD");

    if (method == "GET") {
        auto stmt = conn.prepare("SELECT id FROM workflows ORDER BY is_default DESC, id ASC;");
        auto arr = json::json_value::make_array();
        while (stmt.step() == SQLITE_ROW) {
            arr.add(build_workflow_json(conn, stmt.column_int(0)));
        }
        send_json(worker, header, "200 OK", arr);
        return;
    }

    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    auto body = json::json_value::parse(worker->body());
    if (!body) {
        send_error(worker, header, "400 Bad Request", "Invalid JSON body");
        return;
    }

    std::string name = body.get_string("name");
    std::string description = body.get_string("description");
    bool is_default = body.get_bool("is_default", false);
    int clone_from_id = body.get_int("clone_from_id", 1);

    if (name.empty()) {
        send_error(worker, header, "400 Bad Request", "Workflow name is required");
        return;
    }

    int wf_id = 0;
    try {
        db::sqlite_transaction tx(conn);
        if (is_default) {
            auto clr = conn.prepare("UPDATE workflows SET is_default = 0;");
            clr.step_done();
        }
        auto ins = conn.prepare("INSERT INTO workflows (name, description, is_default, is_system) VALUES (?, ?, ?, 0);");
        ins.bind_text(1, name);
        ins.bind_text(2, description);
        ins.bind_int(3, is_default ? 1 : 0);
        ins.step_done();
        wf_id = static_cast<int>(conn.last_insert_rowid());

        if (clone_from_id > 0) {
            auto cstat = conn.prepare(R"SQL(
                INSERT INTO workflow_statuses (workflow_id, name, category, color, pos_x, pos_y, sort_order, is_initial)
                SELECT ?, name, category, color, pos_x, pos_y, sort_order, is_initial
                FROM workflow_statuses WHERE workflow_id = ?;
            )SQL");
            cstat.bind_int(1, wf_id);
            cstat.bind_int(2, clone_from_id);
            cstat.step_done();

            auto tsel = conn.prepare("SELECT id, name, from_status, to_status, screen_id, sort_order FROM workflow_transitions WHERE workflow_id = ? ORDER BY sort_order ASC, id ASC;");
            tsel.bind_int(1, clone_from_id);
            while (tsel.step() == SQLITE_ROW) {
                int old_tid = tsel.column_int(0);
                auto tins = conn.prepare("INSERT INTO workflow_transitions (workflow_id, name, from_status, to_status, screen_id, sort_order) VALUES (?, ?, ?, ?, ?, ?);");
                tins.bind_int(1, wf_id);
                tins.bind_text(2, tsel.column_str(1));
                tins.bind_text(3, tsel.column_str(2));
                tins.bind_text(4, tsel.column_str(3));
                tins.bind_optional_int(5, tsel.column_int(4));
                tins.bind_int(6, tsel.column_int(5));
                tins.step_done();
                int new_tid = static_cast<int>(conn.last_insert_rowid());

                auto rins = conn.prepare(R"SQL(
                    INSERT INTO workflow_transition_rules (transition_id, rule_category, rule_type, field_name, config_value, description, sort_order)
                    SELECT ?, rule_category, rule_type, field_name, config_value, description, sort_order
                    FROM workflow_transition_rules WHERE transition_id = ?;
                )SQL");
                rins.bind_int(1, new_tid);
                rins.bind_int(2, old_tid);
                rins.step_done();
            }
        } else {
            auto sins = conn.prepare(R"SQL(
                INSERT INTO workflow_statuses (workflow_id, name, category, color, pos_x, pos_y, sort_order, is_initial) VALUES
                (?, 'To Do',       'To Do',       '#0052CC', 140, 140, 10, 1),
                (?, 'In Progress', 'In Progress', '#FF991F', 420, 140, 20, 0),
                (?, 'Done',        'Done',        '#00875A', 700, 140, 30, 0);
            )SQL");
            sins.bind_int(1, wf_id);
            sins.bind_int(2, wf_id);
            sins.bind_int(3, wf_id);
            sins.step_done();
        }
        tx.commit();
    } catch (const std::exception &ex) {
        send_error(worker, header, "400 Bad Request", ex.what());
        return;
    }

    send_json(worker, header, "201 Created", build_workflow_json(conn, wf_id));
}

// GET | PUT | DELETE /api/workflows/([0-9]+)
static void handle_workflow_detail(backend::wsworker *worker,
                                   const std::map<std::string, std::string> &header,
                                   const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing workflow ID");
        return;
    }
    int wf_id = std::atoi(uri_params.front().c_str());

    auto conn = acquire_db_connection();
    std::string method = get_header(header, "REQUEST_METHOD");

    if (method == "GET") {
        auto wf = build_workflow_json(conn, wf_id);
        if (!wf) {
            send_error(worker, header, "404 Not Found", "Workflow not found");
            return;
        }
        send_json(worker, header, "200 OK", wf);
        return;
    }

    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    if (method == "DELETE") {
        auto chk = conn.prepare("SELECT is_system FROM workflows WHERE id = ?;");
        chk.bind_int(1, wf_id);
        if (chk.step() == SQLITE_ROW && chk.column_int(0) != 0) {
            send_error(worker, header, "400 Bad Request", "Cannot delete a system workflow");
            return;
        }
        db::sqlite_transaction tx(conn);
        auto del = conn.prepare("DELETE FROM workflows WHERE id = ?;");
        del.bind_int(1, wf_id);
        del.step_done();
        tx.commit();
        auto res = json::json_value::make_object();
        res.add_bool("deleted", true);
        send_json(worker, header, "200 OK", res);
        return;
    }

    auto body = json::json_value::parse(worker->body());
    if (!body) {
        send_error(worker, header, "400 Bad Request", "Invalid JSON body");
        return;
    }

    try {
        db::sqlite_transaction tx(conn);
        if (body.has_key("name")) {
            std::string name = body.get_string("name");
            std::string description = body.get_string("description");
            auto upd = conn.prepare("UPDATE workflows SET name = COALESCE(NULLIF(?, ''), name), description = ? WHERE id = ?;");
            upd.bind_text(1, name);
            upd.bind_text(2, description);
            upd.bind_int(3, wf_id);
            upd.step_done();
        }
        if (body.has_key("statuses")) {
            json_object *st_arr = nullptr;
            if (json_object_object_get_ex(body.get(), "statuses", &st_arr) && st_arr &&
                json_object_is_type(st_arr, json_type_array)) {
                auto supd = conn.prepare("UPDATE workflow_statuses SET pos_x = ?, pos_y = ?, category = COALESCE(NULLIF(?, ''), category), color = COALESCE(NULLIF(?, ''), color) WHERE id = ? AND workflow_id = ?;");
                size_t len = json_object_array_length(st_arr);
                for (size_t i = 0; i < len; ++i) {
                    json_object *el = json_object_array_get_idx(st_arr, i);
                    if (!el || !json_object_is_type(el, json_type_object)) continue;
                    json_object *jid = nullptr, *jx = nullptr, *jy = nullptr, *jcat = nullptr, *jcol = nullptr;
                    int sid = (json_object_object_get_ex(el, "id", &jid) && jid) ? json_object_get_int(jid) : 0;
                    int px = (json_object_object_get_ex(el, "pos_x", &jx) && jx) ? json_object_get_int(jx) : 100;
                    int py = (json_object_object_get_ex(el, "pos_y", &jy) && jy) ? json_object_get_int(jy) : 140;
                    std::string cat = (json_object_object_get_ex(el, "category", &jcat) && jcat && json_object_get_string(jcat)) ? json_object_get_string(jcat) : "";
                    std::string col = (json_object_object_get_ex(el, "color", &jcol) && jcol && json_object_get_string(jcol)) ? json_object_get_string(jcol) : "";
                    if (sid > 0) {
                        supd.clear_and_reset();
                        supd.bind_int(1, px);
                        supd.bind_int(2, py);
                        supd.bind_text(3, cat);
                        supd.bind_text(4, col);
                        supd.bind_int(5, sid);
                        supd.bind_int(6, wf_id);
                        supd.step_done();
                    }
                }
            }
        }
        tx.commit();
    } catch (const std::exception &ex) {
        send_error(worker, header, "400 Bad Request", ex.what());
        return;
    }

    send_json(worker, header, "200 OK", build_workflow_json(conn, wf_id));
}

// POST /api/workflows/([0-9]+)/statuses
static void handle_workflow_statuses(backend::wsworker *worker,
                                     const std::map<std::string, std::string> &header,
                                     const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing workflow ID");
        return;
    }
    int wf_id = std::atoi(uri_params.front().c_str());

    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    auto body = json::json_value::parse(worker->body());
    if (!body) {
        send_error(worker, header, "400 Bad Request", "Invalid JSON body");
        return;
    }

    std::string name = body.get_string("name");
    std::string category = body.get_string("category", "In Progress");
    std::string color = body.get_string("color", "");
    if (color.empty()) {
        if (category == "To Do") color = "#0052CC";
        else if (category == "Done") color = "#00875A";
        else color = "#FF991F";
    }
    int pos_x = body.get_int("pos_x", 300);
    int pos_y = body.get_int("pos_y", 220);
    int sort_order = body.get_int("sort_order", 100);
    bool is_initial = body.get_bool("is_initial", false);

    if (name.empty()) {
        send_error(worker, header, "400 Bad Request", "Status name is required");
        return;
    }

    try {
        db::sqlite_transaction tx(conn);
        auto ins = conn.prepare(R"SQL(
            INSERT INTO workflow_statuses (workflow_id, name, category, color, pos_x, pos_y, sort_order, is_initial)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT(workflow_id, name) DO UPDATE SET
                category = excluded.category,
                color = excluded.color,
                pos_x = excluded.pos_x,
                pos_y = excluded.pos_y;
        )SQL");
        ins.bind_int(1, wf_id);
        ins.bind_text(2, name);
        ins.bind_text(3, category);
        ins.bind_text(4, color);
        ins.bind_int(5, pos_x);
        ins.bind_int(6, pos_y);
        ins.bind_int(7, sort_order);
        ins.bind_int(8, is_initial ? 1 : 0);
        ins.step_done();
        tx.commit();
    } catch (const std::exception &ex) {
        send_error(worker, header, "400 Bad Request", ex.what());
        return;
    }

    send_json(worker, header, "201 Created", build_workflow_json(conn, wf_id));
}

// PUT | DELETE /api/workflow-statuses/([0-9]+)
static void handle_workflow_status_detail(backend::wsworker *worker,
                                          const std::map<std::string, std::string> &header,
                                          const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing status ID");
        return;
    }
    int status_id = std::atoi(uri_params.front().c_str());

    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    std::string method = get_header(header, "REQUEST_METHOD");
    if (method == "DELETE") {
        db::sqlite_transaction tx(conn);
        auto del = conn.prepare("DELETE FROM workflow_statuses WHERE id = ?;");
        del.bind_int(1, status_id);
        del.step_done();
        tx.commit();
        auto res = json::json_value::make_object();
        res.add_bool("deleted", true);
        send_json(worker, header, "200 OK", res);
        return;
    }

    auto body = json::json_value::parse(worker->body());
    if (!body) {
        send_error(worker, header, "400 Bad Request", "Invalid JSON body");
        return;
    }

    int pos_x = body.get_int("pos_x", 150);
    int pos_y = body.get_int("pos_y", 150);
    std::string category = body.get_string("category");
    std::string color = body.get_string("color");

    try {
        db::sqlite_transaction tx(conn);
        auto upd = conn.prepare(R"SQL(
            UPDATE workflow_statuses
            SET pos_x = ?, pos_y = ?,
                category = COALESCE(NULLIF(?, ''), category),
                color = COALESCE(NULLIF(?, ''), color)
            WHERE id = ?;
        )SQL");
        upd.bind_int(1, pos_x);
        upd.bind_int(2, pos_y);
        upd.bind_text(3, category);
        upd.bind_text(4, color);
        upd.bind_int(5, status_id);
        upd.step_done();
        tx.commit();
    } catch (const std::exception &ex) {
        send_error(worker, header, "400 Bad Request", ex.what());
        return;
    }

    auto res = json::json_value::make_object();
    res.add_bool("ok", true);
    res.add_int("id", status_id);
    res.add_int("pos_x", pos_x);
    res.add_int("pos_y", pos_y);
    send_json(worker, header, "200 OK", res);
}

// POST /api/workflows/([0-9]+)/transitions
static void handle_workflow_transitions(backend::wsworker *worker,
                                        const std::map<std::string, std::string> &header,
                                        const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing workflow ID");
        return;
    }
    int wf_id = std::atoi(uri_params.front().c_str());

    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    auto body = json::json_value::parse(worker->body());
    if (!body) {
        send_error(worker, header, "400 Bad Request", "Invalid JSON body");
        return;
    }

    std::string name = body.get_string("name");
    std::string from_status = body.get_string("from_status", "*");
    if (from_status.empty()) from_status = "*";
    std::string to_status = body.get_string("to_status");
    int screen_id = body.get_int("screen_id", 0);
    int sort_order = body.get_int("sort_order", 100);

    if (name.empty() || to_status.empty()) {
        send_error(worker, header, "400 Bad Request", "Transition name and to_status are required");
        return;
    }

    int tid = 0;
    try {
        db::sqlite_transaction tx(conn);
        auto ins = conn.prepare(R"SQL(
            INSERT INTO workflow_transitions (workflow_id, name, from_status, to_status, screen_id, sort_order)
            VALUES (?, ?, ?, ?, ?, ?);
        )SQL");
        ins.bind_int(1, wf_id);
        ins.bind_text(2, name);
        ins.bind_text(3, from_status);
        ins.bind_text(4, to_status);
        ins.bind_optional_int(5, screen_id);
        ins.bind_int(6, sort_order);
        ins.step_done();
        tid = static_cast<int>(conn.last_insert_rowid());
        tx.commit();
    } catch (const std::exception &ex) {
        send_error(worker, header, "400 Bad Request", ex.what());
        return;
    }

    auto res = json::json_value::make_object();
    res.add_int("id", tid);
    res.add_int("workflow_id", wf_id);
    res.add_str("name", name);
    res.add_str("from_status", from_status);
    res.add_str("to_status", to_status);
    res.add_nullable_int("screen_id", screen_id);
    res.add_int("sort_order", sort_order);
    res.add("rules", json::json_value::make_array());
    res.add("conditions", json::json_value::make_array());
    res.add("validators", json::json_value::make_array());
    res.add("post_functions", json::json_value::make_array());
    send_json(worker, header, "201 Created", res);
}

// PUT | DELETE /api/workflow-transitions/([0-9]+)
static void handle_workflow_transition_detail(backend::wsworker *worker,
                                              const std::map<std::string, std::string> &header,
                                              const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing transition ID");
        return;
    }
    int tid = std::atoi(uri_params.front().c_str());

    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    std::string method = get_header(header, "REQUEST_METHOD");
    if (method == "DELETE") {
        db::sqlite_transaction tx(conn);
        auto del = conn.prepare("DELETE FROM workflow_transitions WHERE id = ?;");
        del.bind_int(1, tid);
        del.step_done();
        tx.commit();
        auto res = json::json_value::make_object();
        res.add_bool("deleted", true);
        send_json(worker, header, "200 OK", res);
        return;
    }

    auto body = json::json_value::parse(worker->body());
    if (!body) {
        send_error(worker, header, "400 Bad Request", "Invalid JSON body");
        return;
    }

    std::string name = body.get_string("name");
    std::string from_status = body.get_string("from_status", "*");
    std::string to_status = body.get_string("to_status");
    int screen_id = body.get_int("screen_id", 0);

    try {
        db::sqlite_transaction tx(conn);
        auto upd = conn.prepare(R"SQL(
            UPDATE workflow_transitions
            SET name = COALESCE(NULLIF(?, ''), name),
                from_status = COALESCE(NULLIF(?, ''), from_status),
                to_status = COALESCE(NULLIF(?, ''), to_status),
                screen_id = ?
            WHERE id = ?;
        )SQL");
        upd.bind_text(1, name);
        upd.bind_text(2, from_status);
        upd.bind_text(3, to_status);
        upd.bind_optional_int(4, screen_id);
        upd.bind_int(5, tid);
        upd.step_done();
        tx.commit();
    } catch (const std::exception &ex) {
        send_error(worker, header, "400 Bad Request", ex.what());
        return;
    }

    auto res = json::json_value::make_object();
    res.add_bool("ok", true);
    res.add_int("id", tid);
    res.add_nullable_int("screen_id", screen_id);
    send_json(worker, header, "200 OK", res);
}

// POST /api/workflow-transitions/([0-9]+)/rules
static void handle_workflow_transition_rules(backend::wsworker *worker,
                                             const std::map<std::string, std::string> &header,
                                             const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing transition ID");
        return;
    }
    int tid = std::atoi(uri_params.front().c_str());

    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    auto body = json::json_value::parse(worker->body());
    if (!body) {
        send_error(worker, header, "400 Bad Request", "Invalid JSON body");
        return;
    }

    std::string rule_category = body.get_string("rule_category");
    std::string rule_type = body.get_string("rule_type");
    std::string field_name = body.get_string("field_name");
    std::string config_value = body.get_string("config_value");
    std::string description = body.get_string("description");
    int sort_order = body.get_int("sort_order", 50);

    if (rule_category.empty() || rule_type.empty()) {
        send_error(worker, header, "400 Bad Request", "rule_category and rule_type are required");
        return;
    }

    int rid = 0;
    try {
        db::sqlite_transaction tx(conn);
        auto ins = conn.prepare(R"SQL(
            INSERT INTO workflow_transition_rules (transition_id, rule_category, rule_type, field_name, config_value, description, sort_order)
            VALUES (?, ?, ?, ?, ?, ?, ?);
        )SQL");
        ins.bind_int(1, tid);
        ins.bind_text(2, rule_category);
        ins.bind_text(3, rule_type);
        ins.bind_text(4, field_name);
        ins.bind_text(5, config_value);
        ins.bind_text(6, description);
        ins.bind_int(7, sort_order);
        ins.step_done();
        rid = static_cast<int>(conn.last_insert_rowid());
        tx.commit();
    } catch (const std::exception &ex) {
        send_error(worker, header, "400 Bad Request", ex.what());
        return;
    }

    auto res = json::json_value::make_object();
    res.add_int("id", rid);
    res.add_int("transition_id", tid);
    res.add_str("rule_category", rule_category);
    res.add_str("rule_type", rule_type);
    res.add_str("field_name", field_name);
    res.add_str("config_value", config_value);
    res.add_str("description", description);
    res.add_int("sort_order", sort_order);
    send_json(worker, header, "201 Created", res);
}

// DELETE /api/workflow-transition-rules/([0-9]+)
static void handle_delete_workflow_transition_rule(backend::wsworker *worker,
                                                   const std::map<std::string, std::string> &header,
                                                   const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing rule ID");
        return;
    }
    int rid = std::atoi(uri_params.front().c_str());

    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    {
        db::sqlite_transaction tx(conn);
        auto del = conn.prepare("DELETE FROM workflow_transition_rules WHERE id = ?;");
        del.bind_int(1, rid);
        del.step_done();
        tx.commit();
    }

    auto res = json::json_value::make_object();
    res.add_bool("deleted", true);
    send_json(worker, header, "200 OK", res);
}

// GET | POST /api/workflow-schemes
static void handle_workflow_schemes(backend::wsworker *worker,
                                    const std::map<std::string, std::string> &header,
                                    const std::list<std::string> &) {
    auto conn = acquire_db_connection();
    std::string method = get_header(header, "REQUEST_METHOD");

    if (method == "GET") {
        auto stmt = conn.prepare(R"SQL(
            SELECT ws.id, ws.name, ws.description, COALESCE(ws.project_id, 0), COALESCE(p.name, 'All Projects'),
                   ws.issue_type, ws.workflow_id, COALESCE(w.name, ''), ws.is_default, ws.created_at
            FROM workflow_schemes ws
            LEFT JOIN projects p ON p.id = ws.project_id
            LEFT JOIN workflows w ON w.id = ws.workflow_id
            ORDER BY ws.is_default DESC, ws.id ASC;
        )SQL");
        auto arr = json::json_value::make_array();
        while (stmt.step() == SQLITE_ROW) {
            auto s = json::json_value::make_object();
            s.add_int("id", stmt.column_int(0));
            s.add_str("name", stmt.column_str(1));
            s.add_str("description", stmt.column_str(2));
            s.add_nullable_int("project_id", stmt.column_int(3));
            s.add_str("project_name", stmt.column_str(4));
            s.add_str("issue_type", stmt.column_str(5));
            s.add_int("workflow_id", stmt.column_int(6));
            s.add_str("workflow_name", stmt.column_str(7));
            s.add_bool("is_default", stmt.column_int(8) != 0);
            s.add_str("created_at", stmt.column_str(9));
            arr.add(std::move(s));
        }
        send_json(worker, header, "200 OK", arr);
        return;
    }

    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    auto body = json::json_value::parse(worker->body());
    if (!body) {
        send_error(worker, header, "400 Bad Request", "Invalid JSON body");
        return;
    }

    std::string name = body.get_string("name", "Project Workflow Scheme Binding");
    std::string description = body.get_string("description");
    int project_id = body.get_int("project_id", 1);
    std::string issue_type = body.get_string("issue_type", "*");
    if (issue_type.empty()) issue_type = "*";
    int workflow_id = body.get_int("workflow_id", 1);
    bool is_default = body.get_bool("is_default", issue_type == "*");

    int scheme_id = 0;
    try {
        db::sqlite_transaction tx(conn);
        auto ups = conn.prepare(R"SQL(
            INSERT INTO workflow_schemes (name, description, project_id, issue_type, workflow_id, is_default)
            VALUES (?, ?, ?, ?, ?, ?)
            ON CONFLICT(project_id, issue_type) DO UPDATE SET
                name = excluded.name,
                description = excluded.description,
                workflow_id = excluded.workflow_id,
                is_default = excluded.is_default;
        )SQL");
        ups.bind_text(1, name);
        ups.bind_text(2, description);
        ups.bind_int(3, project_id);
        ups.bind_text(4, issue_type);
        ups.bind_int(5, workflow_id);
        ups.bind_int(6, is_default ? 1 : 0);
        ups.step_done();
        scheme_id = static_cast<int>(conn.last_insert_rowid());
        tx.commit();
    } catch (const std::exception &ex) {
        send_error(worker, header, "400 Bad Request", ex.what());
        return;
    }

    auto res = json::json_value::make_object();
    res.add_int("id", scheme_id);
    res.add_str("name", name);
    res.add_str("description", description);
    res.add_int("project_id", project_id);
    res.add_str("issue_type", issue_type);
    res.add_int("workflow_id", workflow_id);
    res.add_bool("is_default", is_default);
    send_json(worker, header, "200 OK", res);
}

// DELETE /api/workflow-schemes/([0-9]+)
static void handle_delete_workflow_scheme(backend::wsworker *worker,
                                           const std::map<std::string, std::string> &header,
                                           const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing workflow scheme ID");
        return;
    }
    int sid = std::atoi(uri_params.front().c_str());

    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    {
        db::sqlite_transaction tx(conn);
        auto del = conn.prepare("DELETE FROM workflow_schemes WHERE id = ?;");
        del.bind_int(1, sid);
        del.step_done();
        tx.commit();
    }

    auto res = json::json_value::make_object();
    res.add_bool("deleted", true);
    send_json(worker, header, "200 OK", res);
}

static bool role_in_csv(const std::string &role, const std::string &csv) {
    if (role.empty()) return false;
    std::stringstream ss(csv);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        size_t s = tok.find_first_not_of(" \t");
        size_t e = tok.find_last_not_of(" \t");
        if (s != std::string::npos && tok.substr(s, e - s + 1) == role) {
            return true;
        }
    }
    return false;
}

static bool evaluate_transition_conditions(db::sqlite_db &conn,
                                           int transition_id,
                                           int item_id,
                                           int item_assignee_id,
                                           const auth_user &u_auth,
                                           std::vector<std::string> &blocked_reasons) {
    auto cstmt = conn.prepare(R"SQL(
        SELECT rule_type, field_name, config_value, description
        FROM workflow_transition_rules
        WHERE transition_id = ? AND rule_category = 'condition'
        ORDER BY sort_order ASC, id ASC;
    )SQL");
    cstmt.bind_int(1, transition_id);

    while (cstmt.step() == SQLITE_ROW) {
        std::string rtype = cstmt.column_str(0);
        std::string cfg = cstmt.column_str(2);

        if (rtype == "only_assignee") {
            bool ok = (u_auth.role == "Admin") ||
                      (u_auth.authenticated && item_assignee_id > 0 && u_auth.id == item_assignee_id);
            if (!ok) {
                blocked_reasons.push_back("Condition failed (only_assignee): Only the currently assigned user (or Admin) can trigger this transition");
            }
        } else if (rtype == "user_in_role") {
            bool ok = (u_auth.role == "Admin") || role_in_csv(u_auth.role, cfg);
            if (!ok) {
                blocked_reasons.push_back("Condition failed (user_in_role): Requires user role in [" + cfg + "] (your role: " + (u_auth.role.empty() ? "Unauthenticated" : u_auth.role) + ")");
            }
        } else if (rtype == "subtasks_completed") {
            std::string req_status = cfg.empty() ? "Done" : cfg;
            auto subq = conn.prepare("SELECT COUNT(*) FROM work_items WHERE parent_id = ? AND status != ?;");
            subq.bind_int(1, item_id);
            subq.bind_text(2, req_status);
            int open_subs = 0;
            if (subq.step() == SQLITE_ROW) {
                open_subs = subq.column_int(0);
            }
            if (open_subs > 0) {
                blocked_reasons.push_back("Condition failed (subtasks_completed): Cannot transition because " + std::to_string(open_subs) + " child subtask(s) are not yet '" + req_status + "'");
            }
        } else if (rtype == "permission") {
            if (!u_auth.authenticated) {
                blocked_reasons.push_back("Condition failed (permission): User must have project permission '" + cfg + "'");
            } else if (cfg == "Close Issues" && u_auth.role == "Reporter") {
                blocked_reasons.push_back("Condition failed (permission): Role 'Reporter' does not have 'Close Issues' permission");
            }
        }
    }
    return blocked_reasons.empty();
}

static json::json_value build_item_transitions_payload(db::sqlite_db &conn,
                                                       int item_id,
                                                       const auth_user &u_auth) {
    auto istmt = conn.prepare("SELECT project_id, type, category, status, COALESCE(assignee_id, 0) FROM work_items WHERE id = ?;");
    istmt.bind_int(1, item_id);
    if (istmt.step() != SQLITE_ROW) {
        return json::json_value::make_null();
    }
    int project_id = istmt.column_int(0);
    std::string type = istmt.column_str(1);
    std::string category = istmt.column_str(2);
    std::string cur_status = istmt.column_str(3);
    int assignee_id = istmt.column_int(4);

    int wf_id = resolve_workflow_id_for_item(conn, project_id, type, category);
    std::string wf_name = "Default Agile 7-State Workflow";
    {
        auto wq = conn.prepare("SELECT name FROM workflows WHERE id = ?;");
        wq.bind_int(1, wf_id);
        if (wq.step() == SQLITE_ROW) {
            wf_name = wq.column_str(0);
        }
    }

    auto out = json::json_value::make_object();
    out.add_int("item_id", item_id);
    out.add_int("workflow_id", wf_id);
    out.add_str("workflow_name", wf_name);
    out.add_str("current_status", cur_status);

    // Statuses in active workflow
    auto sstmt = conn.prepare("SELECT id, name, category, color, pos_x, pos_y, sort_order, is_initial FROM workflow_statuses WHERE workflow_id = ? ORDER BY sort_order ASC, id ASC;");
    sstmt.bind_int(1, wf_id);
    auto st_arr = json::json_value::make_array();
    std::string cur_cat = "To Do";
    std::map<std::string, std::pair<std::string, std::string>> status_meta;
    while (sstmt.step() == SQLITE_ROW) {
        std::string sname = sstmt.column_str(1);
        std::string scat = sstmt.column_str(2);
        std::string scol = sstmt.column_str(3);
        status_meta[sname] = {scat, scol};
        if (sname == cur_status) cur_cat = scat;

        auto so = json::json_value::make_object();
        so.add_int("id", sstmt.column_int(0));
        so.add_str("name", sname);
        so.add_str("category", scat);
        so.add_str("color", scol);
        so.add_int("pos_x", sstmt.column_int(4));
        so.add_int("pos_y", sstmt.column_int(5));
        so.add_int("sort_order", sstmt.column_int(6));
        so.add_bool("is_initial", sstmt.column_int(7) != 0);
        st_arr.add(std::move(so));
    }
    out.add_str("current_category", cur_cat);
    out.add("statuses", std::move(st_arr));

    // Transitions from cur_status or '*', preferring specific from_status over '*' per target to_status
    auto tstmt = conn.prepare(R"SQL(
        SELECT wt.id, wt.name, wt.from_status, wt.to_status, COALESCE(wt.screen_id, 0), COALESCE(s.name, ''), wt.sort_order
        FROM workflow_transitions wt
        LEFT JOIN screens s ON s.id = wt.screen_id
        WHERE wt.workflow_id = ?
          AND (wt.from_status = ? OR wt.from_status = '*')
          AND wt.to_status != ?
        ORDER BY CASE WHEN wt.from_status = ? THEN 0 ELSE 1 END, wt.sort_order ASC, wt.id ASC;
    )SQL");
    tstmt.bind_int(1, wf_id);
    tstmt.bind_text(2, cur_status);
    tstmt.bind_text(3, cur_status);
    tstmt.bind_text(4, cur_status);

    std::vector<std::string> seen_targets;
    auto tr_arr = json::json_value::make_array();
    while (tstmt.step() == SQLITE_ROW) {
        int tid = tstmt.column_int(0);
        std::string tname = tstmt.column_str(1);
        std::string from_st = tstmt.column_str(2);
        std::string to_st = tstmt.column_str(3);
        int scr_id = tstmt.column_int(4);
        std::string scr_name = tstmt.column_str(5);

        if (std::find(seen_targets.begin(), seen_targets.end(), to_st) != seen_targets.end()) {
            continue;
        }
        seen_targets.push_back(to_st);

        std::vector<std::string> blocked_reasons;
        bool allowed = evaluate_transition_conditions(conn, tid, item_id, assignee_id, u_auth, blocked_reasons);

        auto validators = load_transition_rules_json(conn, tid, "validator");
        bool has_validators = json_object_array_length(validators.get()) > 0;

        auto to = json::json_value::make_object();
        to.add_int("id", tid);
        to.add_int("workflow_id", wf_id);
        to.add_str("name", tname);
        to.add_str("from_status", from_st);
        to.add_str("to_status", to_st);
        to.add_str("to_category", status_meta.count(to_st) ? status_meta[to_st].first : "In Progress");
        to.add_str("to_color", status_meta.count(to_st) ? status_meta[to_st].second : "#0052CC");
        to.add_nullable_int("screen_id", scr_id);
        to.add_str("screen_name", scr_name);
        to.add_bool("allowed", allowed);
        to.add_bool("requires_screen", scr_id > 0 || has_validators);

        auto br_arr = json::json_value::make_array();
        for (const auto &br : blocked_reasons) {
            br_arr.add(json::json_value::make_string(br));
        }
        to.add("blocked_reasons", std::move(br_arr));
        to.add("conditions", load_transition_rules_json(conn, tid, "condition"));
        to.add("validators", std::move(validators));
        to.add("post_functions", load_transition_rules_json(conn, tid, "post_function"));
        if (scr_id > 0) {
            to.add("screen", build_screen_json(conn, scr_id, 1, type, true));
        } else {
            to.add("screen", json::json_value::make_null());
        }
        tr_arr.add(std::move(to));
    }
    out.add("transitions", std::move(tr_arr));
    return out;
}

static int resolve_work_item_id(db::sqlite_db &conn, const std::string &id_or_key);
static void handle_item_state_transition(backend::wsworker *worker,
                                         const std::map<std::string, std::string> &header,
                                         const std::list<std::string> &uri_params);

// GET | POST /api/items/([A-Za-z0-9_-]+)/transitions
static void handle_item_available_transitions(backend::wsworker *worker,
                                              const std::map<std::string, std::string> &header,
                                              const std::list<std::string> &uri_params) {
    if (get_header(header, "REQUEST_METHOD") == "POST") {
        handle_item_state_transition(worker, header, uri_params);
        return;
    }
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing item ID or key");
        return;
    }
    auto conn = acquire_db_connection();
    int item_id = resolve_work_item_id(conn, uri_params.front());
    if (item_id <= 0) {
        send_error(worker, header, "404 Not Found", "Work item not found");
        return;
    }
    auto u_auth = get_authenticated_user(conn, worker, header);
    auto payload = build_item_transitions_payload(conn, item_id, u_auth);
    if (!payload) {
        send_error(worker, header, "404 Not Found", "Work item not found");
        return;
    }
    send_json(worker, header, "200 OK", payload);
}

// GET /api/sprints & POST /api/sprints
static void handle_sprints(backend::wsworker *worker,
                           const std::map<std::string, std::string> &header,
                           const std::list<std::string> &) {
    auto conn = acquire_db_connection();
    std::string method = get_header(header, "REQUEST_METHOD");

    if (method == "GET") {
        auto args = worker->parse_args(get_header(header, "QUERY_STRING"));
        int project_id = std::atoi(args["project_id"].c_str());

        std::string sql = R"SQL(
            SELECT s.id, s.project_id, s.name, s.goal, s.status, s.start_date, s.end_date, s.created_at,
                   s.is_current,
                   (SELECT COUNT(*) FROM work_items w WHERE w.sprint_id = s.id) AS item_count,
                   (SELECT COUNT(*) FROM work_items w WHERE w.sprint_id = s.id AND w.status = 'Done') AS done_count,
                   (SELECT COALESCE(SUM(w.story_points), 0) FROM work_items w WHERE w.sprint_id = s.id) AS total_points
            FROM sprints s
        )SQL";
        if (project_id > 0) {
            sql += " WHERE s.project_id = ? ";
        }
        sql += " ORDER BY s.is_current DESC, s.id ASC;";

        auto stmt = conn.prepare(sql);
        if (project_id > 0) stmt.bind_int(1, project_id);

        auto arr = json::json_value::make_array();
        while (stmt.step() == SQLITE_ROW) {
            auto s = json::json_value::make_object();
            s.add_int("id", stmt.column_int(0));
            s.add_int("project_id", stmt.column_int(1));
            s.add_str("name", stmt.column_str(2));
            s.add_str("goal", stmt.column_str(3));
            s.add_str("status", stmt.column_str(4));
            s.add_str("start_date", stmt.column_str(5));
            s.add_str("end_date", stmt.column_str(6));
            s.add_str("created_at", stmt.column_str(7));
            s.add_bool("is_current", stmt.column_int(8) != 0);
            s.add_int("item_count", stmt.column_int(9));
            s.add_int("done_count", stmt.column_int(10));
            s.add_int("total_points", stmt.column_int(11));
            arr.add(std::move(s));
        }
        send_json(worker, header, "200 OK", arr);
        return;
    }

    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }
    if (u_auth.role != "Admin") {
        send_error(worker, header, "403 Forbidden", "Only Administrators can create or edit sprints");
        return;
    }

    auto body = json::json_value::parse(worker->body());
    if (!body) {
        send_error(worker, header, "400 Bad Request", "Invalid JSON body");
        return;
    }

    int project_id = body.get_int("project_id", 1);
    std::string name = body.get_string("name");
    std::string goal = body.get_string("goal");
    std::string status = body.get_string("status", "planned");
    std::string start_date = body.get_string("start_date");
    std::string end_date = body.get_string("end_date");
    bool is_current = body.get_bool("is_current", false);
    if (is_current) {
        status = "active";
    }

    if (name.empty()) {
        send_error(worker, header, "400 Bad Request", "Sprint name is required");
        return;
    }

    int new_id = 0;
    try {
        db::sqlite_transaction tx(conn);
        if (is_current) {
            auto clr = conn.prepare("UPDATE sprints SET is_current = 0 WHERE project_id = ?;");
            clr.bind_int(1, project_id);
            clr.step_done();
        }

        auto ins = conn.prepare(R"SQL(
            INSERT INTO sprints (project_id, name, goal, status, start_date, end_date, is_current)
            VALUES (?, ?, ?, ?, ?, ?, ?);
        )SQL");
        ins.bind_int(1, project_id);
        ins.bind_text(2, name);
        ins.bind_text(3, goal);
        ins.bind_text(4, status);
        ins.bind_text(5, start_date);
        ins.bind_text(6, end_date);
        ins.bind_int(7, is_current ? 1 : 0);
        ins.step_done();
        new_id = static_cast<int>(conn.last_insert_rowid());
        tx.commit();
    } catch (const std::exception &ex) {
        send_error(worker, header, "500 Internal Server Error", ex.what());
        return;
    }

    auto res = json::json_value::make_object();
    res.add_int("id", new_id);
    res.add_int("project_id", project_id);
    res.add_str("name", name);
    res.add_str("goal", goal);
    res.add_str("status", status);
    res.add_str("start_date", start_date);
    res.add_str("end_date", end_date);
    res.add_bool("is_current", is_current);
    res.add_int("item_count", 0);
    res.add_int("done_count", 0);
    res.add_int("total_points", 0);
    send_json(worker, header, "201 Created", res);
}

// PUT / DELETE /api/sprints/([0-9]+)
static void handle_sprint_detail(backend::wsworker *worker,
                                 const std::map<std::string, std::string> &header,
                                 const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing sprint ID");
        return;
    }
    int sprint_id = std::atoi(uri_params.front().c_str());

    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }
    if (u_auth.role != "Admin") {
        send_error(worker, header, "403 Forbidden", "Only Administrators can modify sprints");
        return;
    }

    std::string method = get_header(header, "REQUEST_METHOD");
    if (method == "DELETE") {
        db::sqlite_transaction tx(conn);
        auto del = conn.prepare("DELETE FROM sprints WHERE id = ?;");
        del.bind_int(1, sprint_id);
        del.step_done();
        tx.commit();
        auto res = json::json_value::make_object();
        res.add_bool("deleted", true);
        send_json(worker, header, "200 OK", res);
        return;
    }

    auto body = json::json_value::parse(worker->body());
    if (!body) {
        send_error(worker, header, "400 Bad Request", "Invalid JSON body");
        return;
    }

    int project_id = body.get_int("project_id", 1);
    std::string name = body.get_string("name");
    std::string goal = body.get_string("goal");
    std::string status = body.get_string("status", "planned");
    std::string start_date = body.get_string("start_date");
    std::string end_date = body.get_string("end_date");
    bool is_current = body.get_bool("is_current", false);
    if (is_current) {
        status = "active";
    }

    if (name.empty()) {
        send_error(worker, header, "400 Bad Request", "Sprint name is required");
        return;
    }

    try {
        db::sqlite_transaction tx(conn);
        if (is_current) {
            auto clr = conn.prepare("UPDATE sprints SET is_current = 0 WHERE project_id = ?;");
            clr.bind_int(1, project_id);
            clr.step_done();
        }
        auto upd = conn.prepare(R"SQL(
            UPDATE sprints
            SET name = ?, goal = ?, status = ?, start_date = ?, end_date = ?, is_current = ?
            WHERE id = ?;
        )SQL");
        upd.bind_text(1, name);
        upd.bind_text(2, goal);
        upd.bind_text(3, status);
        upd.bind_text(4, start_date);
        upd.bind_text(5, end_date);
        upd.bind_int(6, is_current ? 1 : 0);
        upd.bind_int(7, sprint_id);
        upd.step_done();
        tx.commit();
    } catch (const std::exception &ex) {
        send_error(worker, header, "500 Internal Server Error", ex.what());
        return;
    }

    auto res = json::json_value::make_object();
    res.add_int("id", sprint_id);
    res.add_int("project_id", project_id);
    res.add_str("name", name);
    res.add_str("goal", goal);
    res.add_str("status", status);
    res.add_str("start_date", start_date);
    res.add_str("end_date", end_date);
    res.add_bool("is_current", is_current);
    send_json(worker, header, "200 OK", res);
}

// POST /api/sprints/([0-9]+)/current
static void handle_sprint_set_current(backend::wsworker *worker,
                                      const std::map<std::string, std::string> &header,
                                      const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing sprint ID");
        return;
    }
    int sprint_id = std::atoi(uri_params.front().c_str());

    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }
    if (u_auth.role != "Admin") {
        send_error(worker, header, "403 Forbidden", "Only Administrators can set the Current Sprint");
        return;
    }

    try {
        db::sqlite_transaction tx(conn);
        auto clr = conn.prepare("UPDATE sprints SET is_current = 0;");
        clr.step_done();

        auto setc = conn.prepare("UPDATE sprints SET is_current = 1, status = 'active' WHERE id = ?;");
        setc.bind_int(1, sprint_id);
        setc.step_done();
        tx.commit();
    } catch (const std::exception &ex) {
        send_error(worker, header, "500 Internal Server Error", ex.what());
        return;
    }

    auto res = json::json_value::make_object();
    res.add_bool("ok", true);
    res.add_int("current_sprint_id", sprint_id);
    send_json(worker, header, "200 OK", res);
}

static int resolve_default_resolution_id(db::sqlite_db &conn) {
    auto stmt = conn.prepare("SELECT id FROM resolutions WHERE is_default = 1 ORDER BY id ASC LIMIT 1;");
    if (stmt.step() == SQLITE_ROW) {
        return stmt.column_int(0);
    }
    return 1;
}

static void sync_item_components_and_versions(db::sqlite_db &conn,
                                              int item_id,
                                              const json::json_value &body,
                                              bool is_create) {
    if (is_create || body.has_key("component_ids")) {
        auto del = conn.prepare("DELETE FROM work_item_components WHERE work_item_id = ?;");
        del.bind_int(1, item_id);
        del.step_done();

        auto comp_ids = body.get_int_array("component_ids");
        auto ins = conn.prepare("INSERT OR IGNORE INTO work_item_components (work_item_id, component_id) VALUES (?, ?);");
        for (int cid : comp_ids) {
            ins.clear_and_reset();
            ins.bind_int(1, item_id);
            ins.bind_int(2, cid);
            ins.step_done();
        }
    }

    if (is_create || body.has_key("fix_version_ids")) {
        auto del = conn.prepare("DELETE FROM work_item_versions WHERE work_item_id = ? AND relation_type = 'fix';");
        del.bind_int(1, item_id);
        del.step_done();

        auto fix_ids = body.get_int_array("fix_version_ids");
        auto ins = conn.prepare("INSERT OR IGNORE INTO work_item_versions (work_item_id, version_id, relation_type) VALUES (?, ?, 'fix');");
        for (int vid : fix_ids) {
            ins.clear_and_reset();
            ins.bind_int(1, item_id);
            ins.bind_int(2, vid);
            ins.step_done();
        }
    }

    if (is_create || body.has_key("affects_version_ids")) {
        auto del = conn.prepare("DELETE FROM work_item_versions WHERE work_item_id = ? AND relation_type = 'affects';");
        del.bind_int(1, item_id);
        del.step_done();

        auto aff_ids = body.get_int_array("affects_version_ids");
        auto ins = conn.prepare("INSERT OR IGNORE INTO work_item_versions (work_item_id, version_id, relation_type) VALUES (?, ?, 'affects');");
        for (int vid : aff_ids) {
            ins.clear_and_reset();
            ins.bind_int(1, item_id);
            ins.bind_int(2, vid);
            ins.step_done();
        }
    }
}

struct raw_cf_input {
    bool present = false;
    bool is_null = false;
    bool is_array = false;
    std::string str_val;
    std::string json_arr_val;
};

static std::map<std::string, raw_cf_input> extract_custom_fields_from_body(const json::json_value &body) {
    std::map<std::string, raw_cf_input> out;
    if (!body) return out;

    auto parse_elem = [](json_object *val_obj) -> raw_cf_input {
        raw_cf_input inp;
        inp.present = true;
        if (!val_obj || json_object_is_type(val_obj, json_type_null)) {
            inp.is_null = true;
            return inp;
        }
        if (json_object_is_type(val_obj, json_type_array)) {
            inp.is_array = true;
            const char *js = json_object_to_json_string_ext(val_obj, JSON_C_TO_STRING_PLAIN);
            inp.json_arr_val = js ? js : "[]";
            std::ostringstream oss;
            size_t len = json_object_array_length(val_obj);
            for (size_t i = 0; i < len; ++i) {
                json_object *el = json_object_array_get_idx(val_obj, i);
                if (el && !json_object_is_type(el, json_type_null)) {
                    const char *s = json_object_get_string(el);
                    if (s && *s) {
                        if (oss.tellp() > 0) oss << ", ";
                        oss << s;
                    }
                }
            }
            inp.str_val = oss.str();
            return inp;
        }
        if (json_object_is_type(val_obj, json_type_boolean)) {
            inp.str_val = json_object_get_boolean(val_obj) ? "true" : "false";
            return inp;
        }
        const char *s = json_object_get_string(val_obj);
        inp.str_val = s ? s : "";
        return inp;
    };

    const char *keys_to_check[] = {"custom_fields", "custom_field_values"};
    for (const char *top_key : keys_to_check) {
        json_object *cf_container = nullptr;
        if (!json_object_object_get_ex(body.get(), top_key, &cf_container) || !cf_container) continue;

        if (json_object_is_type(cf_container, json_type_object)) {
            json_object_object_foreach(cf_container, k, v) {
                if (k) {
                    out[std::string(k)] = parse_elem(v);
                }
            }
        } else if (json_object_is_type(cf_container, json_type_array)) {
            size_t len = json_object_array_length(cf_container);
            for (size_t i = 0; i < len; ++i) {
                json_object *entry = json_object_array_get_idx(cf_container, i);
                if (!entry || !json_object_is_type(entry, json_type_object)) continue;
                json_object *fk_obj = nullptr;
                json_object *id_obj = nullptr;
                json_object *val_obj = nullptr;
                json_object_object_get_ex(entry, "field_key", &fk_obj);
                json_object_object_get_ex(entry, "custom_field_id", &id_obj);
                json_object_object_get_ex(entry, "value", &val_obj);
                raw_cf_input parsed = parse_elem(val_obj);
                if (fk_obj && !json_object_is_type(fk_obj, json_type_null)) {
                    const char *fk = json_object_get_string(fk_obj);
                    if (fk && *fk) out[std::string(fk)] = parsed;
                }
                if (id_obj && !json_object_is_type(id_obj, json_type_null)) {
                    int cid = json_object_get_int(id_obj);
                    if (cid > 0) out[std::to_string(cid)] = parsed;
                }
            }
        }
    }
    return out;
}

static bool validate_custom_fields_for_item(db::sqlite_db &conn,
                                            int project_id,
                                            const std::string &issue_type,
                                            const json::json_value &body,
                                            bool is_create,
                                            int existing_item_id,
                                            std::string &err_msg) {
    auto inputs = extract_custom_fields_from_body(body);

    int config_id = 1;
    {
        auto fcs = conn.prepare(R"SQL(
            SELECT configuration_id
            FROM field_configuration_schemes
            WHERE project_id = ? OR is_default = 1
            ORDER BY CASE WHEN project_id = ? THEN 0 ELSE 1 END, id ASC
            LIMIT 1;
        )SQL");
        fcs.bind_int(1, project_id);
        fcs.bind_int(2, project_id);
        if (fcs.step() == SQLITE_ROW) {
            config_id = fcs.column_int(0);
        }
    }

    auto stmt = conn.prepare(R"SQL(
        SELECT cf.id, cf.name, cf.field_key, cf.field_type,
               COALESCE(NULLIF(fci.default_value, ''), cf.default_value, ''),
               COALESCE(fci.is_required, cf.is_required, 0),
               COALESCE(fci.is_hidden, 0),
               cf.min_value IS NOT NULL, COALESCE(cf.min_value, 0.0),
               cf.max_value IS NOT NULL, COALESCE(cf.max_value, 0.0)
        FROM custom_fields cf
        LEFT JOIN field_configuration_items fci
          ON fci.custom_field_id = cf.id
         AND fci.configuration_id = ?
         AND fci.issue_type = COALESCE(
               (SELECT issue_type FROM field_configuration_items
                WHERE configuration_id = ? AND custom_field_id = cf.id AND issue_type IN (?, '*')
                ORDER BY CASE WHEN issue_type = ? THEN 0 ELSE 1 END LIMIT 1),
               '*'
             )
        ORDER BY cf.sort_order ASC, cf.id ASC;
    )SQL");
    stmt.bind_int(1, config_id);
    stmt.bind_int(2, config_id);
    stmt.bind_text(3, issue_type);
    stmt.bind_text(4, issue_type);

    while (stmt.step() == SQLITE_ROW) {
        int cf_id = stmt.column_int(0);
        std::string cf_name = stmt.column_str(1);
        std::string cf_key = stmt.column_str(2);
        std::string cf_type = stmt.column_str(3);
        std::string def_val = stmt.column_str(4);
        bool is_req = stmt.column_int(5) != 0;
        bool is_hidden = stmt.column_int(6) != 0;
        bool has_min = stmt.column_int(7) != 0;
        double min_val = stmt.column_double(8);
        bool has_max = stmt.column_int(9) != 0;
        double max_val = stmt.column_double(10);

        if (cf_type == "formula") continue;

        raw_cf_input inp;
        auto it = inputs.find(cf_key);
        if (it != inputs.end()) {
            inp = it->second;
        } else {
            auto it_id = inputs.find(std::to_string(cf_id));
            if (it_id != inputs.end()) {
                inp = it_id->second;
            }
        }

        // Check required constraint when field is required and not hidden
        if (is_req && !is_hidden) {
            bool has_non_empty = false;
            if (inp.present && !inp.is_null && !inp.str_val.empty()) {
                if (cf_type == "user_picker_single") {
                    has_non_empty = (std::atoi(inp.str_val.c_str()) > 0);
                } else {
                    has_non_empty = true;
                }
            } else if (!inp.present && !is_create && existing_item_id > 0) {
                // Check if existing item already has a stored value
                auto ex = conn.prepare(R"SQL(
                    SELECT COALESCE(text_value, ''), COALESCE(date_value, ''), COALESCE(user_value_id, 0), numeric_value IS NOT NULL
                    FROM custom_field_values
                    WHERE work_item_id = ? AND custom_field_id = ?;
                )SQL");
                ex.bind_int(1, existing_item_id);
                ex.bind_int(2, cf_id);
                if (ex.step() == SQLITE_ROW) {
                    if (!ex.column_str(0).empty() || !ex.column_str(1).empty() || ex.column_int(2) > 0 || ex.column_int(3) != 0) {
                        has_non_empty = true;
                    }
                }
            }
            if (!has_non_empty) {
                err_msg = "Mandatory custom field '" + cf_name + "' (" + cf_key + ") is required";
                return false;
            }
        }

        // Validate value format & bounds when non-empty
        if (inp.present && !inp.is_null && !inp.str_val.empty()) {
            if (cf_type == "number") {
                double num = 0.0;
                try {
                    size_t idx = 0;
                    num = std::stod(inp.str_val, &idx);
                } catch (...) {
                    err_msg = "Custom field '" + cf_name + "' requires a valid numeric value";
                    return false;
                }
                if (has_min && num < min_val) {
                    err_msg = "Custom field '" + cf_name + "' must be >= " + std::to_string(min_val);
                    return false;
                }
                if (has_max && num > max_val) {
                    err_msg = "Custom field '" + cf_name + "' must be <= " + std::to_string(max_val);
                    return false;
                }
            } else if (cf_type == "url") {
                if (!inp.str_val.starts_with("http://") && !inp.str_val.starts_with("https://")) {
                    err_msg = "Custom field '" + cf_name + "' requires a valid http:// or https:// URL";
                    return false;
                }
            }
        }
    }
    return true;
}

static void sync_item_custom_fields(db::sqlite_db &conn,
                                    int item_id,
                                    const json::json_value &body,
                                    bool is_create) {
    auto inputs = extract_custom_fields_from_body(body);
    if (!is_create && inputs.empty()) return;

    auto stmt = conn.prepare(R"SQL(
        SELECT id, field_key, field_type, default_value
        FROM custom_fields
        ORDER BY id ASC;
    )SQL");

    auto ups = conn.prepare(R"SQL(
        INSERT INTO custom_field_values (
            work_item_id, custom_field_id, text_value, numeric_value, date_value, user_value_id, json_value, updated_at
        ) VALUES (?, ?, ?, ?, ?, ?, ?, datetime('now'))
        ON CONFLICT(work_item_id, custom_field_id) DO UPDATE SET
            text_value    = excluded.text_value,
            numeric_value = excluded.numeric_value,
            date_value    = excluded.date_value,
            user_value_id = excluded.user_value_id,
            json_value    = excluded.json_value,
            updated_at    = datetime('now');
    )SQL");

    while (stmt.step() == SQLITE_ROW) {
        int cf_id = stmt.column_int(0);
        std::string cf_key = stmt.column_str(1);
        std::string cf_type = stmt.column_str(2);
        std::string def_val = stmt.column_str(3);

        if (cf_type == "formula") continue;

        raw_cf_input inp;
        auto it = inputs.find(cf_key);
        if (it != inputs.end()) {
            inp = it->second;
        } else {
            auto it_id = inputs.find(std::to_string(cf_id));
            if (it_id != inputs.end()) {
                inp = it_id->second;
            }
        }

        if (!inp.present) {
            if (is_create && !def_val.empty()) {
                inp.present = true;
                inp.str_val = def_val;
                if (cf_type == "select_multi" && def_val.starts_with("[")) {
                    inp.is_array = true;
                    inp.json_arr_val = def_val;
                }
            } else {
                continue;
            }
        }

        std::string text_val = inp.is_null ? "" : inp.str_val;
        bool has_num = false;
        double num_val = 0.0;
        std::string date_val;
        int user_id = 0;
        std::string json_val = inp.json_arr_val;

        if (!inp.is_null && !text_val.empty()) {
            if (cf_type == "number") {
                try {
                    num_val = std::stod(text_val);
                    has_num = true;
                } catch (...) {}
            } else if (cf_type == "date" || cf_type == "datetime") {
                date_val = text_val;
            } else if (cf_type == "user_picker_single") {
                user_id = std::atoi(text_val.c_str());
                if (user_id <= 0) text_val.clear();
            } else if (cf_type == "checkbox") {
                bool b = (text_val == "true" || text_val == "1");
                text_val = b ? "true" : "false";
                has_num = true;
                num_val = b ? 1.0 : 0.0;
            } else if (cf_type == "select_multi" && json_val.empty()) {
                // Convert comma-separated text_val into a JSON array string
                auto jarr = json::json_value::make_array();
                std::stringstream ss(text_val);
                std::string token;
                while (std::getline(ss, token, ',')) {
                    size_t start = token.find_first_not_of(" \t");
                    size_t end = token.find_last_not_of(" \t");
                    if (start != std::string::npos) {
                        jarr.add(json::json_value::make_string(token.substr(start, end - start + 1)));
                    }
                }
                json_val = jarr.to_string();
            }
        } else if (cf_type == "checkbox" && !inp.is_null) {
            text_val = "false";
            has_num = true;
            num_val = 0.0;
        }

        ups.clear_and_reset();
        ups.bind_int(1, item_id);
        ups.bind_int(2, cf_id);
        ups.bind_text(3, text_val);
        if (has_num) ups.bind_double(4, num_val); else ups.bind_null(4);
        ups.bind_text(5, date_val);
        ups.bind_optional_int(6, user_id);
        ups.bind_text(7, json_val);
        ups.step_done();
    }
}

static double evaluate_custom_formula(const std::string &formula_expr, int story_points, int progress) {
    if (formula_expr.find("story_points") != std::string::npos) {
        auto star = formula_expr.find('*');
        if (star != std::string::npos) {
            double mult = std::atof(formula_expr.substr(star + 1).c_str());
            if (mult <= 0.0) mult = 4.0;
            return static_cast<double>(story_points) * mult;
        }
        return static_cast<double>(story_points) * 4.0;
    }
    if (formula_expr.find("progress") != std::string::npos) {
        return static_cast<double>(progress);
    }
    return static_cast<double>(story_points * 4);
}

static json::json_value row_to_work_item_json(db::sqlite_db &conn, db::sqlite_stmt &stmt) {
    auto item = json::json_value::make_object();
    int item_id = stmt.column_int(0);
    item.add_int("id", item_id);
    item.add_int("project_id", stmt.column_int(1));
    item.add_str("key", stmt.column_str(2));
    item.add_str("type", stmt.column_str(3));
    item.add_str("status", stmt.column_str(4));
    item.add_str("priority", stmt.column_str(5));
    item.add_str("title", stmt.column_str(6));
    item.add_str("description", stmt.column_str(7));
    item.add_nullable_int("parent_id", stmt.column_int(8));
    item.add_nullable_int("sprint_id", stmt.column_int(9));
    item.add_nullable_int("assignee_id", stmt.column_int(10));
    item.add_nullable_int("reporter_id", stmt.column_int(11));
    item.add_int("story_points", stmt.column_int(12));
    item.add_int("progress", stmt.column_int(13));
    item.add_str("start_date", stmt.column_str(14));
    item.add_str("due_date", stmt.column_str(15));
    item.add_str("severity", stmt.column_str(16));
    item.add_str("steps_to_reproduce", stmt.column_str(17));
    item.add_str("labels", stmt.column_str(18));
    item.add_int("board_order", stmt.column_int(19));
    item.add_str("created_at", stmt.column_str(20));
    item.add_str("updated_at", stmt.column_str(21));
    item.add_str("assignee_name", stmt.column_str(22));
    item.add_str("assignee_color", stmt.column_str(23));
    item.add_str("reporter_name", stmt.column_str(24));
    item.add_str("parent_key", stmt.column_str(25));
    item.add_str("parent_title", stmt.column_str(26));
    item.add_str("sprint_name", stmt.column_str(27));
    item.add_int("subtask_count", stmt.column_int(28));
    item.add_int("comment_count", stmt.column_int(29));
    item.add_int("attachment_count", stmt.column_int(30));
    item.add_str("category", stmt.column_str(31));
    item.add_nullable_int("resolution_id", stmt.column_int(32));
    item.add_str("resolution_name", stmt.column_str(33));
    item.add_str("resolution_date", stmt.column_str(34));

    // Load associated components
    auto comp_arr = json::json_value::make_array();
    auto comp_ids_arr = json::json_value::make_array();
    {
        auto cstmt = conn.prepare(R"SQL(
            SELECT c.id, c.name
            FROM work_item_components wc
            INNER JOIN components c ON c.id = wc.component_id
            WHERE wc.work_item_id = ?
            ORDER BY c.name ASC;
        )SQL");
        cstmt.bind_int(1, item_id);
        while (cstmt.step() == SQLITE_ROW) {
            int cid = cstmt.column_int(0);
            auto co = json::json_value::make_object();
            co.add_int("id", cid);
            co.add_str("name", cstmt.column_str(1));
            comp_arr.add(std::move(co));
            comp_ids_arr.add(json::json_value::make_int(cid));
        }
    }
    item.add("components", std::move(comp_arr));
    item.add("component_ids", std::move(comp_ids_arr));

    // Load associated fix_versions and affects_versions
    auto fix_arr = json::json_value::make_array();
    auto fix_ids_arr = json::json_value::make_array();
    auto aff_arr = json::json_value::make_array();
    auto aff_ids_arr = json::json_value::make_array();
    {
        auto vstmt = conn.prepare(R"SQL(
            SELECT v.id, v.name, v.status, wv.relation_type
            FROM work_item_versions wv
            INNER JOIN versions v ON v.id = wv.version_id
            WHERE wv.work_item_id = ?
            ORDER BY v.id ASC;
        )SQL");
        vstmt.bind_int(1, item_id);
        while (vstmt.step() == SQLITE_ROW) {
            int vid = vstmt.column_int(0);
            std::string rel = vstmt.column_str(3);
            auto vo = json::json_value::make_object();
            vo.add_int("id", vid);
            vo.add_str("name", vstmt.column_str(1));
            vo.add_str("status", vstmt.column_str(2));
            if (rel == "affects") {
                aff_arr.add(std::move(vo));
                aff_ids_arr.add(json::json_value::make_int(vid));
            } else {
                fix_arr.add(std::move(vo));
                fix_ids_arr.add(json::json_value::make_int(vid));
            }
        }
    }
    item.add("fix_versions", std::move(fix_arr));
    item.add("fix_version_ids", std::move(fix_ids_arr));
    item.add("affects_versions", std::move(aff_arr));
    item.add("affects_version_ids", std::move(aff_ids_arr));

    // Load custom fields EAV values (cached prepared statement)
    int story_pts = stmt.column_int(12);
    int prog = stmt.column_int(13);
    auto cf_map = json::json_value::make_object();
    auto cf_entries = json::json_value::make_array();
    {
        auto cfstmt = conn.prepare(R"SQL(
            SELECT cf.id, cf.field_key, cf.name, cf.field_type, cf.formula_expr, cf.is_required,
                   COALESCE(cfv.text_value, ''),
                   cfv.numeric_value IS NOT NULL, COALESCE(cfv.numeric_value, 0.0),
                   COALESCE(cfv.date_value, ''),
                   COALESCE(cfv.user_value_id, 0),
                   COALESCE(u.display_name, ''),
                   COALESCE(cfv.json_value, '')
            FROM custom_fields cf
            LEFT JOIN custom_field_values cfv ON cfv.custom_field_id = cf.id AND cfv.work_item_id = ?
            LEFT JOIN users u ON u.id = cfv.user_value_id
            ORDER BY cf.sort_order ASC, cf.id ASC;
        )SQL");
        cfstmt.bind_int(1, item_id);
        while (cfstmt.step() == SQLITE_ROW) {
            int cf_id = cfstmt.column_int(0);
            std::string fkey = cfstmt.column_str(1);
            std::string fname = cfstmt.column_str(2);
            std::string ftype = cfstmt.column_str(3);
            std::string formula_expr = cfstmt.column_str(4);
            bool is_req = cfstmt.column_int(5) != 0;
            std::string txt_val = cfstmt.column_str(6);
            bool has_num = cfstmt.column_int(7) != 0;
            double num_val = cfstmt.column_double(8);
            std::string date_val = cfstmt.column_str(9);
            int user_val_id = cfstmt.column_int(10);
            std::string user_name = cfstmt.column_str(11);
            std::string json_val = cfstmt.column_str(12);

            auto entry = json::json_value::make_object();
            entry.add_int("custom_field_id", cf_id);
            entry.add_str("field_key", fkey);
            entry.add_str("name", fname);
            entry.add_str("field_type", ftype);
            entry.add_bool("is_required", is_req);
            entry.add_nullable_int("user_value_id", user_val_id);
            entry.add_str("user_display_name", user_name);

            if (ftype == "formula") {
                double calc = evaluate_custom_formula(formula_expr, story_pts, prog);
                cf_map.add_double(fkey.c_str(), calc);
                entry.add_double("value", calc);
                std::ostringstream oss;
                oss << calc;
                entry.add_str("display_value", oss.str());
            } else if (ftype == "number") {
                if (has_num) {
                    cf_map.add_double(fkey.c_str(), num_val);
                    entry.add_double("value", num_val);
                    entry.add_str("display_value", txt_val);
                } else {
                    cf_map.add(fkey.c_str(), json::json_value::make_null());
                    entry.add("value", json::json_value::make_null());
                    entry.add_str("display_value", "");
                }
            } else if (ftype == "checkbox") {
                bool b = (txt_val == "true" || txt_val == "1" || (has_num && num_val != 0.0));
                cf_map.add_bool(fkey.c_str(), b);
                entry.add_bool("value", b);
                entry.add_str("display_value", b ? "Yes" : "No");
            } else if (ftype == "user_picker_single") {
                cf_map.add_nullable_int(fkey.c_str(), user_val_id);
                entry.add_nullable_int("value", user_val_id);
                entry.add_str("display_value", user_name);
            } else if (ftype == "select_multi") {
                auto parsed_arr = json::json_value::parse(json_val);
                if (parsed_arr && json_object_is_type(parsed_arr.get(), json_type_array)) {
                    cf_map.add(fkey.c_str(), std::move(parsed_arr));
                } else {
                    cf_map.add(fkey.c_str(), json::json_value::make_array());
                }
                auto parsed_arr2 = json::json_value::parse(json_val);
                if (parsed_arr2 && json_object_is_type(parsed_arr2.get(), json_type_array)) {
                    entry.add("value", std::move(parsed_arr2));
                } else {
                    entry.add("value", json::json_value::make_array());
                }
                entry.add_str("display_value", txt_val);
            } else {
                std::string eff = (!date_val.empty()) ? date_val : txt_val;
                cf_map.add_str(fkey.c_str(), eff);
                entry.add_str("value", eff);
                entry.add_str("display_value", eff);
            }
            cf_entries.add(std::move(entry));
        }
    }
    item.add("custom_fields", std::move(cf_map));
    item.add("custom_field_entries", std::move(cf_entries));

    return item;
}

static const char *BASE_WORK_ITEM_SELECT = R"SQL(
    SELECT
        w.id, w.project_id, w.key, w.type, w.status, w.priority,
        w.title, w.description, w.parent_id, w.sprint_id,
        w.assignee_id, w.reporter_id, w.story_points, w.progress,
        w.start_date, w.due_date, w.severity, w.steps_to_reproduce,
        w.labels, w.board_order, w.created_at, w.updated_at,
        COALESCE(ua.display_name, ''), COALESCE(ua.avatar_color, '#6B778C'),
        COALESCE(ur.display_name, ''),
        COALESCE(wp.key, ''), COALESCE(wp.title, ''),
        COALESCE(sp.name, ''),
        (SELECT COUNT(*) FROM work_items sub WHERE sub.parent_id = w.id),
        (SELECT COUNT(*) FROM comments c WHERE c.work_item_id = w.id),
        (SELECT COUNT(*) FROM attachments a WHERE a.work_item_id = w.id),
        w.category,
        w.resolution_id,
        COALESCE(res.name, ''),
        COALESCE(w.resolution_date, '')
    FROM work_items w
    LEFT JOIN users ua ON ua.id = w.assignee_id
    LEFT JOIN users ur ON ur.id = w.reporter_id
    LEFT JOIN work_items wp ON wp.id = w.parent_id
    LEFT JOIN sprints sp ON sp.id = w.sprint_id
    LEFT JOIN resolutions res ON res.id = w.resolution_id
)SQL";

// ============================================================================
// Epic 3 / Step 5: JQL Search Engine, Autocomplete Suggestions & Saved Filters
// ============================================================================

static std::string csv_escape(const std::string &val) {
    bool needs_quotes = false;
    for (char c : val) {
        if (c == ',' || c == '"' || c == '\n' || c == '\r') {
            needs_quotes = true;
            break;
        }
    }
    if (!needs_quotes) return val;
    std::string out = "\"";
    for (char c : val) {
        if (c == '"') out += "\"\"";
        else out.push_back(c);
    }
    out.push_back('"');
    return out;
}

static json::json_value serialize_jql_tokens(std::string_view jql_str) {
    auto arr = json::json_value::make_array();
    try {
        jql::JqlLexer lexer(jql_str);
        auto tokens = lexer.tokenize();
        for (const auto &t : tokens) {
            if (t.type == jql::TokenType::EndOfInput) continue;
            auto tobj = json::json_value::make_object();
            tobj.add_str("type", jql::token_type_name(t.type));
            tobj.add_str("text", t.text);
            tobj.add_str("raw_text", t.raw_text);
            tobj.add_int("pos", static_cast<int>(t.pos));
            tobj.add_int("length", static_cast<int>(t.length));
            tobj.add_int("line", t.line);
            tobj.add_int("col", t.col);
            arr.add(std::move(tobj));
        }
    } catch (...) {
        // Ignore tokenization error during partial token serialization
    }
    return arr;
}

// GET | POST /api/search/jql
static void handle_search_jql(backend::wsworker *worker,
                              const std::map<std::string, std::string> &header,
                              const std::list<std::string> &) {
    auto t_start = std::chrono::steady_clock::now();
    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    std::string method = get_header(header, "REQUEST_METHOD");
    auto args = worker->parse_args(get_header(header, "QUERY_STRING"));

    std::string jql_str = args["jql"];
    std::string format = args["format"].empty() ? "json" : jql::to_lower_ascii(args["format"]);
    int limit = args["limit"].empty() ? 200 : std::clamp(std::atoi(args["limit"].c_str()), 1, 1000);
    int offset = args["offset"].empty() ? 0 : std::max(0, std::atoi(args["offset"].c_str()));
    bool validate_only = (args["validate_only"] == "1" || args["validate_only"] == "true");

    if (method == "POST") {
        auto body = json::json_value::parse(worker->body());
        if (body) {
            if (body.has_key("jql")) jql_str = body.get_string("jql");
            if (body.has_key("format")) format = jql::to_lower_ascii(body.get_string("format", "json"));
            if (body.has_key("limit")) limit = std::clamp(body.get_int("limit", 200), 1, 1000);
            if (body.has_key("offset")) offset = std::max(0, body.get_int("offset", 0));
            if (body.has_key("validate_only")) validate_only = body.get_bool("validate_only", false);
        }
    }

    try {
        jql::JqlLexer lexer(jql_str);
        auto tokens = lexer.tokenize();
        jql::JqlParser parser(tokens);
        auto ast = parser.parse();

        jql::SqlContext ctx(conn,
                            u_auth.authenticated ? u_auth.id : 1,
                            u_auth.authenticated ? u_auth.username : "admin",
                            u_auth.authenticated ? u_auth.display_name : "System Administrator");

        std::string where_sql = ast.to_where_sql(ctx);
        std::string order_by_sql = ast.to_order_by_sql(ctx);

        std::string full_sql = std::string(BASE_WORK_ITEM_SELECT) +
                               " WHERE " + where_sql +
                               " ORDER BY " + order_by_sql +
                               " LIMIT " + std::to_string(limit) +
                               " OFFSET " + std::to_string(offset) + ";";

        auto params_arr = json::json_value::make_array();
        for (const auto &p : ctx.params) {
            params_arr.add(json::json_value::make_string(p));
        }

        auto tok_arr = json::json_value::make_array();
        for (const auto &t : tokens) {
            if (t.type == jql::TokenType::EndOfInput) continue;
            auto tobj = json::json_value::make_object();
            tobj.add_str("type", jql::token_type_name(t.type));
            tobj.add_str("text", t.text);
            tobj.add_str("raw_text", t.raw_text);
            tobj.add_int("pos", static_cast<int>(t.pos));
            tobj.add_int("length", static_cast<int>(t.length));
            tobj.add_int("line", t.line);
            tobj.add_int("col", t.col);
            tok_arr.add(std::move(tobj));
        }

        std::string ast_debug = ast.where_root ? ast.where_root->to_debug_string() : "(ALL ISSUES)";
        if (!ast.order_by.empty()) {
            ast_debug += " ORDER BY ";
            for (size_t i = 0; i < ast.order_by.size(); ++i) {
                if (i > 0) ast_debug += ", ";
                ast_debug += ast.order_by[i].field + (ast.order_by[i].descending ? " DESC" : " ASC");
            }
        }

        if (validate_only) {
            auto t_end = std::chrono::steady_clock::now();
            double elapsed_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
            auto res = json::json_value::make_object();
            res.add_bool("valid", true);
            res.add_str("jql", jql_str);
            res.add_str("where_sql", where_sql);
            res.add_str("order_by_sql", order_by_sql);
            res.add_str("compiled_sql", full_sql);
            res.add("bind_params", std::move(params_arr));
            res.add_str("ast_summary", ast_debug);
            res.add_bool("uses_fts5", ctx.uses_fts5);
            res.add_double("execution_time_ms", elapsed_ms);
            res.add("tokens", std::move(tok_arr));
            send_json(worker, header, "200 OK", res);
            return;
        }

        // Count total matching items
        int total_count = 0;
        {
            std::string count_sql =
                "SELECT COUNT(*) FROM work_items w "
                "LEFT JOIN users ua ON ua.id = w.assignee_id "
                "LEFT JOIN users ur ON ur.id = w.reporter_id "
                "LEFT JOIN work_items wp ON wp.id = w.parent_id "
                "LEFT JOIN sprints sp ON sp.id = w.sprint_id "
                "LEFT JOIN resolutions res ON res.id = w.resolution_id "
                "WHERE " + where_sql + ";";
            auto cstmt = conn.prepare(count_sql);
            for (size_t i = 0; i < ctx.params.size(); ++i) {
                cstmt.bind_text(static_cast<int>(i + 1), ctx.params[i]);
            }
            if (cstmt.step() == SQLITE_ROW) {
                total_count = cstmt.column_int(0);
            }
        }

        // Compute BM25 scores map if FTS5 was used
        std::unordered_map<int, double> bm25_scores;
        if (ctx.uses_fts5 && !ctx.primary_fts_query.empty()) {
            try {
                auto fstmt = conn.prepare(
                    "SELECT CAST(work_item_id AS INTEGER), bm25(work_items_fts, 0.0, 5.0, 4.0, 2.0, 1.5, 2.0, 1.0) "
                    "FROM work_items_fts WHERE work_items_fts MATCH ?;");
                fstmt.bind_text(1, ctx.primary_fts_query);
                while (fstmt.step() == SQLITE_ROW) {
                    bm25_scores[fstmt.column_int(0)] = -fstmt.column_double(1);
                }
            } catch (...) {
                // Ignore if FTS match is column-scoped
            }
        }

        auto stmt = conn.prepare(full_sql);
        for (size_t i = 0; i < ctx.params.size(); ++i) {
            stmt.bind_text(static_cast<int>(i + 1), ctx.params[i]);
        }

        // Export formats: CSV, Markdown, Printable HTML
        if (format == "csv") {
            std::ostringstream csv;
            csv << "Key,Type,Category,Title,Status,Priority,Resolution,Assignee,Reporter,Sprint,Story Points,Progress,Due Date,Created,Updated,Labels\n";
            while (stmt.step() == SQLITE_ROW) {
                csv << csv_escape(stmt.column_str(2)) << ","
                    << csv_escape(stmt.column_str(3)) << ","
                    << csv_escape(stmt.column_str(31)) << ","
                    << csv_escape(stmt.column_str(6)) << ","
                    << csv_escape(stmt.column_str(4)) << ","
                    << csv_escape(stmt.column_str(5)) << ","
                    << csv_escape(stmt.column_str(33)) << ","
                    << csv_escape(stmt.column_str(22)) << ","
                    << csv_escape(stmt.column_str(24)) << ","
                    << csv_escape(stmt.column_str(27)) << ","
                    << stmt.column_int(12) << ","
                    << stmt.column_int(13) << "%,"
                    << csv_escape(stmt.column_str(15)) << ","
                    << csv_escape(stmt.column_str(20)) << ","
                    << csv_escape(stmt.column_str(21)) << ","
                    << csv_escape(stmt.column_str(18)) << "\n";
            }
            std::string body_str = csv.str();
            std::string proto = get_header(header, "SERVER_PROTOCOL");
            if (proto.empty()) proto = "HTTP/1.1";
            FCGX_FPrintF(worker->out(),
                         "%s 200 OK\r\n"
                         "Status: 200 OK\r\n"
                         "Content-Type: text/csv; charset=utf-8\r\n"
                         "Content-Disposition: attachment; filename=\"jira-jql-export.csv\"\r\n"
                         "Content-Length: %d\r\n\r\n%s",
                         proto.c_str(), static_cast<int>(body_str.size()), body_str.c_str());
            return;
        }

        if (format == "markdown" || format == "md") {
            std::ostringstream md;
            md << "# JQL Search Export\n\n";
            md << "- **Query**: `" << (jql_str.empty() ? "(All Issues)" : jql_str) << "`\n";
            md << "- **Total Matches**: " << total_count << "\n\n";
            md << "| Key | Type | Title | Status | Priority | Assignee | Sprint | Points |\n";
            md << "| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :---: |\n";
            while (stmt.step() == SQLITE_ROW) {
                md << "| **" << stmt.column_str(2) << "** | "
                   << stmt.column_str(3) << " | "
                   << stmt.column_str(6) << " | `"
                   << stmt.column_str(4) << "` | "
                   << stmt.column_str(5) << " | "
                   << (stmt.column_str(22).empty() ? "Unassigned" : stmt.column_str(22)) << " | "
                   << (stmt.column_str(27).empty() ? "-" : stmt.column_str(27)) << " | "
                   << stmt.column_int(12) << " |\n";
            }
            std::string body_str = md.str();
            std::string proto = get_header(header, "SERVER_PROTOCOL");
            if (proto.empty()) proto = "HTTP/1.1";
            FCGX_FPrintF(worker->out(),
                         "%s 200 OK\r\n"
                         "Status: 200 OK\r\n"
                         "Content-Type: text/markdown; charset=utf-8\r\n"
                         "Content-Length: %d\r\n\r\n%s",
                         proto.c_str(), static_cast<int>(body_str.size()), body_str.c_str());
            return;
        }

        if (format == "html") {
            std::ostringstream html;
            html << "<!DOCTYPE html><html><head><meta charset='utf-8'><title>JQL Export</title>"
                 << "<style>body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;padding:24px;color:#172B4D}"
                 << "table{width:100%;border-collapse:collapse;margin-top:16px}th,td{border:1px solid #DFE1E6;padding:8px 12px;text-align:left;font-size:13px}"
                 << "th{background:#F4F5F7;font-weight:600}code{background:#F4F5F7;padding:2px 6px;border-radius:4px}</style></head><body>"
                 << "<h2>JQL Search Results (" << total_count << " issues)</h2>"
                 << "<p><strong>JQL:</strong> <code>" << (jql_str.empty() ? "(All Issues)" : jql_str) << "</code></p>"
                 << "<table><thead><tr><th>Key</th><th>Type</th><th>Summary</th><th>Status</th><th>Priority</th><th>Assignee</th><th>Sprint</th><th>Points</th></tr></thead><tbody>";
            while (stmt.step() == SQLITE_ROW) {
                html << "<tr><td><strong>" << stmt.column_str(2) << "</strong></td><td>" << stmt.column_str(3)
                     << "</td><td>" << stmt.column_str(6) << "</td><td>" << stmt.column_str(4)
                     << "</td><td>" << stmt.column_str(5) << "</td><td>"
                     << (stmt.column_str(22).empty() ? "Unassigned" : stmt.column_str(22))
                     << "</td><td>" << stmt.column_str(27) << "</td><td>" << stmt.column_int(12) << "</td></tr>";
            }
            html << "</tbody></table></body></html>";
            std::string body_str = html.str();
            std::string proto = get_header(header, "SERVER_PROTOCOL");
            if (proto.empty()) proto = "HTTP/1.1";
            FCGX_FPrintF(worker->out(),
                         "%s 200 OK\r\n"
                         "Status: 200 OK\r\n"
                         "Content-Type: text/html; charset=utf-8\r\n"
                         "Content-Length: %d\r\n\r\n%s",
                         proto.c_str(), static_cast<int>(body_str.size()), body_str.c_str());
            return;
        }

        auto items_arr = json::json_value::make_array();
        while (stmt.step() == SQLITE_ROW) {
            int wid = stmt.column_int(0);
            auto item_obj = row_to_work_item_json(conn, stmt);
            auto it_bm = bm25_scores.find(wid);
            if (it_bm != bm25_scores.end()) {
                item_obj.add_double("bm25_score", it_bm->second);
            }
            items_arr.add(std::move(item_obj));
        }

        auto t_end = std::chrono::steady_clock::now();
        double elapsed_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

        auto res = json::json_value::make_object();
        res.add_bool("valid", true);
        res.add_str("jql", jql_str);
        res.add_str("where_sql", where_sql);
        res.add_str("order_by_sql", order_by_sql);
        res.add_str("compiled_sql", full_sql);
        res.add("bind_params", std::move(params_arr));
        res.add_str("ast_summary", ast_debug);
        res.add_bool("uses_fts5", ctx.uses_fts5);
        res.add_int("total", total_count);
        res.add_int("limit", limit);
        res.add_int("offset", offset);
        res.add_double("execution_time_ms", elapsed_ms);
        res.add("tokens", std::move(tok_arr));
        res.add("items", std::move(items_arr));

        send_json(worker, header, "200 OK", res);
    } catch (const jql::JqlSyntaxError &syn) {
        auto err_obj = json::json_value::make_object();
        err_obj.add_bool("valid", false);
        err_obj.add_str("jql", jql_str);
        err_obj.add_str("error", syn.what());
        err_obj.add_int("error_pos", static_cast<int>(syn.pos));
        err_obj.add_int("error_length", static_cast<int>(syn.length));
        err_obj.add_int("error_line", syn.line);
        err_obj.add_int("error_col", syn.col);
        err_obj.add("tokens", serialize_jql_tokens(jql_str));
        send_json(worker, header, validate_only ? "200 OK" : "400 Bad Request", err_obj);
    } catch (const std::exception &ex) {
        send_error(worker, header, "400 Bad Request", ex.what());
    }
}

// POST /api/search/validate
static void handle_search_validate(backend::wsworker *worker,
                                   const std::map<std::string, std::string> &header,
                                   const std::list<std::string> &uri_params) {
    auto t_start = std::chrono::steady_clock::now();
    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    auto body = json::json_value::parse(worker->body());
    std::string jql_str = body ? body.get_string("jql") : "";
    (void)uri_params;

    try {
        jql::JqlLexer lexer(jql_str);
        auto tokens = lexer.tokenize();
        jql::JqlParser parser(tokens);
        auto ast = parser.parse();

        jql::SqlContext ctx(conn,
                            u_auth.authenticated ? u_auth.id : 1,
                            u_auth.authenticated ? u_auth.username : "admin",
                            u_auth.authenticated ? u_auth.display_name : "System Administrator");

        std::string where_sql = ast.to_where_sql(ctx);
        std::string order_by_sql = ast.to_order_by_sql(ctx);

        auto params_arr = json::json_value::make_array();
        for (const auto &p : ctx.params) {
            params_arr.add(json::json_value::make_string(p));
        }

        auto tok_arr = json::json_value::make_array();
        for (const auto &t : tokens) {
            if (t.type == jql::TokenType::EndOfInput) continue;
            auto tobj = json::json_value::make_object();
            tobj.add_str("type", jql::token_type_name(t.type));
            tobj.add_str("text", t.text);
            tobj.add_str("raw_text", t.raw_text);
            tobj.add_int("pos", static_cast<int>(t.pos));
            tobj.add_int("length", static_cast<int>(t.length));
            tobj.add_int("line", t.line);
            tobj.add_int("col", t.col);
            tok_arr.add(std::move(tobj));
        }

        std::string ast_debug = ast.where_root ? ast.where_root->to_debug_string() : "(ALL ISSUES)";
        if (!ast.order_by.empty()) {
            ast_debug += " ORDER BY ";
            for (size_t i = 0; i < ast.order_by.size(); ++i) {
                if (i > 0) ast_debug += ", ";
                ast_debug += ast.order_by[i].field + (ast.order_by[i].descending ? " DESC" : " ASC");
            }
        }

        auto t_end = std::chrono::steady_clock::now();
        double elapsed_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

        auto res = json::json_value::make_object();
        res.add_bool("valid", true);
        res.add_str("jql", jql_str);
        res.add_str("where_sql", where_sql);
        res.add_str("order_by_sql", order_by_sql);
        res.add_str("ast_summary", ast_debug);
        res.add_bool("uses_fts5", ctx.uses_fts5);
        res.add_double("execution_time_ms", elapsed_ms);
        res.add("bind_params", std::move(params_arr));
        res.add("tokens", std::move(tok_arr));
        send_json(worker, header, "200 OK", res);
    } catch (const jql::JqlSyntaxError &syn) {
        auto err_obj = json::json_value::make_object();
        err_obj.add_bool("valid", false);
        err_obj.add_str("jql", jql_str);
        err_obj.add_str("error", syn.what());
        err_obj.add_int("error_pos", static_cast<int>(syn.pos));
        err_obj.add_int("error_length", static_cast<int>(syn.length));
        err_obj.add_int("error_line", syn.line);
        err_obj.add_int("error_col", syn.col);
        err_obj.add("tokens", serialize_jql_tokens(jql_str));
        send_json(worker, header, "200 OK", err_obj);
    } catch (const std::exception &ex) {
        auto err_obj = json::json_value::make_object();
        err_obj.add_bool("valid", false);
        err_obj.add_str("jql", jql_str);
        err_obj.add_str("error", ex.what());
        send_json(worker, header, "200 OK", err_obj);
    }
}

// GET /api/search/suggestions
static void handle_search_suggestions(backend::wsworker *worker,
                                      const std::map<std::string, std::string> &header,
                                      const std::list<std::string> &) {
    auto conn = acquire_db_connection();

    auto fields_arr = json::json_value::make_array();
    auto add_field_meta = [&](const char *key, const char *label, const char *type, const char *desc) {
        auto f = json::json_value::make_object();
        f.add_str("name", key);
        f.add_str("label", label);
        f.add_str("type", type);
        f.add_str("description", desc);
        fields_arr.add(std::move(f));
    };

    add_field_meta("project", "Project", "select", "Project key, name, or ID (e.g., project = \"JIRA\")");
    add_field_meta("key", "Issue Key", "string", "Unique work item key (e.g., key = \"JIRA-1\")");
    add_field_meta("type", "Issue Type", "select", "Hierarchy type: Epic, Story, Task, Subtask, Issue");
    add_field_meta("category", "Category", "select", "Work category: Bug, New Feature, Improvement");
    add_field_meta("status", "Status", "status", "Workflow status (supports =, IN, WAS, CHANGED)");
    add_field_meta("statusCategory", "Status Category", "select", "Workflow status category: To Do, In Progress, Done");
    add_field_meta("priority", "Priority", "priority", "Issue priority (Critical, Highest, High, Medium, Low, Lowest)");
    add_field_meta("resolution", "Resolution", "select", "Issue resolution (Fixed, Won't Fix, Duplicate, or IS EMPTY)");
    add_field_meta("assignee", "Assignee", "user", "Assigned user or currentUser() / membersOf()");
    add_field_meta("reporter", "Reporter", "user", "Reporting user or currentUser()");
    add_field_meta("sprint", "Sprint", "sprint", "Sprint name, ID, or openSprints() / closedSprints()");
    add_field_meta("parent", "Parent / Epic", "string", "Parent Epic or Story key/ID");
    add_field_meta("component", "Component", "select", "Project component name or ID");
    add_field_meta("fixVersion", "Fix Version", "select", "Target release / fix version name");
    add_field_meta("affectedVersion", "Affects Version", "select", "Affected release version name");
    add_field_meta("summary", "Summary / Title", "text", "Full-text or substring search on issue title (~)");
    add_field_meta("description", "Description", "text", "Full-text or substring search on description (~)");
    add_field_meta("text", "Full-Text Search (FTS5)", "text", "SQLite FTS5 BM25 search across title, description, labels, steps, and comments");
    add_field_meta("comment", "Comments", "text", "Search inside issue discussion comments (~)");
    add_field_meta("labels", "Labels", "string", "Search issue tags/labels");
    add_field_meta("storyPoints", "Story Points", "number", "Numeric story points estimate (=, !=, >, >=, <, <=)");
    add_field_meta("progress", "Progress %", "number", "Completion percentage (0 to 100)");
    add_field_meta("created", "Created Date", "date", "Creation timestamp (supports now(), startOfDay(), startOfWeek())");
    add_field_meta("updated", "Updated Date", "date", "Last updated timestamp");
    add_field_meta("due", "Due Date", "date", "Target due date");
    add_field_meta("startDate", "Start Date", "date", "Planned start date");
    add_field_meta("resolved", "Resolution Date", "date", "Timestamp when the issue was resolved");

    auto values_map = json::json_value::make_object();

    // Dynamic custom fields
    {
        auto cfstmt = conn.prepare("SELECT id, field_key, name, field_type, description FROM custom_fields ORDER BY sort_order ASC, id ASC;");
        while (cfstmt.step() == SQLITE_ROW) {
            int cf_id = cfstmt.column_int(0);
            std::string fkey = cfstmt.column_str(1);
            std::string fname = cfstmt.column_str(2);
            std::string ftype = cfstmt.column_str(3);
            std::string fdesc = cfstmt.column_str(4);

            auto f = json::json_value::make_object();
            f.add_str("name", fkey);
            f.add_str("cf_syntax", "cf[" + fkey + "]");
            f.add_str("label", fname + " (Custom)");
            f.add_str("type", ftype);
            f.add_str("description", fdesc.empty() ? ("Custom field cf[" + std::to_string(cf_id) + "]") : fdesc);
            fields_arr.add(std::move(f));

            auto opt_stmt = conn.prepare("SELECT value FROM custom_field_options WHERE custom_field_id = ? AND is_active = 1 ORDER BY sort_order ASC;");
            opt_stmt.bind_int(1, cf_id);
            auto opt_arr = json::json_value::make_array();
            while (opt_stmt.step() == SQLITE_ROW) {
                opt_arr.add(json::json_value::make_string(opt_stmt.column_str(0)));
            }
            if (json_object_array_length(opt_arr.get()) > 0) {
                values_map.add(fkey.c_str(), std::move(opt_arr));
            }
        }
    }

    // Populate live field values for autocomplete
    auto query_str_list = [&](const char *sql) {
        auto arr = json::json_value::make_array();
        auto st = conn.prepare(sql);
        while (st.step() == SQLITE_ROW) {
            std::string v = st.column_str(0);
            if (!v.empty()) arr.add(json::json_value::make_string(v));
        }
        return arr;
    };

    values_map.add("project", query_str_list("SELECT key FROM projects ORDER BY id ASC;"));
    values_map.add("status", query_str_list("SELECT DISTINCT name FROM workflow_statuses ORDER BY sort_order ASC, id ASC;"));
    values_map.add("statusCategory", query_str_list("SELECT DISTINCT category FROM workflow_statuses ORDER BY id ASC;"));
    values_map.add("type", query_str_list("SELECT name FROM issue_types ORDER BY hierarchy_level DESC, id ASC;"));
    values_map.add("resolution", query_str_list("SELECT name FROM resolutions ORDER BY sort_order ASC, id ASC;"));
    values_map.add("assignee", query_str_list("SELECT username FROM users ORDER BY id ASC;"));
    values_map.add("reporter", query_str_list("SELECT username FROM users ORDER BY id ASC;"));
    values_map.add("sprint", query_str_list("SELECT name FROM sprints ORDER BY id ASC;"));
    values_map.add("component", query_str_list("SELECT DISTINCT name FROM components ORDER BY name ASC;"));
    values_map.add("fixVersion", query_str_list("SELECT DISTINCT name FROM versions ORDER BY id ASC;"));
    values_map.add("affectedVersion", query_str_list("SELECT DISTINCT name FROM versions ORDER BY id ASC;"));

    auto prio_arr = json::json_value::make_array();
    for (const char *p : {"Critical", "Highest", "High", "Medium", "Low", "Lowest"}) {
        prio_arr.add(json::json_value::make_string(p));
    }
    values_map.add("priority", std::move(prio_arr));

    auto cat_arr = json::json_value::make_array();
    for (const char *c : {"Bug", "New Feature", "Improvement"}) {
        cat_arr.add(json::json_value::make_string(c));
    }
    values_map.add("category", std::move(cat_arr));

    auto ops_arr = json::json_value::make_array();
    for (const char *op : {"=", "!=", "~", "!~", "IN", "NOT IN", "IS EMPTY", "IS NOT EMPTY", ">", ">=", "<", "<=", "WAS", "WAS IN", "WAS NOT", "CHANGED"}) {
        ops_arr.add(json::json_value::make_string(op));
    }

    auto funcs_arr = json::json_value::make_array();
    auto add_func = [&](const char *sig, const char *desc) {
        auto fn = json::json_value::make_object();
        fn.add_str("signature", sig);
        fn.add_str("description", desc);
        funcs_arr.add(std::move(fn));
    };
    add_func("currentUser()", "Matches the currently authenticated user");
    add_func("openSprints()", "Matches all currently active sprints");
    add_func("closedSprints()", "Matches all completed / closed sprints");
    add_func("futureSprints()", "Matches planned future sprints");
    add_func("now()", "Current date and time");
    add_func("now(\"-7d\")", "Relative timestamp offset (supports d, w, h, m, y)");
    add_func("startOfDay()", "Start of the current day (00:00:00)");
    add_func("endOfDay()", "End of the current day (23:59:59)");
    add_func("startOfWeek()", "Start of the current 7-day week window");
    add_func("endOfWeek()", "End of the current week");
    add_func("startOfMonth()", "First day of the current month");
    add_func("endOfMonth()", "Last day of the current month");
    add_func("membersOf(\"Developer\")", "All users belonging to the specified role or group");
    add_func("linkedIssues(\"JIRA-1\")", "All issues linked to the specified issue key");
    add_func("subtaskIssueTypes()", "All subtask hierarchy issue types");
    add_func("standardIssueTypes()", "All standard level-0 issue types");

    auto kw_arr = json::json_value::make_array();
    for (const char *k : {"AND", "OR", "NOT", "ORDER BY", "ASC", "DESC", "EMPTY", "NULL", "FROM", "TO", "BY", "AFTER", "BEFORE", "DURING"}) {
        kw_arr.add(json::json_value::make_string(k));
    }

    auto res = json::json_value::make_object();
    res.add("fields", std::move(fields_arr));
    res.add("operators", std::move(ops_arr));
    res.add("functions", std::move(funcs_arr));
    res.add("keywords", std::move(kw_arr));
    res.add("values", std::move(values_map));
    send_json(worker, header, "200 OK", res);
}

static json::json_value build_saved_filter_json(db::sqlite_db &conn, int filter_id, int current_user_id) {
    auto stmt = conn.prepare(R"SQL(
        SELECT sf.id, sf.owner_id, COALESCE(u.display_name, 'System'),
               sf.name, sf.description, sf.jql_query,
               sf.is_favorite, sf.is_system, sf.share_type, sf.share_target,
               sf.view_columns, sf.sort_order, sf.created_at, sf.updated_at,
               EXISTS (SELECT 1 FROM filter_stars fs WHERE fs.filter_id = sf.id AND fs.user_id = ?)
        FROM saved_filters sf
        LEFT JOIN users u ON u.id = sf.owner_id
        WHERE sf.id = ?;
    )SQL");
    stmt.bind_int(1, current_user_id);
    stmt.bind_int(2, filter_id);
    if (stmt.step() != SQLITE_ROW) {
        return json::json_value();
    }

    auto obj = json::json_value::make_object();
    obj.add_int("id", stmt.column_int(0));
    obj.add_nullable_int("owner_id", stmt.column_int(1));
    obj.add_str("owner_name", stmt.column_str(2));
    obj.add_str("name", stmt.column_str(3));
    obj.add_str("description", stmt.column_str(4));
    obj.add_str("jql_query", stmt.column_str(5));
    bool starred = (stmt.column_int(6) != 0) || (stmt.column_int(14) != 0);
    obj.add_bool("is_favorite", starred);
    obj.add_bool("is_system", stmt.column_int(7) != 0);
    obj.add_str("share_type", stmt.column_str(8));
    obj.add_str("share_target", stmt.column_str(9));
    obj.add_str("view_columns", stmt.column_str(10));
    obj.add_int("sort_order", stmt.column_int(11));
    obj.add_str("created_at", stmt.column_str(12));
    obj.add_str("updated_at", stmt.column_str(13));

    auto sub_stmt = conn.prepare(R"SQL(
        SELECT id, filter_id, user_id, cron_schedule, frequency_label, recipient_scope, email_empty, is_active, last_run_at, created_at
        FROM filter_subscriptions
        WHERE filter_id = ?
        ORDER BY id ASC;
    )SQL");
    sub_stmt.bind_int(1, filter_id);
    auto subs = json::json_value::make_array();
    while (sub_stmt.step() == SQLITE_ROW) {
        auto s = json::json_value::make_object();
        s.add_int("id", sub_stmt.column_int(0));
        s.add_int("filter_id", sub_stmt.column_int(1));
        s.add_nullable_int("user_id", sub_stmt.column_int(2));
        s.add_str("cron_schedule", sub_stmt.column_str(3));
        s.add_str("frequency_label", sub_stmt.column_str(4));
        s.add_str("recipient_scope", sub_stmt.column_str(5));
        s.add_bool("email_empty", sub_stmt.column_int(6) != 0);
        s.add_bool("is_active", sub_stmt.column_int(7) != 0);
        s.add_str("last_run_at", sub_stmt.column_str(8));
        s.add_str("created_at", sub_stmt.column_str(9));
        subs.add(std::move(s));
    }
    obj.add("subscriptions", std::move(subs));
    return obj;
}

// GET | POST /api/filters
static void handle_saved_filters(backend::wsworker *worker,
                                 const std::map<std::string, std::string> &header,
                                 const std::list<std::string> &) {
    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    int uid = u_auth.authenticated ? u_auth.id : 1;
    std::string method = get_header(header, "REQUEST_METHOD");

    if (method == "GET") {
        auto stmt = conn.prepare(R"SQL(
            SELECT id FROM saved_filters
            WHERE share_type = 'public'
               OR owner_id IS NULL
               OR owner_id = ?
               OR (share_type = 'role' AND lower(share_target) = lower(?))
            ORDER BY is_favorite DESC, sort_order ASC, id ASC;
        )SQL");
        stmt.bind_int(1, uid);
        stmt.bind_text(2, u_auth.authenticated ? u_auth.role : "Admin");
        auto arr = json::json_value::make_array();
        while (stmt.step() == SQLITE_ROW) {
            auto f = build_saved_filter_json(conn, stmt.column_int(0), uid);
            if (f) arr.add(std::move(f));
        }
        send_json(worker, header, "200 OK", arr);
        return;
    }

    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    auto body = json::json_value::parse(worker->body());
    if (!body) {
        send_error(worker, header, "400 Bad Request", "Invalid JSON body");
        return;
    }

    std::string name = body.get_string("name");
    std::string description = body.get_string("description");
    std::string jql_query = body.get_string("jql_query");
    if (jql_query.empty()) jql_query = body.get_string("jql");
    bool is_favorite = body.get_bool("is_favorite", true);
    std::string share_type = body.get_string("share_type", "public");
    std::string share_target = body.get_string("share_target", "*");
    std::string view_columns = body.get_string("view_columns", "key,type,title,status,priority,assignee,sprint,story_points,updated_at");

    if (name.empty()) {
        send_error(worker, header, "400 Bad Request", "Filter name is required");
        return;
    }

    // Validate JQL syntax before saving
    try {
        (void)jql::JqlParser::parse_query(jql_query);
    } catch (const jql::JqlSyntaxError &syn) {
        send_error(worker, header, "400 Bad Request", std::string("Invalid JQL syntax: ") + syn.what());
        return;
    }

    int new_id = 0;
    try {
        db::sqlite_transaction tx(conn);
        auto ins = conn.prepare(R"SQL(
            INSERT INTO saved_filters (owner_id, name, description, jql_query, is_favorite, is_system, share_type, share_target, view_columns)
            VALUES (?, ?, ?, ?, ?, 0, ?, ?, ?);
        )SQL");
        ins.bind_int(1, u_auth.id);
        ins.bind_text(2, name);
        ins.bind_text(3, description);
        ins.bind_text(4, jql_query);
        ins.bind_int(5, is_favorite ? 1 : 0);
        ins.bind_text(6, share_type);
        ins.bind_text(7, share_target);
        ins.bind_text(8, view_columns);
        ins.step_done();
        new_id = static_cast<int>(conn.last_insert_rowid());

        if (is_favorite) {
            auto st = conn.prepare("INSERT OR IGNORE INTO filter_stars (filter_id, user_id) VALUES (?, ?);");
            st.bind_int(1, new_id);
            st.bind_int(2, u_auth.id);
            st.step_done();
        }
        tx.commit();
    } catch (const std::exception &ex) {
        send_error(worker, header, "400 Bad Request", ex.what());
        return;
    }

    send_json(worker, header, "201 Created", build_saved_filter_json(conn, new_id, u_auth.id));
}

// GET | PUT | DELETE /api/filters/([0-9]+)
static void handle_saved_filter_detail(backend::wsworker *worker,
                                       const std::map<std::string, std::string> &header,
                                       const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing filter ID");
        return;
    }
    int filter_id = std::atoi(uri_params.front().c_str());
    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    int uid = u_auth.authenticated ? u_auth.id : 1;
    std::string method = get_header(header, "REQUEST_METHOD");

    if (method == "GET") {
        auto f = build_saved_filter_json(conn, filter_id, uid);
        if (!f) {
            send_error(worker, header, "404 Not Found", "Saved filter not found");
            return;
        }
        send_json(worker, header, "200 OK", f);
        return;
    }

    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    if (method == "DELETE") {
        auto chk = conn.prepare("SELECT is_system FROM saved_filters WHERE id = ?;");
        chk.bind_int(1, filter_id);
        if (chk.step() == SQLITE_ROW && chk.column_int(0) != 0) {
            send_error(worker, header, "400 Bad Request", "Cannot delete a built-in system filter");
            return;
        }
        db::sqlite_transaction tx(conn);
        auto del = conn.prepare("DELETE FROM saved_filters WHERE id = ?;");
        del.bind_int(1, filter_id);
        del.step_done();
        tx.commit();
        auto res = json::json_value::make_object();
        res.add_bool("deleted", true);
        send_json(worker, header, "200 OK", res);
        return;
    }

    auto body = json::json_value::parse(worker->body());
    if (!body) {
        send_error(worker, header, "400 Bad Request", "Invalid JSON body");
        return;
    }

    std::string name = body.get_string("name");
    std::string description = body.get_string("description");
    std::string jql_query = body.get_string("jql_query", body.get_string("jql"));
    bool is_favorite = body.get_bool("is_favorite", false);
    std::string share_type = body.get_string("share_type", "public");
    std::string share_target = body.get_string("share_target", "*");
    std::string view_columns = body.get_string("view_columns", "key,type,title,status,priority,assignee,sprint,story_points,updated_at");

    try {
        (void)jql::JqlParser::parse_query(jql_query);
        db::sqlite_transaction tx(conn);
        auto upd = conn.prepare(R"SQL(
            UPDATE saved_filters
            SET name = COALESCE(NULLIF(?, ''), name),
                description = ?,
                jql_query = ?,
                is_favorite = ?,
                share_type = ?,
                share_target = ?,
                view_columns = ?,
                updated_at = datetime('now')
            WHERE id = ?;
        )SQL");
        upd.bind_text(1, name);
        upd.bind_text(2, description);
        upd.bind_text(3, jql_query);
        upd.bind_int(4, is_favorite ? 1 : 0);
        upd.bind_text(5, share_type);
        upd.bind_text(6, share_target);
        upd.bind_text(7, view_columns);
        upd.bind_int(8, filter_id);
        upd.step_done();
        tx.commit();
    } catch (const std::exception &ex) {
        send_error(worker, header, "400 Bad Request", ex.what());
        return;
    }

    send_json(worker, header, "200 OK", build_saved_filter_json(conn, filter_id, u_auth.id));
}

// POST /api/filters/([0-9]+)/star
static void handle_saved_filter_star(backend::wsworker *worker,
                                     const std::map<std::string, std::string> &header,
                                     const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing filter ID");
        return;
    }
    int filter_id = std::atoi(uri_params.front().c_str());
    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    int uid = u_auth.authenticated ? u_auth.id : 1;

    try {
        db::sqlite_transaction tx(conn);
        auto chk = conn.prepare("SELECT is_favorite FROM saved_filters WHERE id = ?;");
        chk.bind_int(1, filter_id);
        if (chk.step() != SQLITE_ROW) {
            send_error(worker, header, "404 Not Found", "Saved filter not found");
            return;
        }
        bool cur_fav = chk.column_int(0) != 0;
        bool next_fav = !cur_fav;

        auto upd = conn.prepare("UPDATE saved_filters SET is_favorite = ?, updated_at = datetime('now') WHERE id = ?;");
        upd.bind_int(1, next_fav ? 1 : 0);
        upd.bind_int(2, filter_id);
        upd.step_done();

        if (next_fav) {
            auto ins = conn.prepare("INSERT OR IGNORE INTO filter_stars (filter_id, user_id) VALUES (?, ?);");
            ins.bind_int(1, filter_id);
            ins.bind_int(2, uid);
            ins.step_done();
        } else {
            auto del = conn.prepare("DELETE FROM filter_stars WHERE filter_id = ? AND user_id = ?;");
            del.bind_int(1, filter_id);
            del.bind_int(2, uid);
            del.step_done();
        }
        tx.commit();
    } catch (const std::exception &ex) {
        send_error(worker, header, "400 Bad Request", ex.what());
        return;
    }

    send_json(worker, header, "200 OK", build_saved_filter_json(conn, filter_id, uid));
}

// GET | POST /api/filters/([0-9]+)/subscriptions
static void handle_filter_subscriptions(backend::wsworker *worker,
                                        const std::map<std::string, std::string> &header,
                                        const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing filter ID");
        return;
    }
    int filter_id = std::atoi(uri_params.front().c_str());
    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    int uid = u_auth.authenticated ? u_auth.id : 1;
    std::string method = get_header(header, "REQUEST_METHOD");

    if (method == "GET") {
        auto f = build_saved_filter_json(conn, filter_id, uid);
        if (!f) {
            send_error(worker, header, "404 Not Found", "Saved filter not found");
            return;
        }
        send_json(worker, header, "200 OK", f);
        return;
    }

    auto body = json::json_value::parse(worker->body());
    if (!body) {
        send_error(worker, header, "400 Bad Request", "Invalid JSON body");
        return;
    }

    std::string cron_schedule = body.get_string("cron_schedule", "0 8 * * 1-5");
    std::string frequency_label = body.get_string("frequency_label", "Daily (Weekdays 08:00)");
    std::string recipient_scope = body.get_string("recipient_scope", "self");
    bool email_empty = body.get_bool("email_empty", false);

    try {
        db::sqlite_transaction tx(conn);
        auto ins = conn.prepare(R"SQL(
            INSERT INTO filter_subscriptions (filter_id, user_id, cron_schedule, frequency_label, recipient_scope, email_empty, is_active, last_run_at)
            VALUES (?, ?, ?, ?, ?, ?, 1, datetime('now'));
        )SQL");
        ins.bind_int(1, filter_id);
        ins.bind_int(2, uid);
        ins.bind_text(3, cron_schedule);
        ins.bind_text(4, frequency_label);
        ins.bind_text(5, recipient_scope);
        ins.bind_int(6, email_empty ? 1 : 0);
        ins.step_done();
        tx.commit();
    } catch (const std::exception &ex) {
        send_error(worker, header, "400 Bad Request", ex.what());
        return;
    }

    send_json(worker, header, "201 Created", build_saved_filter_json(conn, filter_id, uid));
}

// DELETE /api/filter-subscriptions/([0-9]+)
static void handle_delete_filter_subscription(backend::wsworker *worker,
                                              const std::map<std::string, std::string> &header,
                                              const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing subscription ID");
        return;
    }
    int sub_id = std::atoi(uri_params.front().c_str());
    auto conn = acquire_db_connection();
    db::sqlite_transaction tx(conn);
    auto del = conn.prepare("DELETE FROM filter_subscriptions WHERE id = ?;");
    del.bind_int(1, sub_id);
    del.step_done();
    tx.commit();

    auto res = json::json_value::make_object();
    res.add_bool("deleted", true);
    send_json(worker, header, "200 OK", res);
}

// GET /api/items & POST /api/items
static void handle_items(backend::wsworker *worker,
                         const std::map<std::string, std::string> &header,
                         const std::list<std::string> &) {
    auto conn = acquire_db_connection();
    std::string method = get_header(header, "REQUEST_METHOD");

    if (method == "GET") {
        auto args = worker->parse_args(get_header(header, "QUERY_STRING"));
        std::string sql = std::string(BASE_WORK_ITEM_SELECT) + " WHERE 1=1 ";
        std::vector<std::string> bind_vals;

        if (!args["project_id"].empty()) {
            sql += " AND w.project_id = ? ";
            bind_vals.push_back(args["project_id"]);
        }
        if (!args["status"].empty()) {
            sql += " AND w.status = ? ";
            bind_vals.push_back(args["status"]);
        }
        if (!args["type"].empty()) {
            sql += " AND w.type = ? ";
            bind_vals.push_back(args["type"]);
        }
        if (!args["category"].empty()) {
            sql += " AND w.category = ? ";
            bind_vals.push_back(args["category"]);
        }
        if (!args["assignee_id"].empty()) {
            sql += " AND w.assignee_id = ? ";
            bind_vals.push_back(args["assignee_id"]);
        }
        if (!args["sprint_id"].empty()) {
            sql += " AND w.sprint_id = ? ";
            bind_vals.push_back(args["sprint_id"]);
        }
        if (!args["parent_id"].empty()) {
            sql += " AND w.parent_id = ? ";
            bind_vals.push_back(args["parent_id"]);
        }
        if (!args["resolution_id"].empty()) {
            sql += " AND w.resolution_id = ? ";
            bind_vals.push_back(args["resolution_id"]);
        }
        if (!args["component_id"].empty()) {
            sql += " AND EXISTS (SELECT 1 FROM work_item_components wc WHERE wc.work_item_id = w.id AND wc.component_id = ?) ";
            bind_vals.push_back(args["component_id"]);
        }
        if (!args["fix_version_id"].empty()) {
            sql += " AND EXISTS (SELECT 1 FROM work_item_versions wv WHERE wv.work_item_id = w.id AND wv.version_id = ? AND wv.relation_type = 'fix') ";
            bind_vals.push_back(args["fix_version_id"]);
        }
        if (!args["affects_version_id"].empty()) {
            sql += " AND EXISTS (SELECT 1 FROM work_item_versions wv WHERE wv.work_item_id = w.id AND wv.version_id = ? AND wv.relation_type = 'affects') ";
            bind_vals.push_back(args["affects_version_id"]);
        }
        if (!args["search"].empty()) {
            sql += " AND (w.title LIKE ? OR w.key LIKE ? OR w.description LIKE ? OR w.labels LIKE ?) ";
            std::string pattern = "%" + args["search"] + "%";
            bind_vals.push_back(pattern);
            bind_vals.push_back(pattern);
            bind_vals.push_back(pattern);
            bind_vals.push_back(pattern);
        }

        std::string order_by_clause = "w.board_order ASC, w.id ASC";
        if (!args["jql"].empty()) {
            try {
                auto u_auth = get_authenticated_user(conn, worker, header);
                auto ast = jql::JqlParser::parse_query(args["jql"]);
                jql::SqlContext ctx(conn,
                                    u_auth.authenticated ? u_auth.id : 1,
                                    u_auth.authenticated ? u_auth.username : "admin",
                                    u_auth.authenticated ? u_auth.display_name : "System Administrator");
                sql += " AND (" + ast.to_where_sql(ctx) + ") ";
                for (const auto &p : ctx.params) {
                    bind_vals.push_back(p);
                }
                if (!ast.order_by.empty()) {
                    order_by_clause = ast.to_order_by_sql(ctx);
                }
            } catch (const std::exception &ex) {
                send_error(worker, header, "400 Bad Request", ex.what());
                return;
            }
        }

        sql += " ORDER BY " + order_by_clause + ";";
        auto stmt = conn.prepare(sql);
        for (size_t i = 0; i < bind_vals.size(); ++i) {
            stmt.bind_text(static_cast<int>(i + 1), bind_vals[i]);
        }

        auto arr = json::json_value::make_array();
        while (stmt.step() == SQLITE_ROW) {
            arr.add(row_to_work_item_json(conn, stmt));
        }
        send_json(worker, header, "200 OK", arr);
        return;
    }

    // POST /api/items
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    auto body = json::json_value::parse(worker->body());
    if (!body) {
        send_error(worker, header, "400 Bad Request", "Invalid JSON body");
        return;
    }

    int project_id = body.get_int("project_id", 1);
    std::string type = body.get_string("type", "Task");
    std::string category = body.get_string("category", "New Feature");
    std::string status = body.get_string("status", "To Do");
    std::string priority = body.get_string("priority", "Medium");
    std::string title = body.get_string("title");
    std::string description = body.get_string("description");
    int parent_id = body.get_int("parent_id", 0);
    int sprint_id = body.get_int("sprint_id", 0);
    int assignee_id = body.get_int("assignee_id", 0);
    int reporter_id = body.get_int("reporter_id", u_auth.id);
    int story_points = body.get_int("story_points", 0);
    int progress = body.get_int("progress", status == "Done" ? 100 : 0);
    std::string start_date = body.get_string("start_date");
    std::string due_date = body.get_string("due_date");
    std::string severity = body.get_string("severity");
    std::string steps = body.get_string("steps_to_reproduce");
    std::string labels = body.get_string("labels");
    int resolution_id = body.get_int("resolution_id", 0);

    if (title.empty()) {
        send_error(worker, header, "400 Bad Request", "Work item title is required");
        return;
    }

    std::string cf_err;
    if (!validate_custom_fields_for_item(conn, project_id, type, body, true, 0, cf_err)) {
        send_error(worker, header, "400 Bad Request", cf_err);
        return;
    }

    // Component default assignee auto-routing when assignee_id is not explicitly specified
    auto comp_ids = body.get_int_array("component_ids");
    if (assignee_id <= 0 && !comp_ids.empty()) {
        auto cstmt = conn.prepare(R"SQL(
            SELECT c.lead_user_id, c.default_assignee_type, p.lead_user_id
            FROM components c
            INNER JOIN projects p ON p.id = c.project_id
            WHERE c.id = ?;
        )SQL");
        cstmt.bind_int(1, comp_ids.front());
        if (cstmt.step() == SQLITE_ROW) {
            int comp_lead = cstmt.column_int(0);
            std::string def_type = cstmt.column_str(1);
            int proj_lead = cstmt.column_int(2);
            if ((def_type == "component_lead" || def_type == "COMPONENT_LEAD") && comp_lead > 0) {
                assignee_id = comp_lead;
            } else if ((def_type == "project_lead" || def_type == "PROJECT_LEAD" || def_type == "project_default" || def_type == "PROJECT_DEFAULT") && proj_lead > 0) {
                assignee_id = proj_lead;
            }
        }
    }

    if (status == "Done" && resolution_id <= 0) {
        resolution_id = resolve_default_resolution_id(conn);
    }

    int new_item_id = 0;
    try {
        db::sqlite_transaction tx(conn);
        std::string proj_key = "JIRA";
        int next_seq = 1;
        {
            auto pstmt = conn.prepare("SELECT key, issue_seq FROM projects WHERE id = ?;");
            pstmt.bind_int(1, project_id);
            if (pstmt.step() == SQLITE_ROW) {
                proj_key = pstmt.column_str(0);
                next_seq = pstmt.column_int(1) + 1;
            }
        }
        {
            auto upd = conn.prepare("UPDATE projects SET issue_seq = ? WHERE id = ?;");
            upd.bind_int(1, next_seq);
            upd.bind_int(2, project_id);
            upd.step_done();
        }

        std::string item_key = proj_key + "-" + std::to_string(next_seq);
        auto ins = conn.prepare(R"SQL(
            INSERT INTO work_items (
                project_id, key, type, category, status, priority, title, description,
                parent_id, sprint_id, assignee_id, reporter_id, story_points,
                progress, start_date, due_date, severity, steps_to_reproduce, labels, board_order,
                resolution_id, resolution_date
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, CASE WHEN ? > 0 THEN datetime('now') ELSE '' END);
        )SQL");
        ins.bind_int(1, project_id);
        ins.bind_text(2, item_key);
        ins.bind_text(3, type);
        ins.bind_text(4, category);
        ins.bind_text(5, status);
        ins.bind_text(6, priority);
        ins.bind_text(7, title);
        ins.bind_text(8, description);
        ins.bind_optional_int(9, parent_id);
        ins.bind_optional_int(10, sprint_id);
        ins.bind_optional_int(11, assignee_id);
        ins.bind_optional_int(12, reporter_id);
        ins.bind_int(13, story_points);
        ins.bind_int(14, progress);
        ins.bind_text(15, start_date);
        ins.bind_text(16, due_date);
        ins.bind_text(17, severity);
        ins.bind_text(18, steps);
        ins.bind_text(19, labels);
        ins.bind_int(20, next_seq * 10);
        ins.bind_optional_int(21, resolution_id);
        ins.bind_int(22, resolution_id);
        ins.step_done();
        new_item_id = static_cast<int>(conn.last_insert_rowid());

        sync_item_components_and_versions(conn, new_item_id, body, true);
        sync_item_custom_fields(conn, new_item_id, body, true);

        auto hist = conn.prepare(R"SQL(
            INSERT INTO workflow_history (work_item_id, from_status, to_status, changed_by, comment)
            VALUES (?, 'New', ?, ?, 'Work item created');
        )SQL");
        hist.bind_int(1, new_item_id);
        hist.bind_text(2, status);
        hist.bind_int(3, u_auth.id);
        hist.step_done();

        tx.commit();
    } catch (const std::exception &ex) {
        send_error(worker, header, "500 Internal Server Error", ex.what());
        return;
    }

    auto q = conn.prepare(std::string(BASE_WORK_ITEM_SELECT) + " WHERE w.id = ?;");
    q.bind_int(1, new_item_id);
    if (q.step() == SQLITE_ROW) {
        send_json(worker, header, "201 Created", row_to_work_item_json(conn, q));
    } else {
        send_error(worker, header, "500 Internal Server Error", "Failed to load created item");
    }
}

// Resolve either a numeric item ID ("9") or a work item key ("JIRA-9") to its integer ID
static int resolve_work_item_id(db::sqlite_db &conn, const std::string &id_or_key) {
    if (id_or_key.empty()) return 0;
    bool all_digits = true;
    for (unsigned char c : id_or_key) {
        if (!std::isdigit(c)) {
            all_digits = false;
            break;
        }
    }
    if (all_digits) {
        return std::atoi(id_or_key.c_str());
    }
    auto stmt = conn.prepare("SELECT id FROM work_items WHERE UPPER(key) = UPPER(?);");
    stmt.bind_text(1, id_or_key);
    if (stmt.step() == SQLITE_ROW) {
        return stmt.column_int(0);
    }
    return 0;
}

// GET / PUT / DELETE /api/items/([A-Za-z0-9_-]+)
static void handle_item_detail(backend::wsworker *worker,
                               const std::map<std::string, std::string> &header,
                               const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing item ID or key");
        return;
    }
    auto conn = acquire_db_connection();
    int item_id = resolve_work_item_id(conn, uri_params.front());
    if (item_id <= 0) {
        send_error(worker, header, "404 Not Found", "Work item not found");
        return;
    }
    std::string method = get_header(header, "REQUEST_METHOD");

    if (method == "GET") {
        auto q = conn.prepare(std::string(BASE_WORK_ITEM_SELECT) + " WHERE w.id = ?;");
        q.bind_int(1, item_id);
        if (q.step() != SQLITE_ROW) {
            send_error(worker, header, "404 Not Found", "Work item not found");
            return;
        }
        auto item = row_to_work_item_json(conn, q);
        q.reset();

        // Subtasks / child items
        auto sub_stmt = conn.prepare(std::string(BASE_WORK_ITEM_SELECT) + " WHERE w.parent_id = ? ORDER BY w.id ASC;");
        sub_stmt.bind_int(1, item_id);
        auto subtasks = json::json_value::make_array();
        while (sub_stmt.step() == SQLITE_ROW) {
            subtasks.add(row_to_work_item_json(conn, sub_stmt));
        }
        item.add("subtasks", std::move(subtasks));

        // Links
        auto link_stmt = conn.prepare(R"SQL(
            SELECT l.id, l.source_item_id, l.target_item_id, l.link_type,
                   ws.key, ws.title, ws.status, ws.type,
                   wt.key, wt.title, wt.status, wt.type
            FROM work_item_links l
            INNER JOIN work_items ws ON ws.id = l.source_item_id
            INNER JOIN work_items wt ON wt.id = l.target_item_id
            WHERE l.source_item_id = ? OR l.target_item_id = ?
            ORDER BY l.id DESC;
        )SQL");
        link_stmt.bind_int(1, item_id);
        link_stmt.bind_int(2, item_id);
        auto links = json::json_value::make_array();
        while (link_stmt.step() == SQLITE_ROW) {
            auto lk = json::json_value::make_object();
            lk.add_int("id", link_stmt.column_int(0));
            lk.add_int("source_item_id", link_stmt.column_int(1));
            lk.add_int("target_item_id", link_stmt.column_int(2));
            lk.add_str("link_type", link_stmt.column_str(3));
            lk.add_str("source_key", link_stmt.column_str(4));
            lk.add_str("source_title", link_stmt.column_str(5));
            lk.add_str("source_status", link_stmt.column_str(6));
            lk.add_str("source_type", link_stmt.column_str(7));
            lk.add_str("target_key", link_stmt.column_str(8));
            lk.add_str("target_title", link_stmt.column_str(9));
            lk.add_str("target_status", link_stmt.column_str(10));
            lk.add_str("target_type", link_stmt.column_str(11));
            links.add(std::move(lk));
        }
        item.add("links", std::move(links));

        // Comments
        auto c_stmt = conn.prepare(R"SQL(
            SELECT c.id, c.work_item_id, c.author_id, COALESCE(u.display_name, 'User'),
                   COALESCE(u.avatar_color, '#0052CC'), c.body, c.created_at
            FROM comments c
            LEFT JOIN users u ON u.id = c.author_id
            WHERE c.work_item_id = ?
            ORDER BY c.id ASC;
        )SQL");
        c_stmt.bind_int(1, item_id);
        auto comments = json::json_value::make_array();
        while (c_stmt.step() == SQLITE_ROW) {
            auto cm = json::json_value::make_object();
            cm.add_int("id", c_stmt.column_int(0));
            cm.add_int("work_item_id", c_stmt.column_int(1));
            cm.add_nullable_int("author_id", c_stmt.column_int(2));
            cm.add_str("author_name", c_stmt.column_str(3));
            cm.add_str("author_color", c_stmt.column_str(4));
            cm.add_str("body", c_stmt.column_str(5));
            cm.add_str("created_at", c_stmt.column_str(6));
            comments.add(std::move(cm));
        }
        item.add("comments", std::move(comments));

        // Attachments
        auto a_stmt = conn.prepare(R"SQL(
            SELECT a.id, a.work_item_id, a.uploader_id, COALESCE(u.display_name, 'User'),
                   a.original_filename, a.mime_type, a.size_bytes, a.blob_filename, a.created_at
            FROM attachments a
            LEFT JOIN users u ON u.id = a.uploader_id
            WHERE a.work_item_id = ?
            ORDER BY a.id DESC;
        )SQL");
        a_stmt.bind_int(1, item_id);
        auto attachments = json::json_value::make_array();
        while (a_stmt.step() == SQLITE_ROW) {
            auto at = json::json_value::make_object();
            at.add_int("id", a_stmt.column_int(0));
            at.add_int("work_item_id", a_stmt.column_int(1));
            at.add_nullable_int("uploader_id", a_stmt.column_int(2));
            at.add_str("uploader_name", a_stmt.column_str(3));
            at.add_str("original_filename", a_stmt.column_str(4));
            at.add_str("mime_type", a_stmt.column_str(5));
            at.add_int("size_bytes", a_stmt.column_int(6));
            at.add_str("blob_filename", a_stmt.column_str(7));
            at.add_str("url", "/api/blobs/" + a_stmt.column_str(7));
            at.add_str("created_at", a_stmt.column_str(8));
            attachments.add(std::move(at));
        }
        item.add("attachments", std::move(attachments));

        // Workflow history
        auto h_stmt = conn.prepare(R"SQL(
            SELECT h.id, h.from_status, h.to_status, COALESCE(u.display_name, 'System'),
                   h.comment, h.created_at,
                   COALESCE(h.transition_id, 0), COALESCE(h.transition_name, ''), COALESCE(h.executed_post_functions, '')
            FROM workflow_history h
            LEFT JOIN users u ON u.id = h.changed_by
            WHERE h.work_item_id = ?
            ORDER BY h.id DESC;
        )SQL");
        h_stmt.bind_int(1, item_id);
        auto history = json::json_value::make_array();
        while (h_stmt.step() == SQLITE_ROW) {
            auto hi = json::json_value::make_object();
            hi.add_int("id", h_stmt.column_int(0));
            hi.add_str("from_status", h_stmt.column_str(1));
            hi.add_str("to_status", h_stmt.column_str(2));
            hi.add_str("changed_by_name", h_stmt.column_str(3));
            hi.add_str("comment", h_stmt.column_str(4));
            hi.add_str("created_at", h_stmt.column_str(5));
            hi.add_nullable_int("transition_id", h_stmt.column_int(6));
            hi.add_str("transition_name", h_stmt.column_str(7));
            hi.add_str("executed_post_functions", h_stmt.column_str(8));
            history.add(std::move(hi));
        }
        item.add("history", std::move(history));

        // Active workflow & available transitions for this item
        auto u_viewer = get_authenticated_user(conn, worker, header);
        item.add("workflow", build_item_transitions_payload(conn, item_id, u_viewer));

        send_json(worker, header, "200 OK", item);
        return;
    }

    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    if (method == "DELETE") {
        // Remove physical blob files for this item first
        auto b_stmt = conn.prepare("SELECT blob_filename FROM attachments WHERE work_item_id = ?;");
        b_stmt.bind_int(1, item_id);
        while (b_stmt.step() == SQLITE_ROW) {
            std::filesystem::path p = std::filesystem::path(g_blobs_dir) / b_stmt.column_str(0);
            std::error_code ec;
            std::filesystem::remove(p, ec);
        }
        b_stmt.reset();

        {
            db::sqlite_transaction tx(conn);
            auto del = conn.prepare("DELETE FROM work_items WHERE id = ?;");
            del.bind_int(1, item_id);
            del.step_done();
            tx.commit();
        }

        auto res = json::json_value::make_object();
        res.add_bool("deleted", true);
        send_json(worker, header, "200 OK", res);
        return;
    }

    // PUT /api/items/([0-9]+)
    auto body = json::json_value::parse(worker->body());
    if (!body) {
        send_error(worker, header, "400 Bad Request", "Invalid JSON body");
        return;
    }

    std::string old_status;
    int old_project_id = 1;
    int old_resolution_id = 0;
    std::string old_resolution_date;
    {
        auto cur = conn.prepare("SELECT status, COALESCE(resolution_id, 0), COALESCE(resolution_date, ''), project_id FROM work_items WHERE id = ?;");
        cur.bind_int(1, item_id);
        if (cur.step() != SQLITE_ROW) {
            send_error(worker, header, "404 Not Found", "Work item not found");
            return;
        }
        old_status = cur.column_str(0);
        old_resolution_id = cur.column_int(1);
        old_resolution_date = cur.column_str(2);
        old_project_id = cur.column_int(3);
    }

    std::string type = body.get_string("type", "Task");
    std::string cf_err;
    if (!validate_custom_fields_for_item(conn, old_project_id, type, body, false, item_id, cf_err)) {
        send_error(worker, header, "400 Bad Request", cf_err);
        return;
    }
    std::string category = body.get_string("category", "New Feature");
    std::string status = body.get_string("status", old_status);
    std::string priority = body.get_string("priority", "Medium");
    std::string title = body.get_string("title");
    std::string description = body.get_string("description");
    int parent_id = body.get_int("parent_id", 0);
    int sprint_id = body.get_int("sprint_id", 0);
    int assignee_id = body.get_int("assignee_id", 0);
    int story_points = body.get_int("story_points", 0);
    int progress = body.get_int("progress", 0);
    std::string start_date = body.get_string("start_date");
    std::string due_date = body.get_string("due_date");
    std::string severity = body.get_string("severity");
    std::string steps = body.get_string("steps_to_reproduce");
    std::string labels = body.get_string("labels");

    int resolution_id = body.has_key("resolution_id") ? body.get_int("resolution_id", 0) : old_resolution_id;
    bool set_res_now = false;
    std::string resolution_date = old_resolution_date;

    if (status == "Done") {
        if (resolution_id <= 0) {
            resolution_id = resolve_default_resolution_id(conn);
        }
        if (resolution_date.empty() || resolution_id != old_resolution_id) {
            set_res_now = true;
        }
    } else if (old_status == "Done" && status != "Done" && !body.has_key("resolution_id")) {
        // Reopening a closed issue clears its resolution
        resolution_id = 0;
        resolution_date.clear();
    } else if (resolution_id <= 0) {
        resolution_date.clear();
    } else if (resolution_id != old_resolution_id) {
        set_res_now = true;
    }

    {
        db::sqlite_transaction tx(conn);
        auto upd = conn.prepare(R"SQL(
            UPDATE work_items
            SET type = ?, category = ?, status = ?, priority = ?, title = ?, description = ?,
                parent_id = ?, sprint_id = ?, assignee_id = ?, story_points = ?,
                progress = ?, start_date = ?, due_date = ?, severity = ?,
                steps_to_reproduce = ?, labels = ?,
                resolution_id = ?,
                resolution_date = CASE WHEN ? = 1 THEN datetime('now') ELSE ? END,
                updated_at = datetime('now')
            WHERE id = ?;
        )SQL");
        upd.bind_text(1, type);
        upd.bind_text(2, category);
        upd.bind_text(3, status);
        upd.bind_text(4, priority);
        upd.bind_text(5, title);
        upd.bind_text(6, description);
        upd.bind_optional_int(7, parent_id);
        upd.bind_optional_int(8, sprint_id);
        upd.bind_optional_int(9, assignee_id);
        upd.bind_int(10, story_points);
        upd.bind_int(11, progress);
        upd.bind_text(12, start_date);
        upd.bind_text(13, due_date);
        upd.bind_text(14, severity);
        upd.bind_text(15, steps);
        upd.bind_text(16, labels);
        upd.bind_optional_int(17, resolution_id);
        upd.bind_int(18, set_res_now ? 1 : 0);
        upd.bind_text(19, resolution_date);
        upd.bind_int(20, item_id);
        upd.step_done();

        sync_item_components_and_versions(conn, item_id, body, false);
        sync_item_custom_fields(conn, item_id, body, false);

        if (status != old_status) {
            auto hist = conn.prepare(R"SQL(
                INSERT INTO workflow_history (work_item_id, from_status, to_status, changed_by, comment)
                VALUES (?, ?, ?, ?, 'Status updated');
            )SQL");
            hist.bind_int(1, item_id);
            hist.bind_text(2, old_status);
            hist.bind_text(3, status);
            hist.bind_int(4, u_auth.id);
            hist.step_done();
        }
        tx.commit();
    }

    auto q = conn.prepare(std::string(BASE_WORK_ITEM_SELECT) + " WHERE w.id = ?;");
    q.bind_int(1, item_id);
    if (q.step() == SQLITE_ROW) {
        send_json(worker, header, "200 OK", row_to_work_item_json(conn, q));
    } else {
        send_error(worker, header, "404 Not Found", "Work item not found");
    }
}

// PATCH | POST /api/items/([A-Za-z0-9_-]+)/state & POST /api/items/([A-Za-z0-9_-]+)/transitions
static void handle_item_state_transition(backend::wsworker *worker,
                                         const std::map<std::string, std::string> &header,
                                         const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing item ID or key");
        return;
    }
    auto conn = acquire_db_connection();
    int item_id = resolve_work_item_id(conn, uri_params.front());
    if (item_id <= 0) {
        send_error(worker, header, "404 Not Found", "Work item not found");
        return;
    }
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    auto body = json::json_value::parse(worker->body());
    if (!body) {
        send_error(worker, header, "400 Bad Request", "Invalid JSON body");
        return;
    }

    int project_id = 1;
    std::string item_type;
    std::string item_category;
    std::string old_status;
    int old_progress = 0;
    std::string old_start;
    std::string old_due;
    int old_order = 0;
    int old_res_id = 0;
    std::string old_res_date;
    int old_assignee_id = 0;
    int old_reporter_id = 0;
    int old_parent_id = 0;
    std::string old_steps;
    {
        auto cur = conn.prepare(R"SQL(
            SELECT project_id, type, category, status, progress, start_date, due_date, board_order,
                   COALESCE(resolution_id, 0), COALESCE(resolution_date, ''),
                   COALESCE(assignee_id, 0), COALESCE(reporter_id, 0), COALESCE(parent_id, 0),
                   COALESCE(steps_to_reproduce, '')
            FROM work_items WHERE id = ?;
        )SQL");
        cur.bind_int(1, item_id);
        if (cur.step() != SQLITE_ROW) {
            send_error(worker, header, "404 Not Found", "Work item not found");
            return;
        }
        project_id = cur.column_int(0);
        item_type = cur.column_str(1);
        item_category = cur.column_str(2);
        old_status = cur.column_str(3);
        old_progress = cur.column_int(4);
        old_start = cur.column_str(5);
        old_due = cur.column_str(6);
        old_order = cur.column_int(7);
        old_res_id = cur.column_int(8);
        old_res_date = cur.column_str(9);
        old_assignee_id = cur.column_int(10);
        old_reporter_id = cur.column_int(11);
        old_parent_id = cur.column_int(12);
        old_steps = cur.column_str(13);
    }

    int wf_id = resolve_workflow_id_for_item(conn, project_id, item_type, item_category);
    int req_transition_id = body.get_int("transition_id", 0);
    std::string new_status = body.get_string("status", old_status);
    int matched_transition_id = 0;
    std::string matched_transition_name;

    if (req_transition_id > 0) {
        auto tq = conn.prepare("SELECT id, name, from_status, to_status FROM workflow_transitions WHERE id = ? AND workflow_id = ?;");
        tq.bind_int(1, req_transition_id);
        tq.bind_int(2, wf_id);
        if (tq.step() != SQLITE_ROW) {
            send_error(worker, header, "400 Bad Request", "Specified transition_id does not belong to the item's active workflow");
            return;
        }
        std::string f_st = tq.column_str(2);
        if (f_st != "*" && f_st != old_status) {
            send_error(worker, header, "400 Bad Request",
                       "Invalid transition '" + tq.column_str(1) + "': requires current status '" + f_st + "' (actual: '" + old_status + "')");
            return;
        }
        matched_transition_id = tq.column_int(0);
        matched_transition_name = tq.column_str(1);
        new_status = tq.column_str(3);
    } else if (new_status != old_status) {
        auto tq = conn.prepare(R"SQL(
            SELECT id, name
            FROM workflow_transitions
            WHERE workflow_id = ?
              AND to_status = ?
              AND (from_status = ? OR from_status = '*')
            ORDER BY CASE WHEN from_status = ? THEN 0 ELSE 1 END, sort_order ASC, id ASC
            LIMIT 1;
        )SQL");
        tq.bind_int(1, wf_id);
        tq.bind_text(2, new_status);
        tq.bind_text(3, old_status);
        tq.bind_text(4, old_status);
        if (tq.step() != SQLITE_ROW) {
            send_error(worker, header, "400 Bad Request",
                       "Invalid workflow transition: no directed transition from '" + old_status + "' to '" + new_status + "' in active workflow");
            return;
        }
        matched_transition_id = tq.column_int(0);
        matched_transition_name = tq.column_str(1);
    }

    int new_assignee_id = body.has_key("assignee_id") ? body.get_int("assignee_id", old_assignee_id) : old_assignee_id;
    std::string new_start = body.has_key("start_date") ? body.get_string("start_date") : old_start;
    std::string new_due = body.has_key("due_date") ? body.get_string("due_date") : old_due;
    std::string new_steps = body.has_key("steps_to_reproduce") ? body.get_string("steps_to_reproduce") : old_steps;
    int new_order = body.has_key("board_order") ? body.get_int("board_order", old_order) : old_order;
    std::string raw_comment = body.has_key("comment") ? body.get_string("comment") : "";
    std::string comment = raw_comment.empty()
                              ? (matched_transition_name.empty() ? "Transitioned state" : ("Executed transition: " + matched_transition_name))
                              : raw_comment;
    int new_res_id = body.has_key("resolution_id") ? body.get_int("resolution_id", 0) : old_res_id;

    // Step 1 & Step 2: Evaluate Conditions and Validators when transitioning state
    if (matched_transition_id > 0) {
        std::vector<std::string> blocked_reasons;
        if (!evaluate_transition_conditions(conn, matched_transition_id, item_id, old_assignee_id, u_auth, blocked_reasons)) {
            send_error(worker, header, "400 Bad Request", blocked_reasons.front());
            return;
        }

        // Run Validators
        auto vstmt = conn.prepare(R"SQL(
            SELECT rule_type, field_name, config_value, description
            FROM workflow_transition_rules
            WHERE transition_id = ? AND rule_category = 'validator'
            ORDER BY sort_order ASC, id ASC;
        )SQL");
        vstmt.bind_int(1, matched_transition_id);
        auto cf_inputs = extract_custom_fields_from_body(body);

        while (vstmt.step() == SQLITE_ROW) {
            std::string rtype = vstmt.column_str(0);
            std::string fname = vstmt.column_str(1);
            std::string cfg = vstmt.column_str(2);

            if (rtype == "comment_required") {
                if (raw_comment.empty()) {
                    send_error(worker, header, "400 Bad Request",
                               "Validator failed (comment_required): A transition comment is required for '" + matched_transition_name + "'");
                    return;
                }
            } else if (rtype == "field_required") {
                if (fname == "resolution_id" || fname == "resolution") {
                    if (new_res_id <= 0) {
                        send_error(worker, header, "400 Bad Request",
                                   "Validator failed (field_required): Resolution ('resolution_id') is required when executing '" + matched_transition_name + "'");
                        return;
                    }
                } else if (fname == "due_date") {
                    if (new_due.empty()) {
                        send_error(worker, header, "400 Bad Request",
                                   "Validator failed (field_required): Field 'due_date' is required for '" + matched_transition_name + "'");
                        return;
                    }
                } else if (fname == "steps_to_reproduce") {
                    if (new_steps.empty()) {
                        send_error(worker, header, "400 Bad Request",
                                   "Validator failed (field_required): Field 'steps_to_reproduce' is required for '" + matched_transition_name + "'");
                        return;
                    }
                } else if (fname == "assignee_id") {
                    if (new_assignee_id <= 0) {
                        send_error(worker, header, "400 Bad Request",
                                   "Validator failed (field_required): An Assignee is required for '" + matched_transition_name + "'");
                        return;
                    }
                } else if (!fname.empty()) {
                    bool has_cf = false;
                    auto it = cf_inputs.find(fname);
                    if (it != cf_inputs.end() && it->second.present && !it->second.is_null && !it->second.str_val.empty()) {
                        has_cf = true;
                    } else {
                        auto cfq = conn.prepare(R"SQL(
                            SELECT COALESCE(cfv.text_value, ''), COALESCE(cfv.date_value, ''), COALESCE(cfv.user_value_id, 0), cfv.numeric_value IS NOT NULL
                            FROM custom_field_values cfv
                            INNER JOIN custom_fields cf ON cf.id = cfv.custom_field_id
                            WHERE cfv.work_item_id = ? AND cf.field_key = ?;
                        )SQL");
                        cfq.bind_int(1, item_id);
                        cfq.bind_text(2, fname);
                        if (cfq.step() == SQLITE_ROW) {
                            if (!cfq.column_str(0).empty() || !cfq.column_str(1).empty() || cfq.column_int(2) > 0 || cfq.column_int(3) != 0) {
                                has_cf = true;
                            }
                        }
                    }
                    if (!has_cf) {
                        send_error(worker, header, "400 Bad Request",
                                   "Validator failed (field_required): Custom field '" + fname + "' is required for '" + matched_transition_name + "'");
                        return;
                    }
                }
            } else if (rtype == "regex_match" && !cfg.empty()) {
                std::string val_to_check;
                if (fname == "comment") val_to_check = raw_comment;
                else if (fname == "steps_to_reproduce") val_to_check = new_steps;
                else if (cf_inputs.count(fname)) val_to_check = cf_inputs[fname].str_val;
                try {
                    std::regex re(cfg);
                    if (!std::regex_search(val_to_check, re)) {
                        send_error(worker, header, "400 Bad Request",
                                   "Validator failed (regex_match): Field '" + fname + "' does not match format '" + cfg + "'");
                        return;
                    }
                } catch (...) {}
            }
        }
    }

    int new_progress = body.has_key("progress") ? body.get_int("progress", old_progress) : old_progress;
    if (!body.has_key("progress")) {
        if (new_status == "Done") new_progress = 100;
        else if (new_status == "To Do" || new_status == "Backlog") new_progress = 0;
        else if (new_status == "In Progress" && new_progress == 0) new_progress = 40;
        else if (new_status == "In Review" && new_progress < 75) new_progress = 80;
        else if (new_status == "QA / Testing" && new_progress < 85) new_progress = 90;
    }

    bool set_res_now = false;
    std::string new_res_date = old_res_date;
    std::vector<std::string> executed_pf;
    bool trigger_update_parent = false;
    std::string parent_target_status = "Done";

    // Step 3: Execute Post-Functions configured on the transition
    if (matched_transition_id > 0) {
        auto pfstmt = conn.prepare(R"SQL(
            SELECT rule_type, field_name, config_value
            FROM workflow_transition_rules
            WHERE transition_id = ? AND rule_category = 'post_function'
            ORDER BY sort_order ASC, id ASC;
        )SQL");
        pfstmt.bind_int(1, matched_transition_id);
        while (pfstmt.step() == SQLITE_ROW) {
            std::string ptype = pfstmt.column_str(0);
            std::string pfield = pfstmt.column_str(1);
            std::string pval = pfstmt.column_str(2);

            if (ptype == "set_field_value") {
                if (pfield == "progress" && !body.has_key("progress")) {
                    new_progress = std::atoi(pval.c_str());
                } else if (pfield == "resolution" || pfield == "resolution_id") {
                    if (new_res_id <= 0) {
                        auto rq = conn.prepare("SELECT id FROM resolutions WHERE name = ? OR CAST(id AS TEXT) = ? LIMIT 1;");
                        rq.bind_text(1, pval);
                        rq.bind_text(2, pval);
                        if (rq.step() == SQLITE_ROW) {
                            new_res_id = rq.column_int(0);
                        } else {
                            new_res_id = resolve_default_resolution_id(conn);
                        }
                    }
                    set_res_now = true;
                }
                executed_pf.push_back("set_field_value(" + pfield + "=" + pval + ")");
            } else if (ptype == "clear_field_value") {
                if (pfield == "resolution" || pfield == "resolution_id") {
                    new_res_id = 0;
                    new_res_date.clear();
                    set_res_now = false;
                }
                executed_pf.push_back("clear_field_value(" + pfield + ")");
            } else if (ptype == "assign_to_user" && !body.has_key("assignee_id")) {
                if (pval == "reporter" && old_reporter_id > 0) {
                    new_assignee_id = old_reporter_id;
                } else if (pval == "current_user") {
                    new_assignee_id = u_auth.id;
                } else if (pval.starts_with("role:")) {
                    std::string target_role = pval.substr(5);
                    auto uq = conn.prepare("SELECT id FROM users WHERE role = ? ORDER BY id ASC LIMIT 1;");
                    uq.bind_text(1, target_role);
                    if (uq.step() == SQLITE_ROW) {
                        new_assignee_id = uq.column_int(0);
                    }
                } else if (std::atoi(pval.c_str()) > 0) {
                    new_assignee_id = std::atoi(pval.c_str());
                }
                executed_pf.push_back("assign_to_user(" + pval + ")");
            } else if (ptype == "update_parent_status") {
                trigger_update_parent = true;
                if (!pval.empty()) parent_target_status = pval;
                executed_pf.push_back("update_parent_status(" + parent_target_status + ")");
            } else if (ptype == "dispatch_event") {
                executed_pf.push_back("dispatch_event(" + pval + ")");
            }
        }
    }

    if (new_status == "Done") {
        if (new_res_id <= 0) {
            new_res_id = resolve_default_resolution_id(conn);
        }
        if (new_res_date.empty() || new_res_id != old_res_id) {
            set_res_now = true;
        }
    } else if (old_status == "Done" && new_status != "Done" && !body.has_key("resolution_id")) {
        new_res_id = 0;
        new_res_date.clear();
        set_res_now = false;
    } else if (new_res_id <= 0) {
        new_res_date.clear();
    }

    std::string pf_summary;
    for (size_t i = 0; i < executed_pf.size(); ++i) {
        if (i > 0) pf_summary += ", ";
        pf_summary += executed_pf[i];
    }

    {
        db::sqlite_transaction tx(conn);
        auto upd = conn.prepare(R"SQL(
            UPDATE work_items
            SET status = ?, progress = ?, start_date = ?, due_date = ?, board_order = ?,
                assignee_id = ?, steps_to_reproduce = ?,
                resolution_id = ?,
                resolution_date = CASE WHEN ? = 1 THEN datetime('now') ELSE ? END,
                updated_at = datetime('now')
            WHERE id = ?;
        )SQL");
        upd.bind_text(1, new_status);
        upd.bind_int(2, new_progress);
        upd.bind_text(3, new_start);
        upd.bind_text(4, new_due);
        upd.bind_int(5, new_order);
        upd.bind_optional_int(6, new_assignee_id);
        upd.bind_text(7, new_steps);
        upd.bind_optional_int(8, new_res_id);
        upd.bind_int(9, set_res_now ? 1 : 0);
        upd.bind_text(10, new_res_date);
        upd.bind_int(11, item_id);
        upd.step_done();

        sync_item_custom_fields(conn, item_id, body, false);

        if (body.get_bool("post_comment", false) && !raw_comment.empty()) {
            auto cins = conn.prepare("INSERT INTO comments (work_item_id, author_id, body) VALUES (?, ?, ?);");
            cins.bind_int(1, item_id);
            cins.bind_int(2, u_auth.id);
            cins.bind_text(3, raw_comment);
            cins.step_done();
        }

        if (new_status != old_status || matched_transition_id > 0) {
            auto hist = conn.prepare(R"SQL(
                INSERT INTO workflow_history (
                    work_item_id, from_status, to_status, changed_by, comment,
                    transition_id, transition_name, executed_post_functions
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?);
            )SQL");
            hist.bind_int(1, item_id);
            hist.bind_text(2, old_status);
            hist.bind_text(3, new_status);
            hist.bind_int(4, u_auth.id);
            hist.bind_text(5, comment);
            hist.bind_optional_int(6, matched_transition_id);
            hist.bind_text(7, matched_transition_name);
            hist.bind_text(8, pf_summary);
            hist.step_done();
        }

        // Post-function: update_parent_status when all sibling subtasks reach Done
        if (trigger_update_parent && old_parent_id > 0 && new_status == parent_target_status) {
            auto sibq = conn.prepare("SELECT COUNT(*) FROM work_items WHERE parent_id = ? AND status != ?;");
            sibq.bind_int(1, old_parent_id);
            sibq.bind_text(2, parent_target_status);
            int remaining_sibs = 0;
            if (sibq.step() == SQLITE_ROW) {
                remaining_sibs = sibq.column_int(0);
            }
            if (remaining_sibs == 0) {
                auto pq = conn.prepare("SELECT status, type FROM work_items WHERE id = ?;");
                pq.bind_int(1, old_parent_id);
                if (pq.step() == SQLITE_ROW) {
                    std::string p_old_st = pq.column_str(0);
                    std::string p_type = pq.column_str(1);
                    // Auto-close parent Story/Task when all subtasks reach Done
                    if (p_old_st != parent_target_status && (p_type == "Story" || p_type == "Task")) {
                        int def_res = resolve_default_resolution_id(conn);
                        auto pupd = conn.prepare(R"SQL(
                            UPDATE work_items
                            SET status = ?, progress = 100,
                                resolution_id = COALESCE(resolution_id, ?),
                                resolution_date = COALESCE(NULLIF(resolution_date, ''), datetime('now')),
                                updated_at = datetime('now')
                            WHERE id = ?;
                        )SQL");
                        pupd.bind_text(1, parent_target_status);
                        pupd.bind_int(2, def_res);
                        pupd.bind_int(3, old_parent_id);
                        pupd.step_done();

                        auto phist = conn.prepare(R"SQL(
                            INSERT INTO workflow_history (
                                work_item_id, from_status, to_status, changed_by, comment,
                                transition_name, executed_post_functions
                            ) VALUES (?, ?, ?, ?, 'Auto-transitioned by post-function: all child subtasks completed', 'Auto-Close Parent', 'update_parent_status');
                        )SQL");
                        phist.bind_int(1, old_parent_id);
                        phist.bind_text(2, p_old_st);
                        phist.bind_text(3, parent_target_status);
                        phist.bind_int(4, u_auth.id);
                        phist.step_done();
                    }
                }
            }
        }

        tx.commit();
    }

    auto q = conn.prepare(std::string(BASE_WORK_ITEM_SELECT) + " WHERE w.id = ?;");
    q.bind_int(1, item_id);
    if (q.step() == SQLITE_ROW) {
        send_json(worker, header, "200 OK", row_to_work_item_json(conn, q));
    } else {
        send_error(worker, header, "404 Not Found", "Work item not found");
    }
}

// POST /api/items/([A-Za-z0-9_-]+)/comments
static void handle_item_comments(backend::wsworker *worker,
                                 const std::map<std::string, std::string> &header,
                                 const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing item ID or key");
        return;
    }
    auto conn = acquire_db_connection();
    int item_id = resolve_work_item_id(conn, uri_params.front());
    if (item_id <= 0) {
        send_error(worker, header, "404 Not Found", "Work item not found");
        return;
    }
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    auto body = json::json_value::parse(worker->body());
    if (!body) {
        send_error(worker, header, "400 Bad Request", "Invalid JSON body");
        return;
    }
    std::string text = body.get_string("body");
    if (text.empty()) {
        send_error(worker, header, "400 Bad Request", "Comment body is required");
        return;
    }

    int cid = 0;
    {
        db::sqlite_transaction tx(conn);
        auto ins = conn.prepare("INSERT INTO comments (work_item_id, author_id, body) VALUES (?, ?, ?);");
        ins.bind_int(1, item_id);
        ins.bind_int(2, u_auth.id);
        ins.bind_text(3, text);
        ins.step_done();
        cid = static_cast<int>(conn.last_insert_rowid());
        tx.commit();
    }

    auto res = json::json_value::make_object();
    res.add_int("id", cid);
    res.add_int("work_item_id", item_id);
    res.add_int("author_id", u_auth.id);
    res.add_str("author_name", u_auth.display_name);
    res.add_str("author_color", u_auth.avatar_color);
    res.add_str("body", text);
    res.add_str("created_at", "just now");
    send_json(worker, header, "201 Created", res);
}

// POST /api/items/([A-Za-z0-9_-]+)/links
static void handle_item_links(backend::wsworker *worker,
                              const std::map<std::string, std::string> &header,
                              const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing item ID or key");
        return;
    }
    auto conn = acquire_db_connection();
    int source_id = resolve_work_item_id(conn, uri_params.front());
    if (source_id <= 0) {
        send_error(worker, header, "404 Not Found", "Work item not found");
        return;
    }
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    auto body = json::json_value::parse(worker->body());
    if (!body) {
        send_error(worker, header, "400 Bad Request", "Invalid JSON body");
        return;
    }

    int target_id = body.get_int("target_item_id", 0);
    std::string link_type = body.get_string("link_type", "relates_to");
    if (target_id <= 0 || target_id == source_id) {
        send_error(worker, header, "400 Bad Request", "Valid target_item_id is required");
        return;
    }

    int link_id = 0;
    {
        db::sqlite_transaction tx(conn);
        auto ins = conn.prepare("INSERT INTO work_item_links (source_item_id, target_item_id, link_type) VALUES (?, ?, ?);");
        ins.bind_int(1, source_id);
        ins.bind_int(2, target_id);
        ins.bind_text(3, link_type);
        ins.step_done();
        link_id = static_cast<int>(conn.last_insert_rowid());
        tx.commit();
    }

    auto res = json::json_value::make_object();
    res.add_int("id", link_id);
    res.add_int("source_item_id", source_id);
    res.add_int("target_item_id", target_id);
    res.add_str("link_type", link_type);
    send_json(worker, header, "201 Created", res);
}

// GET /api/links
static void handle_all_links(backend::wsworker *worker,
                             const std::map<std::string, std::string> &header,
                             const std::list<std::string> &) {
    auto conn = acquire_db_connection();
    auto stmt = conn.prepare(R"SQL(
        SELECT l.id, l.source_item_id, l.target_item_id, l.link_type,
               ws.key, ws.title, ws.status, ws.type,
               wt.key, wt.title, wt.status, wt.type
        FROM work_item_links l
        INNER JOIN work_items ws ON ws.id = l.source_item_id
        INNER JOIN work_items wt ON wt.id = l.target_item_id
        ORDER BY l.id ASC;
    )SQL");
    auto arr = json::json_value::make_array();
    while (stmt.step() == SQLITE_ROW) {
        auto lk = json::json_value::make_object();
        lk.add_int("id", stmt.column_int(0));
        lk.add_int("source_item_id", stmt.column_int(1));
        lk.add_int("target_item_id", stmt.column_int(2));
        lk.add_str("link_type", stmt.column_str(3));
        lk.add_str("source_key", stmt.column_str(4));
        lk.add_str("source_title", stmt.column_str(5));
        lk.add_str("source_status", stmt.column_str(6));
        lk.add_str("source_type", stmt.column_str(7));
        lk.add_str("target_key", stmt.column_str(8));
        lk.add_str("target_title", stmt.column_str(9));
        lk.add_str("target_status", stmt.column_str(10));
        lk.add_str("target_type", stmt.column_str(11));
        arr.add(std::move(lk));
    }
    send_json(worker, header, "200 OK", arr);
}

// DELETE /api/links/([0-9]+)
static void handle_delete_link(backend::wsworker *worker,
                               const std::map<std::string, std::string> &header,
                               const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing link ID");
        return;
    }
    int link_id = std::atoi(uri_params.front().c_str());
    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    {
        db::sqlite_transaction tx(conn);
        auto del = conn.prepare("DELETE FROM work_item_links WHERE id = ?;");
        del.bind_int(1, link_id);
        del.step_done();
        tx.commit();
    }

    auto res = json::json_value::make_object();
    res.add_bool("deleted", true);
    send_json(worker, header, "200 OK", res);
}

// POST /api/items/([A-Za-z0-9_-]+)/attachments
static void handle_item_attachments(backend::wsworker *worker,
                                    const std::map<std::string, std::string> &header,
                                    const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing item ID or key");
        return;
    }
    auto conn = acquire_db_connection();
    int item_id = resolve_work_item_id(conn, uri_params.front());
    if (item_id <= 0) {
        send_error(worker, header, "404 Not Found", "Work item not found");
        return;
    }
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    std::string content_type = get_header(header, "CONTENT_TYPE");
    std::string orig_filename;
    std::string mime_type = "application/octet-stream";
    std::string file_bytes;

    if (content_type.find("multipart/form-data") != std::string::npos) {
        auto parsed = utils::parse_multipart_file(content_type, worker->body());
        if (!parsed.valid) {
            send_error(worker, header, "400 Bad Request", "Could not parse multipart file upload");
            return;
        }
        orig_filename = parsed.filename;
        mime_type = parsed.mime_type;
        file_bytes = std::move(parsed.data);
    } else {
        auto body = json::json_value::parse(worker->body());
        if (!body) {
            send_error(worker, header, "400 Bad Request", "Invalid attachment payload");
            return;
        }
        orig_filename = body.get_string("filename", "attachment.bin");
        mime_type = body.get_string("mime_type", "application/octet-stream");
        std::string b64 = body.get_string("content_base64");
        auto comma = b64.find(',');
        if (comma != std::string::npos && b64.starts_with("data:")) {
            b64 = b64.substr(comma + 1);
        }
        file_bytes = utils::base64_decode(b64);
    }

    std::string safe_name = utils::sanitize_filename(orig_filename);
    std::string blob_filename = utils::generate_random_hex(12) + "_" + safe_name;
    std::filesystem::create_directories(g_blobs_dir);
    std::filesystem::path blob_path = std::filesystem::path(g_blobs_dir) / blob_filename;

    {
        std::ofstream ofs(blob_path, std::ios::binary);
        if (!ofs.is_open()) {
            send_error(worker, header, "500 Internal Server Error", "Failed to write file to blobs directory");
            return;
        }
        ofs.write(file_bytes.data(), static_cast<std::streamsize>(file_bytes.size()));
    }

    int att_id = 0;
    {
        db::sqlite_transaction tx(conn);
        auto ins = conn.prepare(R"SQL(
            INSERT INTO attachments (work_item_id, uploader_id, original_filename, mime_type, size_bytes, blob_filename)
            VALUES (?, ?, ?, ?, ?, ?);
        )SQL");
        ins.bind_int(1, item_id);
        ins.bind_int(2, u_auth.id);
        ins.bind_text(3, orig_filename);
        ins.bind_text(4, mime_type);
        ins.bind_int(5, static_cast<int>(file_bytes.size()));
        ins.bind_text(6, blob_filename);
        ins.step_done();
        att_id = static_cast<int>(conn.last_insert_rowid());
        tx.commit();
    }

    auto res = json::json_value::make_object();
    res.add_int("id", att_id);
    res.add_int("work_item_id", item_id);
    res.add_int("uploader_id", u_auth.id);
    res.add_str("uploader_name", u_auth.display_name);
    res.add_str("original_filename", orig_filename);
    res.add_str("mime_type", mime_type);
    res.add_int("size_bytes", static_cast<int>(file_bytes.size()));
    res.add_str("blob_filename", blob_filename);
    res.add_str("url", "/api/blobs/" + blob_filename);
    send_json(worker, header, "201 Created", res);
}

// GET /api/blobs/([a-zA-Z0-9._-]+)
static void handle_blob_download(backend::wsworker *worker,
                                 const std::map<std::string, std::string> &header,
                                 const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing blob filename");
        return;
    }
    std::string blob_name = utils::sanitize_filename(uri_params.front());
    std::filesystem::path blob_path = std::filesystem::path(g_blobs_dir) / blob_name;

    if (!std::filesystem::exists(blob_path) || !std::filesystem::is_regular_file(blob_path)) {
        send_error(worker, header, "404 Not Found", "Blob file not found");
        return;
    }

    std::string mime_type = "application/octet-stream";
    std::string orig_name = blob_name;
    {
        auto conn = acquire_db_connection();
        auto stmt = conn.prepare("SELECT original_filename, mime_type FROM attachments WHERE blob_filename = ?;");
        stmt.bind_text(1, blob_name);
        if (stmt.step() == SQLITE_ROW) {
            orig_name = utils::sanitize_filename(stmt.column_str(0));
            mime_type = stmt.column_str(1);
        }
    }

    std::ifstream ifs(blob_path, std::ios::binary);
    std::ostringstream oss;
    oss << ifs.rdbuf();
    std::string data = oss.str();

    std::string proto = get_header(header, "SERVER_PROTOCOL");
    if (proto.empty()) proto = "HTTP/1.1";

    FCGX_FPrintF(worker->out(),
                 "%s 200 OK\r\n"
                 "Status: 200 OK\r\n"
                 "Content-Type: %s\r\n"
                 "Content-Disposition: inline; filename=\"%s\"\r\n"
                 "X-Content-Type-Options: nosniff\r\n"
                 "Content-Length: %d\r\n\r\n",
                 proto.c_str(), mime_type.c_str(), orig_name.c_str(), static_cast<int>(data.size()));
    FCGX_PutStr(data.data(), static_cast<int>(data.size()), worker->out());
}

// DELETE /api/attachments/([0-9]+)
static void handle_delete_attachment(backend::wsworker *worker,
                                     const std::map<std::string, std::string> &header,
                                     const std::list<std::string> &uri_params) {
    if (uri_params.empty()) {
        send_error(worker, header, "400 Bad Request", "Missing attachment ID");
        return;
    }
    int att_id = std::atoi(uri_params.front().c_str());

    auto conn = acquire_db_connection();
    auto u_auth = get_authenticated_user(conn, worker, header);
    if (!u_auth.authenticated) {
        send_error(worker, header, "401 Unauthorized", "Authentication required");
        return;
    }

    std::string blob_name;
    {
        auto stmt = conn.prepare("SELECT blob_filename FROM attachments WHERE id = ?;");
        stmt.bind_int(1, att_id);
        if (stmt.step() == SQLITE_ROW) {
            blob_name = stmt.column_str(0);
        }
    }

    if (!blob_name.empty()) {
        std::error_code ec;
        std::filesystem::remove(std::filesystem::path(g_blobs_dir) / blob_name, ec);
    }

    {
        db::sqlite_transaction tx(conn);
        auto del = conn.prepare("DELETE FROM attachments WHERE id = ?;");
        del.bind_int(1, att_id);
        del.step_done();
        tx.commit();
    }

    auto res = json::json_value::make_object();
    res.add_bool("deleted", true);
    send_json(worker, header, "200 OK", res);
}

static constexpr auto DEFAULT_SOCKET_NAME = ":9002";
static constexpr auto DEFAULT_BACKLOG = 100;

static std::atomic<backend::wsserver *> g_server{nullptr};

static void signal_handler(int sig) {
    (void)sig;
    backend::wsserver *server = g_server.load();
    if (server) {
        server->shutdown();
    }
}

int main(int argc, char *argv[]) {
    std::string socket_name = DEFAULT_SOCKET_NAME;
    int backlog = DEFAULT_BACKLOG;
    int worker_num = 4;
    int opt = 0;

    while ((opt = getopt(argc, argv, "hn:b:w:d:u:")) != -1) {
        switch (opt) {
        case 'n':
            socket_name = optarg;
            break;
        case 'b':
            backlog = std::atoi(optarg);
            break;
        case 'w':
            worker_num = std::atoi(optarg);
            break;
        case 'd':
            g_db_path = optarg;
            break;
        case 'u':
            g_blobs_dir = optarg;
            break;
        case 'h':
            std::cout << "Usage: " << argv[0]
                      << " [-n socket_name] [-b backlog] [-w workers] [-d db_path] [-u blobs_dir]" << std::endl;
            return 0;
        default:
            std::cerr << "Usage: " << argv[0]
                      << " [-n socket_name] [-b backlog] [-w workers] [-d db_path] [-u blobs_dir]" << std::endl;
            return 1;
        }
    }

    try {
        db::ensure_storage_and_migrate(g_db_path, g_blobs_dir);
        init_db_pool(worker_num);
        (void)acquire_db_connection();
    } catch (const std::exception &ex) {
        std::cerr << "[jiralike-demo] FATAL: Database initialization/migration failed: " << ex.what() << std::endl;
        return 1;
    }

    backend::wsserver server;
    g_server.store(&server);

    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGUSR1, &sa, nullptr);

    server.add_routes({
        {"^/api/health$",                           backend::GET,                   handle_health               },
        {"^/api/setup/status$",                     backend::GET,                   handle_setup_status         },
        {"^/api/setup/init$",                       backend::POST,                  handle_setup_init           },
        {"^/api/auth/login$",                       backend::POST,                  handle_auth_login           },
        {"^/api/auth/logout$",                      backend::POST,                  handle_auth_logout          },
        {"^/api/auth/me$",                          backend::GET,                   handle_auth_me              },
        {"^/api/users$",                            backend::GET | backend::POST,   handle_users                },
        {"^/api/projects$",                         backend::GET | backend::POST,   handle_projects             },
        {"^/api/resolutions$",                      backend::GET | backend::POST,   handle_resolutions          },
        {"^/api/issue-types$",                      backend::GET | backend::POST,   handle_issue_types          },
        {"^/api/issue-type-schemes$",               backend::GET | backend::POST,   handle_issue_type_schemes   },
        {"^/api/components$",                       backend::GET | backend::POST,   handle_components           },
        {"^/api/projects/([0-9]+)/components$",     backend::GET | backend::POST,   handle_components           },
        {"^/api/components/([0-9]+)$",              backend::PUT | backend::DELETE, handle_component_detail     },
        {"^/api/versions$",                         backend::GET | backend::POST,   handle_versions             },
        {"^/api/projects/([0-9]+)/versions$",       backend::GET | backend::POST,   handle_versions             },
        {"^/api/versions/([0-9]+)$",                backend::PUT | backend::DELETE, handle_version_detail       },
        {"^/api/versions/([0-9]+)/release-notes$",  backend::GET,                   handle_version_release_notes},
        {"^/api/custom-fields$",                    backend::GET | backend::POST,   handle_custom_fields        },
        {"^/api/custom-fields/([0-9]+)$",           backend::PUT | backend::DELETE, handle_custom_field_detail  },
        {"^/api/custom-fields/([0-9]+)/options$",   backend::POST,                  handle_custom_field_options },
        {"^/api/custom-field-options/([0-9]+)$",    backend::DELETE,                handle_delete_custom_field_option},
        {"^/api/field-configurations$",             backend::GET | backend::POST,   handle_field_configurations },
        {"^/api/screens/resolve$",                  backend::GET,                   handle_resolve_screen       },
        {"^/api/screens$",                          backend::GET | backend::POST,   handle_screens              },
        {"^/api/screens/([0-9]+)$",                 backend::PUT | backend::DELETE, handle_screen_detail        },
        {"^/api/screens/([0-9]+)/tabs$",            backend::POST,                  handle_screen_tabs          },
        {"^/api/screen-tabs/([0-9]+)$",             backend::DELETE,                handle_delete_screen_tab    },
        {"^/api/screen-tabs/([0-9]+)/fields$",      backend::POST,                  handle_screen_tab_fields    },
        {"^/api/screen-tab-fields/([0-9]+)$",       backend::DELETE,                handle_delete_screen_tab_field},
        {"^/api/screen-schemes$",                   backend::GET | backend::POST,   handle_screen_schemes       },
        {"^/api/workflows$",                        backend::GET | backend::POST,   handle_workflows            },
        {"^/api/workflows/([0-9]+)$",               backend::GET | backend::PUT | backend::DELETE, handle_workflow_detail},
        {"^/api/workflows/([0-9]+)/statuses$",      backend::POST,                  handle_workflow_statuses    },
        {"^/api/workflow-statuses/([0-9]+)$",       backend::PUT | backend::DELETE, handle_workflow_status_detail},
        {"^/api/workflows/([0-9]+)/transitions$",   backend::POST,                  handle_workflow_transitions },
        {"^/api/workflow-transitions/([0-9]+)$",    backend::PUT | backend::DELETE, handle_workflow_transition_detail},
        {"^/api/workflow-transitions/([0-9]+)/rules$", backend::POST,               handle_workflow_transition_rules},
        {"^/api/workflow-transition-rules/([0-9]+)$", backend::DELETE,              handle_delete_workflow_transition_rule},
        {"^/api/workflow-schemes$",                 backend::GET | backend::POST,   handle_workflow_schemes     },
        {"^/api/workflow-schemes/([0-9]+)$",        backend::DELETE,                handle_delete_workflow_scheme},
        {"^/api/search/jql$",                       backend::GET | backend::POST,   handle_search_jql           },
        {"^/api/search/validate$",                  backend::POST,                  handle_search_validate      },
        {"^/api/search/suggestions$",               backend::GET,                   handle_search_suggestions   },
        {"^/api/filters$",                          backend::GET | backend::POST,   handle_saved_filters        },
        {"^/api/filters/([0-9]+)$",                 backend::GET | backend::PUT | backend::DELETE, handle_saved_filter_detail},
        {"^/api/filters/([0-9]+)/star$",            backend::POST,                  handle_saved_filter_star    },
        {"^/api/filters/([0-9]+)/subscriptions$",   backend::GET | backend::POST,   handle_filter_subscriptions },
        {"^/api/filter-subscriptions/([0-9]+)$",    backend::DELETE,                handle_delete_filter_subscription},
        {"^/api/sprints$",                          backend::GET | backend::POST,   handle_sprints              },
        {"^/api/sprints/([0-9]+)$",                 backend::PUT | backend::DELETE, handle_sprint_detail        },
        {"^/api/sprints/([0-9]+)/current$",         backend::POST,                  handle_sprint_set_current   },
        {"^/api/items$",                                backend::GET | backend::POST,   handle_items                },
        {"^/api/items/([A-Za-z0-9_-]+)$",               backend::GET | backend::PUT | backend::DELETE, handle_item_detail },
        {"^/api/items/([A-Za-z0-9_-]+)/transitions$",   backend::GET | backend::POST,   handle_item_available_transitions},
        {"^/api/items/([A-Za-z0-9_-]+)/state$",         backend::PATCH | backend::POST, handle_item_state_transition},
        {"^/api/items/([A-Za-z0-9_-]+)/comments$",      backend::POST,                  handle_item_comments        },
        {"^/api/items/([A-Za-z0-9_-]+)/links$",         backend::POST,                  handle_item_links           },
        {"^/api/links$",                                backend::GET,                   handle_all_links            },
        {"^/api/links/([0-9]+)$",                       backend::DELETE,                handle_delete_link          },
        {"^/api/items/([A-Za-z0-9_-]+)/attachments$",   backend::POST,                  handle_item_attachments     },
        {"^/api/blobs/([a-zA-Z0-9._-]+)$",          backend::GET,                   handle_blob_download        },
        {"^/api/attachments/([0-9]+)$",             backend::DELETE,                handle_delete_attachment    },
    });

    server.init(socket_name.c_str(), backlog, worker_num);

    int ret = server.run();
    g_server.store(nullptr);
    if (g_db_pool) {
        g_db_pool->shutdown();
    }
    return ret;
}
