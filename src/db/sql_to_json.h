#pragma once

#include "database.h"
#include <fcgio.h>
#include <json-c/json.h>
#include <string>
#include <string_view>
#include <ostream>
#include <sstream>
#include <span>
#include <vector>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <utility>

namespace backend::db {

// ==================== json-c RAII Object Guard ====================

class json_object_guard {
public:
    explicit json_object_guard(json_object *obj = nullptr) noexcept : m_obj(obj) {}
    ~json_object_guard() noexcept { reset(); }

    json_object_guard(const json_object_guard &) = delete;
    json_object_guard &operator=(const json_object_guard &) = delete;

    json_object_guard(json_object_guard &&other) noexcept : m_obj(other.m_obj) {
        other.m_obj = nullptr;
    }

    json_object_guard &operator=(json_object_guard &&other) noexcept {
        if (this != &other) {
            reset();
            m_obj = other.m_obj;
            other.m_obj = nullptr;
        }
        return *this;
    }

    void reset(json_object *obj = nullptr) noexcept {
        if (m_obj) {
            json_object_put(m_obj);
        }
        m_obj = obj;
    }

    json_object *get() const noexcept { return m_obj; }
    json_object *release() noexcept {
        json_object *tmp = m_obj;
        m_obj = nullptr;
        return tmp;
    }

    explicit operator bool() const noexcept { return m_obj != nullptr; }

    const char *to_string(int flags = JSON_C_TO_STRING_PLAIN) const noexcept {
        return m_obj ? json_object_to_json_string_ext(m_obj, flags) : "";
    }

private:
    json_object *m_obj = nullptr;
};

// ==================== Output Stream Sinks ====================

class stream_sink {
public:
    virtual ~stream_sink() = default;
    virtual void write(std::string_view sv) = 0;
    virtual void put(char c) = 0;
};

class fcgx_sink final : public stream_sink {
public:
    explicit fcgx_sink(FCGX_Stream *stream) : m_stream(stream) {}

    void write(std::string_view sv) override {
        if (m_stream && !sv.empty()) {
            FCGX_PutStr(sv.data(), static_cast<int>(sv.size()), m_stream);
        }
    }

    void put(char c) override {
        if (m_stream) {
            FCGX_PutChar(c, m_stream);
        }
    }

private:
    FCGX_Stream *m_stream = nullptr;
};

class ostream_sink final : public stream_sink {
public:
    explicit ostream_sink(std::ostream &os) : m_os(os) {}

    void write(std::string_view sv) override {
        m_os.write(sv.data(), static_cast<std::streamsize>(sv.size()));
    }

    void put(char c) override {
        m_os.put(c);
    }

private:
    std::ostream &m_os;
};

class string_sink final : public stream_sink {
public:
    explicit string_sink(std::string &str) : m_str(str) {}

    void write(std::string_view sv) override {
        m_str.append(sv);
    }

    void put(char c) override {
        m_str.push_back(c);
    }

private:
    std::string &m_str;
};

// ==================== JSON Formatting Utilities (via json-c) ====================

bool is_valid_callback(std::string_view callback);
std::string to_base64(std::span<const uint8_t> data);
std::string format_timestamp(std::chrono::system_clock::time_point tp);

// Generate json-c objects
json_object *column_value_to_json_c(const row &r, size_t col_idx);
json_object *row_to_json_c(const row &r, const std::vector<std::string> &col_names);
json_object *result_set_to_json_c(result_set &res);

// Serialize a result_set rows array directly: [{"col": val, ...}, ...]
void result_set_to_json(stream_sink &sink, result_set &res);
void result_set_to_json(FCGX_Stream *out, result_set &res);
void result_set_to_json(std::ostream &out, result_set &res);
std::string result_set_to_json_string(result_set &res);

// ==================== Query Options ====================

struct sql_query {
    std::string count_sql;
    std::string data_sql;
    int page = 1;
    int page_size = 25;
    std::string callback;
    bool auto_bind_pagination = true;
};

// ==================== Direct SQL to JSON ====================

void execute_sql_to_json_sink(stream_sink &sink, database &db, std::string_view sql, statement &stmt);

template <typename... Args>
void sql_to_json(stream_sink &sink, database &db, std::string_view sql, Args&&... args) {
    auto stmt = db.prepare(sql);
    if constexpr (sizeof...(Args) > 0) {
        stmt->bind_all(std::forward<Args>(args)...);
    }
    execute_sql_to_json_sink(sink, db, sql, *stmt);
}

template <typename... Args>
void sql_to_json(FCGX_Stream *out, database &db, std::string_view sql, Args&&... args) {
    fcgx_sink sink(out);
    sql_to_json(sink, db, sql, std::forward<Args>(args)...);
}

template <typename... Args>
void sql_to_json(std::ostream &out, database &db, std::string_view sql, Args&&... args) {
    ostream_sink sink(out);
    sql_to_json(sink, db, sql, std::forward<Args>(args)...);
}

template <typename... Args>
std::string sql_to_json_string(database &db, std::string_view sql, Args&&... args) {
    std::string result;
    string_sink sink(result);
    sql_to_json(sink, db, sql, std::forward<Args>(args)...);
    return result;
}

template <typename... Args>
json_object *sql_to_json_object(database &db, std::string_view sql, Args&&... args) {
    auto stmt = db.prepare(sql);
    if constexpr (sizeof...(Args) > 0) {
        stmt->bind_all(std::forward<Args>(args)...);
    }
    auto res = stmt->query();
    if (res) {
        return result_set_to_json_c(*res);
    }
    return json_object_new_array();
}

// ==================== Paginated SQL to JSON ====================

void execute_sql_to_json_paginated_sink(
    stream_sink &sink,
    database &db,
    const sql_query &query,
    statement &data_stmt
);

json_object *execute_sql_to_json_paginated_c(
    database &db,
    const sql_query &query,
    statement &data_stmt
);

template <typename... Args>
void sql_to_json_paginated(stream_sink &sink, database &db, const sql_query &query, Args&&... args) {
    if (!query.callback.empty() && !is_valid_callback(query.callback)) {
        throw db_exception("Invalid callback parameter");
    }

    auto data_stmt = db.prepare(query.data_sql);
    if constexpr (sizeof...(Args) > 0) {
        if (query.auto_bind_pagination) {
            int page = std::max(1, query.page);
            int page_size = std::max(1, query.page_size);
            int64_t offset = static_cast<int64_t>(page - 1) * page_size;
            data_stmt->bind_all(std::forward<Args>(args)..., page_size, offset);
        } else {
            data_stmt->bind_all(std::forward<Args>(args)...);
        }
    } else {
        if (query.auto_bind_pagination) {
            int page = std::max(1, query.page);
            int page_size = std::max(1, query.page_size);
            int64_t offset = static_cast<int64_t>(page - 1) * page_size;
            data_stmt->bind_all(page_size, offset);
        }
    }

    execute_sql_to_json_paginated_sink(sink, db, query, *data_stmt);
}

template <typename... Args>
void sql_to_json_paginated(FCGX_Stream *out, database &db, const sql_query &query, Args&&... args) {
    fcgx_sink sink(out);
    sql_to_json_paginated(sink, db, query, std::forward<Args>(args)...);
}

template <typename... Args>
void sql_to_json_paginated(std::ostream &out, database &db, const sql_query &query, Args&&... args) {
    ostream_sink sink(out);
    sql_to_json_paginated(sink, db, query, std::forward<Args>(args)...);
}

template <typename... Args>
std::string sql_to_json_paginated_string(database &db, const sql_query &query, Args&&... args) {
    std::string result;
    string_sink sink(result);
    sql_to_json_paginated(sink, db, query, std::forward<Args>(args)...);
    return result;
}

template <typename... Args>
json_object *sql_to_json_paginated_object(database &db, const sql_query &query, Args&&... args) {
    auto data_stmt = db.prepare(query.data_sql);
    if constexpr (sizeof...(Args) > 0) {
        if (query.auto_bind_pagination) {
            int page = std::max(1, query.page);
            int page_size = std::max(1, query.page_size);
            int64_t offset = static_cast<int64_t>(page - 1) * page_size;
            data_stmt->bind_all(std::forward<Args>(args)..., page_size, offset);
        } else {
            data_stmt->bind_all(std::forward<Args>(args)...);
        }
    } else {
        if (query.auto_bind_pagination) {
            int page = std::max(1, query.page);
            int page_size = std::max(1, query.page_size);
            int64_t offset = static_cast<int64_t>(page - 1) * page_size;
            data_stmt->bind_all(page_size, offset);
        }
    }

    return execute_sql_to_json_paginated_c(db, query, *data_stmt);
}

} // namespace backend::db
