#pragma once

#include <sqlite3.h>
#include <atomic>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace db {

class sqlite_stmt;

// LRU cache for compiled sqlite3_stmt handles belonging to a single sqlite_db connection.
// Managed via std::shared_ptr so any sqlite_stmt that ever outlives its parent sqlite_db
// safely finalizes its handle instead of accessing dangling memory.
class statement_cache {
public:
    explicit statement_cache(size_t max_capacity = 128) : m_max_capacity(max_capacity) {}

    ~statement_cache() {
        clear();
    }

    statement_cache(const statement_cache &) = delete;
    statement_cache &operator=(const statement_cache &) = delete;

    // Try to check out an idle compiled statement for `sql`.
    sqlite3_stmt *checkout(const std::string &sql) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_closed) return nullptr;

        auto it = m_map.find(sql);
        if (it == m_map.end()) {
            m_misses.fetch_add(1, std::memory_order_relaxed);
            return nullptr;
        }

        sqlite3_stmt *stmt = it->second->second;
        m_lru.erase(it->second);
        m_map.erase(it);

        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        m_hits.fetch_add(1, std::memory_order_relaxed);
        return stmt;
    }

    // Return a statement back to the cache when sqlite_stmt is destroyed/reset.
    void checkin(const std::string &sql, sqlite3_stmt *stmt) noexcept {
        if (!stmt) return;

        // Reset statement and clear bound parameters before caching.
        // If reset reports an error (e.g. schema changed or constraint failure on last step),
        // clear bindings and still check whether the statement is reusable.
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);

        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_closed || m_max_capacity == 0 || sql.empty()) {
            sqlite3_finalize(stmt);
            return;
        }

        // If another instance of the exact same SQL was already returned to the cache,
        // finalize the duplicate and keep the cached one fresh at the front of the LRU.
        auto existing = m_map.find(sql);
        if (existing != m_map.end()) {
            sqlite3_finalize(stmt);
            m_lru.splice(m_lru.begin(), m_lru, existing->second);
            return;
        }

        // Evict least-recently-used statement if at capacity
        if (m_map.size() >= m_max_capacity && !m_lru.empty()) {
            auto &oldest = m_lru.back();
            sqlite3_finalize(oldest.second);
            m_map.erase(oldest.first);
            m_lru.pop_back();
        }

        m_lru.emplace_front(sql, stmt);
        m_map.emplace(m_lru.front().first, m_lru.begin());
    }

    void clear() noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto &entry : m_lru) {
            if (entry.second) {
                sqlite3_finalize(entry.second);
            }
        }
        m_lru.clear();
        m_map.clear();
    }

    void close() noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_closed = true;
        for (auto &entry : m_lru) {
            if (entry.second) {
                sqlite3_finalize(entry.second);
            }
        }
        m_lru.clear();
        m_map.clear();
    }

    size_t size() const noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_map.size();
    }

    uint64_t hits() const noexcept {
        return m_hits.load(std::memory_order_relaxed);
    }

    uint64_t misses() const noexcept {
        return m_misses.load(std::memory_order_relaxed);
    }

private:
    using lru_list_t = std::list<std::pair<std::string, sqlite3_stmt *>>;

    mutable std::mutex m_mutex;
    size_t m_max_capacity = 128;
    bool m_closed = false;
    lru_list_t m_lru;
    std::unordered_map<std::string, lru_list_t::iterator> m_map;
    std::atomic<uint64_t> m_hits{0};
    std::atomic<uint64_t> m_misses{0};
};

class sqlite_stmt {
public:
    sqlite_stmt() = default;

    sqlite_stmt(sqlite3 *db,
                const std::string &sql,
                std::weak_ptr<statement_cache> cache = {})
        : m_sql(sql), m_cache(std::move(cache)) {
        if (auto c = m_cache.lock()) {
            m_stmt = c->checkout(sql);
        }
        if (!m_stmt) {
            unsigned int prep_flags = m_cache.expired() ? 0u : SQLITE_PREPARE_PERSISTENT;
            int rc = sqlite3_prepare_v3(db, sql.c_str(), -1, prep_flags, &m_stmt, nullptr);
            if (rc != SQLITE_OK) {
                std::string err = db ? sqlite3_errmsg(db) : "Failed to prepare statement";
                m_stmt = nullptr;
                throw std::runtime_error("Can't prepare SQL (" + sql + "): " + err);
            }
        }
    }

    ~sqlite_stmt() {
        reset();
    }

    sqlite_stmt(const sqlite_stmt &) = delete;
    sqlite_stmt &operator=(const sqlite_stmt &) = delete;

    sqlite_stmt(sqlite_stmt &&other) noexcept
        : m_stmt(other.m_stmt),
          m_sql(std::move(other.m_sql)),
          m_cache(std::move(other.m_cache)) {
        other.m_stmt = nullptr;
    }

    sqlite_stmt &operator=(sqlite_stmt &&other) noexcept {
        if (this != &other) {
            reset();
            m_stmt = other.m_stmt;
            m_sql = std::move(other.m_sql);
            m_cache = std::move(other.m_cache);
            other.m_stmt = nullptr;
        }
        return *this;
    }

    void reset() noexcept {
        if (m_stmt) {
            if (auto c = m_cache.lock()) {
                c->checkin(m_sql, m_stmt);
            } else {
                sqlite3_finalize(m_stmt);
            }
            m_stmt = nullptr;
        }
    }

    void clear_and_reset() {
        if (m_stmt) {
            sqlite3_reset(m_stmt);
            sqlite3_clear_bindings(m_stmt);
        }
    }

    sqlite3_stmt *get() const noexcept { return m_stmt; }
    explicit operator bool() const noexcept { return m_stmt != nullptr; }

    int step() {
        return sqlite3_step(m_stmt);
    }

    void step_done() {
        int rc = sqlite3_step(m_stmt);
        if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
            sqlite3 *db = sqlite3_db_handle(m_stmt);
            std::string err = db ? sqlite3_errmsg(db) : "Unknown step error";
            throw std::runtime_error("SQLite step failed: " + err);
        }
    }

    void bind_int(int idx, int val) {
        if (sqlite3_bind_int(m_stmt, idx, val) != SQLITE_OK) {
            throw std::runtime_error("SQL bind_int failed");
        }
    }

    void bind_int64(int idx, int64_t val) {
        if (sqlite3_bind_int64(m_stmt, idx, val) != SQLITE_OK) {
            throw std::runtime_error("SQL bind_int64 failed");
        }
    }

    void bind_double(int idx, double val) {
        if (sqlite3_bind_double(m_stmt, idx, val) != SQLITE_OK) {
            throw std::runtime_error("SQL bind_double failed");
        }
    }

    void bind_text(int idx, const std::string &val) {
        if (sqlite3_bind_text(m_stmt, idx, val.c_str(), static_cast<int>(val.size()), SQLITE_TRANSIENT) != SQLITE_OK) {
            throw std::runtime_error("SQL bind_text failed");
        }
    }

    void bind_null(int idx) {
        if (sqlite3_bind_null(m_stmt, idx) != SQLITE_OK) {
            throw std::runtime_error("SQL bind_null failed");
        }
    }

    void bind_optional_int(int idx, int val) {
        if (val <= 0) {
            bind_null(idx);
        } else {
            bind_int(idx, val);
        }
    }

    int column_count() const noexcept {
        return sqlite3_column_count(m_stmt);
    }

    const char *column_name(int col) const noexcept {
        return sqlite3_column_name(m_stmt, col);
    }

    bool column_is_null(int col) const noexcept {
        return sqlite3_column_type(m_stmt, col) == SQLITE_NULL;
    }

    std::string column_str(int col) const {
        const auto *txt = reinterpret_cast<const char *>(sqlite3_column_text(m_stmt, col));
        return txt ? std::string(txt) : std::string();
    }

    const char *column_text(int col) const noexcept {
        return reinterpret_cast<const char *>(sqlite3_column_text(m_stmt, col));
    }

    int column_int(int col) const noexcept {
        return sqlite3_column_int(m_stmt, col);
    }

    int64_t column_int64(int col) const noexcept {
        return sqlite3_column_int64(m_stmt, col);
    }

    double column_double(int col) const noexcept {
        return sqlite3_column_double(m_stmt, col);
    }

private:
    sqlite3_stmt *m_stmt = nullptr;
    std::string m_sql;
    std::weak_ptr<statement_cache> m_cache;
};

class sqlite_db {
public:
    sqlite_db() = default;

    explicit sqlite_db(const std::string &path,
                       int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                       size_t stmt_cache_capacity = 128)
        : m_stmt_cache_capacity(stmt_cache_capacity) {
        open(path, flags);
    }

    ~sqlite_db() {
        close();
    }

    sqlite_db(const sqlite_db &) = delete;
    sqlite_db &operator=(const sqlite_db &) = delete;

    sqlite_db(sqlite_db &&other) noexcept
        : m_db(other.m_db),
          m_path(std::move(other.m_path)),
          m_flags(other.m_flags),
          m_stmt_cache_capacity(other.m_stmt_cache_capacity),
          m_cache(std::move(other.m_cache)) {
        other.m_db = nullptr;
    }

    sqlite_db &operator=(sqlite_db &&other) noexcept {
        if (this != &other) {
            close();
            m_db = other.m_db;
            m_path = std::move(other.m_path);
            m_flags = other.m_flags;
            m_stmt_cache_capacity = other.m_stmt_cache_capacity;
            m_cache = std::move(other.m_cache);
            other.m_db = nullptr;
        }
        return *this;
    }

    void open(const std::string &path,
              int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX) {
        close();
        m_path = path;
        m_flags = flags;
        int rc = sqlite3_open_v2(path.c_str(), &m_db, flags, nullptr);
        if (rc != SQLITE_OK) {
            std::string err = m_db ? sqlite3_errmsg(m_db) : "Failed to open database";
            close();
            throw std::runtime_error("Can't open database '" + path + "': " + err);
        }
        sqlite3_busy_timeout(m_db, 5000);
        exec("PRAGMA foreign_keys = ON;");
        exec("PRAGMA journal_mode = WAL;");
        exec("PRAGMA synchronous = NORMAL;");
        exec("PRAGMA temp_store = MEMORY;");
        exec("PRAGMA mmap_size = 268435456;");
        exec("PRAGMA cache_size = -16000;");

        m_cache = std::make_shared<statement_cache>(m_stmt_cache_capacity);
    }

    void close() noexcept {
        if (m_cache) {
            m_cache->close();
            m_cache.reset();
        }
        if (m_db) {
            sqlite3_close_v2(m_db);
            m_db = nullptr;
        }
    }

    // Liveness & health check used by the connection pool on lease acquisition
    bool ping() const noexcept {
        if (!m_db) return false;
        try {
            auto stmt = prepare("SELECT 1;");
            return stmt.step() == SQLITE_ROW && stmt.column_int(0) == 1;
        } catch (...) {
            return false;
        }
    }

    // Reset persistent SQLite connection state before/after handling a request
    void reset_for_request() {
        if (!m_db) {
            if (!m_path.empty()) {
                open(m_path, m_flags);
            }
            return;
        }
        if (sqlite3_get_autocommit(m_db) == 0) {
            char *err_msg = nullptr;
            sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, &err_msg);
            if (err_msg) sqlite3_free(err_msg);
        }
    }

    void clear_statement_cache() noexcept {
        if (m_cache) {
            m_cache->clear();
        }
    }

    void reset() noexcept {
        close();
    }

    const std::string &path() const noexcept { return m_path; }
    sqlite3 *get() const noexcept { return m_db; }
    explicit operator bool() const noexcept { return m_db != nullptr; }

    const char *errmsg() const noexcept {
        return m_db ? sqlite3_errmsg(m_db) : "Database not open";
    }

    void exec(const std::string &sql) const {
        if (!m_db) throw std::runtime_error("Database connection is closed");
        char *err_msg = nullptr;
        int rc = sqlite3_exec(m_db, sql.c_str(), nullptr, nullptr, &err_msg);
        if (rc != SQLITE_OK) {
            std::string err = err_msg ? err_msg : "Unknown SQLite exec error";
            if (err_msg) sqlite3_free(err_msg);
            throw std::runtime_error("SQL exec error: " + err);
        }
    }

    int64_t last_insert_rowid() const noexcept {
        return m_db ? sqlite3_last_insert_rowid(m_db) : 0;
    }

    int changes() const noexcept {
        return m_db ? sqlite3_changes(m_db) : 0;
    }

    sqlite_stmt prepare(const std::string &sql) const {
        if (!m_db) throw std::runtime_error("Database connection is closed");
        return sqlite_stmt(m_db, sql, m_cache);
    }

    // Prepared statement cache metrics
    size_t cached_statement_count() const noexcept {
        return m_cache ? m_cache->size() : 0;
    }

    uint64_t cache_hits() const noexcept {
        return m_cache ? m_cache->hits() : 0;
    }

    uint64_t cache_misses() const noexcept {
        return m_cache ? m_cache->misses() : 0;
    }

private:
    sqlite3 *m_db = nullptr;
    std::string m_path;
    int m_flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    size_t m_stmt_cache_capacity = 128;
    std::shared_ptr<statement_cache> m_cache;
};

class sqlite_transaction {
public:
    explicit sqlite_transaction(sqlite_db &db, bool immediate = true) : m_db(db) {
        m_db.exec(immediate ? "BEGIN IMMEDIATE;" : "BEGIN;");
    }

    ~sqlite_transaction() {
        if (!m_committed) {
            try {
                m_db.exec("ROLLBACK;");
            } catch (...) {
            }
        }
    }

    sqlite_transaction(const sqlite_transaction &) = delete;
    sqlite_transaction &operator=(const sqlite_transaction &) = delete;

    void commit() {
        if (!m_committed) {
            m_db.exec("COMMIT;");
            m_committed = true;
        }
    }

    void rollback() {
        if (!m_committed) {
            m_db.exec("ROLLBACK;");
            m_committed = true;
        }
    }

private:
    sqlite_db &m_db;
    bool m_committed = false;
};

} // namespace db
