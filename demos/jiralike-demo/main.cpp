#include "wsserver.h"
#include "wsworker.h"
#include "wsroute.h"

#include "src/raii_sqlite.h"
#include "src/connection_pool.h"
#include "src/raii_json.h"
#include "src/crypto_utils.h"
#include "src/db_migrator.h"

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

        sql += " ORDER BY w.board_order ASC, w.id ASC;";
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
                   h.comment, h.created_at
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
            history.add(std::move(hi));
        }
        item.add("history", std::move(history));

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
    int old_resolution_id = 0;
    std::string old_resolution_date;
    {
        auto cur = conn.prepare("SELECT status, COALESCE(resolution_id, 0), COALESCE(resolution_date, '') FROM work_items WHERE id = ?;");
        cur.bind_int(1, item_id);
        if (cur.step() != SQLITE_ROW) {
            send_error(worker, header, "404 Not Found", "Work item not found");
            return;
        }
        old_status = cur.column_str(0);
        old_resolution_id = cur.column_int(1);
        old_resolution_date = cur.column_str(2);
    }

    std::string type = body.get_string("type", "Task");
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

// PATCH /api/items/([A-Za-z0-9_-]+)/state
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

    std::string old_status;
    int old_progress = 0;
    std::string old_start;
    std::string old_due;
    int old_order = 0;
    int old_res_id = 0;
    std::string old_res_date;
    {
        auto cur = conn.prepare(R"SQL(
            SELECT status, progress, start_date, due_date, board_order,
                   COALESCE(resolution_id, 0), COALESCE(resolution_date, '')
            FROM work_items WHERE id = ?;
        )SQL");
        cur.bind_int(1, item_id);
        if (cur.step() != SQLITE_ROW) {
            send_error(worker, header, "404 Not Found", "Work item not found");
            return;
        }
        old_status = cur.column_str(0);
        old_progress = cur.column_int(1);
        old_start = cur.column_str(2);
        old_due = cur.column_str(3);
        old_order = cur.column_int(4);
        old_res_id = cur.column_int(5);
        old_res_date = cur.column_str(6);
    }

    std::string new_status = body.get_string("status", old_status);
    int new_progress = body.has_key("progress") ? body.get_int("progress", old_progress) : old_progress;
    if (!body.has_key("progress")) {
        if (new_status == "Done") new_progress = 100;
        else if (new_status == "To Do" || new_status == "Backlog") new_progress = 0;
        else if (new_status == "In Progress" && new_progress == 0) new_progress = 40;
        else if (new_status == "In Review" && new_progress < 75) new_progress = 80;
        else if (new_status == "QA / Testing" && new_progress < 85) new_progress = 90;
    }
    std::string new_start = body.has_key("start_date") ? body.get_string("start_date") : old_start;
    std::string new_due = body.has_key("due_date") ? body.get_string("due_date") : old_due;
    int new_order = body.has_key("board_order") ? body.get_int("board_order", old_order) : old_order;
    std::string comment = body.get_string("comment", "Transitioned via board drag-and-drop");

    int new_res_id = body.has_key("resolution_id") ? body.get_int("resolution_id", 0) : old_res_id;
    bool set_res_now = false;
    std::string new_res_date = old_res_date;

    if (new_status == "Done") {
        if (new_res_id <= 0) {
            new_res_id = resolve_default_resolution_id(conn);
        }
        if (new_res_date.empty() || new_res_id != old_res_id) {
            set_res_now = true;
        }
    } else if (old_status == "Done" && new_status != "Done" && !body.has_key("resolution_id")) {
        // Clear resolution when transitioning out of Done (Reopen)
        new_res_id = 0;
        new_res_date.clear();
    } else if (new_res_id <= 0) {
        new_res_date.clear();
    }

    {
        db::sqlite_transaction tx(conn);
        auto upd = conn.prepare(R"SQL(
            UPDATE work_items
            SET status = ?, progress = ?, start_date = ?, due_date = ?, board_order = ?,
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
        upd.bind_optional_int(6, new_res_id);
        upd.bind_int(7, set_res_now ? 1 : 0);
        upd.bind_text(8, new_res_date);
        upd.bind_int(9, item_id);
        upd.step_done();

        if (new_status != old_status) {
            auto hist = conn.prepare(R"SQL(
                INSERT INTO workflow_history (work_item_id, from_status, to_status, changed_by, comment)
                VALUES (?, ?, ?, ?, ?);
            )SQL");
            hist.bind_int(1, item_id);
            hist.bind_text(2, old_status);
            hist.bind_text(3, new_status);
            hist.bind_int(4, u_auth.id);
            hist.bind_text(5, comment);
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
        {"^/api/sprints$",                          backend::GET | backend::POST,   handle_sprints              },
        {"^/api/sprints/([0-9]+)$",                 backend::PUT | backend::DELETE, handle_sprint_detail        },
        {"^/api/sprints/([0-9]+)/current$",         backend::POST,                  handle_sprint_set_current   },
        {"^/api/items$",                                backend::GET | backend::POST,   handle_items                },
        {"^/api/items/([A-Za-z0-9_-]+)$",               backend::GET | backend::PUT | backend::DELETE, handle_item_detail },
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
