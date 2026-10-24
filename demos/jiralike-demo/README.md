# Jira-Like Project Management Demo (`jiralike-demo`)

A full-stack, Jira-inspired Agile project and issue tracking demo application powered by a **`cpp_ws_backend` C++23 FastCGI backend**, **SQLite** database, local **filesystem blob storage** for attachments, and an **Angular Single-Page Application (SPA)** frontend.

---

## Overview

`jiralike-demo` demonstrates how to build a modern, stateful, multi-user web application with `cpp_ws_backend`. It provides end-to-end Agile project management workflows including user authentication, hierarchical work item tracking, interactive drag-and-drop board management, file attachment uploads, and custom workflow state transitions.

---

## Key Features

### 1. First-Time Webapp Setup, Authentication & User Management
- **First-Time Webapp Setup Page (`/setup`)**:
  - When the backend starts and the SQLite database file (`db/jiralike.db`) does not yet exist, it automatically creates a blank database, applies the schema migrations, and enters **unconfigured / setup mode**.
  - On launch, the Angular SPA checks `/api/setup/status`; if the instance is unconfigured, it redirects the user to an interactive **Webapp Setup Wizard** to configure the instance name, create the initial `Admin` account, define the first project (name & key prefix, e.g., `JIRA`), and optionally populate sample demo issues.
- **Login Page (`/login`)**: Session/token-based authentication with role-based permissions (`Admin`, `Project Manager`, `Developer`, `Reporter`).
- **User Profiles**: Avatar support, assigned work items overview, and activity history.

### 2. Work Item Hierarchy & Types
Support for standard Jira-like work elements with parent-child and linking relationships:
- **Epic**: Large bodies of work that can be broken down into stories and tasks.
- **Story**: User-centric feature specifications and requirements.
- **Task**: General engineering or operational work items.
- **Subtask**: Granular child items belonging to a parent Story, Task, or Issue.
- **Bug**: Defect reports with severity, reproduction steps, and affected version metadata.
- **Issue**: General project issues, impediments, or change requests.

Each work item supports:
- Unique project key (e.g., `PROJ-101`)
- Title, rich description, priority (`Lowest`, `Low`, `Medium`, `High`, `Highest`, `Critical`)
- Story points / time estimates & due dates (for Kanban and Gantt scheduling)
- Assignee, reporter, labels, sprint assignment, and comments
- File attachments stored in the `blobs/` directory

### 3. Interactive Board & Gantt Views
- **Kanban / State Board**:
  - Visual columns representing workflow states (`Backlog`, `To Do`, `In Progress`, `In Review`, `QA / Testing`, `Done`, `Blocked`).
  - **Drag-and-Drop**: Move work items across state columns to trigger workflow transitions and persist status updates in real time.
  - Swimlanes and quick filters by assignee, work item type, epic, priority, or sprint.
- **Gantt / Timeline View**:
  - Visual schedule of epics, stories, and tasks across time, showing current state, progress, start/end dates, and item dependencies.

### 4. Issue Creation & Workflow Pages
- **Create / Edit Work Item Page & Modal**:
  - Dedicated view and quick-create modal for creating Stories, Tasks, Subtasks, Issues, and Bugs.
  - Dynamic fields based on item type (e.g., parent selector for Subtasks, severity/steps to reproduce for Bugs).
  - Drag-and-drop file attachment upload directly into the item.
- **Work Item Detail View**:
  - Full item inspection, inline field editing, subtask list, linked issues (`blocks`, `is blocked by`, `relates to`), comment thread, attachment gallery/downloads, and transition audit log.
- **Backlog & Sprint Planning**:
  - Organize upcoming work, estimate story points, and manage active/completed sprints.

---

## Architecture & Directory Structure

```text
demos/jiralike-demo/
├── CMakeLists.txt          # CMake build configuration for the FastCGI backend
├── README.md               # Project documentation
├── main.cpp                # FastCGI entry point, DB migration runner & route registration
├── db/
│   ├── schema.sql          # Base SQLite schema definition (v1) and seed data
│   ├── migrations/         # Sequential SQL upgrade scripts (v1 -> v2 -> ... -> latest)
│   │   ├── 001_init.sql
│   │   └── 002_...sql
│   └── jiralike.db         # SQLite database file (created/migrated at startup)
├── blobs/                  # File upload storage directory (attachments, avatars)
├── src/                    # C++ backend handlers (auth, items, workflows, blobs, db_migrator)
├── nginx/                  # Sample Nginx FastCGI + Angular SPA configuration
├── docker/                 # Dockerfile & docker-compose.yml for containerized deployment
├── Ubuntu/                 # Debian/Ubuntu (.deb) packaging scripts & control files
├── Arch/                   # Arch Linux / AUR (PKGBUILD) packaging files
└── web/                    # Angular Single-Page Application (SPA) source
```

---

## Storage Design & Schema Migrations

### Automatic Database Versioning & Upgrade Mechanism
- **`schema_version` Table**: The SQLite database maintains a metadata table (`schema_version`) storing the current integer schema version, applied timestamp, and migration description:
  ```sql
  CREATE TABLE IF NOT EXISTS schema_version (
      version     INTEGER PRIMARY KEY,
      applied_at   TEXT NOT NULL DEFAULT (datetime('now')),
      description TEXT
  );
  ```
- **Startup Version Check, Blank DB Creation & Incremental Upgrades**:
  1. When `jiralike-demo` starts up, before accepting FastCGI requests, it checks whether `db/jiralike.db` exists.
  2. **Blank Database & Setup Mode**: If `db/jiralike.db` does not exist, the backend creates a new blank SQLite database file, runs all schema migrations (`001_init.sql` through the latest version), and marks the application state as **setup required** (no admin user / initial project configured yet). When a user opens the Angular webapp, they are automatically routed to the **Webapp Setup Page (`/setup`)** to complete initial provisioning.
  3. **Incremental Schema Upgrades**: If the database already exists, the backend inspects the `schema_version` table. If the stored version $V_{\text{current}}$ is older than the backend's latest target version $V_{\text{latest}}$, the backend sequentially executes each upgrade script (`db/migrations/`) from $V_{\text{current}} + 1$ up to $V_{\text{latest}}$.
  4. Each version upgrade runs atomically inside an SQLite transaction (`BEGIN IMMEDIATE ... COMMIT`), updating `schema_version` upon success and rolling back with a fatal startup error if any migration step fails.

### SQLite Database Tables (`db/jiralike.db`)
- **`schema_version`**: Tracks current schema version and migration history.
- **`app_config`**: Stores instance configuration and setup state (`is_setup_complete`, `instance_name`, `created_at`).
- **`users`**: User accounts, password hashes, display names, roles, and avatar blob references.
- **`sessions`**: Active authentication tokens and expiration timestamps.
- **`projects`**: Project definitions (`id`, `key`, `name`, `lead_user_id`).
- **`sprints`**: Sprint lifecycle (`planned`, `active`, `closed`, start/end dates).
- **`work_items`**: Core table storing Epics, Stories, Tasks, Subtasks, Bugs, and Issues (`id`, `project_id`, `key`, `type`, `status`, `priority`, `title`, `description`, `parent_id`, `sprint_id`, `assignee_id`, `reporter_id`, `story_points`, `start_date`, `due_date`, `created_at`, `updated_at`).
- **`work_item_links`**: Relationships between items (`blocks`, `relates_to`, `duplicates`).
- **`comments`**: Discussion threads attached to work items.
- **`attachments`**: Metadata for uploaded files (`id`, `work_item_id`, `uploader_id`, `original_filename`, `mime_type`, `size_bytes`, `blob_filename`, `created_at`).
- **`workflow_history`**: Audit log recording state transitions (`from_status`, `to_status`, `changed_by`, `timestamp`).

### Blob Storage (`blobs/`)
- Uploaded files are stored on disk under `demos/jiralike-demo/blobs/` using content-addressed or UUID-based filenames to prevent path traversal and naming collisions.
- Original filenames, MIME types, file sizes, and associations with work items are tracked in the SQLite `attachments` table.

---

## REST / FastCGI API Endpoints

| Method | Endpoint | Description |
| :--- | :--- | :--- |
| `GET` | `/api/setup/status` | Check whether initial database/webapp setup has been completed |
| `POST` | `/api/setup/init` | Initialize blank database with Admin user, initial project, and optional seed data |
| `POST` | `/api/auth/login` | Authenticate user credentials and return session token |
| `POST` | `/api/auth/logout` | Invalidate current session token |
| `GET` | `/api/auth/me` | Retrieve current authenticated user profile |
| `GET` | `/api/projects` | List all projects and workflow configurations |
| `GET` | `/api/items` | Query work items (filter by project, state, type, assignee, sprint) |
| `POST` | `/api/items` | Create a new Task, Story, Subtask, Issue, Bug, or Epic |
| `GET` | `/api/items/([0-9]+)` | Get full work item details, subtasks, comments, and attachments |
| `PUT` | `/api/items/([0-9]+)` | Update work item fields |
| `PATCH` | `/api/items/([0-9]+)/state` | Transition item state (used by Kanban/Gantt drag-and-drop) |
| `DELETE` | `/api/items/([0-9]+)` | Delete a work item |
| `POST` | `/api/items/([0-9]+)/comments` | Add a comment to a work item |
| `POST` | `/api/items/([0-9]+)/attachments` | Upload a file to `blobs/` and link it to the work item |
| `GET` | `/api/blobs/([a-zA-Z0-9._-]+)` | Download or stream an attachment from `blobs/` |

---

## Prerequisites

### Backend
- **C++23 Compiler**: GCC $\ge$ 13 or Clang $\ge$ 16
- **CMake**: $\ge$ 3.16
- **Libraries**:
  - `libfcgi-dev` (`fcgi`)
  - `libsqlite3-dev` (`sqlite3`)
  - `libjson-c-dev` (`json-c`)
  - `pthread`

### Frontend
- **Node.js**: $\ge$ 20 LTS
- **Angular CLI**: $\ge$ 17 (`@angular/cli` & `@angular/cdk` for drag-and-drop)

---

## Building and Running from Source

### 1. Build the C++ FastCGI Backend
From the repository root:
```bash
./build.sh
```

Or build directly via CMake:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### 2. Run the FastCGI Backend (Auto-Initializes & Upgrades DB)
On startup, `jiralike-demo` ensures `db/` and `blobs/` exist, checks the `schema_version` table in `db/jiralike.db`, and automatically applies any pending upgrade scripts from `db/migrations/` up to the latest version:
```bash
cd demos/jiralike-demo
./jiralike-demo -n 127.0.0.1:9002 -w 4
```

### 3. Build / Serve the Angular Frontend
```bash
cd demos/jiralike-demo/web
npm install
npm run build
```
For local development with API proxy to Nginx/FastCGI:
```bash
npm start
```

---

## Packaging & Installation

`jiralike-demo` is designed to be packaged and deployed across three primary distribution targets:

### 1. Docker (`docker/`)
A multi-stage `Dockerfile` and `docker-compose.yml` build the C++ FastCGI backend, compile the Angular SPA production bundle, and run them alongside Nginx with persistent volumes for `db/` and `blobs/`. On container startup, the backend automatically checks `schema_version` and runs any required database migrations.

```bash
cd demos/jiralike-demo/docker
docker compose up --build -d
```

### 2. Ubuntu / Debian `.deb` Packages (`Ubuntu/`)
Debian packaging scripts build modular `.deb` packages for Ubuntu/Debian distributions:
- `jiralike-demo`: FastCGI backend binary, systemd service (`jiralike-demo.service`), SQLite migration scripts (`db/migrations/`), and `/var/lib/jiralike-demo/{db,blobs}` state directories.
- `jiralike-demo-fe`: Pre-built Angular SPA static assets installed under `/usr/share/jiralike-demo/www`.
- `jiralike-demo-config-nginx`: Nginx virtual host configuration routing `/api/` to FastCGI and serving the Angular SPA.

```bash
cd demos/jiralike-demo/Ubuntu
./build-all.sh
sudo dpkg -i *.deb
```

### 3. Arch Linux / AUR (`Arch/`)
An Arch Linux `PKGBUILD` is provided for building and installing via `makepkg` or an AUR helper:
- Builds the C++23 FastCGI backend, Angular frontend, systemd unit, and Nginx config.
- Preserves existing `/var/lib/jiralike-demo/db/jiralike.db` across package upgrades so the backend's startup migration runner seamlessly upgrades the schema to the latest version.

```bash
cd demos/jiralike-demo/Arch
makepkg -si
```
