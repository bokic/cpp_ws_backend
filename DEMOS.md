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

---

## 3. Jira-Like Project Management Demo (`demos/jiralike-demo`)

A full-stack Agile project and issue tracking application powered by `cpp_ws_backend`, SQLite with automatic startup schema versioning/migrations, local `blobs/` file attachment storage, and an Angular Single-Page Application (SPA) frontend.

* **Location:** [`demos/jiralike-demo`](demos/jiralike-demo)
* **Highlights:**
  * Automatic blank SQLite database creation with a first-time Webapp Setup Wizard (`/setup`).
  * Startup `schema_version` inspection and sequential SQL migration runner (`db/migrations/`).
  * Supports Epics, Stories, Tasks, Subtasks, Bugs, and Issues with comments, issue links, and file uploads to `blobs/`.
  * Interactive Kanban State Board (`/board`) and Gantt / State Timeline Matrix (`/gantt`) with drag-and-drop workflow state transitions.
  * Packaging configurations for Docker (`docker/`), Ubuntu/Debian `.deb` (`Ubuntu/`), and Arch Linux / AUR (`Arch/`).
* **Running:**
  ```bash
  cd demos/jiralike-demo
  ./jiralike-demo -n 127.0.0.1:9002 -w 4
  ```
