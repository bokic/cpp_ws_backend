#pragma once

#include "database.h"
#include <sqlite3.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace backend::db {

// ==================== Statement LRU Cache ====================

class statement_cache {
public:
    explicit statement_cache(size_t max_capacity = 128);
    ~statement_cache();

    statement_cache(const statement_cache &) = delete;
    statement_cache &operator=(const statement_cache &) = delete;

    sqlite3_stmt *checkout(const std::string &sql);
    void checkin(const std::string &sql, sqlite3_stmt *stmt) noexcept;
    void clear() noexcept;
    void close() noexcept;
    size_t size() const noexcept;
    uint64_t hits() const noexcept;
    uint64_t misses() const noexcept;

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

struct sqlite_stmt_holder {
    sqlite3 *db = nullptr;
    sqlite3_stmt *stmt = nullptr;
    std::string sql;
    std::weak_ptr<statement_cache> cache;

    sqlite_stmt_holder(sqlite3 *d, sqlite3_stmt *s, std::string q, std::weak_ptr<statement_cache> c);
    ~sqlite_stmt_holder();

    sqlite_stmt_holder(const sqlite_stmt_holder &) = delete;
    sqlite_stmt_holder &operator=(const sqlite_stmt_holder &) = delete;
};

// ==================== sqlite_row ====================

class sqlite_row : public row {
public:
    sqlite_row() = default;
    explicit sqlite_row(std::shared_ptr<sqlite_stmt_holder> holder);
    ~sqlite_row() override = default;

    size_t column_count() const noexcept override;
    std::string_view column_name(size_t index) const override;
    size_t column_index(std::string_view name) const override;
    bool is_null(size_t index) const override;
    column_type get_column_type(size_t index) const override;

    int32_t get_int32(size_t index) const override;
    int64_t get_int64(size_t index) const override;
    double get_double(size_t index) const override;
    std::string get_string(size_t index) const override;
    std::span<const uint8_t> get_blob(size_t index) const override;
    std::chrono::system_clock::time_point get_timestamp(size_t index) const override;

    // Helper for direct pointer to underlying statement
    sqlite3_stmt *get_stmt() const noexcept { return m_holder ? m_holder->stmt : nullptr; }

private:
    std::shared_ptr<sqlite_stmt_holder> m_holder;
    mutable std::vector<std::string> m_cached_names;
    void ensure_column_names() const;
};

// ==================== sqlite_result_set ====================

class sqlite_result_set : public result_set {
public:
    explicit sqlite_result_set(std::shared_ptr<sqlite_stmt_holder> holder);
    ~sqlite_result_set() override;

    bool next() override;
    const row& current_row() const override;
    size_t column_count() const noexcept override;
    std::string_view column_name(size_t index) const override;
    size_t column_index(std::string_view name) const override;
    uint64_t affected_rows() const noexcept override;

private:
    std::shared_ptr<sqlite_stmt_holder> m_holder;
    sqlite_row m_row;
};

// ==================== sqlite_statement ====================

class sqlite_statement : public statement {
public:
    sqlite_statement() = default;
    sqlite_statement(sqlite3 *db, std::string_view sql, std::weak_ptr<statement_cache> cache = {});
    ~sqlite_statement() override = default;

    // Positional binding (1-based index)
    void bind(size_t index, std::nullptr_t) override;
    void bind(size_t index, int32_t val) override;
    void bind(size_t index, int64_t val) override;
    void bind(size_t index, double val) override;
    void bind(size_t index, std::string_view val) override;
    void bind(size_t index, std::span<const uint8_t> val) override;
    void bind(size_t index, std::chrono::system_clock::time_point val) override;

    // Named binding
    void bind(std::string_view name, std::nullptr_t) override;
    void bind(std::string_view name, int32_t val) override;
    void bind(std::string_view name, int64_t val) override;
    void bind(std::string_view name, double val) override;
    void bind(std::string_view name, std::string_view val) override;
    void bind(std::string_view name, std::span<const uint8_t> val) override;
    void bind(std::string_view name, std::chrono::system_clock::time_point val) override;

    void execute() override;
    std::unique_ptr<result_set> query() override;
    void reset() override;

    // Direct accessors & ergonomic compatibility helpers
    int step();
    void step_done();
    sqlite3_stmt *get() const noexcept { return m_holder ? m_holder->stmt : nullptr; }
    void bind_int(int idx, int val) { bind(static_cast<size_t>(idx), val); }
    void bind_int64(int idx, int64_t val) { bind(static_cast<size_t>(idx), val); }
    void bind_double(int idx, double val) { bind(static_cast<size_t>(idx), val); }
    void bind_text(int idx, const std::string &val) { bind(static_cast<size_t>(idx), std::string_view(val)); }
    void bind_null(int idx) { bind(static_cast<size_t>(idx), nullptr); }

private:
    int get_param_index(std::string_view name);
    std::shared_ptr<sqlite_stmt_holder> m_holder;
};

class sqlite_database;

// ==================== sqlite_transaction ====================

class sqlite_transaction : public transaction {
public:
    explicit sqlite_transaction(sqlite_database &db, bool immediate = true);
    explicit sqlite_transaction(sqlite3 *db_handle, bool immediate = true);
    ~sqlite_transaction() override;

    sqlite_transaction(const sqlite_transaction &) = delete;
    sqlite_transaction &operator=(const sqlite_transaction &) = delete;

    sqlite_transaction(sqlite_transaction &&other) noexcept;
    sqlite_transaction &operator=(sqlite_transaction &&other) noexcept;

    void commit() override;
    void rollback() override;
    bool is_active() const noexcept override;

private:
    sqlite3 *m_db = nullptr;
    bool m_active = false;
};

// ==================== sqlite_database ====================

class sqlite_database : public database {
public:
    sqlite_database() = default;
    explicit sqlite_database(
        const std::string &path,
        int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        size_t stmt_cache_capacity = 128
    );
    ~sqlite_database() override;

    sqlite_database(const sqlite_database &) = delete;
    sqlite_database &operator=(const sqlite_database &) = delete;

    sqlite_database(sqlite_database &&other) noexcept;
    sqlite_database &operator=(sqlite_database &&other) noexcept;

    // database interface
    void connect(const std::string &conn_str) override;
    void disconnect() override;
    bool is_connected() const noexcept override;
    bool ping() noexcept override;
    void execute(std::string_view sql) override;
    std::unique_ptr<statement> prepare(std::string_view sql) override;
    std::unique_ptr<transaction> begin_transaction() override;

    // Extended methods from jiralike-demo
    void open(
        const std::string &path,
        int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX
    );
    void close() noexcept;
    void reset_for_request();
    void exec(const std::string &sql) { execute(sql); }
    int64_t last_insert_rowid() const noexcept;
    int changes() const noexcept;
    const char *errmsg() const noexcept;
    const std::string &path() const noexcept { return m_path; }
    sqlite3 *get() const noexcept { return m_db; }
    explicit operator bool() const noexcept { return m_db != nullptr; }

    std::unique_ptr<transaction> begin_transaction(bool immediate);

    // Prepared statement cache metrics
    size_t cached_statement_count() const noexcept;
    uint64_t cache_hits() const noexcept;
    uint64_t cache_misses() const noexcept;
    void clear_statement_cache() noexcept;

private:
    sqlite3 *m_db = nullptr;
    std::string m_path;
    int m_flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    size_t m_stmt_cache_capacity = 128;
    std::shared_ptr<statement_cache> m_cache;
};

using sqlite_db = sqlite_database;

} // namespace backend::db
