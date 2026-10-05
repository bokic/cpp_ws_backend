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
        int rc = sqlite3_open_v2(path.c_str(), &m_db, flags, nullptr);
        if (rc != SQLITE_OK) {
            std::string err = m_db ? sqlite3_errmsg(m_db) : "Failed to open database";
            reset();
            throw std::runtime_error("Can't open database: " + err);
        }
    }

    ~sqlite_db() {
        reset();
    }

    sqlite_db(const sqlite_db &) = delete;
    sqlite_db &operator=(const sqlite_db &) = delete;

    sqlite_db(sqlite_db &&other) noexcept : m_db(other.m_db) {
        other.m_db = nullptr;
    }

    sqlite_db &operator=(sqlite_db &&other) noexcept {
        if (this != &other) {
            reset();
            m_db = other.m_db;
            other.m_db = nullptr;
        }
        return *this;
    }

    void reset() noexcept {
        if (m_db) {
            sqlite3_close_v2(m_db);
            m_db = nullptr;
        }
    }

    sqlite3 *get() const noexcept { return m_db; }
    explicit operator bool() const noexcept { return m_db != nullptr; }

    const char *errmsg() const noexcept {
        return m_db ? sqlite3_errmsg(m_db) : "Database not open";
    }

    sqlite_stmt prepare(const std::string &sql) const;

private:
    sqlite3 *m_db = nullptr;
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

} // namespace db
