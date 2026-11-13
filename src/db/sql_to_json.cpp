#include "sql_to_json.h"
#include <iomanip>
#include <ctime>
#include <cmath>
#include <cstring>

namespace backend::db {

// ==================== JSONP Callback Validator ====================

bool is_valid_callback(std::string_view callback) {
    if (callback.empty() || callback.size() > 128) {
        return false;
    }

    size_t start = 0;
    while (start < callback.size()) {
        size_t dot_pos = callback.find('.', start);
        if (dot_pos == std::string_view::npos) {
            dot_pos = callback.size();
        }

        std::string_view segment = callback.substr(start, dot_pos - start);
        if (segment.empty()) {
            return false;
        }

        char first = segment[0];
        if (!((first >= 'a' && first <= 'z') ||
              (first >= 'A' && first <= 'Z') ||
              first == '_' || first == '$')) {
            return false;
        }

        for (size_t i = 1; i < segment.size(); ++i) {
            char c = segment[i];
            if (!((c >= 'a' && c <= 'z') ||
                  (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') ||
                  c == '_' || c == '$')) {
                return false;
            }
        }

        start = dot_pos + 1;
    }

    return callback.back() != '.';
}

// ==================== Base64 & Timestamp Formatting ====================

std::string to_base64(std::span<const uint8_t> data) {
    static constexpr char b64_chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string out;
    size_t len = data.size();
    out.reserve(((len + 2) / 3) * 4);

    size_t i = 0;
    while (i + 2 < len) {
        uint32_t triple = (static_cast<uint32_t>(data[i]) << 16) |
                          (static_cast<uint32_t>(data[i + 1]) << 8) |
                          static_cast<uint32_t>(data[i + 2]);
        out.push_back(b64_chars[(triple >> 18) & 0x3F]);
        out.push_back(b64_chars[(triple >> 12) & 0x3F]);
        out.push_back(b64_chars[(triple >> 6) & 0x3F]);
        out.push_back(b64_chars[triple & 0x3F]);
        i += 3;
    }

    if (i < len) {
        if (len - i == 1) {
            uint32_t triple = static_cast<uint32_t>(data[i]) << 16;
            out.push_back(b64_chars[(triple >> 18) & 0x3F]);
            out.push_back(b64_chars[(triple >> 12) & 0x3F]);
            out.push_back('=');
            out.push_back('=');
        } else if (len - i == 2) {
            uint32_t triple = (static_cast<uint32_t>(data[i]) << 16) |
                              (static_cast<uint32_t>(data[i + 1]) << 8);
            out.push_back(b64_chars[(triple >> 18) & 0x3F]);
            out.push_back(b64_chars[(triple >> 12) & 0x3F]);
            out.push_back(b64_chars[(triple >> 6) & 0x3F]);
            out.push_back('=');
        }
    }

    return out;
}

std::string format_timestamp(std::chrono::system_clock::time_point tp) {
    auto time_t_val = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_buf{};
    gmtime_r(&time_t_val, &tm_buf);

    char buf[32];
    size_t n = std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    if (n > 0) {
        return std::string(buf, n);
    }
    return {};
}

// ==================== json-c Object Builders ====================

json_object *column_value_to_json_c(const row &r, size_t col_idx) {
    if (r.is_null(col_idx)) {
        return json_object_new_null();
    }

    switch (r.get_column_type(col_idx)) {
        case column_type::null_value:
            return json_object_new_null();

        case column_type::integer:
            return json_object_new_int64(r.get_int64(col_idx));

        case column_type::floating_point: {
            double d = r.get_double(col_idx);
            if (std::isnan(d) || std::isinf(d)) {
                return json_object_new_null();
            }
            return json_object_new_double(d);
        }

        case column_type::blob: {
            std::string b64 = to_base64(r.get_blob(col_idx));
            return json_object_new_string_len(b64.data(), static_cast<int>(b64.size()));
        }

        case column_type::timestamp: {
            std::string ts = format_timestamp(r.get_timestamp(col_idx));
            return json_object_new_string_len(ts.data(), static_cast<int>(ts.size()));
        }

        case column_type::text:
        default: {
            std::string s = r.get_string(col_idx);
            return json_object_new_string_len(s.data(), static_cast<int>(s.size()));
        }
    }
}

json_object *row_to_json_c(const row &r, const std::vector<std::string> &col_names) {
    json_object *row_obj = json_object_new_object();
    size_t col_count = col_names.size();
    for (size_t c = 0; c < col_count; ++c) {
        json_object *val = column_value_to_json_c(r, c);
        json_object_object_add(row_obj, col_names[c].c_str(), val);
    }
    return row_obj;
}

json_object *result_set_to_json_c(result_set &res) {
    json_object *arr = json_object_new_array();
    size_t col_count = res.column_count();
    std::vector<std::string> col_names;
    col_names.reserve(col_count);
    for (size_t c = 0; c < col_count; ++c) {
        col_names.emplace_back(res.column_name(c));
    }

    while (res.next()) {
        json_object *row_obj = row_to_json_c(res.current_row(), col_names);
        json_object_array_add(arr, row_obj);
    }

    return arr;
}

// ==================== Result Set Serialization ====================

void result_set_to_json(stream_sink &sink, result_set &res) {
    sink.put('[');
    bool first_row = true;
    size_t col_count = res.column_count();

    std::vector<std::string> col_names;
    col_names.reserve(col_count);
    for (size_t c = 0; c < col_count; ++c) {
        col_names.emplace_back(res.column_name(c));
    }

    while (res.next()) {
        if (!first_row) {
            sink.put(',');
        }
        first_row = false;

        json_object_guard row_obj(row_to_json_c(res.current_row(), col_names));
        const char *json_str = row_obj.to_string();
        if (json_str) {
            sink.write(json_str);
        }
    }

    sink.put(']');
}

void result_set_to_json(FCGX_Stream *out, result_set &res) {
    fcgx_sink sink(out);
    result_set_to_json(sink, res);
}

void result_set_to_json(std::ostream &out, result_set &res) {
    ostream_sink sink(out);
    result_set_to_json(sink, res);
}

std::string result_set_to_json_string(result_set &res) {
    std::string result;
    string_sink sink(result);
    result_set_to_json(sink, res);
    return result;
}

// ==================== Direct SQL to JSON Sink Execution ====================

void execute_sql_to_json_sink(stream_sink &sink, [[maybe_unused]] database &db, [[maybe_unused]] std::string_view sql, statement &stmt) {
    auto res = stmt.query();
    if (res) {
        result_set_to_json(sink, *res);
    } else {
        sink.write("[]");
    }
}

// ==================== Paginated SQL to JSON Sink Execution ====================

void execute_sql_to_json_paginated_sink(
    stream_sink &sink,
    database &db,
    const sql_query &query,
    statement &data_stmt
) {
    bool has_callback = !query.callback.empty();
    if (has_callback) {
        sink.write(query.callback);
        sink.put('(');
    }

    int64_t total_records = 0;
    if (!query.count_sql.empty()) {
        auto count_stmt = db.prepare(query.count_sql);
        auto count_res = count_stmt->query();
        if (count_res && count_res->next()) {
            total_records = count_res->current_row().get<int64_t>(0);
        }
    }

    int page = std::max(1, query.page);
    int page_size = std::max(1, query.page_size);
    int64_t total_pages = (total_records + page_size - 1) / page_size;

    // Use json-c to serialize pagination metadata envelope
    json_object_guard meta(json_object_new_object());
    json_object_object_add(meta.get(), "page", json_object_new_int(page));
    json_object_object_add(meta.get(), "page_size", json_object_new_int(page_size));
    json_object_object_add(meta.get(), "total_records", json_object_new_int64(total_records));
    json_object_object_add(meta.get(), "total_pages", json_object_new_int64(total_pages));

    std::string meta_str = meta.to_string();
    if (!meta_str.empty() && meta_str.back() == '}') {
        meta_str.pop_back(); // Remove trailing '}'
        sink.write(meta_str);
        sink.write(",\"rows\":");
    } else {
        sink.write("{\"rows\":");
    }

    auto data_res = data_stmt.query();
    if (data_res) {
        result_set_to_json(sink, *data_res);
    } else {
        sink.write("[]");
    }

    sink.put('}');

    if (has_callback) {
        sink.put(')');
    }
}

json_object *execute_sql_to_json_paginated_c(
    database &db,
    const sql_query &query,
    statement &data_stmt
) {
    int64_t total_records = 0;
    if (!query.count_sql.empty()) {
        auto count_stmt = db.prepare(query.count_sql);
        auto count_res = count_stmt->query();
        if (count_res && count_res->next()) {
            total_records = count_res->current_row().get<int64_t>(0);
        }
    }

    int page = std::max(1, query.page);
    int page_size = std::max(1, query.page_size);
    int64_t total_pages = (total_records + page_size - 1) / page_size;

    json_object *envelope = json_object_new_object();
    json_object_object_add(envelope, "page", json_object_new_int(page));
    json_object_object_add(envelope, "page_size", json_object_new_int(page_size));
    json_object_object_add(envelope, "total_records", json_object_new_int64(total_records));
    json_object_object_add(envelope, "total_pages", json_object_new_int64(total_pages));

    auto data_res = data_stmt.query();
    if (data_res) {
        json_object_object_add(envelope, "rows", result_set_to_json_c(*data_res));
    } else {
        json_object_object_add(envelope, "rows", json_object_new_array());
    }

    return envelope;
}

} // namespace backend::db
