#include "mysql_driver.h"

namespace backend::db {

// ==================== mysql_row ====================

mysql_row::mysql_row() = default;
mysql_row::~mysql_row() = default;

size_t mysql_row::column_count() const noexcept {
    return 0;
}

std::string_view mysql_row::column_name([[maybe_unused]] size_t index) const {
    return {};
}

size_t mysql_row::column_index([[maybe_unused]] std::string_view name) const {
    return 0;
}

bool mysql_row::is_null([[maybe_unused]] size_t index) const {
    return true;
}

int32_t mysql_row::get_int32([[maybe_unused]] size_t index) const {
    return 0;
}

int64_t mysql_row::get_int64([[maybe_unused]] size_t index) const {
    return 0;
}

double mysql_row::get_double([[maybe_unused]] size_t index) const {
    return 0.0;
}

std::string mysql_row::get_string([[maybe_unused]] size_t index) const {
    return {};
}

std::span<const uint8_t> mysql_row::get_blob([[maybe_unused]] size_t index) const {
    return {};
}

std::chrono::system_clock::time_point mysql_row::get_timestamp([[maybe_unused]] size_t index) const {
    return {};
}

// ==================== mysql_result_set ====================

mysql_result_set::mysql_result_set() = default;
mysql_result_set::~mysql_result_set() = default;

bool mysql_result_set::next() {
    return false;
}

const row& mysql_result_set::current_row() const {
    return m_row;
}

size_t mysql_result_set::column_count() const noexcept {
    return 0;
}

std::string_view mysql_result_set::column_name([[maybe_unused]] size_t index) const {
    return {};
}

size_t mysql_result_set::column_index([[maybe_unused]] std::string_view name) const {
    return 0;
}

uint64_t mysql_result_set::affected_rows() const noexcept {
    return 0;
}

// ==================== mysql_statement ====================

mysql_statement::mysql_statement() = default;
mysql_statement::mysql_statement([[maybe_unused]] std::string_view sql) {}
mysql_statement::~mysql_statement() = default;

void mysql_statement::bind([[maybe_unused]] size_t index, [[maybe_unused]] std::nullptr_t) {}
void mysql_statement::bind([[maybe_unused]] size_t index, [[maybe_unused]] int32_t val) {}
void mysql_statement::bind([[maybe_unused]] size_t index, [[maybe_unused]] int64_t val) {}
void mysql_statement::bind([[maybe_unused]] size_t index, [[maybe_unused]] double val) {}
void mysql_statement::bind([[maybe_unused]] size_t index, [[maybe_unused]] std::string_view val) {}
void mysql_statement::bind([[maybe_unused]] size_t index, [[maybe_unused]] std::span<const uint8_t> val) {}
void mysql_statement::bind([[maybe_unused]] size_t index, [[maybe_unused]] std::chrono::system_clock::time_point val) {}

void mysql_statement::bind([[maybe_unused]] std::string_view name, [[maybe_unused]] std::nullptr_t) {}
void mysql_statement::bind([[maybe_unused]] std::string_view name, [[maybe_unused]] int32_t val) {}
void mysql_statement::bind([[maybe_unused]] std::string_view name, [[maybe_unused]] int64_t val) {}
void mysql_statement::bind([[maybe_unused]] std::string_view name, [[maybe_unused]] double val) {}
void mysql_statement::bind([[maybe_unused]] std::string_view name, [[maybe_unused]] std::string_view val) {}
void mysql_statement::bind([[maybe_unused]] std::string_view name, [[maybe_unused]] std::span<const uint8_t> val) {}
void mysql_statement::bind([[maybe_unused]] std::string_view name, [[maybe_unused]] std::chrono::system_clock::time_point val) {}

void mysql_statement::execute() {}

std::unique_ptr<result_set> mysql_statement::query() {
    return std::make_unique<mysql_result_set>();
}

void mysql_statement::reset() {}

// ==================== mysql_transaction ====================

mysql_transaction::mysql_transaction() : m_active(true) {}
mysql_transaction::~mysql_transaction() {
    if (m_active) {
        rollback();
    }
}

void mysql_transaction::commit() {
    m_active = false;
}

void mysql_transaction::rollback() {
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

void mysql_database::connect([[maybe_unused]] const std::string &conn_str) {
    m_connected = true;
}

void mysql_database::disconnect() {
    m_connected = false;
}

bool mysql_database::is_connected() const noexcept {
    return m_connected;
}

bool mysql_database::ping() noexcept {
    return m_connected;
}

void mysql_database::execute([[maybe_unused]] std::string_view sql) {}

std::unique_ptr<statement> mysql_database::prepare(std::string_view sql) {
    return std::make_unique<mysql_statement>(sql);
}

std::unique_ptr<transaction> mysql_database::begin_transaction() {
    return std::make_unique<mysql_transaction>();
}

} // namespace backend::db
