# Project Audit: Bugs, Crashes, Memory Corruption, and Performance Bottlenecks (`TODO.md`)

This document tracks newly discovered bugs, memory corruption hazards, crash vectors, concurrency races, functional defects, and performance bottlenecks identified across `cpp_ws_backend` and its demos.

---

## Summary Table

| ID | Title | Severity | Component | Category | Status |
| :--- | :--- | :---: | :--- | :--- | :---: |
| **1.1** | Data race & double-join crash in `wsthreadpool::stop()` | **CRITICAL** | `wsthreadpool` | Concurrency / Crash | **RESOLVED** (`f4b6a1f`) |
| **1.2** | Detached `sig_thread` holds dangling stack reference to `wsserver` | **CRITICAL** | Demos | Use-After-Free / Crash | **RESOLVED** (`e41fb92`) |
| **1.3** | `mysql_real_escape_string` error return `(unsigned long)-1` causes integer wrap & heap corruption | **CRITICAL** | MySQL Driver | Memory Corruption | **RESOLVED** (`9409046`) |
| **1.4** | Signed `int` overflow on large `string_view`/`span` in `sqlite_statement::bind` causes buffer over-read | **HIGH** | SQLite Driver | Memory Corruption / UB | **RESOLVED** (`88ac488`) |
| **1.5** | `connection_pool::acquire()` leaks active lease & leaves dangling pointer in `m_live_connections` if `shared_from_this()` throws | **HIGH** | Connection Pool | Use-After-Free / Leak | **RESOLVED** (`f9d3319`) |
| **1.6** | Null pointer dereference in `pooled_connection` forwarding methods when empty/moved-from | **MEDIUM** | Connection Pool | Crash | **RESOLVED** (`eb0e9ca`) |
| **1.7** | Closed `sqlite3*` handle dereferenced by outliving `sqlite_result_set` / `sqlite_statement` | **MEDIUM** | SQLite Driver | Use-After-Free | **RESOLVED** (`2ccbdf5`) |
| **2.1** | Data race on `wsserver::m_socket_path` during concurrent `shutdown()` | **HIGH** | `wsserver` | Concurrency / Data Race | **RESOLVED** (`251bf60`) |
| **2.2** | Premature pool/hook teardown & unsafe `close(fd)` while `FCGX_Accept_r` is blocked in `wsserver::shutdown()` | **HIGH** | `wsserver` | Concurrency / Race | **RESOLVED** (`f28b99d`) |
| **2.3** | `connection_pool::acquire()` hands out new connections after `shutdown()` has begun | **MEDIUM** | Connection Pool | Concurrency | **RESOLVED** (`5af75fa`) |
| **2.4** | Unsynchronized access to `m_workers`, `m_max_queue_size`, and `m_router` in `wsthreadpool` | **LOW** | `wsthreadpool` | Concurrency / Data Race | **RESOLVED** (`a6e9466`) |
| **2.5** | `wsthreadpool::start()` does not reset `m_finished`, breaking pool restart | **LOW** | `wsthreadpool` | Concurrency / Logic | **RESOLVED** (`369aa9b`) |
| **3.1** | `wsworker::m_request` is never cleared after `process()`, leaking keep-alive sockets (`ipcFd`) | **HIGH** | `wsworker` | Resource Leak / Bug | **RESOLVED** (`085ce82`) |
| **3.2** | `postgres_row::get_blob()` returns raw ASCII hex string (`\x...`) instead of decoded binary bytes | **HIGH** | PostgreSQL Driver | Functional Bug | **RESOLVED** (`e3d035c`) |
| **3.3** | `mysql_statement` SQL parser corrupts `:=`, `::`, and standalone `:` into `NULL` or `?` parameters | **HIGH** | MySQL Driver | Functional Bug | **RESOLVED** (`945820e`) |
| **3.4** | `sql_to_json_paginated` never binds query arguments to `count_sql` | **HIGH** | `sql_to_json` | Functional Bug | **RESOLVED** (`650c917`) |
| **3.5** | `sqlite_statement::bind` binds SQL `NULL` instead of empty string/blob when `data() == nullptr` | **MEDIUM** | SQLite Driver | Functional Bug | **RESOLVED** (`b234b3b`) |
| **3.6** | `sqlite_statement::execute()` clears bound parameters & leaves VDBE active on step failure | **MEDIUM** | SQLite Driver | Functional Bug | **RESOLVED** (`eb9412f`) |
| **3.7** | `mysql_row::get_int64()` / `get_int32()` fails to decode binary `BIT(n)` columns | **MEDIUM** | MySQL Driver | Functional Bug | **RESOLVED** (`6633d25`) |
| **3.8** | `postgres_row::get_timestamp()` ignores timezone offsets and reuses dirty `std::tm` buffer | **MEDIUM** | DB Drivers | Functional Bug | **RESOLVED** (`f54404e`) |
| **3.9** | Truncated HTTP request bodies are silently passed to route handlers when stream closes early | **MEDIUM** | `wsworker` | Functional Bug | **RESOLVED** (`faa9f31`) |
| **3.10**| `route::is_exact` ignores `std::regex_constants::icase` and other syntax flags | **MEDIUM** | `wsroute` | Functional Bug | **RESOLVED** (`5b16345`) |
| **3.11**| HTTP 200 headers sent before query execution in `sqlite-demo`, corrupting HTTP 500 error responses | **MEDIUM** | `sqlite-demo` | Functional Bug | **RESOLVED** (`dbf7513`) |
| **3.12**| `execute_sql_to_json_paginated_sink` does not validate `query.callback` when called directly | **MEDIUM** | `sql_to_json` | Security / XSS | **RESOLVED** (`0050cb9`) |
| **3.13**| `connection_pool::evict_stale_locked()` never evicts expired `max_lifetime` connections at or below `min_size` | **MEDIUM** | Connection Pool | Functional Bug | **RESOLVED** (`89e3c35`) |
| **3.14**| `mysql_database::connect()` does not set `utf8mb4` character set & `execute()` ignores `mysql_store_result` errors | **MEDIUM** | MySQL Driver | Functional Bug | **RESOLVED** (`2149648`) |
| **3.15**| `postgres_statement` and `sqlite_row` truncate strings containing embedded null bytes (`\0`) | **LOW** | DB Drivers | Functional Bug | **RESOLVED** (`008e8de`) |
| **3.16**| `journal-demo` reads the oldest 100 journal entries instead of the latest logs | **LOW** | `journal-demo` | Functional Bug | **RESOLVED** (`47dc4c6`) |
| **3.17**| Missing `SIGPIPE` ignore handling across server & database drivers | **MEDIUM** | Server / Demos | Crash / Stability | **RESOLVED** (`83ea948`) |
| **4.1** | `connection_pool` closes database connections while holding the global pool mutex `m_mutex` | **HIGH** | Connection Pool | Performance | **RESOLVED** (`2db1263`) |
| **4.2** | `result_set_to_json` allocates and frees a full `json_object` DOM tree per row | **MEDIUM** | `sql_to_json` | Performance | **RESOLVED** (`bca462a`) |
| **4.3** | `wsworker::parse_request` & `parse_args` perform excessive `std::map` & temporary `std::string` allocations | **MEDIUM** | `wsworker` | Performance | **RESOLVED** (`8fb90b9`) |
| **4.4** | `route` compiles `std::regex` even for exact static routes (`is_exact == true`) | **LOW** | `wsroute` | Performance | **RESOLVED** (`2660dd7`) |
| **4.5** | Single-worker loop in `wsserver::run()` reconstructs `wsworker` on every request | **LOW** | `wsserver` | Performance | **RESOLVED** (`dc733ca`) |
| **4.6** | `wsthreadpool::addWork()` performs blocking 503 I/O on the main accept thread | **LOW** | `wsthreadpool` | Performance | **RESOLVED** (`74a2620`) |

---

## 1. Memory Corruption, Crashes & Undefined Behavior

### [x] 1.1. Data Race & Double-Join Crash in `wsthreadpool::stop()`
* **Files:** [src/wsthreadpool.cpp#L12-L33](file:///home/boris/projects/cpp_ws_backend/src/wsthreadpool.cpp#L12-L33), [src/wsthreadpool.h#L47](file:///home/boris/projects/cpp_ws_backend/src/wsthreadpool.h#L47)
* **Severity:** **CRITICAL**
* **Status:** **RESOLVED** (`f4b6a1f`)
* **Description:**
  `wsthreadpool::stop()` checks `if (m_started)` without holding `m_lock`, then briefly locks `m_lock` only to set `m_finished = true`, and then iterates over `m_thread_pool`, calls `t.join()`, `m_thread_pool.clear()`, and sets `m_started = false` completely outside `m_lock`:
  ```cpp
  void backend::wsthreadpool::stop()
  {
      if (m_started)
      {
          {
              std::unique_lock<std::mutex> lock(m_lock);
              m_finished = true;
          }
          m_data_condition.notify_all();
          for (auto &t : m_thread_pool) {
              if (t.joinable()) t.join();
          }
          m_thread_pool.clear();
          m_started = false;
      }
  }
  ```
  When `server.shutdown()` is triggered from the signal thread in `sqlite-demo` or `journal-demo`, `wsserver::run()` unblocks and returns from `main()`, invoking `wsserver::~wsserver() -> shutdown() -> m_thread_pool.stop()`. Both threads can enter `if (m_started)` concurrently, calling `t.join()` on the same `std::thread` instances and `m_thread_pool.clear()` simultaneously, causing `std::system_error`, heap corruption, or `SIGSEGV`.
* **Fix:**
  Check and clear `m_started` and move `m_thread_pool` into a local `std::vector<std::thread>` while holding `m_lock`, then join the local vector outside the lock.

---

### [x] 1.2. Detached `sig_thread` Holds Dangling Stack Reference to `wsserver`
* **Files:** [demos/sqlite-demo/main.cpp#L165-L179](file:///home/boris/projects/cpp_ws_backend/demos/sqlite-demo/main.cpp#L165-L179), [demos/journal-demo/main.cpp#L112-L126](file:///home/boris/projects/cpp_ws_backend/demos/journal-demo/main.cpp#L112-L126)
* **Severity:** **CRITICAL**
* **Status:** **RESOLVED** (`e41fb92`)
* **Description:**
  Both demos spawn and `detach()` a signal thread at the start of `main()` capturing `&server` by reference:
  ```cpp
  std::thread sig_thread([&server, sig_mask]() {
      int sig = 0;
      sigwait(&sig_mask, &sig);
      server.shutdown();
  });
  sig_thread.detach();
  ```
  If `main()` exits early (e.g., `-h`, invalid CLI option, exception thrown by `g_db_pool` construction or `server.init()`, or `server.run()` returning on socket error), stack variable `server` is destroyed while `sig_thread` remains alive. If a signal arrives during teardown, `sig_thread` calls `server.shutdown()` on a destroyed stack object (use-after-free).
* **Fix:**
  Start the signal handler thread only after CLI parsing and initialization succeed, keep it joinable, and before `main()` exits, send a signal to self (or use `sigtimedwait` / `signalfd` / pipe) and `join()` `sig_thread` before `server` is destroyed.

---

### [x] 1.3. `mysql_real_escape_string` Error Return `(unsigned long)-1` Causes Integer Wrap & Heap Corruption
* **File:** [src/db/mysql_driver.cpp#L186-L193](file:///home/boris/projects/cpp_ws_backend/src/db/mysql_driver.cpp#L186-L193)
* **Severity:** **CRITICAL**
* **Status:** **RESOLVED** (`9409046`)
* **Description:**
  In `escape_sql_string`:
  ```cpp
  out.resize(1 + val.size() * 2 + 1);
  out[0] = '\'';
  unsigned long len = api.mysql_real_escape_string(conn, out.data() + 1, val.data(),
                                                   static_cast<unsigned long>(val.size()));
  out.resize(1 + len);
  out.push_back('\'');
  ```
  `mysql_real_escape_string` returns `(unsigned long)-1` (`ULONG_MAX`) if an error occurs (such as `NO_BACKSLASH_ESCAPES` SQL mode being active without quote context, or invalid charset state). When `len == ULONG_MAX`, `1 + len` wraps around to `0` on 64-bit platforms, or produces a corrupted string `"'"` (missing closing quote, breaking SQL syntax and enabling SQL injection across adjacent parameters!).
* **Fix:**
  Check `if (len == static_cast<unsigned long>(-1))` and throw `db_exception("MySQL string escaping failed")`.

---

### [x] 1.4. Signed `int` Overflow on Large `string_view`/`span` in `sqlite_statement::bind` Causes Buffer Over-Read
* **File:** [src/db/sqlite_driver.cpp#L582-L594](file:///home/boris/projects/cpp_ws_backend/src/db/sqlite_driver.cpp#L582-L594)
* **Severity:** **HIGH**
* **Status:** **RESOLVED** (`88ac488`)
* **Description:**
  `sqlite_statement::bind` casts `val.size()` to `int`:
  ```cpp
  sqlite3_bind_text(m_holder->stmt, static_cast<int>(index), val.data(), static_cast<int>(val.size()), SQLITE_TRANSIENT)
  ```
  If `val.size() > INT_MAX`, `static_cast<int>(val.size())` overflows to a negative integer. In SQLite, passing a negative byte length to `sqlite3_bind_text` instructs SQLite to read up to the first `\0` terminator (`strlen()`), causing an out-of-bounds heap read on a non-null-terminated `std::string_view` (or use `sqlite3_bind_text64` / `sqlite3_bind_blob64` or reject `val.size() > INT_MAX`).

---

### [x] 1.5. `connection_pool::acquire()` Leaks Active Lease & Leaves Dangling Pointer in `m_live_connections` if `shared_from_this()` Throws
* **File:** [src/db/connection_pool.h#L153-L175](file:///home/boris/projects/cpp_ws_backend/src/db/connection_pool.h#L153-L175), [src/db/connection_pool.h#L279](file:///home/boris/projects/cpp_ws_backend/src/db/connection_pool.h#L279)
* **Severity:** **HIGH**
* **Status:** **RESOLVED** (`f9d3319`)
* **Description:**
  `connection_pool` has a public constructor, allowing stack or `unique_ptr` instantiation. At the end of `acquire()` (outside the `try/catch` block):
  ```cpp
  return pooled_connection<Connection>(this->shared_from_this(), std::move(conn), created_at);
  ```
  If `connection_pool` is not owned by a `std::shared_ptr`, `this->shared_from_this()` throws `std::bad_weak_ptr` before `conn` is moved into `pooled_connection`. If `conn` was newly allocated, `conn` is destroyed when stack unwinding exits `acquire()`, leaving its raw pointer in `m_live_connections` (dangling pointer dereferenced by `stats()`) and permanently leaking `m_active_leases` and `m_total_connections`.
* **Fix:**
  Use `this->weak_from_this().lock()` (or wrap in the `try/catch` block), and if `m_factory()` returns `nullptr` during pre-warming in the constructor, throw `db_exception`.

---

### [x] 1.6. Null Pointer Dereference in `pooled_connection` Forwarding Methods
* **File:** [src/db/connection_pool.h#L89-L138](file:///home/boris/projects/cpp_ws_backend/src/db/connection_pool.h#L89-L138)
* **Severity:** **MEDIUM**
* **Status:** **RESOLVED** (`eb0e9ca`)
* **Description:**
  A default-constructed or moved-from `pooled_connection` (or one after `.release()` is called) has `m_conn == nullptr`. Calling `prepare()`, `execute()`, `exec()`, `last_insert_rowid()`, `changes()`, `errmsg()`, or `path()` directly dereferences `m_conn` without a null check, crashing with `SIGSEGV`.
* **Fix:**
  Validate `if (!m_conn)` in `prepare()`, `execute()`, and `exec()` (throwing `db_exception`), and guard `last_insert_rowid()`, `changes()`, `errmsg()`, and `path()` with `m_conn != nullptr`.

---

### [x] 1.7. Closed `sqlite3*` Handle Dereferenced by Outliving `sqlite_result_set` / `sqlite_statement`
* **Files:** [src/db/sqlite_driver.h#L73-L84](file:///home/boris/projects/cpp_ws_backend/src/db/sqlite_driver.h#L73-L84), [src/db/sqlite_driver.cpp#L470](file:///home/boris/projects/cpp_ws_backend/src/db/sqlite_driver.cpp#L470), [src/db/sqlite_driver.cpp#L490-L493](file:///home/boris/projects/cpp_ws_backend/src/db/sqlite_driver.cpp#L490-L493)
* **Severity:** **MEDIUM**
* **Status:** **RESOLVED** (`2ccbdf5`)
* **Description:**
  Unlike `postgres_database` (`std::shared_ptr<pg_conn_holder>`) and `mysql_database` (`std::shared_ptr<mysql_conn_holder>`), `sqlite_stmt_holder` stores a raw `sqlite3 *db` pointer. If `sqlite_database::close()` is called while a `sqlite_result_set` or `sqlite_statement` is still in scope, subsequent calls to `affected_rows()` (`sqlite3_changes(m_holder->db)`) or `next()` (`sqlite3_errmsg(m_holder->db)`) access a closed `sqlite3*` connection handle.
* **Fix:**
  Manage the `sqlite3*` handle via a shared holder or `sqlite3_db_handle(m_holder->stmt)` so lifetime is safely coordinated.

---

## 2. Concurrency, Race Conditions & Thread Safety

### [x] 2.1. Data Race on `wsserver::m_socket_path` During Concurrent `shutdown()`
* **Files:** [src/wsserver.h#L57](file:///home/boris/projects/cpp_ws_backend/src/wsserver.h#L57), [src/wsserver.cpp#L47-L54](file:///home/boris/projects/cpp_ws_backend/src/wsserver.cpp#L47-L54)
* **Severity:** **HIGH**
* **Status:** **RESOLVED** (`251bf60`)
* **Description:**
  `wsserver::shutdown()` reads and modifies `m_socket_path` (`m_socket_path.clear()`) without holding any mutex. Because `shutdown()` is invoked both from the signal handler thread and from `wsserver::~wsserver()`, concurrent execution of `shutdown()` causes a data race on `std::string m_socket_path`.
* **Fix:**
  Protect `m_socket_path` with `m_hooks_mutex` (moving it into a local `std::string` under the lock before calling `unlink()`).

---

### [x] 2.2. Premature Pool/Hook Teardown & Unsafe `close(fd)` While `FCGX_Accept_r` Is Blocked
* **File:** [src/wsserver.cpp#L38-L76](file:///home/boris/projects/cpp_ws_backend/src/wsserver.cpp#L38-L76), [src/wsserver.cpp#L122-L169](file:///home/boris/projects/cpp_ws_backend/src/wsserver.cpp#L122-L169)
* **Severity:** **HIGH**
* **Status:** **RESOLVED** (`f28b99d`)
* **Description:**
  1. `wsserver::m_sock_fd` initializes to `0` instead of `-1`. If `init()` is not called, `m_sock_fd` defaults to file descriptor `0` (`stdin`).
  2. In `wsserver::shutdown()`, calling `close(fd)` while the main thread is blocked inside `FCGX_Accept_r()` (`accept(fd, ...)`) is unsafe on Linux: `close(fd)` does not reliably wake a thread blocked in `accept()`, and frees the file descriptor number `fd` for immediate reuse by other threads while `accept()` is still using it. Calling `::shutdown(fd, SHUT_RDWR)` before closing is required to wake `accept()`.
  3. When `shutdown()` is called from a signal thread, it runs `m_thread_pool.stop()` and `m_shutdown_hooks` (which shuts down the database pool) while `wsserver::run()` may still be mid-request (especially when `workers == 0`, where `m_thread_pool.stop()` is a no-op and `worker.process(request)` runs on the main thread!).

---

### [x] 2.3. `connection_pool::acquire()` Hands Out Connections After `shutdown()` Has Begun
* **File:** [src/db/connection_pool.h#L216-L236](file:///home/boris/projects/cpp_ws_backend/src/db/connection_pool.h#L216-L236), [src/db/connection_pool.h#L247-L279](file:///home/boris/projects/cpp_ws_backend/src/db/connection_pool.h#L247-L279)
* **Severity:** **MEDIUM**
* **Status:** **RESOLVED** (`5af75fa`)
* **Description:**
  In `connection_pool::acquire()`, the mutex `m_mutex` is unlocked while calling `m_factory()`, `conn_reset_if_possible()`, and `conn->ping()`. If `pool->shutdown()` is called during this window, `acquire()` does not re-check `m_shutdown` after re-locking and returns an active lease to the caller after the pool has shut down.

---

### [x] 2.4. Unsynchronized Access to `m_workers`, `m_max_queue_size`, and `m_router` in `wsthreadpool`
* **Files:** [src/wsthreadpool.h#L29-L47](file:///home/boris/projects/cpp_ws_backend/src/wsthreadpool.h#L29-L47), [src/wsthreadpool.cpp#L40-L75](file:///home/boris/projects/cpp_ws_backend/src/wsthreadpool.cpp#L40-L75)
* **Severity:** **LOW**
* **Status:** **RESOLVED** (`a6e9466`)
* **Description:**
  `m_max_queue_size` is read inside `addWork()` under `m_lock`, but `setMaxQueueSize()` and `maxQueueSize()` read/write it without `m_lock` or `std::atomic`. Similarly, `m_workers` and `m_router` are accessed without synchronization.

---

### [x] 2.5. `wsthreadpool::start()` Does Not Reset `m_finished`
* **File:** [src/wsthreadpool.cpp#L54-L70](file:///home/boris/projects/cpp_ws_backend/src/wsthreadpool.cpp#L54-L70)
* **Severity:** **LOW**
* **Status:** **RESOLVED** (`369aa9b`)
* **Description:**
  `wsthreadpool::stop()` sets `m_finished = true`, but `wsthreadpool::start()` never resets `m_finished = false`. If a thread pool is stopped and started again, all spawned worker threads immediately exit their loop, and `addWork()` drops all incoming requests.

---

## 3. Functional Defects, Security & Logic Bugs

### [x] 3.1. `wsworker::m_request` Is Never Cleared After `process()`, Leaking Keep-Alive Sockets (`ipcFd`)
* **File:** [src/wsworker.cpp#L129-L148](file:///home/boris/projects/cpp_ws_backend/src/wsworker.cpp#L129-L148)
* **Severity:** **HIGH**
* **Status:** **RESOLVED** (`085ce82`)
* **Description:**
  At the beginning of `wsworker::process()`, `m_request = request;` stores the `std::shared_ptr<FCGX_Request>` in the worker member variable, and never resets it when `process()` exits.
  Because worker threads in `wsthreadpool` persist across the lifetime of the server, each idle worker retains the `shared_ptr<FCGX_Request>` of its most recent request until a new request is assigned to that specific worker. Consequently:
  1. `make_fcgx_request()`'s custom deleter (`FCGX_Free(r, 1)`) is not executed when the request finishes.
  2. When a web server uses FastCGI keep-alive (`FCGI_KEEP_CONN`), `FCGX_Finish_r()` leaves `request->ipcFd` open (`FCGX_Free(r, 0)`), so the client/upstream socket file descriptor remains open and hanging in idle workers.
* **Fix:**
  Reset `m_request.reset()` inside the RAII scope guard at the exit of `wsworker::process()`.

---

### [x] 3.2. `postgres_row::get_blob()` Returns Raw ASCII Hex String (`\x...`) Instead of Decoded Binary Bytes
* **File:** [src/db/postgres_driver.cpp#L259-L266](file:///home/boris/projects/cpp_ws_backend/src/db/postgres_driver.cpp#L259-L266), [src/db/postgres_driver.cpp#L552-L561](file:///home/boris/projects/cpp_ws_backend/src/db/postgres_driver.cpp#L552-L561)
* **Severity:** **HIGH**
* **Status:** **RESOLVED** (`e3d035c`)
* **Description:**
  `postgres_statement::query()` calls `PQexecParams` with `resultFormat = 0` (text format). In text format, PostgreSQL encodes `BYTEA` columns as hex strings prefixed with `\x` (e.g., `"\xdeadbeef"`).
  `postgres_row::get_blob()` directly returns `PQgetvalue` as a `std::span<const uint8_t>` without decoding the hex/escape representation! Reading a 4-byte `BYTEA` value returns the 10-byte ASCII string `"\xdeadbeef"`, and `sql_to_json` base64-encodes the ASCII hex characters instead of the binary payload.
* **Fix:**
  Decode PostgreSQL hex (`\x...`) and octal bytea escape sequences (or use `PQunescapeBytea`) into a cached buffer in `postgres_row` before returning the `std::span<const uint8_t>`.

---

### [x] 3.3. `mysql_statement` SQL Parser Corrupts `:=`, `::`, and Standalone `:` Into `NULL` or `?` Parameters
* **File:** [src/db/mysql_driver.cpp#L575-L605](file:///home/boris/projects/cpp_ws_backend/src/db/mysql_driver.cpp#L575-L605), [src/db/mysql_driver.cpp#L616-L626](file:///home/boris/projects/cpp_ws_backend/src/db/mysql_driver.cpp#L616-L626)
* **Severity:** **HIGH**
* **Status:** **RESOLVED** (`945820e`)
* **Description:**
  In `mysql_statement`'s SQL parser:
  1. When `:` is not followed by `[a-zA-Z0-9_]` (for example, MySQL user variable assignment `SET @n := 0`), `name` is extracted as `""` (empty string). Unlike `postgres_statement`, `mysql_statement` does not check `if (name.empty())`! It strips the `:` from the SQL query and inserts a parameter chunk with `name = ""`.
  2. Worse, positional `?` placeholders also push `""` into `m_param_names`. Thus, `:=` is either replaced with `NULL=` or bound to the first `?` positional parameter!
  3. In `mysql_statement::get_param_index(name)`, `if (m_param_names[i] == name)` does not check `!m_param_names[i].empty()`, allowing `bind("", ...)` or `bind(":", ...)` to overwrite the first positional `?` parameter.
* **Fix:**
  Skip `::` and `:=` (or when `name.empty()`, append `':'` to `cur_text` and `continue`), and check `!m_param_names[i].empty()` in parameter name lookups.

---

### [x] 3.4. `sql_to_json_paginated` Never Binds Query Arguments to `count_sql`
* **Files:** [src/db/sql_to_json.h#L216-L242](file:///home/boris/projects/cpp_ws_backend/src/db/sql_to_json.h#L216-L242), [src/db/sql_to_json.cpp#L245-L251](file:///home/boris/projects/cpp_ws_backend/src/db/sql_to_json.cpp#L245-L251), [src/db/sql_to_json.cpp#L295-L301](file:///home/boris/projects/cpp_ws_backend/src/db/sql_to_json.cpp#L295-L301)
* **Severity:** **HIGH**
* **Status:** **RESOLVED** (`650c917`)
* **Description:**
  When `sql_to_json_paginated(sink, db, query, args...)` is called with filter arguments `args...`, the arguments are bound only to `data_stmt` (`query.data_sql`). Inside `execute_sql_to_json_paginated_sink` and `execute_sql_to_json_paginated_c`, `query.count_sql` is prepared and executed with **zero parameters bound**:
  ```cpp
  if (!query.count_sql.empty()) {
      auto count_stmt = db.prepare(query.count_sql);
      auto count_res = count_stmt->query();
  ```
  Any parameterized `count_sql` (e.g., `SELECT COUNT(*) FROM orders WHERE user_id = ?`) executes with unbound `NULL` parameters and returns `total_records = 0` (or throws on drivers requiring bound parameters).
* **Fix:**
  Prepare `count_stmt` in `sql_to_json_paginated` and bind `args...` to `count_stmt` whenever `sizeof...(Args) > 0` and `!query.count_sql.empty()`.

---

### [x] 3.5. `sqlite_statement::bind` Binds SQL `NULL` Instead of Empty String/Blob When `data() == nullptr`
* **File:** [src/db/sqlite_driver.cpp#L582-L594](file:///home/boris/projects/cpp_ws_backend/src/db/sqlite_driver.cpp#L582-L594)
* **Severity:** **MEDIUM**
* **Status:** **RESOLVED** (`b234b3b`)
* **Description:**
  In SQLite's C API, passing a `nullptr` data pointer to `sqlite3_bind_text` or `sqlite3_bind_blob`—even with `n = 0`—binds a SQL `NULL` rather than an empty string `""` or zero-byte blob `x''`.
  When a caller passes a default-constructed `std::string_view{}` or `std::span<const uint8_t>{}` (`data() == nullptr`, `size() == 0`), `sqlite_statement::bind` passes `val.data()` (`nullptr`) directly to SQLite, silently inserting `NULL` into `NOT NULL` columns.
* **Fix:**
  Pass `val.data() ? val.data() : ""` to `sqlite3_bind_text` and `val.data() ? static_cast<const void*>(val.data()) : static_cast<const void*>("")` to `sqlite3_bind_blob`.

---

### [x] 3.6. `sqlite_statement::execute()` Clears Bound Parameters & Leaves VDBE Active on Step Failure
* **File:** [src/db/sqlite_driver.cpp#L629-L633](file:///home/boris/projects/cpp_ws_backend/src/db/sqlite_driver.cpp#L629-L633), [src/db/sqlite_driver.cpp#L652-L659](file:///home/boris/projects/cpp_ws_backend/src/db/sqlite_driver.cpp#L652-L659)
* **Severity:** **MEDIUM**
* **Status:** **RESOLVED** (`eb9412f`)
* **Description:**
  1. `sqlite_statement::execute()` calls `step_done()` followed by `reset()`. Because `reset()` calls `sqlite3_clear_bindings()`, `execute()` wipes all bound parameters after execution (unlike `postgres_statement::execute()` and `mysql_statement::execute()`, which preserve bindings unless `reset()` is explicitly called).
  2. In `step_done()`, if `sqlite3_step()` returns an error (e.g., `SQLITE_CONSTRAINT`), an exception is thrown without calling `sqlite3_reset(m_holder->stmt)`, leaving the SQLite VDBE cursor open and holding locks until `sqlite_statement` is destroyed.
* **Fix:**
  In `step_done()`, always call `sqlite3_reset(m_holder->stmt)` before throwing on error, and in `execute()`, call `sqlite3_reset(m_holder->stmt)` without clearing bindings.

---

### [x] 3.7. `mysql_row::get_int64()` / `get_int32()` Fails to Decode Binary `BIT(n)` Columns
* **File:** [src/db/mysql_driver.cpp#L394](file:///home/boris/projects/cpp_ws_backend/src/db/mysql_driver.cpp#L394), [src/db/mysql_driver.cpp#L428-L442](file:///home/boris/projects/cpp_ws_backend/src/db/mysql_driver.cpp#L428-L442)
* **Severity:** **MEDIUM**
* **Status:** **RESOLVED** (`6633d25`)
* **Description:**
  `mysql_row::get_column_type()` maps `MYSQL_TYPE_BIT` (`16`) to `column_type::integer`. However, MySQL transmits `BIT(n)` values as raw big-endian binary bytes (e.g., `"\x01"` for `b'1'`), not ASCII digits. `mysql_row::get_int64()` attempts `std::from_chars` / `std::stoll` on the raw byte buffer, failing and returning `0` for all non-zero `BIT` columns.
* **Fix:**
  When `m_holder->fields[index].type == 16` (`MYSQL_TYPE_BIT`) and length is 1..8, unpack the big-endian bytes into `uint64_t`/`int64_t` if ASCII digit parsing does not consume the buffer.

---

### [x] 3.8. `get_timestamp()` Ignores Timezone Offsets and Reuses Dirty `std::tm` Buffers
* **Files:** [src/db/postgres_driver.cpp#L268-L279](file:///home/boris/projects/cpp_ws_backend/src/db/postgres_driver.cpp#L268-L279), [src/db/sqlite_driver.cpp#L442-L447](file:///home/boris/projects/cpp_ws_backend/src/db/sqlite_driver.cpp#L442-L447), [src/db/mysql_driver.cpp#L475-L481](file:///home/boris/projects/cpp_ws_backend/src/db/mysql_driver.cpp#L475-L481)
* **Severity:** **MEDIUM**
* **Status:** **RESOLVED** (`f54404e`)
* **Description:**
  1. In all three drivers, `std::tm tm_buf{}` is passed to chained `strptime(...) || strptime(...)` calls. A failed `strptime` call partially mutates `tm_buf` before returning `nullptr`, corrupting subsequent fallback `strptime` parses (e.g., `%Y-%m-%d`).
  2. In `postgres_row::get_timestamp()`, PostgreSQL `TIMESTAMPTZ` values include fractional seconds and a timezone offset (e.g., `2026-11-15 15:22:29.123+01`). `strptime("%Y-%m-%d %H:%M:%S", ...)` stops at `.123+01` and ignores the timezone offset, causing `timegm(&tm_buf)` to misinterpret offset timestamps as UTC.

---

### [x] 3.9. Truncated HTTP Request Bodies Silently Passed to Route Handlers
* **File:** [src/wsworker.cpp#L179-L189](file:///home/boris/projects/cpp_ws_backend/src/wsworker.cpp#L179-L189)
* **Severity:** **MEDIUM**
* **Status:** **RESOLVED** (`faa9f31`)
* **Description:**
  When reading the request payload of size `contentLen`, if the client connection drops early (`FCGX_GetStr` returns `<= 0` before `total_read == contentLen`), `wsworker::process()` resizes `m_body` to `total_read` and invokes the route handler with an incomplete/truncated body instead of returning `400 Bad Request`.

---

### [x] 3.10. `route::is_exact` Ignores `std::regex_constants::icase` and Other Syntax Flags
* **Files:** [src/wsroute.h#L50-L56](file:///home/boris/projects/cpp_ws_backend/src/wsroute.h#L50-L56), [src/wsworker.cpp#L224-L231](file:///home/boris/projects/cpp_ws_backend/src/wsworker.cpp#L224-L231)
* **Severity:** **MEDIUM**
* **Status:** **RESOLVED** (`5b16345`)
* **Description:**
  `route` sets `is_exact(!contains_regex_meta(pattern))` regardless of the `flags` argument. If a route is registered with `std::regex_constants::icase` (e.g., `route("/api/items", GET, fn, std::regex_constants::icase)`), `wsworker::process()` uses case-sensitive `path == route.pattern_str`, breaking case-insensitive matching.
* **Fix:**
  Only enable `is_exact` when `(flags & std::regex_constants::icase) == 0`.

---

### [x] 3.11. HTTP 200 Headers Sent Before Query Execution in `sqlite-demo`, Corrupting Error Responses
* **File:** [demos/sqlite-demo/main.cpp#L78-L108](file:///home/boris/projects/cpp_ws_backend/demos/sqlite-demo/main.cpp#L78-L108), [demos/sqlite-demo/main.cpp#L113-L144](file:///home/boris/projects/cpp_ws_backend/demos/sqlite-demo/main.cpp#L113-L144), [demos/sqlite-demo/main.cpp#L149-L158](file:///home/boris/projects/cpp_ws_backend/demos/sqlite-demo/main.cpp#L149-L158)
* **Severity:** **MEDIUM**
* **Status:** **RESOLVED** (`dbf7513`)
* **Description:**
  1. `request_ws_jsGrid_customers`, `request_ws_jsGrid_artists`, and `request_ws_jsGrid_artist_song_type` call `send_http_headers(worker->out(), ...)` (emitting `HTTP/1.1 200 OK\r\n\r\n`) **before** calling `sql_to_json_paginated` / `sql_to_json`. If `callback` validation or SQL execution throws an exception, `wsworker::process()` catches it and appends `Status: 500 Internal Server Error\r\n\r\nInternal Server Error` into the body of the already-committed HTTP 200 response.
  2. Client input validation errors (`page_size <= 0`, invalid `sidx`/`sord`, `id <= 0`) throw `std::invalid_argument`, which `wsworker` turns into `500 Internal Server Error` instead of `400 Bad Request`, and `page_size` has no upper limit.

---

### [x] 3.12. `execute_sql_to_json_paginated_sink` Does Not Validate `query.callback` When Called Directly
* **File:** [src/db/sql_to_json.cpp#L232-L243](file:///home/boris/projects/cpp_ws_backend/src/db/sql_to_json.cpp#L232-L243)
* **Severity:** **MEDIUM (SECURITY)**
* **Status:** **RESOLVED** (`0050cb9`)
* **Description:**
  `is_valid_callback(query.callback)` is checked in the template wrapper `sql_to_json_paginated`, but not inside `execute_sql_to_json_paginated_sink` (which is part of the public header API in `sql_to_json.h`). Calling `execute_sql_to_json_paginated_sink` directly with an untrusted `query.callback` allows arbitrary JavaScript injection (XSS via JSONP).
* **Fix:**
  Move or duplicate the `if (!query.callback.empty() && !is_valid_callback(query.callback))` check inside `execute_sql_to_json_paginated_sink`.

---

### [x] 3.13. `connection_pool::evict_stale_locked()` Never Evicts Expired `max_lifetime` Connections at or Below `min_size`
* **File:** [src/db/connection_pool.h#L200-L210](file:///home/boris/projects/cpp_ws_backend/src/db/connection_pool.h#L200-L210), [src/db/connection_pool.h#L397-L417](file:///home/boris/projects/cpp_ws_backend/src/db/connection_pool.h#L397-L417)
* **Severity:** **MEDIUM**
* **Status:** **RESOLVED** (`89e3c35`)
* **Description:**
  `evict_stale_locked()` only evicts connections while `m_total_connections > m_options.min_size`. When `m_total_connections <= m_options.min_size`, idle connections whose `max_lifetime` has expired are never evicted, and `acquire()` pops and returns them from `m_idle.front()` without checking `max_lifetime`.

---

### [x] 3.14. `mysql_database::connect()` Does Not Set `utf8mb4` Charset & `execute()` Ignores `mysql_store_result` Errors
* **File:** [src/db/mysql_driver.cpp#L926-L936](file:///home/boris/projects/cpp_ws_backend/src/db/mysql_driver.cpp#L926-L936), [src/db/mysql_driver.cpp#L951-L964](file:///home/boris/projects/cpp_ws_backend/src/db/mysql_driver.cpp#L951-L964)
* **Severity:** **MEDIUM**
* **Status:** **RESOLVED** (`2149648`)
* **Description:**
  1. `mysql_database::connect()` does not configure `utf8mb4` on the connection (`mysql_set_character_set(conn, "utf8mb4")`), risking `latin1` default encoding and improper multibyte escaping in `mysql_real_escape_string`.
  2. `mysql_database::execute()` calls `mysql_store_result(m_conn->conn)` to drain any result set, but unlike `mysql_statement::execute()`, it does not check `if (!res && api.mysql_errno(m_conn->conn) != 0)`.

---

### [x] 3.15. `postgres_statement` and `sqlite_row` Truncate Strings Containing Embedded Null Bytes (`\0`)
* **Files:** [src/db/sqlite_driver.cpp#L414-L418](file:///home/boris/projects/cpp_ws_backend/src/db/sqlite_driver.cpp#L414-L418), [src/db/sqlite_driver.cpp#L693-L697](file:///home/boris/projects/cpp_ws_backend/src/db/sqlite_driver.cpp#L693-L697), [src/db/postgres_driver.cpp#L283-L297](file:///home/boris/projects/cpp_ws_backend/src/db/postgres_driver.cpp#L283-L297)
* **Severity:** **LOW**
* **Status:** **RESOLVED** (`008e8de`)
* **Description:**
  1. `sqlite_row::get_string()` and `sqlite_statement::column_str()` construct `std::string(txt)` without using `sqlite3_column_bytes()`, truncating strings at the first embedded `\0` and performing an unnecessary `strlen()` scan.
  2. `postgres_result_set::next()` does not reset `m_row.set_row_index(-1)` when `next()` returns `false` at the end of the result set, leaving `current_row()` pointing to the final row.
  3. Out-of-range column indices (`index >= column_count()`) in `sqlite_row` and `postgres_row` are passed unchecked into `sqlite3_column_*` and `PQget*` functions.

---

### [x] 3.16. `journal-demo` Reads the Oldest 100 Journal Entries Instead of Recent Logs
* **File:** [demos/journal-demo/main.cpp#L80-L95](file:///home/boris/projects/cpp_ws_backend/demos/journal-demo/main.cpp#L80-L95)
* **Severity:** **LOW**
* **Status:** **RESOLVED** (`47dc4c6`)
* **Description:**
  `sd_journal_open` positions the read cursor at the head (oldest entry) of the systemd journal. Iterating with `sd_journal_next` up to `c == 100` always returns the oldest 100 entries since the journal was created rather than the most recent 100 entries (`sd_journal_seek_tail` + `sd_journal_previous`), and increments `c` even on entries that lack a `MESSAGE` field.

---

### [x] 3.17. Missing `SIGPIPE` Ignore Handling Across Server & Database Drivers
* **Files:** [src/wsserver.cpp#L11-L15](file:///home/boris/projects/cpp_ws_backend/src/wsserver.cpp#L11-L15), [demos/sqlite-demo/main.cpp#L167-L172](file:///home/boris/projects/cpp_ws_backend/demos/sqlite-demo/main.cpp#L167-L172), [demos/journal-demo/main.cpp#L114-L119](file:///home/boris/projects/cpp_ws_backend/demos/journal-demo/main.cpp#L114-L119)
* **Severity:** **MEDIUM**
* **Status:** **RESOLVED** (`83ea948`)
* **Description:**
  Neither `wsserver` nor the demo applications ignore or block `SIGPIPE` (`signal(SIGPIPE, SIG_IGN)`). If an upstream FastCGI peer or remote PostgreSQL/MySQL server abruptly closes a socket while the backend writes to it, the kernel delivers `SIGPIPE`, terminating the entire backend process immediately.

---

## 4. Performance Bottlenecks

### [x] 4.1. `connection_pool` Closes Database Connections While Holding the Global Pool Mutex `m_mutex`
* **File:** [src/db/connection_pool.h#L321-L329](file:///home/boris/projects/cpp_ws_backend/src/db/connection_pool.h#L321-L329), [src/db/connection_pool.h#L397-L417](file:///home/boris/projects/cpp_ws_backend/src/db/connection_pool.h#L397-L417)
* **Severity:** **HIGH (PERFORMANCE)**
* **Status:** **RESOLVED** (`2db1263`)
* **Description:**
  `evict_stale_locked()` is called inside `acquire()` while holding `m_mutex`. When idle connections expire, `evict_stale_locked()` calls `conn_disconnect_if_possible()` and destroys the `Connection` object (running `sqlite3_close_v2` + finalizing all cached statements, `PQfinish`, or `mysql_close` network teardown) **while holding `m_mutex`**. This stalls all other worker threads trying to acquire or release database connections.
* **Fix:**
  Move evicted `std::unique_ptr<Connection>` instances into a temporary local vector while holding `m_mutex`, and disconnect/destroy them after unlocking `m_mutex`.

---

### [x] 4.2. `result_set_to_json` Allocates and Frees a Full `json_object` DOM Tree Per Row
* **File:** [src/db/sql_to_json.cpp#L186-L197](file:///home/boris/projects/cpp_ws_backend/src/db/sql_to_json.cpp#L186-L197)
* **Severity:** **MEDIUM (PERFORMANCE)**
* **Status:** **RESOLVED** (`bca462a`)
* **Description:**
  In `result_set_to_json(stream_sink &sink, result_set &res)`:
  ```cpp
  while (res.next()) {
      ...
      json_object_guard row_obj(row_to_json_c(res.current_row(), col_names));
      const char *json_str = row_obj.to_string();
      if (json_str) {
          sink.write(json_str);
      }
  }
  ```
  Every row allocates a `json_object` hash table (`lh_table`), allocates a `json_object` per column, formats the object into a `printbuf`, copies it into `sink`, and frees all objects. Also, `json_object_to_json_string_ext` is passed to `sink.write(const char*)`, which runs `strlen()` over the serialized row instead of using `json_object_to_json_string_length()`.

---

### [x] 4.3. `wsworker::parse_request` & `parse_args` Perform Excessive `std::map` & Temporary `std::string` Allocations
* **File:** [src/wsworker.cpp#L20-L51](file:///home/boris/projects/cpp_ws_backend/src/wsworker.cpp#L20-L51), [src/wsworker.cpp#L68-L127](file:///home/boris/projects/cpp_ws_backend/src/wsworker.cpp#L68-L127), [src/wsworker.cpp#L152-L204](file:///home/boris/projects/cpp_ws_backend/src/wsworker.cpp#L152-L204)
* **Severity:** **MEDIUM (PERFORMANCE)**
* **Status:** **RESOLVED** (`8fb90b9`)
* **Description:**
  1. `url_decode(const std::string &src)` takes `const std::string &` instead of `std::string_view`, forcing `parse_args()` to heap-allocate a temporary `std::string(item.substr(...))` for every key and every value before `url_decode` allocates another `std::string`.
  2. `wsworker::process()` performs redundant `std::map` lookups (`header.count("CONTENT_LENGTH")` followed by `header["CONTENT_LENGTH"]`, and `header.count("REQUEST_METHOD")` followed by `header["REQUEST_METHOD"]`), and mutates `header` via `header["SCRIPT_NAME"]`.

---

### [x] 4.4. `route` Compiles `std::regex` Even for Exact Static Routes (`is_exact == true`)
* **File:** [src/wsroute.h#L50-L56](file:///home/boris/projects/cpp_ws_backend/src/wsroute.h#L50-L56)
* **Severity:** **LOW (PERFORMANCE)**
* **Status:** **RESOLVED** (`2660dd7`)
* **Description:**
  In `route::route(std::string_view pattern, ...)`, `uri(pattern.data(), pattern.size(), flags)` is unconditionally compiled even when `is_exact` is `true` and `uri` will never be used during matching.

---

### [x] 4.5. Single-Worker Loop in `wsserver::run()` Reconstructs `wsworker` on Every Request
* **File:** [src/wsserver.cpp#L130-L145](file:///home/boris/projects/cpp_ws_backend/src/wsserver.cpp#L130-L145)
* **Severity:** **LOW (PERFORMANCE)**
* **Status:** **RESOLVED** (`dc733ca`)
* **Description:**
  When `m_thread_pool.count() == 0`, `backend::wsworker worker(m_router);` is instantiated inside the `while (1)` loop on every request instead of outside the loop, preventing reuse of `m_body`'s pre-allocated capacity across requests.

---

### [x] 4.6. `wsthreadpool::addWork()` Performs Blocking 503 I/O on the Main Accept Thread
* **File:** [src/wsthreadpool.cpp#L90-L99](file:///home/boris/projects/cpp_ws_backend/src/wsthreadpool.cpp#L90-L99)
* **Severity:** **LOW (PERFORMANCE)**
* **Status:** **RESOLVED** (`74a2620`)
* **Description:**
  When the work queue reaches `m_max_queue_size`, `addWork()` synchronously calls `FCGX_FPrintF` and `FCGX_Finish_r` on the caller thread (the main `wsserver::run()` accept loop). A slow or stalled client socket can block the accept thread during overload.
