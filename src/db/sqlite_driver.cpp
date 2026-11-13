#include "sqlite_driver.h"
#include <ctime>
#include <cstring>
#include <algorithm>

namespace backend::db {

// ==================== statement_cache ====================

statement_cache::statement_cache(size_t max_capacity) : m_max_capacity(max_capacity) {}

statement_cache::~statement_cache() {
    clear();
}

sqlite3_stmt *statement_cache::checkout(const std::string &sql) {
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

void statement_cache::checkin(const std::string &sql, sqlite3_stmt *stmt) noexcept {
    if (!stmt) return;

    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_closed || m_max_capacity == 0 || sql.empty()) {
        sqlite3_finalize(stmt);
        return;
    }

    auto existing = m_map.find(sql);
    if (existing != m_map.end()) {
        sqlite3_finalize(stmt);
        m_lru.splice(m_lru.begin(), m_lru, existing->second);
        return;
    }

    if (m_map.size() >= m_max_capacity && !m_lru.empty()) {
        auto &oldest = m_lru.back();
        sqlite3_finalize(oldest.second);
        m_map.erase(oldest.first);
        m_lru.pop_back();
    }

    m_lru.emplace_front(sql, stmt);
    m_map.emplace(m_lru.front().first, m_lru.begin());
}

void statement_cache::clear() noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto &entry : m_lru) {
        if (entry.second) {
            sqlite3_finalize(entry.second);
        }
    }
    m_lru.clear();
    m_map.clear();
}

void statement_cache::close() noexcept {
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

size_t statement_cache::size() const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_map.size();
}

uint64_t statement_cache::hits() const noexcept {
    return m_hits.load(std::memory_order_relaxed);
}

uint64_t statement_cache::misses() const noexcept {
    return m_misses.load(std::memory_order_relaxed);
}

// ==================== sqlite_stmt_holder ====================

sqlite_stmt_holder::sqlite_stmt_holder(
    sqlite3 *d, sqlite3_stmt *s, std::string q, std::weak_ptr<statement_cache> c
) : db(d), stmt(s), sql(std::move(q)), cache(std::move(c)) {}

sqlite_stmt_holder::~sqlite_stmt_holder() {
    if (stmt) {
        if (auto c = cache.lock()) {
            c->checkin(sql, stmt);
        } else {
            sqlite3_finalize(stmt);
        }
        stmt = nullptr;
    }
}

// ==================== sqlite_row ====================

sqlite_row::sqlite_row(std::shared_ptr<sqlite_stmt_holder> holder)
    : m_holder(std::move(holder)) {}

void sqlite_row::ensure_column_names() const {
    if (!m_cached_names.empty() || !m_holder || !m_holder->stmt) return;
    int count = sqlite3_column_count(m_holder->stmt);
    m_cached_names.reserve(count);
    for (int i = 0; i < count; ++i) {
        const char *name = sqlite3_column_name(m_holder->stmt, i);
        m_cached_names.emplace_back(name ? name : "");
    }
}

size_t sqlite_row::column_count() const noexcept {
    if (!m_holder || !m_holder->stmt) return 0;
    return static_cast<size_t>(sqlite3_column_count(m_holder->stmt));
}

std::string_view sqlite_row::column_name(size_t index) const {
    ensure_column_names();
    if (index < m_cached_names.size()) {
        return m_cached_names[index];
    }
    return {};
}

size_t sqlite_row::column_index(std::string_view name) const {
    ensure_column_names();
    for (size_t i = 0; i < m_cached_names.size(); ++i) {
        if (m_cached_names[i] == name) {
            return i;
        }
    }
    throw db_exception("Column not found in SQLite row: " + std::string(name));
}

bool sqlite_row::is_null(size_t index) const {
    if (!m_holder || !m_holder->stmt) return true;
    return sqlite3_column_type(m_holder->stmt, static_cast<int>(index)) == SQLITE_NULL;
}

column_type sqlite_row::get_column_type(size_t index) const {
    if (!m_holder || !m_holder->stmt) return column_type::null_value;
    int type = sqlite3_column_type(m_holder->stmt, static_cast<int>(index));
    switch (type) {
        case SQLITE_INTEGER: return column_type::integer;
        case SQLITE_FLOAT:   return column_type::floating_point;
        case SQLITE_TEXT:    return column_type::text;
        case SQLITE_BLOB:    return column_type::blob;
        case SQLITE_NULL:
        default:             return column_type::null_value;
    }
}

int32_t sqlite_row::get_int32(size_t index) const {
    if (!m_holder || !m_holder->stmt) return 0;
    return sqlite3_column_int(m_holder->stmt, static_cast<int>(index));
}

int64_t sqlite_row::get_int64(size_t index) const {
    if (!m_holder || !m_holder->stmt) return 0;
    return sqlite3_column_int64(m_holder->stmt, static_cast<int>(index));
}

double sqlite_row::get_double(size_t index) const {
    if (!m_holder || !m_holder->stmt) return 0.0;
    return sqlite3_column_double(m_holder->stmt, static_cast<int>(index));
}

std::string sqlite_row::get_string(size_t index) const {
    if (!m_holder || !m_holder->stmt) return {};
    const auto *txt = reinterpret_cast<const char *>(sqlite3_column_text(m_holder->stmt, static_cast<int>(index)));
    return txt ? std::string(txt) : std::string();
}

std::span<const uint8_t> sqlite_row::get_blob(size_t index) const {
    if (!m_holder || !m_holder->stmt) return {};
    const void *b = sqlite3_column_blob(m_holder->stmt, static_cast<int>(index));
    int bytes = sqlite3_column_bytes(m_holder->stmt, static_cast<int>(index));
    if (!b || bytes <= 0) return {};
    return std::span<const uint8_t>(reinterpret_cast<const uint8_t *>(b), static_cast<size_t>(bytes));
}

std::chrono::system_clock::time_point sqlite_row::get_timestamp(size_t index) const {
    if (!m_holder || !m_holder->stmt) return {};
    int type = sqlite3_column_type(m_holder->stmt, static_cast<int>(index));
    if (type == SQLITE_INTEGER) {
        int64_t val = sqlite3_column_int64(m_holder->stmt, static_cast<int>(index));
        if (val > 100000000000LL) {
            return std::chrono::system_clock::time_point(std::chrono::milliseconds(val));
        }
        return std::chrono::system_clock::from_time_t(static_cast<std::time_t>(val));
    }

    std::string str = get_string(index);
    if (str.empty()) return {};

    std::tm tm_buf{};
    if (strptime(str.c_str(), "%Y-%m-%d %H:%M:%S", &tm_buf) != nullptr ||
        strptime(str.c_str(), "%Y-%m-%dT%H:%M:%SZ", &tm_buf) != nullptr) {
        return std::chrono::system_clock::from_time_t(timegm(&tm_buf));
    }
    return {};
}

// ==================== sqlite_result_set ====================

sqlite_result_set::sqlite_result_set(std::shared_ptr<sqlite_stmt_holder> holder)
    : m_holder(std::move(holder)), m_row(m_holder) {}

sqlite_result_set::~sqlite_result_set() {
    if (m_holder && m_holder->stmt) {
        sqlite3_reset(m_holder->stmt);
    }
}

bool sqlite_result_set::next() {
    if (!m_holder || !m_holder->stmt) return false;
    int rc = sqlite3_step(m_holder->stmt);
    if (rc == SQLITE_ROW) {
        return true;
    }
    if (rc == SQLITE_DONE) {
        return false;
    }
    std::string err = m_holder->db ? sqlite3_errmsg(m_holder->db) : "Unknown SQLite step error";
    throw db_exception("SQLite step error: " + err);
}

const row& sqlite_result_set::current_row() const {
    return m_row;
}

size_t sqlite_result_set::column_count() const noexcept {
    return m_row.column_count();
}

std::string_view sqlite_result_set::column_name(size_t index) const {
    return m_row.column_name(index);
}

size_t sqlite_result_set::column_index(std::string_view name) const {
    return m_row.column_index(name);
}

uint64_t sqlite_result_set::affected_rows() const noexcept {
    if (!m_holder || !m_holder->db) return 0;
    return static_cast<uint64_t>(sqlite3_changes(m_holder->db));
}

// ==================== sqlite_statement ====================

sqlite_statement::sqlite_statement(
    sqlite3 *db, std::string_view sql, std::weak_ptr<statement_cache> cache
) {
    if (!db) {
        throw db_exception("Cannot prepare statement: SQLite database connection is null");
    }

    std::string sql_str(sql);
    sqlite3_stmt *stmt = nullptr;
    if (auto c = cache.lock()) {
        stmt = c->checkout(sql_str);
    }

    if (!stmt) {
        unsigned int prep_flags = cache.expired() ? 0u : SQLITE_PREPARE_PERSISTENT;
        int rc = sqlite3_prepare_v3(db, sql_str.c_str(), -1, prep_flags, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            std::string err = sqlite3_errmsg(db);
            throw db_exception("Failed to prepare SQLite statement (" + sql_str + "): " + err);
        }
    }

    m_holder = std::make_shared<sqlite_stmt_holder>(db, stmt, std::move(sql_str), std::move(cache));
}

int sqlite_statement::get_param_index(std::string_view name) {
    if (!m_holder || !m_holder->stmt) {
        throw db_exception("Statement is not initialized");
    }
    std::string name_str(name);
    int idx = sqlite3_bind_parameter_index(m_holder->stmt, name_str.c_str());
    if (idx == 0 && !name_str.empty() && name_str[0] != ':' && name_str[0] != '@' && name_str[0] != '$') {
        std::string with_colon = ":" + name_str;
        idx = sqlite3_bind_parameter_index(m_holder->stmt, with_colon.c_str());
        if (idx == 0) {
            std::string with_at = "@" + name_str;
            idx = sqlite3_bind_parameter_index(m_holder->stmt, with_at.c_str());
        }
        if (idx == 0) {
            std::string with_dollar = "$" + name_str;
            idx = sqlite3_bind_parameter_index(m_holder->stmt, with_dollar.c_str());
        }
    }
    if (idx == 0) {
        throw db_exception("Named parameter not found in SQLite query: " + name_str);
    }
    return idx;
}

void sqlite_statement::bind(size_t index, std::nullptr_t) {
    if (!m_holder || !m_holder->stmt) throw db_exception("Statement not initialized");
    if (sqlite3_bind_null(m_holder->stmt, static_cast<int>(index)) != SQLITE_OK) {
        throw db_exception("Failed to bind null to column " + std::to_string(index));
    }
}

void sqlite_statement::bind(size_t index, int32_t val) {
    if (!m_holder || !m_holder->stmt) throw db_exception("Statement not initialized");
    if (sqlite3_bind_int(m_holder->stmt, static_cast<int>(index), val) != SQLITE_OK) {
        throw db_exception("Failed to bind int32 to column " + std::to_string(index));
    }
}

void sqlite_statement::bind(size_t index, int64_t val) {
    if (!m_holder || !m_holder->stmt) throw db_exception("Statement not initialized");
    if (sqlite3_bind_int64(m_holder->stmt, static_cast<int>(index), val) != SQLITE_OK) {
        throw db_exception("Failed to bind int64 to column " + std::to_string(index));
    }
}

void sqlite_statement::bind(size_t index, double val) {
    if (!m_holder || !m_holder->stmt) throw db_exception("Statement not initialized");
    if (sqlite3_bind_double(m_holder->stmt, static_cast<int>(index), val) != SQLITE_OK) {
        throw db_exception("Failed to bind double to column " + std::to_string(index));
    }
}

void sqlite_statement::bind(size_t index, std::string_view val) {
    if (!m_holder || !m_holder->stmt) throw db_exception("Statement not initialized");
    if (sqlite3_bind_text(m_holder->stmt, static_cast<int>(index), val.data(), static_cast<int>(val.size()), SQLITE_TRANSIENT) != SQLITE_OK) {
        throw db_exception("Failed to bind text to column " + std::to_string(index));
    }
}

void sqlite_statement::bind(size_t index, std::span<const uint8_t> val) {
    if (!m_holder || !m_holder->stmt) throw db_exception("Statement not initialized");
    if (sqlite3_bind_blob(m_holder->stmt, static_cast<int>(index), val.data(), static_cast<int>(val.size()), SQLITE_TRANSIENT) != SQLITE_OK) {
        throw db_exception("Failed to bind blob to column " + std::to_string(index));
    }
}

void sqlite_statement::bind(size_t index, std::chrono::system_clock::time_point val) {
    auto sec = std::chrono::duration_cast<std::chrono::seconds>(val.time_since_epoch()).count();
    bind(index, static_cast<int64_t>(sec));
}

void sqlite_statement::bind(std::string_view name, std::nullptr_t) {
    bind(static_cast<size_t>(get_param_index(name)), nullptr);
}

void sqlite_statement::bind(std::string_view name, int32_t val) {
    bind(static_cast<size_t>(get_param_index(name)), val);
}

void sqlite_statement::bind(std::string_view name, int64_t val) {
    bind(static_cast<size_t>(get_param_index(name)), val);
}

void sqlite_statement::bind(std::string_view name, double val) {
    bind(static_cast<size_t>(get_param_index(name)), val);
}

void sqlite_statement::bind(std::string_view name, std::string_view val) {
    bind(static_cast<size_t>(get_param_index(name)), val);
}

void sqlite_statement::bind(std::string_view name, std::span<const uint8_t> val) {
    bind(static_cast<size_t>(get_param_index(name)), val);
}

void sqlite_statement::bind(std::string_view name, std::chrono::system_clock::time_point val) {
    bind(static_cast<size_t>(get_param_index(name)), val);
}

void sqlite_statement::execute() {
    if (!m_holder || !m_holder->stmt) throw db_exception("Statement not initialized");
    step_done();
    reset();
}

std::unique_ptr<result_set> sqlite_statement::query() {
    if (!m_holder || !m_holder->stmt) throw db_exception("Statement not initialized");
    return std::make_unique<sqlite_result_set>(m_holder);
}

void sqlite_statement::reset() {
    if (m_holder && m_holder->stmt) {
        sqlite3_reset(m_holder->stmt);
        sqlite3_clear_bindings(m_holder->stmt);
    }
}

int sqlite_statement::step() {
    if (!m_holder || !m_holder->stmt) throw db_exception("Statement not initialized");
    return sqlite3_step(m_holder->stmt);
}

void sqlite_statement::step_done() {
    if (!m_holder || !m_holder->stmt) throw db_exception("Statement not initialized");
    int rc = sqlite3_step(m_holder->stmt);
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        std::string err = m_holder->db ? sqlite3_errmsg(m_holder->db) : "Unknown SQLite step error";
        throw db_exception("SQLite step error: " + err);
    }
}

// ==================== sqlite_transaction ====================

sqlite_transaction::sqlite_transaction(sqlite_database &db, bool immediate)
    : m_db(db.get()), m_active(true) {
    if (!m_db) throw db_exception("Cannot begin transaction on unopened database");
    char *err = nullptr;
    int rc = sqlite3_exec(m_db, immediate ? "BEGIN IMMEDIATE;" : "BEGIN;", nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = err ? err : "Failed to begin transaction";
        if (err) sqlite3_free(err);
        throw db_exception("Transaction begin failed: " + msg);
    }
}

sqlite_transaction::sqlite_transaction(sqlite3 *db_handle, bool immediate)
    : m_db(db_handle), m_active(true) {
    if (!m_db) throw db_exception("Cannot begin transaction on null database handle");
    char *err = nullptr;
    int rc = sqlite3_exec(m_db, immediate ? "BEGIN IMMEDIATE;" : "BEGIN;", nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = err ? err : "Failed to begin transaction";
        if (err) sqlite3_free(err);
        throw db_exception("Transaction begin failed: " + msg);
    }
}

sqlite_transaction::~sqlite_transaction() {
    if (m_active && m_db) {
        char *err = nullptr;
        sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, &err);
        if (err) sqlite3_free(err);
    }
}

sqlite_transaction::sqlite_transaction(sqlite_transaction &&other) noexcept
    : m_db(other.m_db), m_active(other.m_active) {
    other.m_db = nullptr;
    other.m_active = false;
}

sqlite_transaction &sqlite_transaction::operator=(sqlite_transaction &&other) noexcept {
    if (this != &other) {
        if (m_active && m_db) {
            char *err = nullptr;
            sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, &err);
            if (err) sqlite3_free(err);
        }
        m_db = other.m_db;
        m_active = other.m_active;
        other.m_db = nullptr;
        other.m_active = false;
    }
    return *this;
}

void sqlite_transaction::commit() {
    if (!m_active || !m_db) return;
    char *err = nullptr;
    int rc = sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = err ? err : "Failed to commit transaction";
        if (err) sqlite3_free(err);
        throw db_exception("Transaction commit failed: " + msg);
    }
    m_active = false;
}

void sqlite_transaction::rollback() {
    if (!m_active || !m_db) return;
    char *err = nullptr;
    int rc = sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, &err);
    if (err) sqlite3_free(err);
    m_active = false;
    if (rc != SQLITE_OK) {
        throw db_exception("Transaction rollback failed");
    }
}

bool sqlite_transaction::is_active() const noexcept {
    return m_active;
}

// ==================== sqlite_database ====================

sqlite_database::sqlite_database(const std::string &path, int flags, size_t stmt_cache_capacity)
    : m_stmt_cache_capacity(stmt_cache_capacity) {
    open(path, flags);
}

sqlite_database::~sqlite_database() {
    close();
}

sqlite_database::sqlite_database(sqlite_database &&other) noexcept
    : m_db(other.m_db),
      m_path(std::move(other.m_path)),
      m_flags(other.m_flags),
      m_stmt_cache_capacity(other.m_stmt_cache_capacity),
      m_cache(std::move(other.m_cache)) {
    other.m_db = nullptr;
}

sqlite_database &sqlite_database::operator=(sqlite_database &&other) noexcept {
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

void sqlite_database::connect(const std::string &conn_str) {
    open(conn_str, m_flags);
}

void sqlite_database::disconnect() {
    close();
}

bool sqlite_database::is_connected() const noexcept {
    return m_db != nullptr;
}

bool sqlite_database::ping() noexcept {
    if (!m_db) return false;
    try {
        auto stmt = prepare("SELECT 1;");
        auto res = stmt->query();
        return res && res->next() && res->current_row().get_int32(0) == 1;
    } catch (...) {
        return false;
    }
}

void sqlite_database::execute(std::string_view sql) {
    if (!m_db) throw db_exception("Database connection is closed");
    std::string sql_str(sql);
    char *err_msg = nullptr;
    int rc = sqlite3_exec(m_db, sql_str.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::string err = err_msg ? err_msg : "Unknown SQLite exec error";
        if (err_msg) sqlite3_free(err_msg);
        throw db_exception("SQLite execute failed (" + sql_str + "): " + err);
    }
}

std::unique_ptr<statement> sqlite_database::prepare(std::string_view sql) {
    if (!m_db) throw db_exception("Database connection is closed");
    return std::make_unique<sqlite_statement>(m_db, sql, m_cache);
}

std::unique_ptr<transaction> sqlite_database::begin_transaction() {
    return begin_transaction(true);
}

std::unique_ptr<transaction> sqlite_database::begin_transaction(bool immediate) {
    return std::make_unique<sqlite_transaction>(*this, immediate);
}

void sqlite_database::open(const std::string &path, int flags) {
    close();
    m_path = path;
    m_flags = flags;
    int rc = sqlite3_open_v2(path.c_str(), &m_db, flags, nullptr);
    if (rc != SQLITE_OK) {
        std::string err = m_db ? sqlite3_errmsg(m_db) : "Failed to open SQLite database";
        close();
        throw db_exception("Can't open database '" + path + "': " + err);
    }

    sqlite3_busy_timeout(m_db, 5000);
    execute("PRAGMA foreign_keys = ON;");
    execute("PRAGMA journal_mode = WAL;");
    execute("PRAGMA synchronous = NORMAL;");
    execute("PRAGMA temp_store = MEMORY;");
    execute("PRAGMA mmap_size = 268435456;");
    execute("PRAGMA cache_size = -16000;");

    m_cache = std::make_shared<statement_cache>(m_stmt_cache_capacity);
}

void sqlite_database::close() noexcept {
    if (m_cache) {
        m_cache->close();
        m_cache.reset();
    }
    if (m_db) {
        sqlite3_close_v2(m_db);
        m_db = nullptr;
    }
}

void sqlite_database::reset_for_request() {
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

int64_t sqlite_database::last_insert_rowid() const noexcept {
    return m_db ? sqlite3_last_insert_rowid(m_db) : 0;
}

int sqlite_database::changes() const noexcept {
    return m_db ? sqlite3_changes(m_db) : 0;
}

const char *sqlite_database::errmsg() const noexcept {
    return m_db ? sqlite3_errmsg(m_db) : "Database not open";
}

size_t sqlite_database::cached_statement_count() const noexcept {
    return m_cache ? m_cache->size() : 0;
}

uint64_t sqlite_database::cache_hits() const noexcept {
    return m_cache ? m_cache->hits() : 0;
}

uint64_t sqlite_database::cache_misses() const noexcept {
    return m_cache ? m_cache->misses() : 0;
}

void sqlite_database::clear_statement_cache() noexcept {
    if (m_cache) {
        m_cache->clear();
    }
}

} // namespace backend::db
