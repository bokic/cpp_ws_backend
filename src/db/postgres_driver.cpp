#include "postgres_driver.h"
#include <dlfcn.h>
#include <charconv>
#include <ctime>
#include <cstring>
#include <algorithm>

namespace backend::db {

// ==================== Dynamic libpq Loader ====================

struct pq_api {
    void *handle = nullptr;

    typedef void *(*PQconnectdb_t)(const char *conninfo);
    typedef void (*PQfinish_t)(void *conn);
    typedef int (*PQstatus_t)(const void *conn);
    typedef char *(*PQerrorMessage_t)(const void *conn);
    typedef void *(*PQexec_t)(void *conn, const char *query);
    typedef void *(*PQexecParams_t)(void *conn, const char *command, int nParams,
                                    const unsigned int *paramTypes,
                                    const char *const *paramValues,
                                    const int *paramLengths,
                                    const int *paramFormats,
                                    int resultFormat);
    typedef int (*PQresultStatus_t)(const void *res);
    typedef char *(*PQresultErrorMessage_t)(const void *res);
    typedef void (*PQclear_t)(void *res);
    typedef int (*PQntuples_t)(const void *res);
    typedef int (*PQnfields_t)(const void *res);
    typedef char *(*PQfname_t)(const void *res, int field_num);
    typedef int (*PQfnumber_t)(const void *res, const char *field_name);
    typedef unsigned int (*PQftype_t)(const void *res, int field_num);
    typedef int (*PQgetisnull_t)(const void *res, int tup_num, int field_num);
    typedef char *(*PQgetvalue_t)(const void *res, int tup_num, int field_num);
    typedef int (*PQgetlength_t)(const void *res, int tup_num, int field_num);
    typedef char *(*PQcmdTuples_t)(void *res);

    PQconnectdb_t PQconnectdb = nullptr;
    PQfinish_t PQfinish = nullptr;
    PQstatus_t PQstatus = nullptr;
    PQerrorMessage_t PQerrorMessage = nullptr;
    PQexec_t PQexec = nullptr;
    PQexecParams_t PQexecParams = nullptr;
    PQresultStatus_t PQresultStatus = nullptr;
    PQresultErrorMessage_t PQresultErrorMessage = nullptr;
    PQclear_t PQclear = nullptr;
    PQntuples_t PQntuples = nullptr;
    PQnfields_t PQnfields = nullptr;
    PQfname_t PQfname = nullptr;
    PQfnumber_t PQfnumber = nullptr;
    PQftype_t PQftype = nullptr;
    PQgetisnull_t PQgetisnull = nullptr;
    PQgetvalue_t PQgetvalue = nullptr;
    PQgetlength_t PQgetlength = nullptr;
    PQcmdTuples_t PQcmdTuples = nullptr;

    static pq_api &instance() {
        static pq_api api;
        return api;
    }

    bool load() {
        if (handle) return true;
        const char *libs[] = {"libpq.so.5", "libpq.so"};
        for (const char *lib : libs) {
            handle = dlopen(lib, RTLD_LAZY | RTLD_LOCAL);
            if (handle) break;
        }
        if (!handle) return false;

        #define LOAD_SYM(name) name = reinterpret_cast<name##_t>(dlsym(handle, #name)); if (!name) return false;
        LOAD_SYM(PQconnectdb);
        LOAD_SYM(PQfinish);
        LOAD_SYM(PQstatus);
        LOAD_SYM(PQerrorMessage);
        LOAD_SYM(PQexec);
        LOAD_SYM(PQexecParams);
        LOAD_SYM(PQresultStatus);
        LOAD_SYM(PQresultErrorMessage);
        LOAD_SYM(PQclear);
        LOAD_SYM(PQntuples);
        LOAD_SYM(PQnfields);
        LOAD_SYM(PQfname);
        LOAD_SYM(PQfnumber);
        LOAD_SYM(PQftype);
        LOAD_SYM(PQgetisnull);
        LOAD_SYM(PQgetvalue);
        LOAD_SYM(PQgetlength);
        LOAD_SYM(PQcmdTuples);
        #undef LOAD_SYM

        return true;
    }

    void ensure_loaded() {
        if (!load()) {
            const char *err = dlerror();
            throw db_exception("Failed to dynamically load PostgreSQL libpq: " + std::string(err ? err : "library not found"));
        }
    }
};

// libpq status constants
static constexpr int PG_CONNECTION_OK = 0;
static constexpr int PG_COMMAND_OK = 1;
static constexpr int PG_TUPLES_OK = 2;

// Standard PostgreSQL type OIDs
static constexpr unsigned int PG_OID_BOOL = 16;
static constexpr unsigned int PG_OID_INT8 = 20;
static constexpr unsigned int PG_OID_INT2 = 21;
static constexpr unsigned int PG_OID_INT4 = 23;
static constexpr unsigned int PG_OID_FLOAT4 = 700;
static constexpr unsigned int PG_OID_FLOAT8 = 701;
static constexpr unsigned int PG_OID_NUMERIC = 1700;
static constexpr unsigned int PG_OID_BYTEA = 17;
static constexpr unsigned int PG_OID_TIMESTAMP = 1114;
static constexpr unsigned int PG_OID_TIMESTAMPTZ = 1184;

struct pg_conn_holder {
    void *conn = nullptr;

    ~pg_conn_holder() {
        if (conn) {
            pq_api::instance().PQfinish(conn);
            conn = nullptr;
        }
    }
};

struct pg_result_holder {
    void *res = nullptr;

    ~pg_result_holder() {
        if (res) {
            pq_api::instance().PQclear(res);
            res = nullptr;
        }
    }
};

// ==================== postgres_row ====================

postgres_row::postgres_row(std::shared_ptr<pg_result_holder> holder, int row_idx)
    : m_holder(std::move(holder)), m_row_idx(row_idx) {}

void postgres_row::ensure_column_names() const {
    if (!m_cached_names.empty() || !m_holder || !m_holder->res) return;
    auto &api = pq_api::instance();
    int count = api.PQnfields(m_holder->res);
    m_cached_names.reserve(count);
    for (int i = 0; i < count; ++i) {
        const char *name = api.PQfname(m_holder->res, i);
        m_cached_names.emplace_back(name ? name : "");
    }
}

size_t postgres_row::column_count() const noexcept {
    if (!m_holder || !m_holder->res) return 0;
    return static_cast<size_t>(pq_api::instance().PQnfields(m_holder->res));
}

std::string_view postgres_row::column_name(size_t index) const {
    ensure_column_names();
    if (index < m_cached_names.size()) {
        return m_cached_names[index];
    }
    return {};
}

size_t postgres_row::column_index(std::string_view name) const {
    ensure_column_names();
    for (size_t i = 0; i < m_cached_names.size(); ++i) {
        if (m_cached_names[i] == name) {
            return i;
        }
    }
    throw db_exception("Column not found in PostgreSQL row: " + std::string(name));
}

bool postgres_row::is_null(size_t index) const {
    if (!m_holder || !m_holder->res || m_row_idx < 0) return true;
    return pq_api::instance().PQgetisnull(m_holder->res, m_row_idx, static_cast<int>(index)) != 0;
}

column_type postgres_row::get_column_type(size_t index) const {
    if (is_null(index)) return column_type::null_value;
    unsigned int oid = pq_api::instance().PQftype(m_holder->res, static_cast<int>(index));
    switch (oid) {
        case PG_OID_BOOL:
        case PG_OID_INT2:
        case PG_OID_INT4:
        case PG_OID_INT8:
            return column_type::integer;
        case PG_OID_FLOAT4:
        case PG_OID_FLOAT8:
        case PG_OID_NUMERIC:
            return column_type::floating_point;
        case PG_OID_BYTEA:
            return column_type::blob;
        case PG_OID_TIMESTAMP:
        case PG_OID_TIMESTAMPTZ:
            return column_type::timestamp;
        default:
            return column_type::text;
    }
}

int32_t postgres_row::get_int32(size_t index) const {
    return static_cast<int32_t>(get_int64(index));
}

int64_t postgres_row::get_int64(size_t index) const {
    std::string s = get_string(index);
    if (s.empty()) return 0;
    if (s == "t" || s == "true") return 1;
    if (s == "f" || s == "false") return 0;
    try {
        return std::stoll(s);
    } catch (...) {
        return 0;
    }
}

double postgres_row::get_double(size_t index) const {
    std::string s = get_string(index);
    if (s.empty()) return 0.0;
    try {
        return std::stod(s);
    } catch (...) {
        return 0.0;
    }
}

std::string postgres_row::get_string(size_t index) const {
    if (is_null(index)) return {};
    auto &api = pq_api::instance();
    const char *val = api.PQgetvalue(m_holder->res, m_row_idx, static_cast<int>(index));
    int len = api.PQgetlength(m_holder->res, m_row_idx, static_cast<int>(index));
    return (val && len > 0) ? std::string(val, static_cast<size_t>(len)) : std::string();
}

std::span<const uint8_t> postgres_row::get_blob(size_t index) const {
    if (is_null(index)) return {};
    auto &api = pq_api::instance();
    const char *val = api.PQgetvalue(m_holder->res, m_row_idx, static_cast<int>(index));
    int len = api.PQgetlength(m_holder->res, m_row_idx, static_cast<int>(index));
    if (!val || len <= 0) return {};
    return std::span<const uint8_t>(reinterpret_cast<const uint8_t *>(val), static_cast<size_t>(len));
}

std::chrono::system_clock::time_point postgres_row::get_timestamp(size_t index) const {
    std::string s = get_string(index);
    if (s.empty()) return {};

    std::tm tm_buf{};
    if (strptime(s.c_str(), "%Y-%m-%d %H:%M:%S", &tm_buf) != nullptr ||
        strptime(s.c_str(), "%Y-%m-%dT%H:%M:%SZ", &tm_buf) != nullptr ||
        strptime(s.c_str(), "%Y-%m-%d", &tm_buf) != nullptr) {
        return std::chrono::system_clock::from_time_t(timegm(&tm_buf));
    }
    return {};
}

// ==================== postgres_result_set ====================

postgres_result_set::postgres_result_set(std::shared_ptr<pg_result_holder> holder)
    : m_holder(std::move(holder)), m_row(m_holder) {
    if (m_holder && m_holder->res) {
        m_total_tuples = pq_api::instance().PQntuples(m_holder->res);
    }
}

bool postgres_result_set::next() {
    if (m_current_tuple + 1 < m_total_tuples) {
        ++m_current_tuple;
        m_row.set_row_index(m_current_tuple);
        return true;
    }
    return false;
}

const row& postgres_result_set::current_row() const {
    return m_row;
}

size_t postgres_result_set::column_count() const noexcept {
    return m_row.column_count();
}

std::string_view postgres_result_set::column_name(size_t index) const {
    return m_row.column_name(index);
}

size_t postgres_result_set::column_index(std::string_view name) const {
    return m_row.column_index(name);
}

uint64_t postgres_result_set::affected_rows() const noexcept {
    if (!m_holder || !m_holder->res) return 0;
    const char *cmd_tuples = pq_api::instance().PQcmdTuples(m_holder->res);
    if (cmd_tuples && cmd_tuples[0] != '\0') {
        try {
            return std::stoull(cmd_tuples);
        } catch (...) {
        }
    }
    return 0;
}

// ==================== postgres_statement ====================

postgres_statement::postgres_statement(std::shared_ptr<pg_conn_holder> conn, std::string_view sql)
    : m_conn(std::move(conn)) {
    // Parse SQL: translate '?' and ':name' to '$1, $2, ...'
    int param_num = 1;
    size_t i = 0;
    while (i < sql.size()) {
        char c = sql[i];
        if (c == '?') {
            m_translated_sql += "$" + std::to_string(param_num++);
            m_params.push_back({true, ""});
            ++i;
        } else if (c == ':' && (i == 0 || (!std::isalnum(static_cast<unsigned char>(sql[i - 1])) && sql[i - 1] != ':'))) {
            size_t start = ++i;
            while (i < sql.size() && (std::isalnum(static_cast<unsigned char>(sql[i])) || sql[i] == '_')) {
                ++i;
            }
            std::string name(sql.substr(start, i - start));
            m_translated_sql += "$" + std::to_string(param_num++);
            m_param_names.push_back(name);
            m_params.push_back({true, ""});
        } else {
            m_translated_sql.push_back(c);
            ++i;
        }
    }
}

int postgres_statement::get_param_index(std::string_view name) {
    if (!name.empty() && name[0] == ':') {
        name.remove_prefix(1);
    }
    for (size_t i = 0; i < m_param_names.size(); ++i) {
        if (m_param_names[i] == name) {
            return static_cast<int>(i + 1);
        }
    }
    throw db_exception("Named parameter not found in PostgreSQL statement: " + std::string(name));
}

void postgres_statement::bind(size_t index, std::nullptr_t) {
    if (index == 0 || index > m_params.size()) {
        m_params.resize(std::max(m_params.size(), index), {true, ""});
    }
    m_params[index - 1] = {true, ""};
}

void postgres_statement::bind(size_t index, int32_t val) {
    bind(index, static_cast<int64_t>(val));
}

void postgres_statement::bind(size_t index, int64_t val) {
    if (index == 0 || index > m_params.size()) {
        m_params.resize(std::max(m_params.size(), index), {true, ""});
    }
    m_params[index - 1] = {false, std::to_string(val)};
}

void postgres_statement::bind(size_t index, double val) {
    if (index == 0 || index > m_params.size()) {
        m_params.resize(std::max(m_params.size(), index), {true, ""});
    }
    char buf[64];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), val);
    m_params[index - 1] = {false, std::string(buf, ptr - buf)};
}

void postgres_statement::bind(size_t index, std::string_view val) {
    if (index == 0 || index > m_params.size()) {
        m_params.resize(std::max(m_params.size(), index), {true, ""});
    }
    m_params[index - 1] = {false, std::string(val)};
}

void postgres_statement::bind(size_t index, std::span<const uint8_t> val) {
    // Hex escape bytea format: \x...
    static constexpr char hex_chars[] = "0123456789abcdef";
    std::string hex = "\\x";
    hex.reserve(2 + val.size() * 2);
    for (uint8_t b : val) {
        hex.push_back(hex_chars[(b >> 4) & 0x0F]);
        hex.push_back(hex_chars[b & 0x0F]);
    }
    bind(index, std::string_view(hex));
}

void postgres_statement::bind(size_t index, std::chrono::system_clock::time_point val) {
    auto time_t_val = std::chrono::system_clock::to_time_t(val);
    std::tm tm_buf{};
    gmtime_r(&time_t_val, &tm_buf);
    char buf[32];
    size_t n = std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    bind(index, std::string_view(buf, n));
}

void postgres_statement::bind(std::string_view name, std::nullptr_t) {
    bind(static_cast<size_t>(get_param_index(name)), nullptr);
}

void postgres_statement::bind(std::string_view name, int32_t val) {
    bind(static_cast<size_t>(get_param_index(name)), val);
}

void postgres_statement::bind(std::string_view name, int64_t val) {
    bind(static_cast<size_t>(get_param_index(name)), val);
}

void postgres_statement::bind(std::string_view name, double val) {
    bind(static_cast<size_t>(get_param_index(name)), val);
}

void postgres_statement::bind(std::string_view name, std::string_view val) {
    bind(static_cast<size_t>(get_param_index(name)), val);
}

void postgres_statement::bind(std::string_view name, std::span<const uint8_t> val) {
    bind(static_cast<size_t>(get_param_index(name)), val);
}

void postgres_statement::bind(std::string_view name, std::chrono::system_clock::time_point val) {
    bind(static_cast<size_t>(get_param_index(name)), val);
}

void postgres_statement::execute() {
    query();
}

std::unique_ptr<result_set> postgres_statement::query() {
    if (!m_conn || !m_conn->conn) {
        throw db_exception("PostgreSQL connection is not open");
    }

    auto &api = pq_api::instance();
    api.ensure_loaded();

    std::vector<const char *> param_values;
    param_values.reserve(m_params.size());
    for (const auto &p : m_params) {
        param_values.push_back(p.is_null ? nullptr : p.value.c_str());
    }

    void *res = api.PQexecParams(
        m_conn->conn,
        m_translated_sql.c_str(),
        static_cast<int>(param_values.size()),
        nullptr,
        param_values.data(),
        nullptr,
        nullptr,
        0
    );

    if (!res) {
        throw db_exception("PostgreSQL query execution failed: " + std::string(api.PQerrorMessage(m_conn->conn)));
    }

    int status = api.PQresultStatus(res);
    if (status != PG_COMMAND_OK && status != PG_TUPLES_OK) {
        std::string err = api.PQresultErrorMessage(res);
        api.PQclear(res);
        throw db_exception("PostgreSQL execution error: " + err);
    }

    auto holder = std::make_shared<pg_result_holder>();
    holder->res = res;
    return std::make_unique<postgres_result_set>(std::move(holder));
}

void postgres_statement::reset() {
    for (auto &p : m_params) {
        p = {true, ""};
    }
}

// ==================== postgres_transaction ====================

postgres_transaction::postgres_transaction(std::shared_ptr<pg_conn_holder> conn)
    : m_conn(std::move(conn)), m_active(true) {
    if (!m_conn || !m_conn->conn) throw db_exception("Cannot begin transaction: connection is closed");
    auto &api = pq_api::instance();
    api.ensure_loaded();
    void *res = api.PQexec(m_conn->conn, "BEGIN;");
    if (!res || api.PQresultStatus(res) != PG_COMMAND_OK) {
        std::string err = res ? api.PQresultErrorMessage(res) : api.PQerrorMessage(m_conn->conn);
        if (res) api.PQclear(res);
        throw db_exception("PostgreSQL BEGIN failed: " + err);
    }
    api.PQclear(res);
}

postgres_transaction::~postgres_transaction() {
    if (m_active && m_conn && m_conn->conn) {
        auto &api = pq_api::instance();
        void *res = api.PQexec(m_conn->conn, "ROLLBACK;");
        if (res) api.PQclear(res);
    }
}

postgres_transaction::postgres_transaction(postgres_transaction &&other) noexcept
    : m_conn(std::move(other.m_conn)), m_active(other.m_active) {
    other.m_active = false;
}

postgres_transaction &postgres_transaction::operator=(postgres_transaction &&other) noexcept {
    if (this != &other) {
        if (m_active && m_conn && m_conn->conn) {
            auto &api = pq_api::instance();
            void *res = api.PQexec(m_conn->conn, "ROLLBACK;");
            if (res) api.PQclear(res);
        }
        m_conn = std::move(other.m_conn);
        m_active = other.m_active;
        other.m_active = false;
    }
    return *this;
}

void postgres_transaction::commit() {
    if (!m_active || !m_conn || !m_conn->conn) return;
    auto &api = pq_api::instance();
    void *res = api.PQexec(m_conn->conn, "COMMIT;");
    if (!res || api.PQresultStatus(res) != PG_COMMAND_OK) {
        std::string err = res ? api.PQresultErrorMessage(res) : api.PQerrorMessage(m_conn->conn);
        if (res) api.PQclear(res);
        throw db_exception("PostgreSQL COMMIT failed: " + err);
    }
    api.PQclear(res);
    m_active = false;
}

void postgres_transaction::rollback() {
    if (!m_active || !m_conn || !m_conn->conn) return;
    auto &api = pq_api::instance();
    void *res = api.PQexec(m_conn->conn, "ROLLBACK;");
    if (res) api.PQclear(res);
    m_active = false;
}

bool postgres_transaction::is_active() const noexcept {
    return m_active;
}

// ==================== postgres_database ====================

postgres_database::postgres_database() = default;

postgres_database::postgres_database(const std::string &conn_str) {
    connect(conn_str);
}

postgres_database::~postgres_database() {
    disconnect();
}

postgres_database::postgres_database(postgres_database &&other) noexcept
    : m_conn(std::move(other.m_conn)) {}

postgres_database &postgres_database::operator=(postgres_database &&other) noexcept {
    if (this != &other) {
        disconnect();
        m_conn = std::move(other.m_conn);
    }
    return *this;
}

void postgres_database::connect(const std::string &conn_str) {
    disconnect();
    auto &api = pq_api::instance();
    api.ensure_loaded();

    void *conn = api.PQconnectdb(conn_str.c_str());
    if (!conn) {
        throw db_exception("PostgreSQL allocation failed during connection");
    }

    if (api.PQstatus(conn) != PG_CONNECTION_OK) {
        std::string err = api.PQerrorMessage(conn);
        api.PQfinish(conn);
        throw db_exception("Failed to connect to PostgreSQL: " + err);
    }

    m_conn = std::make_shared<pg_conn_holder>();
    m_conn->conn = conn;
}

void postgres_database::disconnect() {
    m_conn.reset();
}

bool postgres_database::is_connected() const noexcept {
    if (!m_conn || !m_conn->conn) return false;
    return pq_api::instance().PQstatus(m_conn->conn) == PG_CONNECTION_OK;
}

bool postgres_database::ping() noexcept {
    if (!is_connected()) return false;
    try {
        auto stmt = prepare("SELECT 1;");
        auto res = stmt->query();
        return res && res->next() && res->current_row().get_int32(0) == 1;
    } catch (...) {
        return false;
    }
}

void postgres_database::execute(std::string_view sql) {
    if (!is_connected()) throw db_exception("PostgreSQL connection is not open");
    auto &api = pq_api::instance();
    void *res = api.PQexec(m_conn->conn, std::string(sql).c_str());
    if (!res) {
        throw db_exception("PostgreSQL execute failed: " + std::string(api.PQerrorMessage(m_conn->conn)));
    }
    int status = api.PQresultStatus(res);
    if (status != PG_COMMAND_OK && status != PG_TUPLES_OK) {
        std::string err = api.PQresultErrorMessage(res);
        api.PQclear(res);
        throw db_exception("PostgreSQL execute error: " + err);
    }
    api.PQclear(res);
}

std::unique_ptr<statement> postgres_database::prepare(std::string_view sql) {
    if (!is_connected()) throw db_exception("PostgreSQL connection is not open");
    return std::make_unique<postgres_statement>(m_conn, sql);
}

std::unique_ptr<transaction> postgres_database::begin_transaction() {
    if (!is_connected()) throw db_exception("PostgreSQL connection is not open");
    return std::make_unique<postgres_transaction>(m_conn);
}

} // namespace backend::db
