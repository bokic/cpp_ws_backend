# Demo Applications

This project includes standalone demo applications demonstrating how to build FastCGI backends using `cpp_ws_backend`.

---

## 1. SQLite Demo (`demos/sqlite-demo`)

A database-backed FastCGI web backend integrated with SQLite (`chinook.db`) and `json-c`, accompanied by an interactive frontend UI.

* **Location:** [`demos/sqlite-demo`](demos/sqlite-demo)
* **Endpoints:**
  * `POST /ws/db/tables` - Returns a JSON list of database tables.
  * `GET /ws/jsGrid/customers` - Paginated customer records with sorting and optional JSONP callback support.
  * `GET /ws/jsGrid/artists` - Paginated artist records with album and track counts.
  * `POST /ws/jsGrid/artist_song_type` - Genre breakdown and song counts for a specified artist ID.
* **Frontend:**
  * Interactive web interface powered by jQuery and jqGrid under [`demos/sqlite-demo/www`](demos/sqlite-demo/www).
* **Running:**
  ```bash
  cd demos/sqlite-demo
  ./sqlite-demo -n 127.0.0.1:9000 -w 4
  ```

---

## 2. System Journal Demo (`demos/journal-demo`)

A FastCGI backend demonstrating integration with the systemd journal API (`sd_journal`).

* **Location:** [`demos/journal-demo`](demos/journal-demo)
* **Endpoints:**
  * `GET /ws/system/logs` or `POST /ws/system/logs` - Streams recent system log messages formatted as a JSON array.
* **Dependencies:**
  * `systemd` (`libsystemd`)
  * `json-c`
* **Running:**
  ```bash
  cd demos/journal-demo
  ./journal-demo -n 127.0.0.1:9001 -w 2
  ```
