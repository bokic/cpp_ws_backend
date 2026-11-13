#include "sqlite_driver.h"

namespace backend::db {

// ==================== sqlite_row ====================

sqlite_row::sqlite_row() = default;
sqlite_row::~sqlite_row() = default;

size_t sqlite_row::column_count() const noexcept {
    return 0;
}

std::string_view sqlite_row::column_name([[maybe_unused]] size_t index) const {
    return {};
}

size_t sqlite_row::column_index([[maybe_unused]] std::string_view name) const {
    return 0;
}

bool sqlite_row::is_null([[maybe_unused]] size_t index) const {
    return true;
}

int32_t sqlite_row::get_int32([[maybe_unused]] size_t index) const {
    return 0;
}

int64_t sqlite_row::get_int64([[maybe_unused]] size_t index) const {
    return 0;
}

double sqlite_row::get_double([[maybe_unused]] size_t index) const {
    return 0.0;
}

std::string sqlite_row::get_string([[maybe_unused]] size_t index) const {
    return {};
}

std::span<const uint8_t> sqlite_row::get_blob([[maybe_unused]] size_t index) const {
    return {};
}

std::chrono::system_clock::time_point sqlite_row::get_timestamp([[maybe_unused]] size_t index) const {
    return {};
}

// ==================== sqlite_result_set ====================

sqlite_result_set::sqlite_result_set() = default;
sqlite_result_set::~sqlite_result_set() = default;

bool sqlite_result_set::next() {
    return false;
}

const row& sqlite_result_set::current_row() const {
    return m_row;
}

size_t sqlite_result_set::column_count() const noexcept {
    return 0;
}

std::string_view sqlite_result_set::column_name([[maybe_unused]] size_t index) const {
    return {};
}

size_t sqlite_result_set::column_index([[maybe_unused]] std::string_view name) const {
    return 0;
}

uint64_t sqlite_result_set::affected_rows() const noexcept {
    return 0;
}

// ==================== sqlite_statement ====================

sqlite_statement::sqlite_statement() = default;
sqlite_statement::sqlite_statement([[maybe_unused]] std::string_view sql) {}
sqlite_statement::~sqlite_statement() = default;

void sqlite_statement::bind([[maybe_unused]] size_t index, [[maybe_unused]] std::nullptr_t) {}
void sqlite_statement::bind([[maybe_unused]] size_t index, [[maybe_unused]] int32_t val) {}
void sqlite_statement::bind([[maybe_unused]] size_t index, [[maybe_unused]] int64_t val) {}
void sqlite_statement::bind([[maybe_unused]] size_t index, [[maybe_unused]] double val) {}
void sqlite_statement::bind([[maybe_unused]] size_t index, [[maybe_unused]] std::string_view val) {}
void sqlite_statement::bind([[maybe_unused]] size_t index, [[maybe_unused]] std::span<const uint8_t> val) {}
void sqlite_statement::bind([[maybe_unused]] size_t index, [[maybe_unused]] std::chrono::system_clock::time_point val) {}

void sqlite_statement::bind([[maybe_unused]] std::string_view name, [[maybe_unused]] std::nullptr_t) {}
void sqlite_statement::bind([[maybe_unused]] std::string_view name, [[maybe_unused]] int32_t val) {}
void sqlite_statement::bind([[maybe_unused]] std::string_view name, [[maybe_unused]] int64_t val) {}
void sqlite_statement::bind([[maybe_unused]] std::string_view name, [[maybe_unused]] double val) {}
void sqlite_statement::bind([[maybe_unused]] std::string_view name, [[maybe_unused]] std::string_view val) {}
void sqlite_statement::bind([[maybe_unused]] std::string_view name, [[maybe_unused]] std::span<const uint8_t> val) {}
void sqlite_statement::bind([[maybe_unused]] std::string_view name, [[maybe_unused]] std::chrono::system_clock::time_point val) {}

void sqlite_statement::execute() {}

std::unique_ptr<result_set> sqlite_statement::query() {
    return std::make_unique<sqlite_result_set>();
}

void sqlite_statement::reset() {}

// ==================== sqlite_transaction ====================

sqlite_transaction::sqlite_transaction() : m_active(true) {}
sqlite_transaction::~sqlite_transaction() {
    if (m_active) {
        rollback();
    }
}

void sqlite_transaction::commit() {
    m_active = false;
}

void sqlite_transaction::rollback() {
    m_active = false;
}

bool sqlite_transaction::is_active() const noexcept {
    return m_active;
}

// ==================== sqlite_database ====================

sqlite_database::sqlite_database() = default;
sqlite_database::sqlite_database(const std::string &conn_str) {
    connect(conn_str);
}

sqlite_database::~sqlite_database() {
    disconnect();
}

void sqlite_database::connect([[maybe_unused]] const std::string &conn_str) {
    m_connected = true;
}

void sqlite_database::disconnect() {
    m_connected = false;
}

bool sqlite_database::is_connected() const noexcept {
    return m_connected;
}

bool sqlite_database::ping() noexcept {
    return m_connected;
}

void sqlite_database::execute([[maybe_unused]] std::string_view sql) {}

std::unique_ptr<statement> sqlite_database::prepare(std::string_view sql) {
    return std::make_unique<sqlite_statement>(sql);
}

std::unique_ptr<transaction> sqlite_database::begin_transaction() {
    return std::make_unique<sqlite_transaction>();
}

} // namespace backend::db
