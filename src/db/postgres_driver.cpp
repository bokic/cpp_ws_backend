#include "postgres_driver.h"

namespace backend::db {

// ==================== postgres_row ====================

postgres_row::postgres_row() = default;
postgres_row::~postgres_row() = default;

size_t postgres_row::column_count() const noexcept {
    return 0;
}

std::string_view postgres_row::column_name([[maybe_unused]] size_t index) const {
    return {};
}

size_t postgres_row::column_index([[maybe_unused]] std::string_view name) const {
    return 0;
}

bool postgres_row::is_null([[maybe_unused]] size_t index) const {
    return true;
}

int32_t postgres_row::get_int32([[maybe_unused]] size_t index) const {
    return 0;
}

int64_t postgres_row::get_int64([[maybe_unused]] size_t index) const {
    return 0;
}

double postgres_row::get_double([[maybe_unused]] size_t index) const {
    return 0.0;
}

std::string postgres_row::get_string([[maybe_unused]] size_t index) const {
    return {};
}

std::span<const uint8_t> postgres_row::get_blob([[maybe_unused]] size_t index) const {
    return {};
}

std::chrono::system_clock::time_point postgres_row::get_timestamp([[maybe_unused]] size_t index) const {
    return {};
}

// ==================== postgres_result_set ====================

postgres_result_set::postgres_result_set() = default;
postgres_result_set::~postgres_result_set() = default;

bool postgres_result_set::next() {
    return false;
}

const row& postgres_result_set::current_row() const {
    return m_row;
}

size_t postgres_result_set::column_count() const noexcept {
    return 0;
}

std::string_view postgres_result_set::column_name([[maybe_unused]] size_t index) const {
    return {};
}

size_t postgres_result_set::column_index([[maybe_unused]] std::string_view name) const {
    return 0;
}

uint64_t postgres_result_set::affected_rows() const noexcept {
    return 0;
}

// ==================== postgres_statement ====================

postgres_statement::postgres_statement() = default;
postgres_statement::postgres_statement([[maybe_unused]] std::string_view sql) {}
postgres_statement::~postgres_statement() = default;

void postgres_statement::bind([[maybe_unused]] size_t index, [[maybe_unused]] std::nullptr_t) {}
void postgres_statement::bind([[maybe_unused]] size_t index, [[maybe_unused]] int32_t val) {}
void postgres_statement::bind([[maybe_unused]] size_t index, [[maybe_unused]] int64_t val) {}
void postgres_statement::bind([[maybe_unused]] size_t index, [[maybe_unused]] double val) {}
void postgres_statement::bind([[maybe_unused]] size_t index, [[maybe_unused]] std::string_view val) {}
void postgres_statement::bind([[maybe_unused]] size_t index, [[maybe_unused]] std::span<const uint8_t> val) {}
void postgres_statement::bind([[maybe_unused]] size_t index, [[maybe_unused]] std::chrono::system_clock::time_point val) {}

void postgres_statement::bind([[maybe_unused]] std::string_view name, [[maybe_unused]] std::nullptr_t) {}
void postgres_statement::bind([[maybe_unused]] std::string_view name, [[maybe_unused]] int32_t val) {}
void postgres_statement::bind([[maybe_unused]] std::string_view name, [[maybe_unused]] int64_t val) {}
void postgres_statement::bind([[maybe_unused]] std::string_view name, [[maybe_unused]] double val) {}
void postgres_statement::bind([[maybe_unused]] std::string_view name, [[maybe_unused]] std::string_view val) {}
void postgres_statement::bind([[maybe_unused]] std::string_view name, [[maybe_unused]] std::span<const uint8_t> val) {}
void postgres_statement::bind([[maybe_unused]] std::string_view name, [[maybe_unused]] std::chrono::system_clock::time_point val) {}

void postgres_statement::execute() {}

std::unique_ptr<result_set> postgres_statement::query() {
    return std::make_unique<postgres_result_set>();
}

void postgres_statement::reset() {}

// ==================== postgres_transaction ====================

postgres_transaction::postgres_transaction() : m_active(true) {}
postgres_transaction::~postgres_transaction() {
    if (m_active) {
        rollback();
    }
}

void postgres_transaction::commit() {
    m_active = false;
}

void postgres_transaction::rollback() {
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

void postgres_database::connect([[maybe_unused]] const std::string &conn_str) {
    m_connected = true;
}

void postgres_database::disconnect() {
    m_connected = false;
}

bool postgres_database::is_connected() const noexcept {
    return m_connected;
}

bool postgres_database::ping() noexcept {
    return m_connected;
}

void postgres_database::execute([[maybe_unused]] std::string_view sql) {}

std::unique_ptr<statement> postgres_database::prepare(std::string_view sql) {
    return std::make_unique<postgres_statement>(sql);
}

std::unique_ptr<transaction> postgres_database::begin_transaction() {
    return std::make_unique<postgres_transaction>();
}

} // namespace backend::db
