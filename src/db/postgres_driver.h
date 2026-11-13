#pragma once

#include "database.h"
#include <string>
#include <vector>
#include <memory>

namespace backend::db {

class postgres_row : public row {
public:
    postgres_row();
    ~postgres_row() override;

    size_t column_count() const noexcept override;
    std::string_view column_name(size_t index) const override;
    size_t column_index(std::string_view name) const override;
    bool is_null(size_t index) const override;

    int32_t get_int32(size_t index) const override;
    int64_t get_int64(size_t index) const override;
    double get_double(size_t index) const override;
    std::string get_string(size_t index) const override;
    std::span<const uint8_t> get_blob(size_t index) const override;
    std::chrono::system_clock::time_point get_timestamp(size_t index) const override;
};

class postgres_result_set : public result_set {
public:
    postgres_result_set();
    ~postgres_result_set() override;

    bool next() override;
    const row& current_row() const override;
    size_t column_count() const noexcept override;
    std::string_view column_name(size_t index) const override;
    size_t column_index(std::string_view name) const override;
    uint64_t affected_rows() const noexcept override;

private:
    postgres_row m_row;
};

class postgres_statement : public statement {
public:
    postgres_statement();
    explicit postgres_statement(std::string_view sql);
    ~postgres_statement() override;

    // Positional binding
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
};

class postgres_transaction : public transaction {
public:
    postgres_transaction();
    ~postgres_transaction() override;

    void commit() override;
    void rollback() override;
    bool is_active() const noexcept override;

private:
    bool m_active{false};
};

class postgres_database : public database {
public:
    postgres_database();
    explicit postgres_database(const std::string &conn_str);
    ~postgres_database() override;

    void connect(const std::string &conn_str) override;
    void disconnect() override;
    bool is_connected() const noexcept override;
    bool ping() noexcept override;

    void execute(std::string_view sql) override;
    std::unique_ptr<statement> prepare(std::string_view sql) override;
    std::unique_ptr<transaction> begin_transaction() override;

private:
    bool m_connected{false};
};

using pg_database = postgres_database;
using postgres_db = postgres_database;

} // namespace backend::db
