#pragma once

#include "database.h"
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <span>
#include <chrono>

namespace backend::db {

struct pg_conn_holder;
struct pg_result_holder;

class postgres_row : public row {
public:
    postgres_row() = default;
    explicit postgres_row(std::shared_ptr<pg_result_holder> holder, int row_idx = -1);
    ~postgres_row() override = default;

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

    void set_row_index(int idx) noexcept { m_row_idx = idx; }

private:
    std::shared_ptr<pg_result_holder> m_holder;
    int m_row_idx = -1;
    mutable std::vector<std::string> m_cached_names;
    void ensure_column_names() const;
};

class postgres_result_set : public result_set {
public:
    explicit postgres_result_set(std::shared_ptr<pg_result_holder> holder);
    ~postgres_result_set() override = default;

    bool next() override;
    const row& current_row() const override;
    size_t column_count() const noexcept override;
    std::string_view column_name(size_t index) const override;
    size_t column_index(std::string_view name) const override;
    uint64_t affected_rows() const noexcept override;

private:
    std::shared_ptr<pg_result_holder> m_holder;
    postgres_row m_row;
    int m_current_tuple = -1;
    int m_total_tuples = 0;
};

class postgres_statement : public statement {
public:
    postgres_statement() = default;
    postgres_statement(std::shared_ptr<pg_conn_holder> conn, std::string_view sql);
    ~postgres_statement() override = default;

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
    int get_param_index(std::string_view name);
    std::shared_ptr<pg_conn_holder> m_conn;
    std::string m_translated_sql;
    std::vector<std::string> m_param_names;
    struct param_entry {
        bool is_null = true;
        std::string value;
    };
    std::vector<param_entry> m_params;
};

class postgres_database;

class postgres_transaction : public transaction {
public:
    explicit postgres_transaction(std::shared_ptr<pg_conn_holder> conn);
    ~postgres_transaction() override;

    postgres_transaction(const postgres_transaction &) = delete;
    postgres_transaction &operator=(const postgres_transaction &) = delete;

    postgres_transaction(postgres_transaction &&other) noexcept;
    postgres_transaction &operator=(postgres_transaction &&other) noexcept;

    void commit() override;
    void rollback() override;
    bool is_active() const noexcept override;

private:
    std::shared_ptr<pg_conn_holder> m_conn;
    bool m_active = false;
};

class postgres_database : public database {
public:
    postgres_database();
    explicit postgres_database(const std::string &conn_str);
    ~postgres_database() override;

    postgres_database(const postgres_database &) = delete;
    postgres_database &operator=(const postgres_database &) = delete;

    postgres_database(postgres_database &&other) noexcept;
    postgres_database &operator=(postgres_database &&other) noexcept;

    void connect(const std::string &conn_str) override;
    void disconnect() override;
    bool is_connected() const noexcept override;
    bool ping() noexcept override;

    void execute(std::string_view sql) override;
    std::unique_ptr<statement> prepare(std::string_view sql) override;
    std::unique_ptr<transaction> begin_transaction() override;

    std::shared_ptr<pg_conn_holder> get_holder() const { return m_conn; }

private:
    std::shared_ptr<pg_conn_holder> m_conn;
};

using postgres_db = postgres_database;

} // namespace backend::db
