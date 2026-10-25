#pragma once

#include <sqlite3.h>
#include <string>
#include <stdexcept>
#include <utility>

namespace db {

class sqlite_stmt;

class sqlite_db {
public:
    sqlite_db() = default;

    explicit sqlite_db(const std::string &path, int flags = SQLITE_OPEN_READONLY) {
        open(path, flags);
    }

    ~sqlite_db() {
        close();
    }

    sqlite_db(const sqlite_db &) = delete;
    sqlite_db &operator=(const sqlite_db &) = delete;

    sqlite_db(sqlite_db &&other) noexcept
        : m_db(other.m_db), m_path(std::move(other.m_path)), m_flags(other.m_flags) {
        other.m_db = nullptr;
    }

    sqlite_db &operator=(sqlite_db &&other) noexcept {
        if (this != &other) {
            close();
            m_db = other.m_db;
            m_path = std::move(other.m_path);
            m_flags = other.m_flags;
            other.m_db = nullptr;
        }
        return *this;
    }

    void open(const std::string &path, int flags = SQLITE_OPEN_READONLY) {
        close();
        m_path = path;
        m_flags = flags;
        int rc = sqlite3_open_v2(path.c_str(), &m_db, flags, nullptr);
        if (rc != SQLITE_OK) {
            std::string err = m_db ? sqlite3_errmsg(m_db) : "Failed to open database";
            close();
            throw std::runtime_error("Can't open database: " + err);
        }
        sqlite3_busy_timeout(m_db, 5000);
        sqlite3_exec(m_db, "PRAGMA journal_mode = WAL;", nullptr, nullptr, nullptr);
        sqlite3_exec(m_db, "PRAGMA synchronous = NORMAL;", nullptr, nullptr, nullptr);
    }

    void close() noexcept {
        if (m_db) {
            sqlite3_close_v2(m_db);
            m_db = nullptr;
        }
    }

    void reset_for_request() {
        if (!m_db) {
            if (!m_path.empty()) {
                open(m_path, m_flags);
            }
            return;
        }
        sqlite3_stmt *stmt = nullptr;
        while ((stmt = sqlite3_next_stmt(m_db, stmt)) != nullptr) {
            sqlite3_reset(stmt);
            sqlite3_clear_bindings(stmt);
        }
        if (sqlite3_get_autocommit(m_db) == 0) {
            char *err_msg = nullptr;
            sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, &err_msg);
            if (err_msg) sqlite3_free(err_msg);
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

    sqlite_stmt prepare(const std::string &sql) const;

private:
    sqlite3 *m_db = nullptr;
    std::string m_path;
    int m_flags = SQLITE_OPEN_READONLY;
};

class sqlite_stmt {
public:
    sqlite_stmt() = default;

    sqlite_stmt(sqlite3 *db, const std::string &sql) {
        int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &m_stmt, nullptr);
        if (rc != SQLITE_OK) {
            std::string err = db ? sqlite3_errmsg(db) : "Failed to prepare statement";
            reset();
            throw std::runtime_error("Can't prepare SQL: " + err);
        }
    }

    ~sqlite_stmt() {
        reset();
    }

    sqlite_stmt(const sqlite_stmt &) = delete;
    sqlite_stmt &operator=(const sqlite_stmt &) = delete;

    sqlite_stmt(sqlite_stmt &&other) noexcept : m_stmt(other.m_stmt) {
        other.m_stmt = nullptr;
    }

    sqlite_stmt &operator=(sqlite_stmt &&other) noexcept {
        if (this != &other) {
            reset();
            m_stmt = other.m_stmt;
            other.m_stmt = nullptr;
        }
        return *this;
    }

    void reset() noexcept {
        if (m_stmt) {
            sqlite3_finalize(m_stmt);
            m_stmt = nullptr;
        }
    }

    sqlite3_stmt *get() const noexcept { return m_stmt; }
    explicit operator bool() const noexcept { return m_stmt != nullptr; }

    int step() {
        return sqlite3_step(m_stmt);
    }

    void bind_int(int idx, int val) {
        int rc = sqlite3_bind_int(m_stmt, idx, val);
        if (rc != SQLITE_OK) {
            throw std::runtime_error("SQL parameter bind fail!");
        }
    }

    int column_count() const noexcept {
        return sqlite3_column_count(m_stmt);
    }

    const char *column_name(int col) const noexcept {
        return sqlite3_column_name(m_stmt, col);
    }

    const char *column_text(int col) const noexcept {
        return reinterpret_cast<const char *>(sqlite3_column_text(m_stmt, col));
    }

    int column_int(int col) const noexcept {
        return sqlite3_column_int(m_stmt, col);
    }

private:
    sqlite3_stmt *m_stmt = nullptr;
};

inline sqlite_stmt sqlite_db::prepare(const std::string &sql) const {
    if (!m_db) throw std::runtime_error("Database connection is closed");
    return sqlite_stmt(m_db, sql);
}

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

private:
    sqlite_db &m_db;
    bool m_committed = false;
};

} // namespace db
