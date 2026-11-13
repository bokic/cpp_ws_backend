#include "mysql_driver.h"
#include <dlfcn.h>
#include <charconv>
#include <ctime>
#include <cstring>
#include <algorithm>
#include <cctype>

namespace backend::db {

// ==================== Dynamic MySQL/MariaDB Loader ====================

struct mysql_field {
    char *name;                 /* Name of column */
    char *org_name;             /* Name of original column */
    char *table;                /* Table of column */
    char *org_table;            /* Name of original table */
    char *db;                   /* table schema */
    char *catalog;              /* table catalog */
    char *def;                  /* Default value */
    unsigned long length;       /* Width of column */
    unsigned long max_length;   /* Max width of selected set */
    unsigned int name_length;
    unsigned int org_name_length;
    unsigned int table_length;
    unsigned int org_table_length;
    unsigned int db_length;
    unsigned int catalog_length;
    unsigned int def_length;
    unsigned int flags;         /* Div flags */
    unsigned int decimals;      /* Number of decimals in field */
    unsigned int charsetnr;     /* Character set number */
    int type;                   /* enum enum_field_types */
    void *extension;
};

struct mysql_api {
    void *handle = nullptr;

    typedef void *(*mysql_init_t)(void *mysql);
    typedef void *(*mysql_real_connect_t)(void *mysql, const char *host,
                                          const char *user, const char *passwd,
                                          const char *db, unsigned int port,
                                          const char *unix_socket,
                                          unsigned long clientflag);
    typedef void (*mysql_close_t)(void *sock);
    typedef unsigned int (*mysql_errno_t)(void *mysql);
    typedef const char *(*mysql_error_t)(void *mysql);
    typedef int (*mysql_ping_t)(void *mysql);
    typedef int (*mysql_real_query_t)(void *mysql, const char *q, unsigned long length);
    typedef unsigned long (*mysql_real_escape_string_t)(void *mysql, char *to,
                                                        const char *from,
                                                        unsigned long length);
    typedef void *(*mysql_store_result_t)(void *mysql);
    typedef void (*mysql_free_result_t)(void *result);
    typedef char **(*mysql_fetch_row_t)(void *result);
    typedef unsigned long *(*mysql_fetch_lengths_t)(void *result);
    typedef unsigned int (*mysql_num_fields_t)(void *res);
    typedef const mysql_field *(*mysql_fetch_fields_t)(void *res);
    typedef uint64_t (*mysql_affected_rows_t)(void *mysql);
    typedef int (*mysql_autocommit_t)(void *mysql, int mode);
    typedef int (*mysql_commit_t)(void *mysql);
    typedef int (*mysql_rollback_t)(void *mysql);

    mysql_init_t mysql_init = nullptr;
    mysql_real_connect_t mysql_real_connect = nullptr;
    mysql_close_t mysql_close = nullptr;
    mysql_errno_t mysql_errno = nullptr;
    mysql_error_t mysql_error = nullptr;
    mysql_ping_t mysql_ping = nullptr;
    mysql_real_query_t mysql_real_query = nullptr;
    mysql_real_escape_string_t mysql_real_escape_string = nullptr;
    mysql_store_result_t mysql_store_result = nullptr;
    mysql_free_result_t mysql_free_result = nullptr;
    mysql_fetch_row_t mysql_fetch_row = nullptr;
    mysql_fetch_lengths_t mysql_fetch_lengths = nullptr;
    mysql_num_fields_t mysql_num_fields = nullptr;
    mysql_fetch_fields_t mysql_fetch_fields = nullptr;
    mysql_affected_rows_t mysql_affected_rows = nullptr;
    mysql_autocommit_t mysql_autocommit = nullptr;
    mysql_commit_t mysql_commit = nullptr;
    mysql_rollback_t mysql_rollback = nullptr;

    static mysql_api &instance() {
        static mysql_api api;
        return api;
    }

    bool load() {
        if (handle) return true;
        const char *libs[] = {
            "libmariadb.so.3",
            "libmariadb.so",
            "libmysqlclient.so.24",
            "libmysqlclient.so.23",
            "libmysqlclient.so.22",
            "libmysqlclient.so.21",
            "libmysqlclient.so.20",
            "libmysqlclient.so.18",
            "libmysqlclient.so"
        };
        for (const char *lib : libs) {
            handle = dlopen(lib, RTLD_LAZY | RTLD_LOCAL);
            if (handle) break;
        }
        if (!handle) return false;

        #define LOAD_SYM(name) name = reinterpret_cast<name##_t>(dlsym(handle, #name)); if (!name) return false;
        LOAD_SYM(mysql_init);
        LOAD_SYM(mysql_real_connect);
        LOAD_SYM(mysql_close);
        LOAD_SYM(mysql_errno);
        LOAD_SYM(mysql_error);
        LOAD_SYM(mysql_ping);
        LOAD_SYM(mysql_real_query);
        LOAD_SYM(mysql_real_escape_string);
        LOAD_SYM(mysql_store_result);
        LOAD_SYM(mysql_free_result);
        LOAD_SYM(mysql_fetch_row);
        LOAD_SYM(mysql_fetch_lengths);
        LOAD_SYM(mysql_num_fields);
        LOAD_SYM(mysql_fetch_fields);
        LOAD_SYM(mysql_affected_rows);
        LOAD_SYM(mysql_autocommit);
        LOAD_SYM(mysql_commit);
        LOAD_SYM(mysql_rollback);
        #undef LOAD_SYM

        return true;
    }

    void ensure_loaded() {
        if (!load()) {
            const char *err = dlerror();
            throw db_exception("Failed to dynamically load MySQL/MariaDB client library: " +
                               std::string(err ? err : "library not found"));
        }
    }
};

struct mysql_conn_holder {
    void *conn = nullptr;

    ~mysql_conn_holder() {
        if (conn) {
            mysql_api::instance().mysql_close(conn);
            conn = nullptr;
        }
    }
};

struct mysql_result_holder {
    std::shared_ptr<mysql_conn_holder> conn_holder;
    void *res = nullptr;
    uint64_t affected = 0;
    unsigned int num_fields = 0;
    const mysql_field *fields = nullptr;

    ~mysql_result_holder() {
        if (res) {
            mysql_api::instance().mysql_free_result(res);
            res = nullptr;
        }
    }
};

// ==================== Helpers ====================

static std::string escape_sql_string(void *conn, std::string_view val) {
    auto &api = mysql_api::instance();
    std::string out = "'";
    if (val.empty()) {
        out += "'";
        return out;
    }
    if (conn && api.mysql_real_escape_string) {
        std::string escaped;
        escaped.resize(val.size() * 2 + 1);
        unsigned long len = api.mysql_real_escape_string(conn, escaped.data(), val.data(),
                                                         static_cast<unsigned long>(val.size()));
        escaped.resize(len);
        out += escaped;
    } else {
        for (char c : val) {
            switch (c) {
                case '\'': out += "\\'"; break;
                case '\"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\0': out += "\\0"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default: out += c; break;
            }
        }
    }
    out += "'";
    return out;
}

static std::string escape_sql_blob(std::span<const uint8_t> val) {
    static constexpr char hex_chars[] = "0123456789ABCDEF";
    std::string out = "X'";
    out.reserve(3 + val.size() * 2);
    for (uint8_t b : val) {
        out.push_back(hex_chars[(b >> 4) & 0x0F]);
        out.push_back(hex_chars[b & 0x0F]);
    }
    out += "'";
    return out;
}

static std::string format_sql_timestamp(std::chrono::system_clock::time_point tp) {
    auto t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_buf{};
    gmtime_r(&t, &tm_buf);
    char buf[32];
    size_t n = std::strftime(buf, sizeof(buf), "'%Y-%m-%d %H:%M:%S'", &tm_buf);
    return std::string(buf, n);
}

struct parsed_mysql_conn {
    std::string host = "localhost";
    std::string user;
    std::string passwd;
    std::string db;
    unsigned int port = 3306;
    std::string unix_socket;
};

static parsed_mysql_conn parse_mysql_conn_str(std::string_view conn_str) {
    parsed_mysql_conn res;
    if (conn_str.empty()) return res;

    if (conn_str.starts_with("mysql://") || conn_str.starts_with("mariadb://")) {
        size_t scheme_pos = conn_str.find("://");
        conn_str.remove_prefix(scheme_pos + 3);

        auto at_pos = conn_str.find('@');
        if (at_pos != std::string_view::npos) {
            std::string_view userinfo = conn_str.substr(0, at_pos);
            conn_str.remove_prefix(at_pos + 1);
            auto colon = userinfo.find(':');
            if (colon != std::string_view::npos) {
                res.user = std::string(userinfo.substr(0, colon));
                res.passwd = std::string(userinfo.substr(colon + 1));
            } else {
                res.user = std::string(userinfo);
            }
        }

        auto slash_pos = conn_str.find('/');
        std::string_view hostport = (slash_pos != std::string_view::npos) ? conn_str.substr(0, slash_pos) : conn_str;
        if (slash_pos != std::string_view::npos) {
            conn_str.remove_prefix(slash_pos + 1);
            auto q_pos = conn_str.find('?');
            res.db = std::string((q_pos != std::string_view::npos) ? conn_str.substr(0, q_pos) : conn_str);
        }

        auto colon = hostport.find(':');
        if (colon != std::string_view::npos) {
            res.host = std::string(hostport.substr(0, colon));
            unsigned int p = 0;
            auto [ptr, ec] = std::from_chars(hostport.data() + colon + 1, hostport.data() + hostport.size(), p);
            if (ec == std::errc() && p > 0) res.port = p;
        } else if (!hostport.empty()) {
            res.host = std::string(hostport);
        }
        return res;
    }

    if (conn_str.find('=') != std::string_view::npos) {
        size_t start = 0;
        while (start < conn_str.size()) {
            while (start < conn_str.size() && (conn_str[start] == ';' || conn_str[start] == '&' ||
                   std::isspace(static_cast<unsigned char>(conn_str[start])))) {
                ++start;
            }
            if (start >= conn_str.size()) break;

            size_t eq = conn_str.find('=', start);
            if (eq == std::string_view::npos) break;

            std::string_view key = conn_str.substr(start, eq - start);
            while (!key.empty() && std::isspace(static_cast<unsigned char>(key.front()))) key.remove_prefix(1);
            while (!key.empty() && std::isspace(static_cast<unsigned char>(key.back()))) key.remove_suffix(1);

            size_t val_start = eq + 1;
            size_t val_end = val_start;
            if (val_start < conn_str.size() && (conn_str[val_start] == '\'' || conn_str[val_start] == '\"')) {
                char q = conn_str[val_start++];
                val_end = conn_str.find(q, val_start);
                if (val_end == std::string_view::npos) val_end = conn_str.size();
                start = val_end + 1;
            } else {
                while (val_end < conn_str.size() && conn_str[val_end] != ';' && conn_str[val_end] != '&' &&
                       !std::isspace(static_cast<unsigned char>(conn_str[val_end]))) {
                    ++val_end;
                }
                start = val_end;
            }
            std::string_view val = conn_str.substr(val_start, val_end - val_start);

            if (key == "host" || key == "server") {
                res.host = std::string(val);
            } else if (key == "user" || key == "username" || key == "uid") {
                res.user = std::string(val);
            } else if (key == "password" || key == "passwd" || key == "pwd") {
                res.passwd = std::string(val);
            } else if (key == "database" || key == "db" || key == "dbname") {
                res.db = std::string(val);
            } else if (key == "port") {
                unsigned int p = 0;
                auto [ptr, ec] = std::from_chars(val.data(), val.data() + val.size(), p);
                if (ec == std::errc() && p > 0) res.port = p;
            } else if (key == "unix_socket" || key == "socket") {
                res.unix_socket = std::string(val);
            }
        }
        return res;
    }

    res.db = std::string(conn_str);
    return res;
}

// ==================== mysql_row ====================

mysql_row::mysql_row(std::shared_ptr<mysql_result_holder> holder)
    : m_holder(std::move(holder)) {
}

size_t mysql_row::column_count() const noexcept {
    return m_holder ? m_holder->num_fields : 0;
}

void mysql_row::ensure_column_names() const {
    if (!m_cached_names.empty() || !m_holder || !m_holder->fields) return;
    m_cached_names.reserve(m_holder->num_fields);
    for (size_t i = 0; i < m_holder->num_fields; ++i) {
        m_cached_names.emplace_back(m_holder->fields[i].name ? m_holder->fields[i].name : "");
    }
}

std::string_view mysql_row::column_name(size_t index) const {
    if (!m_holder || index >= m_holder->num_fields || !m_holder->fields) {
        throw db_exception("Column index out of range: " + std::to_string(index));
    }
    return m_holder->fields[index].name ? m_holder->fields[index].name : "";
}

size_t mysql_row::column_index(std::string_view name) const {
    ensure_column_names();
    for (size_t i = 0; i < m_cached_names.size(); ++i) {
        if (m_cached_names[i] == name) {
            return i;
        }
    }
    throw db_exception("Column not found: " + std::string(name));
}

bool mysql_row::is_null(size_t index) const {
    if (!m_row_data || !m_holder || index >= m_holder->num_fields) {
        return true;
    }
    return m_row_data[index] == nullptr;
}

column_type mysql_row::get_column_type(size_t index) const {
    if (is_null(index)) return column_type::null_value;
    if (!m_holder || !m_holder->fields || index >= m_holder->num_fields) {
        return column_type::text;
    }
    int t = m_holder->fields[index].type;
    switch (t) {
        case 1:  // MYSQL_TYPE_TINY
        case 2:  // MYSQL_TYPE_SHORT
        case 3:  // MYSQL_TYPE_LONG
        case 8:  // MYSQL_TYPE_LONGLONG
        case 9:  // MYSQL_TYPE_INT24
        case 13: // MYSQL_TYPE_YEAR
        case 16: // MYSQL_TYPE_BIT
            return column_type::integer;
        case 0:  // MYSQL_TYPE_DECIMAL
        case 4:  // MYSQL_TYPE_FLOAT
        case 5:  // MYSQL_TYPE_DOUBLE
        case 246:// MYSQL_TYPE_NEWDECIMAL
            return column_type::floating_point;
        case 7:  // MYSQL_TYPE_TIMESTAMP
        case 10: // MYSQL_TYPE_DATE
        case 11: // MYSQL_TYPE_TIME
        case 12: // MYSQL_TYPE_DATETIME
        case 14: // MYSQL_TYPE_NEWDATE
        case 17: // MYSQL_TYPE_TIMESTAMP2
        case 18: // MYSQL_TYPE_DATETIME2
        case 19: // MYSQL_TYPE_TIME2
            return column_type::timestamp;
        case 249:// MYSQL_TYPE_TINY_BLOB
        case 250:// MYSQL_TYPE_MEDIUM_BLOB
        case 251:// MYSQL_TYPE_LONG_BLOB
        case 252:// MYSQL_TYPE_BLOB
        case 255:// MYSQL_TYPE_GEOMETRY
            if (m_holder->fields[index].flags & 128) {
                return column_type::blob;
            }
            return column_type::text;
        default:
            return column_type::text;
    }
}

int32_t mysql_row::get_int32(size_t index) const {
    return static_cast<int32_t>(get_int64(index));
}

int64_t mysql_row::get_int64(size_t index) const {
    if (is_null(index) || !m_lengths) return 0;
    std::string_view s(m_row_data[index], m_lengths[index]);
    if (s.empty()) return 0;
    int64_t val = 0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
    if (ec == std::errc()) {
        return val;
    }
    try {
        return std::stoll(std::string(s));
    } catch (...) {
        return 0;
    }
}

double mysql_row::get_double(size_t index) const {
    if (is_null(index) || !m_lengths) return 0.0;
    std::string_view s(m_row_data[index], m_lengths[index]);
    if (s.empty()) return 0.0;
    double val = 0.0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
    if (ec == std::errc()) {
        return val;
    }
    try {
        return std::stod(std::string(s));
    } catch (...) {
        return 0.0;
    }
}

std::string mysql_row::get_string(size_t index) const {
    if (is_null(index) || !m_lengths) return {};
    return std::string(m_row_data[index], m_lengths[index]);
}

std::span<const uint8_t> mysql_row::get_blob(size_t index) const {
    if (is_null(index) || !m_lengths) return {};
    return std::span<const uint8_t>(reinterpret_cast<const uint8_t *>(m_row_data[index]), m_lengths[index]);
}

std::chrono::system_clock::time_point mysql_row::get_timestamp(size_t index) const {
    if (is_null(index) || !m_lengths) return {};
    std::string s(m_row_data[index], m_lengths[index]);
    if (s.empty()) return {};

    std::tm tm_buf{};
    if (strptime(s.c_str(), "%Y-%m-%d %H:%M:%S", &tm_buf) != nullptr ||
        strptime(s.c_str(), "%Y-%m-%dT%H:%M:%SZ", &tm_buf) != nullptr ||
        strptime(s.c_str(), "%Y-%m-%d", &tm_buf) != nullptr) {
        return std::chrono::system_clock::from_time_t(timegm(&tm_buf));
    }
    return {};
}

// ==================== mysql_result_set ====================

mysql_result_set::mysql_result_set(std::shared_ptr<mysql_result_holder> holder)
    : m_holder(std::move(holder)), m_row(m_holder) {
}

bool mysql_result_set::next() {
    if (!m_holder || !m_holder->res) {
        return false;
    }
    auto &api = mysql_api::instance();
    char **row_data = api.mysql_fetch_row(m_holder->res);
    if (!row_data) {
        m_row.update_row_data(nullptr, nullptr);
        return false;
    }
    unsigned long *lengths = api.mysql_fetch_lengths(m_holder->res);
    m_row.update_row_data(row_data, lengths);
    return true;
}

const row& mysql_result_set::current_row() const {
    return m_row;
}

size_t mysql_result_set::column_count() const noexcept {
    return m_row.column_count();
}

std::string_view mysql_result_set::column_name(size_t index) const {
    return m_row.column_name(index);
}

size_t mysql_result_set::column_index(std::string_view name) const {
    return m_row.column_index(name);
}

uint64_t mysql_result_set::affected_rows() const noexcept {
    return m_holder ? m_holder->affected : 0;
}

// ==================== mysql_statement ====================

mysql_statement::mysql_statement(std::shared_ptr<mysql_conn_holder> conn, std::string_view sql)
    : m_conn(std::move(conn)) {
    size_t i = 0;
    std::string cur_text;

    while (i < sql.size()) {
        char c = sql[i];
        if (c == '-' && i + 1 < sql.size() && sql[i + 1] == '-') {
            cur_text += sql.substr(i, 2);
            i += 2;
            while (i < sql.size() && sql[i] != '\n') {
                cur_text += sql[i++];
            }
            continue;
        }
        if (c == '/' && i + 1 < sql.size() && sql[i + 1] == '*') {
            cur_text += sql.substr(i, 2);
            i += 2;
            while (i + 1 < sql.size() && !(sql[i] == '*' && sql[i + 1] == '/')) {
                cur_text += sql[i++];
            }
            if (i + 1 < sql.size()) {
                cur_text += sql.substr(i, 2);
                i += 2;
            }
            continue;
        }
        if (c == '\'' || c == '\"' || c == '`') {
            char quote = c;
            cur_text += quote;
            ++i;
            while (i < sql.size()) {
                if (sql[i] == '\\' && i + 1 < sql.size()) {
                    cur_text += sql[i++];
                    cur_text += sql[i++];
                } else if (sql[i] == quote) {
                    cur_text += sql[i++];
                    if (i < sql.size() && sql[i] == quote) {
                        cur_text += sql[i++];
                    } else {
                        break;
                    }
                } else {
                    cur_text += sql[i++];
                }
            }
            continue;
        }
        if (c == '?') {
            m_chunks.push_back({std::move(cur_text), -1});
            cur_text.clear();
            int idx = static_cast<int>(m_param_names.size());
            m_param_names.push_back("");
            m_chunks.push_back({"", idx});
            ++i;
            continue;
        }
        if (c == ':' && (i == 0 || (!std::isalnum(static_cast<unsigned char>(sql[i - 1])) && sql[i - 1] != ':'))) {
            size_t start = ++i;
            while (i < sql.size() && (std::isalnum(static_cast<unsigned char>(sql[i])) || sql[i] == '_')) {
                ++i;
            }
            std::string name(sql.substr(start, i - start));
            m_chunks.push_back({std::move(cur_text), -1});
            cur_text.clear();
            int idx = -1;
            for (size_t k = 0; k < m_param_names.size(); ++k) {
                if (m_param_names[k] == name) {
                    idx = static_cast<int>(k);
                    break;
                }
            }
            if (idx == -1) {
                idx = static_cast<int>(m_param_names.size());
                m_param_names.push_back(name);
            }
            m_chunks.push_back({"", idx});
            continue;
        }
        cur_text += c;
        ++i;
    }
    if (!cur_text.empty()) {
        m_chunks.push_back({std::move(cur_text), -1});
    }

    m_bound.resize(m_param_names.size());
}

int mysql_statement::get_param_index(std::string_view name) {
    if (!name.empty() && name[0] == ':') {
        name.remove_prefix(1);
    }
    for (size_t i = 0; i < m_param_names.size(); ++i) {
        if (m_param_names[i] == name) {
            return static_cast<int>(i + 1);
        }
    }
    throw db_exception("Named parameter not found in MySQL statement: " + std::string(name));
}

void mysql_statement::bind(size_t index, std::nullptr_t) {
    if (index == 0) throw db_exception("MySQL parameter index is 1-based");
    if (index > m_bound.size()) {
        m_bound.resize(std::max(m_bound.size(), index));
    }
    m_bound[index - 1] = {true, "NULL"};
}

void mysql_statement::bind(size_t index, int32_t val) {
    bind(index, static_cast<int64_t>(val));
}

void mysql_statement::bind(size_t index, int64_t val) {
    if (index == 0) throw db_exception("MySQL parameter index is 1-based");
    if (index > m_bound.size()) {
        m_bound.resize(std::max(m_bound.size(), index));
    }
    m_bound[index - 1] = {false, std::to_string(val)};
}

void mysql_statement::bind(size_t index, double val) {
    if (index == 0) throw db_exception("MySQL parameter index is 1-based");
    if (index > m_bound.size()) {
        m_bound.resize(std::max(m_bound.size(), index));
    }
    char buf[64];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), val);
    m_bound[index - 1] = {false, std::string(buf, ptr - buf)};
}

void mysql_statement::bind(size_t index, std::string_view val) {
    if (index == 0) throw db_exception("MySQL parameter index is 1-based");
    if (index > m_bound.size()) {
        m_bound.resize(std::max(m_bound.size(), index));
    }
    void *conn = (m_conn && m_conn->conn) ? m_conn->conn : nullptr;
    m_bound[index - 1] = {false, escape_sql_string(conn, val)};
}

void mysql_statement::bind(size_t index, std::span<const uint8_t> val) {
    if (index == 0) throw db_exception("MySQL parameter index is 1-based");
    if (index > m_bound.size()) {
        m_bound.resize(std::max(m_bound.size(), index));
    }
    m_bound[index - 1] = {false, escape_sql_blob(val)};
}

void mysql_statement::bind(size_t index, std::chrono::system_clock::time_point val) {
    if (index == 0) throw db_exception("MySQL parameter index is 1-based");
    if (index > m_bound.size()) {
        m_bound.resize(std::max(m_bound.size(), index));
    }
    m_bound[index - 1] = {false, format_sql_timestamp(val)};
}

void mysql_statement::bind(std::string_view name, std::nullptr_t) {
    bind(static_cast<size_t>(get_param_index(name)), nullptr);
}

void mysql_statement::bind(std::string_view name, int32_t val) {
    bind(static_cast<size_t>(get_param_index(name)), val);
}

void mysql_statement::bind(std::string_view name, int64_t val) {
    bind(static_cast<size_t>(get_param_index(name)), val);
}

void mysql_statement::bind(std::string_view name, double val) {
    bind(static_cast<size_t>(get_param_index(name)), val);
}

void mysql_statement::bind(std::string_view name, std::string_view val) {
    bind(static_cast<size_t>(get_param_index(name)), val);
}

void mysql_statement::bind(std::string_view name, std::span<const uint8_t> val) {
    bind(static_cast<size_t>(get_param_index(name)), val);
}

void mysql_statement::bind(std::string_view name, std::chrono::system_clock::time_point val) {
    bind(static_cast<size_t>(get_param_index(name)), val);
}

std::string mysql_statement::build_final_sql() {
    std::string sql;
    for (const auto &chunk : m_chunks) {
        if (chunk.param_idx < 0) {
            sql += chunk.text;
        } else {
            size_t idx = static_cast<size_t>(chunk.param_idx);
            if (idx < m_bound.size() && !m_bound[idx].is_null) {
                sql += m_bound[idx].escaped_literal;
            } else {
                sql += "NULL";
            }
        }
    }
    return sql;
}

void mysql_statement::execute() {
    query();
}

std::unique_ptr<result_set> mysql_statement::query() {
    if (!m_conn || !m_conn->conn) {
        throw db_exception("MySQL connection is not open");
    }
    auto &api = mysql_api::instance();
    api.ensure_loaded();

    std::string sql = build_final_sql();
    int rc = api.mysql_real_query(m_conn->conn, sql.data(), static_cast<unsigned long>(sql.size()));
    if (rc != 0) {
        throw db_exception("MySQL query execution failed: " + std::string(api.mysql_error(m_conn->conn)));
    }

    void *res = api.mysql_store_result(m_conn->conn);
    if (!res && api.mysql_errno(m_conn->conn) != 0) {
        throw db_exception("MySQL store result failed: " + std::string(api.mysql_error(m_conn->conn)));
    }

    auto holder = std::make_shared<mysql_result_holder>();
    holder->conn_holder = m_conn;
    holder->res = res;
    holder->affected = api.mysql_affected_rows(m_conn->conn);
    if (res) {
        holder->num_fields = api.mysql_num_fields(res);
        holder->fields = api.mysql_fetch_fields(res);
    }
    return std::make_unique<mysql_result_set>(std::move(holder));
}

void mysql_statement::reset() {
    for (auto &b : m_bound) {
        b.is_null = true;
        b.escaped_literal = "NULL";
    }
}

// ==================== mysql_transaction ====================

mysql_transaction::mysql_transaction(std::shared_ptr<mysql_conn_holder> conn)
    : m_conn(std::move(conn)) {
    if (!m_conn || !m_conn->conn) {
        throw db_exception("MySQL connection is not open");
    }
    auto &api = mysql_api::instance();
    api.ensure_loaded();

    if (api.mysql_autocommit(m_conn->conn, 0) != 0) {
        throw db_exception("Failed to disable autocommit: " + std::string(api.mysql_error(m_conn->conn)));
    }
    if (api.mysql_real_query(m_conn->conn, "START TRANSACTION", 17) != 0) {
        api.mysql_autocommit(m_conn->conn, 1);
        throw db_exception("Failed to start transaction: " + std::string(api.mysql_error(m_conn->conn)));
    }
    m_active = true;
}

mysql_transaction::~mysql_transaction() {
    if (m_active && m_conn && m_conn->conn) {
        try {
            rollback();
        } catch (...) {
        }
    }
}

mysql_transaction::mysql_transaction(mysql_transaction &&other) noexcept
    : m_conn(std::move(other.m_conn)), m_active(other.m_active) {
    other.m_active = false;
}

mysql_transaction &mysql_transaction::operator=(mysql_transaction &&other) noexcept {
    if (this != &other) {
        if (m_active && m_conn && m_conn->conn) {
            try {
                rollback();
            } catch (...) {
            }
        }
        m_conn = std::move(other.m_conn);
        m_active = other.m_active;
        other.m_active = false;
    }
    return *this;
}

void mysql_transaction::commit() {
    if (!m_active) {
        throw db_exception("Transaction is not active");
    }
    if (!m_conn || !m_conn->conn) {
        throw db_exception("MySQL connection is closed");
    }
    auto &api = mysql_api::instance();
    if (api.mysql_commit(m_conn->conn) != 0) {
        throw db_exception("Failed to commit transaction: " + std::string(api.mysql_error(m_conn->conn)));
    }
    api.mysql_autocommit(m_conn->conn, 1);
    m_active = false;
}

void mysql_transaction::rollback() {
    if (!m_active) {
        throw db_exception("Transaction is not active");
    }
    if (!m_conn || !m_conn->conn) {
        throw db_exception("MySQL connection is closed");
    }
    auto &api = mysql_api::instance();
    if (api.mysql_rollback(m_conn->conn) != 0) {
        throw db_exception("Failed to rollback transaction: " + std::string(api.mysql_error(m_conn->conn)));
    }
    api.mysql_autocommit(m_conn->conn, 1);
    m_active = false;
}

bool mysql_transaction::is_active() const noexcept {
    return m_active;
}

// ==================== mysql_database ====================

mysql_database::mysql_database() = default;

mysql_database::mysql_database(const std::string &conn_str) {
    connect(conn_str);
}

mysql_database::~mysql_database() {
    disconnect();
}

mysql_database::mysql_database(mysql_database &&other) noexcept
    : m_conn(std::move(other.m_conn)) {
}

mysql_database &mysql_database::operator=(mysql_database &&other) noexcept {
    if (this != &other) {
        disconnect();
        m_conn = std::move(other.m_conn);
    }
    return *this;
}

void mysql_database::connect(const std::string &conn_str) {
    auto &api = mysql_api::instance();
    api.ensure_loaded();

    disconnect();

    void *conn = api.mysql_init(nullptr);
    if (!conn) {
        throw db_exception("mysql_init failed to allocate connection handle");
    }

    auto parsed = parse_mysql_conn_str(conn_str);
    const char *host_str = parsed.host.empty() ? nullptr : parsed.host.c_str();
    const char *user_str = parsed.user.empty() ? nullptr : parsed.user.c_str();
    const char *pass_str = parsed.passwd.empty() ? nullptr : parsed.passwd.c_str();
    const char *db_str = parsed.db.empty() ? nullptr : parsed.db.c_str();
    const char *socket_str = parsed.unix_socket.empty() ? nullptr : parsed.unix_socket.c_str();

    void *res = api.mysql_real_connect(conn, host_str, user_str, pass_str, db_str,
                                       parsed.port, socket_str, 0);
    if (!res) {
        std::string err = api.mysql_error(conn);
        api.mysql_close(conn);
        throw db_exception("Failed to connect to MySQL database: " + err);
    }

    m_conn = std::make_shared<mysql_conn_holder>();
    m_conn->conn = conn;
}

void mysql_database::disconnect() {
    m_conn.reset();
}

bool mysql_database::is_connected() const noexcept {
    return m_conn && m_conn->conn != nullptr;
}

bool mysql_database::ping() noexcept {
    if (!is_connected()) return false;
    return mysql_api::instance().mysql_ping(m_conn->conn) == 0;
}

void mysql_database::execute(std::string_view sql) {
    if (!is_connected()) {
        throw db_exception("MySQL connection is not open");
    }
    auto &api = mysql_api::instance();
    int rc = api.mysql_real_query(m_conn->conn, sql.data(), static_cast<unsigned long>(sql.size()));
    if (rc != 0) {
        throw db_exception("MySQL execute failed: " + std::string(api.mysql_error(m_conn->conn)));
    }
    void *res = api.mysql_store_result(m_conn->conn);
    if (res) {
        api.mysql_free_result(res);
    }
}

std::unique_ptr<statement> mysql_database::prepare(std::string_view sql) {
    if (!is_connected()) {
        throw db_exception("MySQL connection is not open");
    }
    return std::make_unique<mysql_statement>(m_conn, sql);
}

std::unique_ptr<transaction> mysql_database::begin_transaction() {
    if (!is_connected()) {
        throw db_exception("MySQL connection is not open");
    }
    return std::make_unique<mysql_transaction>(m_conn);
}

} // namespace backend::db
