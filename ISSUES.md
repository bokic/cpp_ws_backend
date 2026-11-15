# Known Issues, Bugs, and Performance Bottlenecks

This document catalogs identified bugs, potential memory corruption vulnerabilities, concurrency hazards, and performance bottlenecks in `cpp_ws_backend`.

---

## Table of Contents

- [Summary of Findings](#summary-of-findings)
- [1. Memory Corruption & Crash Vulnerabilities](#1-memory-corruption--crash-vulnerabilities)
  - [1.1. Out-of-Bounds Buffer Read in `journal-demo`](#11-out-of-bounds-buffer-read-in-journal-demo)
  - [1.2. Integer Underflow & Invalid Memory Write on `bind(0, ...)` in PostgreSQL Driver](#12-integer-underflow--invalid-memory-write-on-bind0---in-postgresql-driver)
  - [1.3. Use-After-Free / Dangling Pointers in `mysql_row`](#13-use-after-free--dangling-pointers-in-mysql_row)
  - [1.4. Undefined Behavior via `std::string_view((char*)nullptr)` in `statement::bind_all`](#14-undefined-behavior-via-stdstring_viewcharnullptr-in-statementbind_all)
  - [1.5. Null Pointer Dereference in `connection_pool::acquire()`](#15-null-pointer-dereference-in-connection_poolacquire)
  - [1.6. Undefined Behavior in `url_decode` with `<cctype>` Functions](#16-undefined-behavior-in-url_decode-with-cctype-functions)
- [2. Concurrency, Race Conditions & Thread Safety](#2-concurrency-race-conditions--thread-safety)
  - [2.1. Async-Signal-Safety Violation in Daemon Signal Handlers](#21-async-signal-safety-violation-in-daemon-signal-handlers)
  - [2.2. Unsynchronized Dynamic Library Loaders](#22-unsynchronized-dynamic-library-loaders)
  - [2.3. Data Race on Connection State in `connection_pool::stats()`](#23-data-race-on-connection-state-in-connection_poolstats)
  - [2.4. Inverted Shutdown Sequence Drops In-Flight Worker Requests](#24-inverted-shutdown-sequence-drops-in-flight-worker-requests)
  - [2.5. Unsynchronized Dynamic Route Registration](#25-unsynchronized-dynamic-route-registration)
- [3. Functional Defects & Logic Bugs](#3-functional-defects--logic-bugs)
  - [3.1. FastCGI Memory Leak on Every HTTP Request (`FCGX_Free` Omitted)](#31-fastcgi-memory-leak-on-every-http-request-fcgx_free-omitted)
  - [3.2. Partial HTTP Body Reads for Chunked/Large Payloads](#32-partial-http-body-reads-for-chunkedlarge-payloads)
  - [3.3. Duplicate Named Parameters Ignored in PostgreSQL Driver](#33-duplicate-named-parameters-ignored-in-postgresql-driver)
  - [3.4. SQL Literals Corrupted by PostgreSQL Parameter Parser](#34-sql-literals-corrupted-by-postgresql-parameter-parser)
  - [3.5. Read-Only SQLite Databases Cannot Be Opened](#35-read-only-sqlite-databases-cannot-be-opened)
  - [3.6. Dirty Transaction State Leaked across Pooled Connections (Postgres & MySQL)](#36-dirty-transaction-state-leaked-across-pooled-connections-postgres--mysql)
  - [3.7. Failed MySQL Rollback Leaves Autocommit Permanently Disabled](#37-failed-mysql-rollback-leaves-autocommit-permanently-disabled)
  - [3.8. Unix Domain Sockets Left on Filesystem upon Server Shutdown](#38-unix-domain-sockets-left-on-filesystem-upon-server-shutdown)
  - [3.9. Silent Server Exit on `FCGX_OpenSocket` Failure](#39-silent-server-exit-on-fcgx_opensocket-failure)
  - [3.10. Resource Leak on Error in `journal-demo`](#310-resource-leak-on-error-in-journal-demo)
- [4. Security & Information Disclosure](#4-security--information-disclosure)
  - [4.1. Sensitive Information Disclosure via `error.what()` in HTTP 500 Responses](#41-sensitive-information-disclosure-via-errorwhat-in-http-500-responses)
  - [4.2. HTTP Header Injection / Response Splitting via `SERVER_PROTOCOL`](#42-http-header-injection--response-splitting-via-server_protocol)
- [5. Performance Bottlenecks](#5-performance-bottlenecks)
  - [5.1. Heap Allocation Storm in FastCGI Header Parsing (`parse_request`)](#51-heap-allocation-storm-in-fastcgi-header-parsing-parse_request)
  - [5.2. Permanent Memory Accumulation in Worker Threads (`m_body`)](#52-permanent-memory-accumulation-in-worker-threads-m_body)
  - [5.3. Linear Regex Evaluation on Every Request](#53-linear-regex-evaluation-on-every-request)
  - [5.4. Bypassing Stream Sinks in `sqlite-demo`](#54-bypassing-stream-sinks-in-sqlite-demo)
  - [5.5. Client-Side Query Formatting and Escaping in MySQL Driver](#55-client-side-query-formatting-and-escaping-in-mysql-driver)
  - [5.6. Unbounded Request Queue in `wsthreadpool`](#56-unbounded-request-queue-in-wsthreadpool)

---

## Summary of Findings

| ID | Title | Severity | Component | Status | Category |
| :--- | :--- | :---: | :--- | :---: | :--- |
| **1.1** | `sd_journal_get_data` buffer over-read | **CRITICAL** | `journal-demo` | **RESOLVED** (`7ac2db5`) | Memory Corruption |
| **1.2** | Out-of-bounds memory write on `bind(0)` | **CRITICAL** | PostgreSQL Driver | **RESOLVED** (`fc73154`) | Memory Corruption |
| **1.3** | Use-after-free on copied `mysql_row` | **HIGH** | MySQL Driver | **RESOLVED** (`7ef3f73`) | Memory Corruption |
| **1.4** | Null pointer in `string_view` constructor | **HIGH** | Database Core | **RESOLVED** (`490b3c5`) | Crash / UB |
| **1.5** | Null dereference in `acquire()` | **MEDIUM** | Connection Pool | **RESOLVED** (`ecb2727`) | Crash |
| **1.6** | Signed char in `isxdigit` in `url_decode` | **LOW** | Worker | **RESOLVED** (`471864e`) | Undefined Behavior |
| **2.1** | Non-async-signal-safe shutdown in signal handlers | **CRITICAL** | Demos | **RESOLVED** (`12078b8`) | Concurrency / Deadlock |
| **2.2** | Data race in dynamic library loaders | **HIGH** | DB Drivers | **RESOLVED** (`fd71daa`) | Concurrency / Data Race |
| **2.3** | Data race in `connection_pool::stats()` | **HIGH** | Connection Pool | **RESOLVED** (`a125ab8`) | Concurrency / Data Race |
| **2.4** | Inverted shutdown sequence drops active requests | **MEDIUM** | Server Core | **RESOLVED** (`982c42d`) | Concurrency |
| **2.5** | Dynamic route modification not thread-safe | **LOW** | Server Core | **RESOLVED** (`a072966`) | Concurrency |
| **3.1** | FastCGI internal buffers leaked every request | **HIGH** | Server Core | **RESOLVED** (`95adf0d`) | Resource Leak |
| **3.2** | Partial HTTP body read over stream | **HIGH** | Worker | **RESOLVED** (`5a3a5ba`) | Functional Bug |
| **3.3** | Duplicate named parameters dropped in Postgres | **HIGH** | PostgreSQL Driver | **RESOLVED** (`16e1060`) | Functional Bug |
| **3.4** | SQL string literals corrupted by Postgres parser | **MEDIUM** | PostgreSQL Driver | **RESOLVED** (`91e45d7`) | Functional Bug |
| **3.5** | Cannot open read-only SQLite database | **MEDIUM** | SQLite Driver | **RESOLVED** (`20b58a0`) | Functional Bug |
| **3.6** | Transaction leak in pooled Postgres & MySQL | **MEDIUM** | Connection Pool | **RESOLVED** (`a2c5ee0`) | Functional Bug |
| **3.7** | Failed MySQL rollback leaves autocommit off | **MEDIUM** | MySQL Driver | **RESOLVED** (`71afebb`) | Functional Bug |
| **3.8** | Stale Unix domain socket file blocks restart | **LOW** | Server Core | **RESOLVED** (`09194d6`) | Operational Bug |
| **3.9** | Silent server exit on bind/socket error | **LOW** | Server Core | **RESOLVED** (`f04a272`) | Operational Bug |
| **3.10**| Leaked `sd_journal` and JSON on exception | **LOW** | `journal-demo` | **RESOLVED** (`939fd10`) | Resource Leak |
| **4.1** | Exception message disclosure in HTTP 500 | **MEDIUM** | Worker | **RESOLVED** (`132e9e1`) | Security |
| **4.2** | CRLF injection via `SERVER_PROTOCOL` | **MEDIUM** | Demos | **RESOLVED** (`03b2ea0`) | Security |
| **5.1** | 100+ heap allocations per request in header parsing | **HIGH** | Worker | Performance |
| **5.2** | Indefinite 10MB memory retention in worker threads | **MEDIUM** | Worker | Performance |
| **5.3** | Linear regex matching on all routes | **MEDIUM** | Worker | Performance |
| **5.4** | Demo buffers JSON instead of streaming | **MEDIUM** | Demos | Performance |
| **5.5** | Client-side query formatting in MySQL | **MEDIUM** | MySQL Driver | Performance |
| **5.6** | Unbounded thread pool queue | **LOW** | Thread Pool | Performance |

---

## 1. Memory Corruption & Crash Vulnerabilities

### 1.1. Out-of-Bounds Buffer Read in `journal-demo`
* **File:** [demos/journal-demo/main.cpp#L45-L53](file:///home/boris/projects/cpp_ws_backend/demos/journal-demo/main.cpp#L45-L53)
* **Severity:** **CRITICAL**
* **Description:**
  `sd_journal_get_data` fills a pointer `msg` and length `len`. The systemd documentation explicitly states that this returned data is **not null-terminated**.
  Line 49 invokes:
  ```cpp
  json_object_array_add(json_data, json_object_new_string(reinterpret_cast<const char *>(msg + 8)));
  ```
  `json_object_new_string(const char *s)` expects a null-terminated C string and runs `strlen()`. It reads past the journal entry buffer into unallocated or unrelated memory, leading to memory corruption or `SIGSEGV`.
* **Fix:**
  Use `json_object_new_string_len`:
  ```cpp
  json_object_array_add(json_data, json_object_new_string_len(reinterpret_cast<const char *>(msg + 8), len - 8));
  ```

---

### 1.2. Integer Underflow & Invalid Memory Write on `bind(0, ...)` in PostgreSQL Driver
* **File:** [src/db/postgres_driver.cpp#L353-L386](file:///home/boris/projects/cpp_ws_backend/src/db/postgres_driver.cpp#L353-L386)
* **Severity:** **CRITICAL**
* **Description:**
  In all positional overloads of `postgres_statement::bind(size_t index, ...)`:
  ```cpp
  void postgres_statement::bind(size_t index, int64_t val) {
      if (index == 0 || index > m_params.size()) {
          m_params.resize(std::max(m_params.size(), index), {true, ""});
      }
      m_params[index - 1] = {false, std::to_string(val)};
  }
  ```
  If a caller accidentally passes 0 (e.g. assuming 0-based indexing):
  `std::max(m_params.size(), (size_t)0)` does not expand `m_params`. Then `index - 1` underflows to `SIZE_MAX` (`18446744073709551615`). The expression `m_params[SIZE_MAX] = ...` writes to unmapped memory and immediately crashes with a segmentation fault.
* **Fix:**
  Reject `index == 0`:
  ```cpp
  if (index == 0) {
      throw db_exception("PostgreSQL parameter index is 1-based");
  }
  if (index > m_params.size()) {
      m_params.resize(index, {true, ""});
  }
  m_params[index - 1] = {false, std::to_string(val)};
  ```

---

### 1.3. Use-After-Free / Dangling Pointers in `mysql_row`
* **File:** [src/db/mysql_driver.h#L41-L43](file:///home/boris/projects/cpp_ws_backend/src/db/mysql_driver.h#L41-L43), [src/db/mysql_driver.cpp#L483-L490](file:///home/boris/projects/cpp_ws_backend/src/db/mysql_driver.cpp#L483-L490)
* **Severity:** **HIGH**
* **Description:**
  `mysql_row` stores raw pointers:
  ```cpp
  char **m_row_data = nullptr;
  unsigned long *m_lengths = nullptr;
  ```
  These point directly into the internal buffers of `MYSQL_RES`. Because `mysql_row` has a default copy constructor, a user can copy a row (e.g. `auto r = rs->current_row();`).
  When `rs` is destroyed or advances, `mysql_free_result` frees the `MYSQL_RES` buffer. The copied row's pointers become dangling. Any subsequent call to `r.get_string(i)` or `r.get_blob(i)` reads freed memory (use-after-free).
* **Fix:**
  Delete the copy constructor and copy assignment operator for `mysql_row`, or copy the column contents into internal owned `std::string` buffers upon copy.

---

### 1.4. Undefined Behavior via `std::string_view((char*)nullptr)` in `statement::bind_all`
* **File:** [src/db/database.h#L157-L158](file:///home/boris/projects/cpp_ws_backend/src/db/database.h#L157-L158)
* **Severity:** **HIGH**
* **Description:**
  In `database.h`:
  ```cpp
  } else if constexpr (std::is_convertible_v<T, std::string_view>) {
      bind(idx, std::string_view(arg));
  }
  ```
  If a caller passes a typed null pointer `const char *s = nullptr;` into `bind_all(s)` or `query(sql, s)`:
  `std::is_convertible_v<const char*, std::string_view>` evaluates to `true`.
  Invoking `std::string_view(nullptr)` is undefined behavior in C++ and causes an immediate crash in `strlen(nullptr)`.
* **Fix:**
  ```cpp
  } else if constexpr (std::is_convertible_v<T, std::string_view>) {
      if constexpr (std::is_pointer_v<Decayed>) {
          if (arg == nullptr) {
              bind(idx, nullptr);
              return;
          }
      }
      bind(idx, std::string_view(arg));
  }
  ```

---

### 1.5. Null Pointer Dereference in `connection_pool::acquire()`
* **File:** [src/db/connection_pool.h#L244-L246](file:///home/boris/projects/cpp_ws_backend/src/db/connection_pool.h#L244-L246)
* **Severity:** **MEDIUM**
* **Description:**
  If the connection factory lambda returns `nullptr` without throwing:
  ```cpp
  conn = m_factory();
  ...
  conn_reset_if_possible(conn.get());
  if (m_options.ping_on_acquire && !conn->ping())
  ```
  `conn->ping()` dereferences a null pointer, causing a crash.
* **Fix:**
  Add a null check right after calling `m_factory()`:
  ```cpp
  conn = m_factory();
  if (!conn) {
      throw db_exception("Database connection factory returned null");
  }
  ```

---

### 1.6. Undefined Behavior in `url_decode` with `<cctype>` Functions
* **File:** [src/wsworker.cpp#L28](file:///home/boris/projects/cpp_ws_backend/src/wsworker.cpp#L28)
* **Severity:** **LOW / UB**
* **Description:**
  `isxdigit(src[i + 1])` passes signed `char`. If `src[i + 1]` is negative (`>= 0x80`), passing negative integers other than `EOF` to `<cctype>` functions invokes undefined behavior (out-of-bounds array access in glibc's `__ctype_b_loc`).
* **Fix:**
  ```cpp
  isxdigit(static_cast<unsigned char>(src[i + 1])) && isxdigit(static_cast<unsigned char>(src[i + 2]))
  ```

---

## 2. Concurrency, Race Conditions & Thread Safety

### 2.1. Async-Signal-Safety Violation in Daemon Signal Handlers
* **File:** [demos/sqlite-demo/main.cpp#L137-L145](file:///home/boris/projects/cpp_ws_backend/demos/sqlite-demo/main.cpp#L137-L145), [demos/journal-demo/main.cpp#L77-L85](file:///home/boris/projects/cpp_ws_backend/demos/journal-demo/main.cpp#L77-L85)
* **Severity:** **CRITICAL**
* **Status:** **RESOLVED** (`12078b8`)
* **Description:**
  The signal handler:
  ```cpp
  static void signal_handler(int sig) {
      (void)sig;
      backend::wsserver *server = g_server.load();
      if (server) server->shutdown();
  }
  ```
  invokes `server->shutdown()`, which locks `std::mutex m_hooks_mutex`, performs vector moves and heap allocations, and calls `pool->shutdown()`, which locks `m_mutex` and condition variables.
  None of these functions are POSIX async-signal-safe. If `SIGINT` or `SIGTERM` arrives while any thread holds a heap lock or pool lock, the process permanently deadlocks.
* **Fix:**
  Set an atomic flag in the signal handler (`g_stop_requested = 1`) and handle termination cleanly from the main loop, or use a self-pipe / `signalfd`.

---

### 2.2. Unsynchronized Dynamic Library Loaders
* **File:** [src/db/sqlite_driver.cpp#L118](file:///home/boris/projects/cpp_ws_backend/src/db/sqlite_driver.cpp#L118), [src/db/postgres_driver.cpp#L63](file:///home/boris/projects/cpp_ws_backend/src/db/postgres_driver.cpp#L63), [src/db/mysql_driver.cpp#L89](file:///home/boris/projects/cpp_ws_backend/src/db/mysql_driver.cpp#L89)
* **Severity:** **HIGH**
* **Status:** **RESOLVED** (`fd71daa`)
* **Description:**
  `sqlite_api::instance().load()`, `pq_api::instance().load()`, and `mysql_api::instance().load()` check `if (handle) return true;` without any synchronization.
  When a multi-worker pool starts up, worker threads call `ensure_loaded()` concurrently, leading to simultaneous `dlopen()` calls and unsynchronized writes to member function pointers (data race).
* **Fix:**
  Synchronize initialization using `std::call_once` with `std::once_flag`:
  ```cpp
  std::once_flag load_flag;
  void ensure_loaded() {
      std::call_once(load_flag, [this]() {
          if (!load()) throw db_exception("Failed to load driver library");
      });
  }
  ```

---

### 2.3. Data Race on Connection State in `connection_pool::stats()`
* **File:** [src/db/connection_pool.h#L347-L355](file:///home/boris/projects/cpp_ws_backend/src/db/connection_pool.h#L347-L355)
* **Severity:** **HIGH**
* **Status:** **RESOLVED** (`a125ab8`)
* **Description:**
  `connection_pool::stats()` locks `m_mutex` and iterates over `m_live_connections`, invoking:
  ```cpp
  s.total_stmt_cache_hits += c->cache_hits();
  s.total_stmt_cache_misses += c->cache_misses();
  s.total_cached_statements += c->cached_statement_count();
  ```
  However, connections in `m_live_connections` are actively being used by worker threads without holding `m_mutex`. If a worker thread modifies `m_cache` concurrently (e.g. in `sqlite_database::close()`), concurrent reads and writes to `m_cache` occur without synchronization.
* **Fix:**
  Ensure statement cache counters are atomic (`std::atomic<uint64_t>`), and protect cache pointer access.

---

### 2.4. Inverted Shutdown Sequence Drops In-Flight Worker Requests
* **File:** [src/wsserver.cpp#L26-L53](file:///home/boris/projects/cpp_ws_backend/src/wsserver.cpp#L26-L53)
* **Severity:** **MEDIUM**
* **Status:** **RESOLVED** (`982c42d`)
* **Description:**
  `wsserver::shutdown()` executes shutdown hooks (closing the database pool) **before** stopping or joining the worker threads.
  Active worker threads processing in-flight requests attempt to query the database, but `pool->acquire()` throws `pool is shut down`, returning HTTP 500 errors to clients during shutdown.
* **Fix:**
  Stop and drain `m_thread_pool` before executing `m_shutdown_hooks`.

---

### 2.5. Unsynchronized Dynamic Route Registration
* **File:** [src/wsroute.h#L56-L83](file:///home/boris/projects/cpp_ws_backend/src/wsroute.h#L56-L83)
* **Severity:** **LOW**
* **Status:** **RESOLVED** (`a072966`)
* **Description:**
  `router::add_route()` and `router::use()` mutate `m_routes` and `m_middlewares` without a mutex. If routes or middlewares are added while the server is already processing requests on worker threads, a data race and crash will occur.
* **Fix:**
  Freeze routes once `wsserver::run()` begins, or protect the router lists with a `std::shared_mutex`.

---

## 3. Functional Defects & Logic Bugs

### 3.1. FastCGI Memory Leak on Every HTTP Request (`FCGX_Free` Omitted)
* **File:** [src/wsserver.cpp#L99-L132](file:///home/boris/projects/cpp_ws_backend/src/wsserver.cpp#L99-L132)
* **Severity:** **HIGH**
* **Status:** **RESOLVED** (`95adf0d`)
* **Description:**
  `wsserver::run()` instantiates requests via `std::make_shared<FCGX_Request>()`.
  `FCGX_InitRequest` and `FCGX_Accept_r` allocate C-level structures (`paramsPtr`, stream buffers, environment tables). `FCGX_Finish_r` flushes streams and closes the connection, but libfcgi requires `FCGX_Free(req, 1)` to release the allocated buffers.
  Because default C++ `delete` is invoked when `shared_ptr<FCGX_Request>` destructs, `FCGX_Free` is never called, leaking heap memory on every single HTTP request.
* **Fix:**
  Specify a custom deleter:
  ```cpp
  auto request = std::shared_ptr<FCGX_Request>(new FCGX_Request(), [](FCGX_Request *r) {
      if (r) {
          FCGX_Free(r, 1);
          delete r;
      }
  });
  ```

---

### 3.2. Partial HTTP Body Reads for Chunked/Large Payloads
* **File:** [src/wsworker.cpp#L155-L157](file:///home/boris/projects/cpp_ws_backend/src/wsworker.cpp#L155-L157)
* **Severity:** **HIGH**
* **Status:** **RESOLVED** (`5a3a5ba`)
* **Description:**
  `wsworker::process()` reads the request body via:
  ```cpp
  m_body.resize(static_cast<unsigned int>(contentLen));
  FCGX_GetStr(&m_body.at(0), contentLen, request->in);
  ```
  `FCGX_GetStr` returns the number of bytes read and is not guaranteed to read all `contentLen` bytes in one call over a TCP stream. If fewer bytes are returned, the request body is truncated and `m_body` retains trailing null or garbage bytes.
* **Fix:**
  Read in a loop:
  ```cpp
  int total_read = 0;
  while (total_read < contentLen) {
      int n = FCGX_GetStr(&m_body[total_read], contentLen - total_read, request->in);
      if (n <= 0) break;
      total_read += n;
  }
  m_body.resize(total_read);
  ```

---

### 3.3. Duplicate Named Parameters Ignored in PostgreSQL Driver
* **File:** [src/db/postgres_driver.cpp#L325-L351](file:///home/boris/projects/cpp_ws_backend/src/db/postgres_driver.cpp#L325-L351)
* **Severity:** **HIGH**
* **Status:** **RESOLVED** (`16e1060`)
* **Description:**
  When translating `:name` into `$1, $2, ...`, if a query reuses a parameter (e.g. `WHERE start_date >= :dt AND end_date <= :dt`), the parser emits `$1` and `$2` and appends `"dt"` to `m_param_names` twice.
  However, `get_param_index("dt")` searches linearly from index 0 and returns the first index (`$1`). The second placeholder `$2` is never bound and defaults to NULL.
* **Fix:**
  Deduplicate parameter names during SQL parsing (as done in `mysql_driver.cpp`) and map all occurrences to the same placeholder index, or bind to all matching occurrences.

---

### 3.4. SQL Literals Corrupted by PostgreSQL Parameter Parser
* **File:** [src/db/postgres_driver.cpp#L319-L338](file:///home/boris/projects/cpp_ws_backend/src/db/postgres_driver.cpp#L319-L338)
* **Severity:** **MEDIUM**
* **Status:** **RESOLVED** (`91e45d7`)
* **Description:**
  The tokenizer in `postgres_statement` does not track string literals or comments. A query containing `?` or `:name` inside quotes (e.g. `SELECT * FROM logs WHERE note = 'What?'`) has the literal altered to `'What$1'`, breaking valid queries.
* **Fix:**
  Adopt quote/comment-skipping logic in `postgres_statement` identical to `mysql_statement`.

---

### 3.5. Read-Only SQLite Databases Cannot Be Opened
* **File:** [src/db/sqlite_driver.cpp#L894-L899](file:///home/boris/projects/cpp_ws_backend/src/db/sqlite_driver.cpp#L894-L899)
* **Severity:** **MEDIUM**
* **Status:** **RESOLVED** (`20b58a0`)
* **Description:**
  `sqlite_database::open()` unconditionally executes `PRAGMA journal_mode = WAL;`. When opening a database with `SQLITE_OPEN_READONLY` or on a read-only filesystem, SQLite returns `SQLITE_READONLY` (error 8). `open()` catches this as an error and throws, making it impossible to open read-only databases.
* **Fix:**
  Only execute WAL pragmas if `!(flags & SQLITE_OPEN_READONLY)`.

---

### 3.6. Dirty Transaction State Leaked across Pooled Connections (Postgres & MySQL)
* **File:** [src/db/connection_pool.h#L360-L365](file:///home/boris/projects/cpp_ws_backend/src/db/connection_pool.h#L360-L365)
* **Severity:** **MEDIUM**
* **Status:** **RESOLVED** (`a2c5ee0`)
* **Description:**
  `connection_pool` calls `c->reset_for_request()` upon connection acquire and release.
  `sqlite_database` implements this by rolling back open transactions. Neither `postgres_database` nor `mysql_database` implements `reset_for_request()`. If an exception occurs mid-transaction on a PostgreSQL or MySQL connection, the connection is returned to the pool with an active transaction, corrupting subsequent requests.
* **Fix:**
  Implement `reset_for_request()` on `postgres_database` (executing `DISCARD ALL;` or checking `PQtransactionStatus`) and `mysql_database` (executing `ROLLBACK; SET autocommit=1;`).

---

### 3.7. Failed MySQL Rollback Leaves Autocommit Permanently Disabled
* **File:** [src/db/mysql_driver.cpp#L828-L832](file:///home/boris/projects/cpp_ws_backend/src/db/mysql_driver.cpp#L828-L832)
* **Severity:** **MEDIUM**
* **Status:** **RESOLVED** (`71afebb`)
* **Description:**
  In `mysql_transaction::rollback()`:
  ```cpp
  if (api.mysql_rollback(m_conn->conn) != 0) {
      throw db_exception("Failed to rollback transaction: " + std::string(api.mysql_error(m_conn->conn)));
  }
  api.mysql_autocommit(m_conn->conn, 1);
  m_active = false;
  ```
  If `mysql_rollback` fails, the exception is thrown before `api.mysql_autocommit(..., 1)` is called. The connection remains with autocommit disabled, causing subsequent queries to not persist changes.
* **Fix:**
  Ensure `api.mysql_autocommit(m_conn->conn, 1)` and `m_active = false` run regardless of rollback failure.

---

### 3.8. Unix Domain Sockets Left on Filesystem upon Server Shutdown
* **File:** [src/wsserver.cpp#L29-L33](file:///home/boris/projects/cpp_ws_backend/src/wsserver.cpp#L29-L33)
* **Severity:** **LOW**
* **Status:** **RESOLVED** (`09194d6`)
* **Description:**
  When configuring a Unix domain socket path (e.g. `/tmp/app.sock`), `close(fd)` does not delete the socket file. On process restart, `FCGX_OpenSocket` fails with `EADDRINUSE`.
* **Fix:**
  Record `m_socket_path` in `wsserver::init()` and call `unlink(m_socket_path.c_str())` during `shutdown()`.

---

### 3.9. Silent Server Exit on `FCGX_OpenSocket` Failure
* **File:** [src/wsserver.cpp#L22](file:///home/boris/projects/cpp_ws_backend/src/wsserver.cpp#L22), [src/wsserver.cpp#L102](file:///home/boris/projects/cpp_ws_backend/src/wsserver.cpp#L102)
* **Severity:** **LOW**
* **Status:** **RESOLVED** (`f04a272`)
* **Description:**
  If `FCGX_OpenSocket` fails (e.g. port already bound), it returns `-1`. `wsserver::init` stores `-1` into `m_sock_fd` without validation. `wsserver::run()` then immediately fails and returns without logging any error message or throwing an exception.
* **Fix:**
  Check `if (m_sock_fd < 0)` in `init()` and throw `std::system_error` or log to stderr.

---

### 3.10. Resource Leak on Error in `journal-demo`
* **File:** [demos/journal-demo/main.cpp#L33-L70](file:///home/boris/projects/cpp_ws_backend/demos/journal-demo/main.cpp#L33-L70)
* **Severity:** **LOW**
* **Status:** **RESOLVED** (`939fd10`)
* **Description:**
  `sd_journal *journal` and `json_object *json` are managed manually without RAII. If an exception is thrown during message parsing, `sd_journal_close(journal)` and `json_object_put(json)` are skipped, leaking descriptors and memory.
* **Fix:**
  Wrap both resources in RAII wrappers or unique pointers with custom deleters.

---

## 4. Security & Information Disclosure

### 4.1. Sensitive Information Disclosure via `error.what()` in HTTP 500 Responses
* **File:** [src/wsworker.cpp#L230-L235](file:///home/boris/projects/cpp_ws_backend/src/wsworker.cpp#L230-L235)
* **Severity:** **MEDIUM**
* **Status:** **RESOLVED** (`132e9e1`)
* **Description:**
  ```cpp
  } catch (const std::exception &error) {
      FCGX_FPrintF(request->out, "Status: 500 Internal Server Error\r\n\r\nInternal error: %s", error.what());
  }
  ```
  Unfiltered exception messages disclose database file paths, table schemas, failed SQL queries, and driver connection details directly to external clients.
* **Fix:**
  Log `error.what()` internally and return a generic `Status: 500 Internal Server Error\r\n\r\nInternal Server Error`.

---

### 4.2. HTTP Header Injection / Response Splitting via `SERVER_PROTOCOL`
* **File:** [demos/sqlite-demo/main.cpp#L31](file:///home/boris/projects/cpp_ws_backend/demos/sqlite-demo/main.cpp#L31), [demos/journal-demo/main.cpp#L66](file:///home/boris/projects/cpp_ws_backend/demos/journal-demo/main.cpp#L66)
* **Severity:** **MEDIUM**
* **Status:** **RESOLVED** (`03b2ea0`)
* **Description:**
  `send_json_response` prints `SERVER_PROTOCOL` directly into the HTTP response line without validating for carriage returns or newlines (`\r\n`). A client sending a crafted protocol value could inject arbitrary HTTP headers.
* **Fix:**
  Validate that `protocol` matches `^HTTP/[0-9]\.[0-9]$`, or default to `"HTTP/1.1"`.

---

## 5. Performance Bottlenecks

### 5.1. Heap Allocation Storm in FastCGI Header Parsing (`parse_request`)
* **File:** [src/wsworker.cpp#L63-L82](file:///home/boris/projects/cpp_ws_backend/src/wsworker.cpp#L63-L82)
* **Severity:** **HIGH (PERFORMANCE)**
* **Description:**
  On every single incoming HTTP request, `parse_request` reads all environment strings from `request->envp` and inserts them into a `std::map<std::string, std::string>`.
  For a typical FastCGI environment of 40 variables, this executes ~120 dynamic heap allocations and copies per request, causing massive allocator contention across worker threads.
* **Fix:**
  Use libfcgi's non-allocating parameter lookup `FCGX_GetParam(name, request->envp)` for required parameters (`SCRIPT_NAME`, `REQUEST_METHOD`, `CONTENT_LENGTH`, `QUERY_STRING`).

---

### 5.2. Permanent Memory Accumulation in Worker Threads (`m_body`)
* **File:** [src/wsworker.cpp#L121](file:///home/boris/projects/cpp_ws_backend/src/wsworker.cpp#L121), [src/wsworker.cpp#L155](file:///home/boris/projects/cpp_ws_backend/src/wsworker.cpp#L155)
* **Severity:** **MEDIUM (PERFORMANCE)**
* **Description:**
  Each worker thread holds a long-lived `wsworker` instance. If a client posts a 10MB payload, `m_body.resize(10000000)` allocates 10MB.
  On subsequent requests, `m_body.clear()` sets `size = 0`, but does not reduce capacity. In a thread pool with 64 workers, 640MB of memory remains allocated in idle worker string capacities indefinitely.
* **Fix:**
  Shrink or reset `m_body` if `m_body.capacity() > 64 * 1024`:
  ```cpp
  if (m_body.capacity() > 65536) {
      std::string().swap(m_body);
  } else {
      m_body.clear();
  }
  ```

---

### 5.3. Linear Regex Evaluation on Every Request
* **File:** [src/wsworker.cpp#L187-L191](file:///home/boris/projects/cpp_ws_backend/src/wsworker.cpp#L187-L191)
* **Severity:** **MEDIUM (PERFORMANCE)**
* **Description:**
  `wsworker::process` iterates sequentially through all configured routes and runs `std::regex_match(path.c_str(), cm, route.uri)`.
  In `libstdc++`, `std::regex` is notoriously slow and allocates DFA/NFA match states on every execution. As route count increases, routing overhead scales linearly with high CPU overhead.
* **Fix:**
  Use exact-path hashing (e.g. `std::unordered_map<std::string, route>`) for static paths, reserving regular expressions only for dynamic parameterized paths.

---

### 5.4. Bypassing Stream Sinks in `sqlite-demo`
* **File:** [demos/sqlite-demo/main.cpp#L79-L80](file:///home/boris/projects/cpp_ws_backend/demos/sqlite-demo/main.cpp#L79-L80)
* **Severity:** **MEDIUM (PERFORMANCE)**
* **Description:**
  `request_ws_jsGrid_customers` and other handlers call `sql_to_json_paginated_string`, which allocates and builds an entire multi-megabyte JSON payload in a `std::string`, before formatting it into `FCGX_FPrintF`.
  `sql_to_json.h` already provides `fcgx_sink` to stream row-by-row directly into FastCGI output without buffering.
* **Fix:**
  Write headers directly and invoke `sql_to_json_paginated(worker->out(), *conn, q)`.

---

### 5.5. Client-Side Query Formatting and Escaping in MySQL Driver
* **File:** [src/db/mysql_driver.cpp#L699-L731](file:///home/boris/projects/cpp_ws_backend/src/db/mysql_driver.cpp#L699-L731)
* **Severity:** **MEDIUM (PERFORMANCE)**
* **Description:**
  `mysql_statement` does not use the MySQL C API prepared statement interface (`mysql_stmt_init`, `mysql_stmt_prepare`, `mysql_stmt_execute`). Instead, it synthesizes SQL via string concatenation and manual escaping, forcing the MySQL server to re-parse and compile the SQL query on every execution.
* **Fix:**
  Migrate `mysql_statement` to use MySQL's native prepared statement API.

---

### 5.6. Unbounded Request Queue in `wsthreadpool`
* **File:** [src/wsthreadpool.h#L37](file:///home/boris/projects/cpp_ws_backend/src/wsthreadpool.h#L37), [src/wsthreadpool.cpp#L74](file:///home/boris/projects/cpp_ws_backend/src/wsthreadpool.cpp#L74)
* **Severity:** **LOW (PERFORMANCE / STABILITY)**
* **Description:**
  `wsthreadpool::addWork` pushes requests to `std::queue<std::shared_ptr<FCGX_Request>> m_work_queue` without any bounds. If incoming request rate exceeds worker processing throughput, memory consumption grows indefinitely until OOM.
* **Fix:**
  Configure a maximum queue capacity (e.g. 1024). When the queue is full, immediately return `HTTP 503 Service Unavailable`.
