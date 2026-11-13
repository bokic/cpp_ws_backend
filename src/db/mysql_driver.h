#pragma once

#include "database.h"
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <span>
#include <chrono>

namespace backend::db {

struct mysql_conn_holder;
struct mysql_result_holder;

class mysql_row : public row {
public:
    mysql_row() = default;
    explicit mysql_row(std::shared_ptr<mysql_result_holder> holder);
    ~mysql_row() override = default;

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

    void update_row_data(char **row_data, unsigned long *lengths) noexcept {
        m_row_data = row_data;
        m_lengths = lengths;
    }

private:
    std::shared_ptr<mysql_result_holder> m_holder;
    char **m_row_data = nullptr;
    unsigned long *m_lengths = nullptr;
    mutable std::vector<std::string> m_cached_names;
    void ensure_column_names() const;
};

class mysql_result_set : public result_set {
public:
    explicit mysql_result_set(std::shared_ptr<mysql_result_holder> holder);
    ~mysql_result_set() override = default;

    bool next() override;
    const row& current_row() const override;
    size_t column_count() const noexcept override;
    std::string_view column_name(size_t index) const override;
    size_t column_index(std::string_view name) const override;
    uint64_t affected_rows() const noexcept override;

private:
    std::shared_ptr<mysql_result_holder> m_holder;
    mysql_row m_row;
};

class mysql_statement : public statement {
public:
    mysql_statement() = default;
    mysql_statement(std::shared_ptr<mysql_conn_holder> conn, std::string_view sql);
    ~mysql_statement() override = default;

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

private:
    std::string build_final_sql();
    int get_param_index(std::string_view name);

    std::shared_ptr<mysql_conn_holder> m_conn;
    struct sql_chunk {
        std::string text;
        int param_idx = -1;
    };
    std::vector<sql_chunk> m_chunks;
    std::vector<std::string> m_param_names;
    struct bound_value {
        bool is_null = true;
        std::string escaped_literal = "NULL";
    };
    std::vector<bound_value> m_bound;
};

class mysql_database;

class mysql_transaction : public transaction {
public:
    explicit mysql_transaction(std::shared_ptr<mysql_conn_holder> conn);
    ~mysql_transaction() override;

    mysql_transaction(const mysql_transaction &) = delete;
    mysql_transaction &operator=(const mysql_transaction &) = delete;

    mysql_transaction(mysql_transaction &&other) noexcept;
    mysql_transaction &operator=(mysql_transaction &&other) noexcept;

    void commit() override;
    void rollback() override;
    bool is_active() const noexcept override;

private:
    std::shared_ptr<mysql_conn_holder> m_conn;
    bool m_active = false;
};

class mysql_database : public database {
public:
    mysql_database();
    explicit mysql_database(const std::string &conn_str);
    ~mysql_database() override;

    mysql_database(const mysql_database &) = delete;
    mysql_database &operator=(const mysql_database &) = delete;

    mysql_database(mysql_database &&other) noexcept;
    mysql_database &operator=(mysql_database &&other) noexcept;

    void connect(const std::string &conn_str) override;
    void disconnect() override;
    bool is_connected() const noexcept override;
    bool ping() noexcept override;

    void execute(std::string_view sql) override;
    std::unique_ptr<statement> prepare(std::string_view sql) override;
    std::unique_ptr<transaction> begin_transaction() override;

    std::shared_ptr<mysql_conn_holder> get_holder() const { return m_conn; }

private:
    std::shared_ptr<mysql_conn_holder> m_conn;
};

using mysql_db = mysql_database;

} // namespace backend::db
