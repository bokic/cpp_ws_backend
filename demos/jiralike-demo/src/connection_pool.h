#pragma once

#include "raii_sqlite.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace db {

struct pool_options {
    size_t min_size = 4;
    size_t max_size = 32;
    std::chrono::milliseconds acquire_timeout{5000};
    std::chrono::milliseconds idle_timeout{60000};
    std::chrono::milliseconds max_lifetime{0}; // 0 = unlimited lifetime
    bool ping_on_acquire = true;
};

struct pool_stats {
    size_t total_connections = 0;
    size_t idle_connections = 0;
    size_t active_leases = 0;
    size_t min_size = 0;
    size_t max_size = 0;
    uint64_t total_acquisitions = 0;
    uint64_t total_timeouts = 0;
    uint64_t total_health_failures = 0;
    uint64_t total_stmt_cache_hits = 0;
    uint64_t total_stmt_cache_misses = 0;
    size_t total_cached_statements = 0;
    bool is_shutdown = false;
};

template <typename Connection>
class connection_pool;

// RAII move-only lease guard returned by connection_pool<Connection>::acquire().
// Automatically resets connection state and returns the handle to the pool upon destruction.
template <typename Connection = sqlite_db>
class pooled_connection {
public:
    using clock_type = std::chrono::steady_clock;
    using time_point = clock_type::time_point;

    pooled_connection() noexcept = default;

    pooled_connection(std::shared_ptr<connection_pool<Connection>> pool,
                      std::unique_ptr<Connection> conn,
                      time_point created_at) noexcept
        : m_pool(std::move(pool)),
          m_conn(std::move(conn)),
          m_created_at(created_at) {}

    ~pooled_connection() {
        release();
    }

    pooled_connection(const pooled_connection &) = delete;
    pooled_connection &operator=(const pooled_connection &) = delete;

    pooled_connection(pooled_connection &&other) noexcept
        : m_pool(std::move(other.m_pool)),
          m_conn(std::move(other.m_conn)),
          m_created_at(other.m_created_at) {}

    pooled_connection &operator=(pooled_connection &&other) noexcept {
        if (this != &other) {
            release();
            m_pool = std::move(other.m_pool);
            m_conn = std::move(other.m_conn);
            m_created_at = other.m_created_at;
        }
        return *this;
    }

    void release() noexcept;

    Connection *get() noexcept { return m_conn.get(); }
    const Connection *get() const noexcept { return m_conn.get(); }

    Connection *operator->() noexcept { return m_conn.get(); }
    const Connection *operator->() const noexcept { return m_conn.get(); }

    Connection &operator*() noexcept { return *m_conn; }
    const Connection &operator*() const noexcept { return *m_conn; }

    // Implicit conversion to Connection& so pooled_connection can be passed directly
    // to helpers expecting `sqlite_db &` (e.g., `sqlite_transaction tx(conn)`).
    operator Connection &() noexcept { return *m_conn; }
    operator const Connection &() const noexcept { return *m_conn; }

    explicit operator bool() const noexcept { return m_conn != nullptr && static_cast<bool>(*m_conn); }

    // Direct forwarding helpers for ergonomic dot-syntax (`conn.prepare(...)`, `conn.exec(...)`)
    sqlite_stmt prepare(const std::string &sql) const { return m_conn->prepare(sql); }
    void exec(const std::string &sql) const { m_conn->exec(sql); }
    int64_t last_insert_rowid() const noexcept { return m_conn->last_insert_rowid(); }
    int changes() const noexcept { return m_conn->changes(); }
    const char *errmsg() const noexcept { return m_conn->errmsg(); }
    const std::string &path() const noexcept { return m_conn->path(); }
    bool ping() const noexcept { return m_conn && m_conn->ping(); }

private:
    std::shared_ptr<connection_pool<Connection>> m_pool;
    std::unique_ptr<Connection> m_conn;
    time_point m_created_at{};
};

template <typename Connection = sqlite_db>
class connection_pool : public std::enable_shared_from_this<connection_pool<Connection>> {
public:
    using clock_type = std::chrono::steady_clock;
    using time_point = clock_type::time_point;
    using factory_type = std::function<std::unique_ptr<Connection>()>;

    connection_pool(factory_type factory, pool_options options = {})
        : m_factory(std::move(factory)), m_options(options) {
        if (!m_factory) {
            throw std::invalid_argument("connection_pool requires a valid connection factory");
        }
        if (m_options.max_size == 0) {
            m_options.max_size = 1;
        }
        if (m_options.min_size > m_options.max_size) {
            m_options.min_size = m_options.max_size;
        }

        // Pre-warm minimum pool capacity
        for (size_t i = 0; i < m_options.min_size; ++i) {
            auto conn = m_factory();
            if (conn) {
                auto now = clock_type::now();
                m_live_connections.insert(conn.get());
                m_idle.push_back(idle_entry{std::move(conn), now, now});
                ++m_total_connections;
            }
        }
    }

    ~connection_pool() {
        shutdown();
    }

    connection_pool(const connection_pool &) = delete;
    connection_pool &operator=(const connection_pool &) = delete;

    // Acquire a leased connection using the pool's configured default timeout
    pooled_connection<Connection> acquire() {
        return acquire(m_options.acquire_timeout);
    }

    // Acquire a leased connection with an explicit timeout bound
    pooled_connection<Connection> acquire(std::chrono::milliseconds timeout) {
        auto deadline = clock_type::now() + timeout;
        std::unique_ptr<Connection> conn;
        time_point created_at{};

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            while (true) {
                if (m_shutdown) {
                    throw std::runtime_error("Cannot acquire database connection: pool is shut down");
                }

                evict_stale_locked(clock_type::now());

                // 1. Reuse an idle connection if available (LIFO from front for hot cache locality)
                if (!m_idle.empty()) {
                    auto entry = std::move(m_idle.front());
                    m_idle.pop_front();
                    conn = std::move(entry.conn);
                    created_at = entry.created_at;
                    ++m_active_leases;
                    break;
                }

                // 2. Allocate a new connection if below max_size
                if (m_total_connections < m_options.max_size) {
                    ++m_total_connections;
                    ++m_active_leases;
                    lock.unlock();
                    try {
                        conn = m_factory();
                        created_at = clock_type::now();
                    } catch (...) {
                        lock.lock();
                        --m_total_connections;
                        --m_active_leases;
                        if (m_active_leases == 0 && m_shutdown) {
                            m_shutdown_cv.notify_all();
                        }
                        m_cv.notify_one();
                        throw;
                    }
                    lock.lock();
                    m_live_connections.insert(conn.get());
                    break;
                }

                // 3. Wait for an active lease to be returned
                if (m_cv.wait_until(lock, deadline) == std::cv_status::timeout) {
                    m_total_timeouts.fetch_add(1, std::memory_order_relaxed);
                    throw std::runtime_error("Timed out waiting to acquire database connection from pool");
                }
            }
        }

        // Perform connection reset & health check outside the pool mutex
        try {
            conn->reset_for_request();
            if (m_options.ping_on_acquire && !conn->ping()) {
                m_total_health_failures.fetch_add(1, std::memory_order_relaxed);
                auto replacement = m_factory();
                replacement->reset_for_request();
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    retire_connection_locked(conn.get());
                    m_live_connections.insert(replacement.get());
                }
                conn = std::move(replacement);
                created_at = clock_type::now();
            }
        } catch (...) {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (conn) {
                retire_connection_locked(conn.get());
            }
            --m_total_connections;
            --m_active_leases;
            if (m_active_leases == 0 && m_shutdown) {
                m_shutdown_cv.notify_all();
            }
            m_cv.notify_one();
            throw;
        }

        m_total_acquisitions.fetch_add(1, std::memory_order_relaxed);
        return pooled_connection<Connection>(this->shared_from_this(), std::move(conn), created_at);
    }

    // Return a leased connection back to the pool
    void release(std::unique_ptr<Connection> conn, time_point created_at) noexcept {
        if (!conn) return;

        bool healthy = true;
        try {
            conn->reset_for_request();
        } catch (...) {
            healthy = false;
        }

        auto now = clock_type::now();
        bool lifetime_expired = (m_options.max_lifetime.count() > 0) &&
                                ((now - created_at) >= m_options.max_lifetime);

        std::unique_lock<std::mutex> lock(m_mutex);
        --m_active_leases;

        if (m_shutdown || !healthy || lifetime_expired) {
            retire_connection_locked(conn.get());
            --m_total_connections;
            lock.unlock();
            conn.reset();
            lock.lock();
        } else {
            m_idle.push_front(idle_entry{std::move(conn), created_at, now});
        }

        if (m_shutdown && m_active_leases == 0) {
            m_shutdown_cv.notify_all();
        } else {
            m_cv.notify_one();
        }
    }

    // Gracefully drain active leases and close all pooled database connections
    void shutdown() noexcept {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_shutdown) return;
        m_shutdown = true;

        // Close all currently idle connections
        while (!m_idle.empty()) {
            auto entry = std::move(m_idle.front());
            m_idle.pop_front();
            if (entry.conn) {
                retire_connection_locked(entry.conn.get());
                entry.conn->close();
            }
            --m_total_connections;
        }

        m_cv.notify_all();

        // Wait for any in-flight leased connections to be returned
        m_shutdown_cv.wait_for(lock, std::chrono::seconds(10), [this]() {
            return m_active_leases == 0;
        });
    }

    pool_stats stats() const noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        pool_stats s;
        s.total_connections = m_total_connections;
        s.idle_connections = m_idle.size();
        s.active_leases = m_active_leases;
        s.min_size = m_options.min_size;
        s.max_size = m_options.max_size;
        s.total_acquisitions = m_total_acquisitions.load(std::memory_order_relaxed);
        s.total_timeouts = m_total_timeouts.load(std::memory_order_relaxed);
        s.total_health_failures = m_total_health_failures.load(std::memory_order_relaxed);
        s.total_stmt_cache_hits = m_retired_stmt_hits.load(std::memory_order_relaxed);
        s.total_stmt_cache_misses = m_retired_stmt_misses.load(std::memory_order_relaxed);
        s.is_shutdown = m_shutdown;

        for (const Connection *c : m_live_connections) {
            if (c) {
                s.total_stmt_cache_hits += c->cache_hits();
                s.total_stmt_cache_misses += c->cache_misses();
                s.total_cached_statements += c->cached_statement_count();
            }
        }
        return s;
    }

private:
    struct idle_entry {
        std::unique_ptr<Connection> conn;
        time_point created_at;
        time_point last_used;
    };

    void retire_connection_locked(const Connection *conn) noexcept {
        if (!conn) return;
        m_live_connections.erase(conn);
        m_retired_stmt_hits.fetch_add(conn->cache_hits(), std::memory_order_relaxed);
        m_retired_stmt_misses.fetch_add(conn->cache_misses(), std::memory_order_relaxed);
    }

    void evict_stale_locked(time_point now) noexcept {
        if (m_options.idle_timeout.count() <= 0 && m_options.max_lifetime.count() <= 0) {
            return;
        }
        while (m_total_connections > m_options.min_size && !m_idle.empty()) {
            const auto &oldest = m_idle.back();
            bool idle_expired = (m_options.idle_timeout.count() > 0) &&
                                ((now - oldest.last_used) >= m_options.idle_timeout);
            bool life_expired = (m_options.max_lifetime.count() > 0) &&
                                ((now - oldest.created_at) >= m_options.max_lifetime);
            if (!idle_expired && !life_expired) {
                break;
            }
            if (oldest.conn) {
                retire_connection_locked(oldest.conn.get());
                oldest.conn->close();
            }
            m_idle.pop_back();
            --m_total_connections;
        }
    }

    factory_type m_factory;
    pool_options m_options;

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::condition_variable m_shutdown_cv;
    std::deque<idle_entry> m_idle;
    std::unordered_set<const Connection *> m_live_connections;

    size_t m_total_connections = 0;
    size_t m_active_leases = 0;
    bool m_shutdown = false;

    std::atomic<uint64_t> m_total_acquisitions{0};
    std::atomic<uint64_t> m_total_timeouts{0};
    std::atomic<uint64_t> m_total_health_failures{0};
    std::atomic<uint64_t> m_retired_stmt_hits{0};
    std::atomic<uint64_t> m_retired_stmt_misses{0};
};

template <typename Connection>
inline void pooled_connection<Connection>::release() noexcept {
    if (m_conn) {
        if (m_pool) {
            m_pool->release(std::move(m_conn), m_created_at);
        } else {
            m_conn.reset();
        }
        m_pool.reset();
    }
}

} // namespace db
