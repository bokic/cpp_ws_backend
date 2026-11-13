#pragma once

#include "db/db.h"
#include <string>
#include <string_view>
#include <memory>
#include <stdexcept>

namespace db {

using statement_cache = backend::db::statement_cache;
using sqlite_stmt = backend::db::sqlite_statement;

class sqlite_db : public backend::db::sqlite_database {
public:
    using backend::db::sqlite_database::sqlite_database;

    sqlite_stmt prepare(const std::string &sql) const {
        if (!get()) throw std::runtime_error("Database connection is closed");
        return prepare_stmt(sql);
    }

    sqlite_stmt prepare(std::string_view sql) const {
        if (!get()) throw std::runtime_error("Database connection is closed");
        return prepare_stmt(sql);
    }

    sqlite_stmt prepare(const char *sql) const {
        if (!get()) throw std::runtime_error("Database connection is closed");
        return prepare_stmt(sql);
    }
};

class sqlite_transaction {
public:
    explicit sqlite_transaction(sqlite_db &db, bool immediate = true) : m_db(db) {
        m_db.exec(immediate ? "BEGIN IMMEDIATE;" : "BEGIN;");
    }

    template <typename ConnectionType>
    explicit sqlite_transaction(backend::db::pooled_connection<ConnectionType> &conn, bool immediate = true)
        : sqlite_transaction(*conn, immediate) {}

    ~sqlite_transaction() {
        if (!m_committed) {
            try {
                m_db.exec("ROLLBACK;");
            } catch (...) {
            }
        }
    }

    sqlite_transaction(const sqlite_transaction &) = delete;
    sqlite_transaction &operator=(const sqlite_transaction &) = delete;

    void commit() {
        if (!m_committed) {
            m_db.exec("COMMIT;");
            m_committed = true;
        }
    }

    void rollback() {
        if (!m_committed) {
            m_db.exec("ROLLBACK;");
            m_committed = true;
        }
    }

private:
    sqlite_db &m_db;
    bool m_committed = false;
};

using sqlite_tx = sqlite_transaction;

#ifndef SQLITE_OK
#define SQLITE_OK           0
#endif
#ifndef SQLITE_ROW
#define SQLITE_ROW          100
#endif
#ifndef SQLITE_DONE
#define SQLITE_DONE         101
#endif
#ifndef SQLITE_INTEGER
#define SQLITE_INTEGER      1
#endif
#ifndef SQLITE_FLOAT
#define SQLITE_FLOAT        2
#endif
#ifndef SQLITE_TEXT
#define SQLITE_TEXT         3
#endif
#ifndef SQLITE_BLOB
#define SQLITE_BLOB         4
#endif
#ifndef SQLITE_NULL
#define SQLITE_NULL         5
#endif

} // namespace db
