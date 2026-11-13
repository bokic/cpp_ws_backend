#include "sqlite_driver.h"
#include <dlfcn.h>
#include <ctime>
#include <cstring>
#include <algorithm>

namespace backend::db {

// ==================== Dynamic SQLite3 Loader ====================

#ifndef SQLITE_OK
#define SQLITE_OK           0
#endif
#ifndef SQLITE_ROW
#define SQLITE_ROW          100
#endif
#ifndef SQLITE_DONE
#define SQLITE_DONE         101
#endif
#ifndef SQLITE_INTEGER
#define SQLITE_INTEGER      1
#endif
#ifndef SQLITE_FLOAT
#define SQLITE_FLOAT        2
#endif
#ifndef SQLITE_TEXT
#define SQLITE_TEXT         3
#endif
#ifndef SQLITE_BLOB
#define SQLITE_BLOB         4
#endif
#ifndef SQLITE_NULL
#define SQLITE_NULL         5
#endif
#ifndef SQLITE_PREPARE_PERSISTENT
#define SQLITE_PREPARE_PERSISTENT 0x01
#endif
#ifndef SQLITE_TRANSIENT
#define SQLITE_TRANSIENT    ((void(*)(void*))-1)
#endif

struct sqlite_api {
    void *handle = nullptr;

    typedef int (*sqlite3_open_v2_t)(const char *filename, sqlite3 **ppDb, int flags, const char *zVfs);
    typedef int (*sqlite3_close_v2_t)(sqlite3 *db);
    typedef int (*sqlite3_busy_timeout_t)(sqlite3 *db, int ms);
    typedef int (*sqlite3_exec_t)(sqlite3 *db, const char *sql, int (*callback)(void*,int,char**,char**), void *arg, char **errmsg);
    typedef void (*sqlite3_free_t)(void *ptr);
    typedef const char *(*sqlite3_errmsg_t)(sqlite3 *db);
    typedef int (*sqlite3_changes_t)(sqlite3 *db);
    typedef int64_t (*sqlite3_last_insert_rowid_t)(sqlite3 *db);
    typedef int (*sqlite3_get_autocommit_t)(sqlite3 *db);
    typedef sqlite3_stmt *(*sqlite3_next_stmt_t)(sqlite3 *pDb, sqlite3_stmt *pStmt);
    typedef int (*sqlite3_prepare_v3_t)(sqlite3 *db, const char *zSql, int nByte, unsigned int prepFlags, sqlite3_stmt **ppStmt, const char **pzTail);
    typedef int (*sqlite3_prepare_v2_t)(sqlite3 *db, const char *zSql, int nByte, sqlite3_stmt **ppStmt, const char **pzTail);
    typedef int (*sqlite3_finalize_t)(sqlite3_stmt *pStmt);
    typedef int (*sqlite3_reset_t)(sqlite3_stmt *pStmt);
    typedef int (*sqlite3_clear_bindings_t)(sqlite3_stmt *pStmt);
    typedef int (*sqlite3_step_t)(sqlite3_stmt *pStmt);
    typedef int (*sqlite3_column_count_t)(sqlite3_stmt *pStmt);
    typedef const char *(*sqlite3_column_name_t)(sqlite3_stmt *pStmt, int N);
    typedef int (*sqlite3_column_type_t)(sqlite3_stmt *pStmt, int iCol);
    typedef int (*sqlite3_column_int_t)(sqlite3_stmt *pStmt, int iCol);
    typedef int64_t (*sqlite3_column_int64_t)(sqlite3_stmt *pStmt, int iCol);
    typedef double (*sqlite3_column_double_t)(sqlite3_stmt *pStmt, int iCol);
    typedef const unsigned char *(*sqlite3_column_text_t)(sqlite3_stmt *pStmt, int iCol);
    typedef const void *(*sqlite3_column_blob_t)(sqlite3_stmt *pStmt, int iCol);
    typedef int (*sqlite3_column_bytes_t)(sqlite3_stmt *pStmt, int iCol);
    typedef int (*sqlite3_bind_null_t)(sqlite3_stmt *pStmt, int idx);
    typedef int (*sqlite3_bind_int_t)(sqlite3_stmt *pStmt, int idx, int val);
    typedef int (*sqlite3_bind_int64_t)(sqlite3_stmt *pStmt, int idx, int64_t val);
    typedef int (*sqlite3_bind_double_t)(sqlite3_stmt *pStmt, int idx, double val);
    typedef int (*sqlite3_bind_text_t)(sqlite3_stmt *pStmt, int idx, const char *val, int n, void (*del)(void*));
    typedef int (*sqlite3_bind_blob_t)(sqlite3_stmt *pStmt, int idx, const void *val, int n, void (*del)(void*));
    typedef int (*sqlite3_bind_parameter_index_t)(sqlite3_stmt *pStmt, const char *zName);
    typedef int (*sqlite3_bind_parameter_count_t)(sqlite3_stmt *pStmt);

    sqlite3_open_v2_t sqlite3_open_v2 = nullptr;
    sqlite3_close_v2_t sqlite3_close_v2 = nullptr;
    sqlite3_busy_timeout_t sqlite3_busy_timeout = nullptr;
    sqlite3_exec_t sqlite3_exec = nullptr;
    sqlite3_free_t sqlite3_free = nullptr;
    sqlite3_errmsg_t sqlite3_errmsg = nullptr;
    sqlite3_changes_t sqlite3_changes = nullptr;
    sqlite3_last_insert_rowid_t sqlite3_last_insert_rowid = nullptr;
    sqlite3_get_autocommit_t sqlite3_get_autocommit = nullptr;
    sqlite3_next_stmt_t sqlite3_next_stmt = nullptr;
    sqlite3_prepare_v3_t sqlite3_prepare_v3 = nullptr;
    sqlite3_prepare_v2_t sqlite3_prepare_v2 = nullptr;
    sqlite3_finalize_t sqlite3_finalize = nullptr;
    sqlite3_reset_t sqlite3_reset = nullptr;
    sqlite3_clear_bindings_t sqlite3_clear_bindings = nullptr;
    sqlite3_step_t sqlite3_step = nullptr;
    sqlite3_column_count_t sqlite3_column_count = nullptr;
    sqlite3_column_name_t sqlite3_column_name = nullptr;
    sqlite3_column_type_t sqlite3_column_type = nullptr;
    sqlite3_column_int_t sqlite3_column_int = nullptr;
    sqlite3_column_int64_t sqlite3_column_int64 = nullptr;
    sqlite3_column_double_t sqlite3_column_double = nullptr;
    sqlite3_column_text_t sqlite3_column_text = nullptr;
    sqlite3_column_blob_t sqlite3_column_blob = nullptr;
    sqlite3_column_bytes_t sqlite3_column_bytes = nullptr;
    sqlite3_bind_null_t sqlite3_bind_null = nullptr;
    sqlite3_bind_int_t sqlite3_bind_int = nullptr;
    sqlite3_bind_int64_t sqlite3_bind_int64 = nullptr;
    sqlite3_bind_double_t sqlite3_bind_double = nullptr;
    sqlite3_bind_text_t sqlite3_bind_text = nullptr;
    sqlite3_bind_blob_t sqlite3_bind_blob = nullptr;
    sqlite3_bind_parameter_index_t sqlite3_bind_parameter_index = nullptr;
    sqlite3_bind_parameter_count_t sqlite3_bind_parameter_count = nullptr;

    static sqlite_api &instance() {
        static sqlite_api api;
        return api;
    }

    bool load() {
        if (handle) return true;
        const char *libs[] = {
            "libsqlite3.so.0",
            "libsqlite3.so"
        };
        for (const char *lib : libs) {
            handle = dlopen(lib, RTLD_LAZY | RTLD_LOCAL);
            if (handle) break;
        }
        if (!handle) return false;

        #define LOAD_SYM(name) name = reinterpret_cast<name##_t>(dlsym(handle, #name)); if (!name) return false;
        LOAD_SYM(sqlite3_open_v2);
        LOAD_SYM(sqlite3_close_v2);
        LOAD_SYM(sqlite3_busy_timeout);
        LOAD_SYM(sqlite3_exec);
        LOAD_SYM(sqlite3_free);
        LOAD_SYM(sqlite3_errmsg);
        LOAD_SYM(sqlite3_changes);
        LOAD_SYM(sqlite3_last_insert_rowid);
        LOAD_SYM(sqlite3_get_autocommit);
        LOAD_SYM(sqlite3_next_stmt);
        LOAD_SYM(sqlite3_prepare_v2);
        sqlite3_prepare_v3 = reinterpret_cast<sqlite3_prepare_v3_t>(dlsym(handle, "sqlite3_prepare_v3"));
        LOAD_SYM(sqlite3_finalize);
        LOAD_SYM(sqlite3_reset);
        LOAD_SYM(sqlite3_clear_bindings);
        LOAD_SYM(sqlite3_step);
        LOAD_SYM(sqlite3_column_count);
        LOAD_SYM(sqlite3_column_name);
        LOAD_SYM(sqlite3_column_type);
        LOAD_SYM(sqlite3_column_int);
        LOAD_SYM(sqlite3_column_int64);
        LOAD_SYM(sqlite3_column_double);
        LOAD_SYM(sqlite3_column_text);
        LOAD_SYM(sqlite3_column_blob);
        LOAD_SYM(sqlite3_column_bytes);
        LOAD_SYM(sqlite3_bind_null);
        LOAD_SYM(sqlite3_bind_int);
        LOAD_SYM(sqlite3_bind_int64);
        LOAD_SYM(sqlite3_bind_double);
        LOAD_SYM(sqlite3_bind_text);
        LOAD_SYM(sqlite3_bind_blob);
        LOAD_SYM(sqlite3_bind_parameter_index);
        LOAD_SYM(sqlite3_bind_parameter_count);
        #undef LOAD_SYM

        return true;
    }

    void ensure_loaded() {
        if (!load()) {
            const char *err = dlerror();
            throw db_exception("Failed to dynamically load SQLite3 library: " +
                               std::string(err ? err : "library not found"));
        }
    }
};

// Internal function call wrappers for loaded symbols
#define sqlite3_open_v2 sqlite_api::instance().sqlite3_open_v2
#define sqlite3_close_v2 sqlite_api::instance().sqlite3_close_v2
#define sqlite3_busy_timeout sqlite_api::instance().sqlite3_busy_timeout
#define sqlite3_exec sqlite_api::instance().sqlite3_exec
#define sqlite3_free sqlite_api::instance().sqlite3_free
#define sqlite3_errmsg sqlite_api::instance().sqlite3_errmsg
#define sqlite3_changes sqlite_api::instance().sqlite3_changes
#define sqlite3_last_insert_rowid sqlite_api::instance().sqlite3_last_insert_rowid
#define sqlite3_get_autocommit sqlite_api::instance().sqlite3_get_autocommit
#define sqlite3_next_stmt sqlite_api::instance().sqlite3_next_stmt
#define sqlite3_finalize sqlite_api::instance().sqlite3_finalize
#define sqlite3_reset sqlite_api::instance().sqlite3_reset
#define sqlite3_clear_bindings sqlite_api::instance().sqlite3_clear_bindings
#define sqlite3_step sqlite_api::instance().sqlite3_step
#define sqlite3_column_count sqlite_api::instance().sqlite3_column_count
#define sqlite3_column_name sqlite_api::instance().sqlite3_column_name
#define sqlite3_column_type sqlite_api::instance().sqlite3_column_type
#define sqlite3_column_int sqlite_api::instance().sqlite3_column_int
#define sqlite3_column_int64 sqlite_api::instance().sqlite3_column_int64
#define sqlite3_column_double sqlite_api::instance().sqlite3_column_double
#define sqlite3_column_text sqlite_api::instance().sqlite3_column_text
#define sqlite3_column_blob sqlite_api::instance().sqlite3_column_blob
#define sqlite3_column_bytes sqlite_api::instance().sqlite3_column_bytes
#define sqlite3_bind_null sqlite_api::instance().sqlite3_bind_null
#define sqlite3_bind_int sqlite_api::instance().sqlite3_bind_int
#define sqlite3_bind_int64 sqlite_api::instance().sqlite3_bind_int64
#define sqlite3_bind_double sqlite_api::instance().sqlite3_bind_double
#define sqlite3_bind_text sqlite_api::instance().sqlite3_bind_text
#define sqlite3_bind_blob sqlite_api::instance().sqlite3_bind_blob
#define sqlite3_bind_parameter_index sqlite_api::instance().sqlite3_bind_parameter_index
#define sqlite3_bind_parameter_count sqlite_api::instance().sqlite3_bind_parameter_count

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

    if (sqlite_api::instance().sqlite3_reset) {
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
    }
    m_hits.fetch_add(1, std::memory_order_relaxed);
    return stmt;
}

void statement_cache::checkin(const std::string &sql, sqlite3_stmt *stmt) noexcept {
    if (!stmt) return;

    if (sqlite_api::instance().sqlite3_reset) {
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_closed || m_max_capacity == 0 || sql.empty()) {
        if (sqlite_api::instance().sqlite3_finalize) {
            sqlite3_finalize(stmt);
        }
        return;
    }

    auto existing = m_map.find(sql);
    if (existing != m_map.end()) {
        if (sqlite_api::instance().sqlite3_finalize) {
            sqlite3_finalize(stmt);
        }
        m_lru.splice(m_lru.begin(), m_lru, existing->second);
        return;
    }

    if (m_map.size() >= m_max_capacity && !m_lru.empty()) {
        auto &oldest = m_lru.back();
        if (sqlite_api::instance().sqlite3_finalize) {
            sqlite3_finalize(oldest.second);
        }
        m_map.erase(oldest.first);
        m_lru.pop_back();
    }

    m_lru.emplace_front(sql, stmt);
    m_map.emplace(m_lru.front().first, m_lru.begin());
}

void statement_cache::clear() noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto &entry : m_lru) {
        if (entry.second && sqlite_api::instance().sqlite3_finalize) {
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
        if (entry.second && sqlite_api::instance().sqlite3_finalize) {
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
        } else if (sqlite_api::instance().sqlite3_finalize) {
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
    if (m_holder && m_holder->stmt && sqlite_api::instance().sqlite3_reset) {
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

    auto &api = sqlite_api::instance();
    api.ensure_loaded();

    std::string sql_str(sql);
    sqlite3_stmt *stmt = nullptr;
    if (auto c = cache.lock()) {
        stmt = c->checkout(sql_str);
    }

    if (!stmt) {
        unsigned int prep_flags = cache.expired() ? 0u : SQLITE_PREPARE_PERSISTENT;
        int rc = SQLITE_OK;
        if (prep_flags != 0 && api.sqlite3_prepare_v3) {
            rc = api.sqlite3_prepare_v3(db, sql_str.c_str(), -1, prep_flags, &stmt, nullptr);
        } else {
            rc = api.sqlite3_prepare_v2(db, sql_str.c_str(), -1, &stmt, nullptr);
        }
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
    if (m_holder && m_holder->stmt && sqlite_api::instance().sqlite3_reset) {
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

void sqlite_statement::bind_optional_int(int idx, int val) {
    if (val <= 0) {
        bind_null(idx);
    } else {
        bind_int(idx, val);
    }
}

void sqlite_statement::clear_and_reset() {
    reset();
}

int sqlite_statement::column_count() const noexcept {
    if (!m_holder || !m_holder->stmt) return 0;
    return sqlite3_column_count(m_holder->stmt);
}

const char *sqlite_statement::column_name(int col) const noexcept {
    if (!m_holder || !m_holder->stmt) return nullptr;
    return sqlite3_column_name(m_holder->stmt, col);
}

bool sqlite_statement::column_is_null(int col) const noexcept {
    if (!m_holder || !m_holder->stmt) return true;
    return sqlite3_column_type(m_holder->stmt, col) == SQLITE_NULL;
}

int sqlite_statement::column_type(int col) const noexcept {
    if (!m_holder || !m_holder->stmt) return SQLITE_NULL;
    return sqlite3_column_type(m_holder->stmt, col);
}

std::string sqlite_statement::column_str(int col) const {
    if (!m_holder || !m_holder->stmt) return {};
    const auto *txt = reinterpret_cast<const char *>(sqlite3_column_text(m_holder->stmt, col));
    return txt ? std::string(txt) : std::string();
}

const char *sqlite_statement::column_text(int col) const noexcept {
    if (!m_holder || !m_holder->stmt) return nullptr;
    return reinterpret_cast<const char *>(sqlite3_column_text(m_holder->stmt, col));
}

int sqlite_statement::column_int(int col) const noexcept {
    if (!m_holder || !m_holder->stmt) return 0;
    return sqlite3_column_int(m_holder->stmt, col);
}

int64_t sqlite_statement::column_int64(int col) const noexcept {
    if (!m_holder || !m_holder->stmt) return 0;
    return sqlite3_column_int64(m_holder->stmt, col);
}

double sqlite_statement::column_double(int col) const noexcept {
    if (!m_holder || !m_holder->stmt) return 0.0;
    return sqlite3_column_double(m_holder->stmt, col);
}

// ==================== sqlite_transaction ====================

sqlite_transaction::sqlite_transaction(sqlite_database &db, bool immediate)
    : m_db(db.get()), m_active(true) {
    if (!m_db) throw db_exception("Cannot begin transaction on unopened database");
    sqlite_api::instance().ensure_loaded();

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
    sqlite_api::instance().ensure_loaded();

    char *err = nullptr;
    int rc = sqlite3_exec(m_db, immediate ? "BEGIN IMMEDIATE;" : "BEGIN;", nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = err ? err : "Failed to begin transaction";
        if (err) sqlite3_free(err);
        throw db_exception("Transaction begin failed: " + msg);
    }
}

sqlite_transaction::~sqlite_transaction() {
    if (m_active && m_db && sqlite_api::instance().sqlite3_exec) {
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
        if (m_active && m_db && sqlite_api::instance().sqlite3_exec) {
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

sqlite_statement sqlite_database::prepare_stmt(std::string_view sql) const {
    if (!m_db) throw db_exception("Database connection is closed");
    return sqlite_statement(m_db, sql, m_cache);
}

std::unique_ptr<transaction> sqlite_database::begin_transaction() {
    return begin_transaction(true);
}

std::unique_ptr<transaction> sqlite_database::begin_transaction(bool immediate) {
    return std::make_unique<sqlite_transaction>(*this, immediate);
}

void sqlite_database::open(const std::string &path, int flags) {
    sqlite_api::instance().ensure_loaded();

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
        if (sqlite_api::instance().sqlite3_close_v2) {
            sqlite3_close_v2(m_db);
        }
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
    auto &api = sqlite_api::instance();
    if (api.sqlite3_next_stmt && api.sqlite3_reset && api.sqlite3_clear_bindings) {
        sqlite3_stmt *stmt = nullptr;
        while ((stmt = api.sqlite3_next_stmt(m_db, stmt)) != nullptr) {
            api.sqlite3_reset(stmt);
            api.sqlite3_clear_bindings(stmt);
        }
    }
    if (api.sqlite3_get_autocommit && api.sqlite3_get_autocommit(m_db) == 0) {
        char *err_msg = nullptr;
        if (api.sqlite3_exec) {
            api.sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, &err_msg);
        }
        if (err_msg && api.sqlite3_free) api.sqlite3_free(err_msg);
    }
}

int64_t sqlite_database::last_insert_rowid() const noexcept {
    return (m_db && sqlite_api::instance().sqlite3_last_insert_rowid) ? sqlite3_last_insert_rowid(m_db) : 0;
}

int sqlite_database::changes() const noexcept {
    return (m_db && sqlite_api::instance().sqlite3_changes) ? sqlite3_changes(m_db) : 0;
}

const char *sqlite_database::errmsg() const noexcept {
    return (m_db && sqlite_api::instance().sqlite3_errmsg) ? sqlite3_errmsg(m_db) : "Database not open";
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
