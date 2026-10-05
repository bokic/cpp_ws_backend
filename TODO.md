# Project Roadmap & Todos

This document outlines planned architectural enhancements, core features, and design proposals for `cpp_ws_backend`.

---

## 1. Database Connection Pooling

### Motivation
Handlers currently open and close database connections on every incoming request. While SQLite file handles are relatively fast, repeated initialization incurs overhead, file descriptor churn, and PRAGMA re-configuration. Furthermore, integrating remote databases (PostgreSQL, MySQL) requires persistent connections to avoid TLS and TCP handshake latency.

### Planned Capabilities
- **Thread-Safe Resource Pool**:
  - Generic `connection_pool<Connection>` managing reusable database handles across the worker thread pool.
  - Configurable minimum/maximum capacity, idle connection timeouts, and maximum lifetime.
  - Non-blocking and timeout-bounded lease acquisitions (`acquire(std::chrono::milliseconds timeout)`).
- **RAII Lease Guard (`pooled_connection`)**:
  - Move-only wrapper returned by `acquire()`.
  - Automatically resets connection state and returns the handle to the pool when leaving scope.
- **Liveness & Health Checks**:
  - Validation check on lease (e.g. `SELECT 1` or fast ping) to discard stale or broken connections.
- **Graceful Shutdown**:
  - Coordinated shutdown integration with `wsserver::shutdown()` to wait for active leases to complete and close all connections cleanly.

### Proposed Interface Concept
```cpp
// Initialize pool with min/max connections
auto pool = std::make_shared<db::connection_pool<sqlite_db>>(
    []() { return std::make_unique<sqlite_db>("app.db"); },
    pool_options{ .min_size = 4, .max_size = 16, .acquire_timeout = 2000ms }
);

// Acquire handle with automatic RAII return
{
    auto conn = pool->acquire();
    auto stmt = conn->prepare("SELECT * FROM items WHERE id = ?");
    stmt.bind_int(1, item_id);
    // ...
} // Connection automatically returns to pool here
```

---

## 2. Database Abstraction Layer (DAL)

### Motivation
Demo and backend services directly interface with raw SQLite C APIs. Introducing a lightweight, type-safe Database Abstraction Layer decouples routing and business logic from specific database engines, paving the way for multi-database support (PostgreSQL via `libpq`, SQLite, MySQL/MariaDB).

### Planned Capabilities
- **Unified C++ Driver Interface**:
  - Interface contracts (`database`, `statement`, `result_set`) using C++20 concepts or lightweight polymorphic interfaces.
- **Type-Safe Parameter Binding**:
  - Variadic and named binding supporting primitive types (`int`, `int64_t`, `double`, `std::string_view`, `std::span<const uint8_t>`, `std::chrono::system_clock::time_point`, `std::nullptr_t`).
  - Elimination of manual SQL injection risks and datatype mismatch errors.
- **Type-Safe Row & Column Access**:
  - Safe column extraction by index or column name:
    ```cpp
    int id = row.get<int>("id");
    std::string name = row.get<std::string>("name");
    std::optional<std::string> email = row.get_optional<std::string>("email");
    ```
- **RAII Transaction Management**:
  - `transaction` scope guard supporting automatic rollback on exception or scope exit unless explicitly committed:
    ```cpp
    auto tx = db.begin_transaction();
    db.execute("UPDATE accounts SET balance = balance - 100 WHERE id = 1");
    db.execute("UPDATE accounts SET balance = balance + 100 WHERE id = 2");
    tx.commit();
    ```

---

## 3. One-Line SQL to JSON Serialization

### Motivation
A significant portion of web API handlers performs a repetitive pattern: execute a SQL query, iterate through columns, construct JSON objects/arrays, and serialize to the HTTP output stream. Boilerplate code increases bug surface area (such as incorrect column counts or memory leaks) and degrades developer productivity.

### Planned Capabilities
- **Direct Query-to-Stream Serialization**:
  - Stream database rows directly to the FastCGI output buffer (`FCGX_Stream` or `std::ostream`) without building large in-memory JSON DOM trees.
  - Drastic reduction in memory allocations and CPU cache thrashing for large result sets.
- **Automatic Type Mapping**:
  - SQL `INTEGER` $\rightarrow$ JSON Number (Integer)
  - SQL `REAL` / `FLOAT` $\rightarrow$ JSON Number (Floating Point)
  - SQL `TEXT` / `VARCHAR` $\rightarrow$ JSON String (with automatic UTF-8 escaping)
  - SQL `NULL` $\rightarrow$ JSON `null`
  - SQL `BLOB` $\rightarrow$ Base64-encoded JSON string
- **Flexible Response Formatting & Enveloping**:
  - Raw array output: `[{"id": 1, ...}, {"id": 2, ...}]`
  - Paginated metadata envelope:
    ```json
    {
      "page": 1,
      "page_size": 25,
      "total_records": 100,
      "total_pages": 4,
      "rows": [...]
    }
    ```
  - Optional JSONP callback wrapper with identifier validation.

### Proposed Interface Concept
```cpp
// Direct serialization to worker output stream
db::sql_to_json(worker->out(), db, "SELECT id, name, email FROM users WHERE active = ?", 1);

// Paginated grid response in a single call
db::sql_to_json_paginated(
    worker->out(),
    db,
    sql_query{
        .count_sql = "SELECT COUNT(*) FROM users",
        .data_sql  = "SELECT id, name, email FROM users ORDER BY id LIMIT ? OFFSET ?",
        .page      = current_page,
        .page_size = page_size,
        .callback  = callback_param
    }
);
```
