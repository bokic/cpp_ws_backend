# Jira-Like Enterprise Project Management Demo (`jiralike-demo`)

A high-performance, full-featured, enterprise-grade Jira clone powered by a **`cpp_ws_backend` C++23 FastCGI backend**, **SQLite** with connection pooling and WAL mode, **local filesystem blob storage**, and a modern **Angular 19+ Single-Page Application (SPA)** frontend.

---

## Overview

`jiralike-demo` provides a comprehensive, production-ready implementation of Atlassian Jira workflows, combining the speed and low memory footprint of a native C++23 backend with an intuitive, responsive Angular user interface. It incorporates advanced issue tracking, agile boards, JQL search, workflow graphs, automation rules, ITSM service desks, developer tooling, Gantt roadmaps, enterprise security (SSO/2FA), internationalization (i18n), and rate-limiting governance.

---

## Complete Feature Matrix

### 1. Core Domain Model & Issue Architecture
- **Work Item Hierarchy**: Epics, Stories, Tasks, Bugs, Improvements, New Features, and granular Subtasks with recursive parent-child rollups.
- **Configurable Issue Types & Schemes**: Custom issue type definitions, default types, and project-specific Issue Type Schemes.
- **Custom Fields Engine**: Full EAV architecture supporting Text, Multiline, Number, Date, Select Dropdowns, Multiselect, Checkbox, User Picker, and URL field types with custom options.
- **Field Configurations & Schemes**: Per-project and per-type control over field visibility, required validations, and helper descriptions.
- **Screens & Screen Schemes**: Screen layouts organized into tabs, mapped to distinct Create, View, and Edit operations across issue types.
- **Resolutions & Components**: Project subsystems (`components`) with auto-component leads, and customizable issue resolution statuses (`Fixed`, `Won't Fix`, `Duplicate`, `Incomplete`, `Cannot Reproduce`).
- **Versions / Releases**: Milestone tracking with start dates, release dates, description, and release lifecycle states (`unreleased`, `released`, `archived`).
- **File Attachments & Blob Storage**: Multi-file attachments with metadata tracking, content-addressed disk storage, inline image previews, and direct streaming downloads.

### 2. Directed Graph Workflow Engine
- **Visual Workflow Designer**: Directed graph modeling workflow statuses, allowed transitions, and step ordering.
- **Transition Screens**: Modals prompting users for required fields (e.g., Resolution, Comment, Fix Version) during state changes.
- **Workflow Conditions**: Execution guards (e.g., Only Assignee Can Transition, Role Required, Permission Required).
- **Workflow Validators**: Field validation rules (e.g., require comment on rejection, ensure all subtasks are closed before resolving parent).
- **Workflow Post-Functions**: Automated hooks triggering upon transition (e.g., auto-assign to reporter, clear resolution, dispatch notification events).
- **Workflow Schemes**: Associate workflows with issue types across projects.

### 3. JQL (Jira Query Language) & Full-Text Search
- **C++ AST Lexer & Parser**: Complete native JQL syntax engine supporting boolean logic (`AND`, `OR`, `NOT`), comparison operators (`=`, `!=`, `<`, `<=`, `>`, `>=`, `~`, `!~`, `IN`, `IS`, `IS NOT`), and multi-column ordering (`ORDER BY`).
- **Built-in JQL Functions**: Dynamic evaluation of `currentUser()`, `now()`, `startOfDay()`, `endOfDay()`, `startOfWeek()`, `endOfWeek()`, `startOfMonth()`, `endOfMonth()`, `openSprints()`, and `closedSprints()`.
- **SQLite FTS5 Full-Text Index**: Virtual table (`work_items_fts`) indexing titles and descriptions with synchronized insert/update/delete database triggers.
- **Saved Filters & Permissions**: User-saved JQL queries with private or project-shared permissions and email/in-app subscription schedules.
- **Search UI & Export Engine**: CodeMirror-style JQL query box with syntax validation, dynamic filter bars, and 1-click export to CSV and JSON.

### 4. Agile Multi-Board Engine & Backlog Planning
- **Scrum & Kanban Multi-Boards**: Multiple boards per project with configurable columns mapped to one or more workflow statuses.
- **Angular CDK Drag-and-Drop**: Fluid, real-time board interaction with automatic state transition triggers and column reordering.
- **Swimlanes & Quick Filters**: Group board cards by Assignee, Epic, or Priority; one-click filtering by user or custom JQL presets.
- **Agile Backlog Planning**: Dedicated backlog management view for ranking items, creating/editing upcoming sprints, starting sprints with duration limits, and completing sprints with automated incomplete issue roll-forward.
- **Estimation Triad**: Original Estimate, Remaining Estimate, and Time Spent tracking with support for standard Jira duration strings (`2w 3d 4h 30m`).

### 5. Agile Analytics & Reporting Suite
- **Sprint Burndown Chart**: Story point and hourly burn rate tracking against ideal linear guideline curves.
- **Sprint Burnup Chart**: Dual-line visualization comparing total scope changes against completed work items.
- **Velocity Chart**: Historical velocity tracking across completed sprints, comparing committed vs delivered story points.
- **Cumulative Flow Diagram (CFD)**: Longitudinal visualization of work in progress across all workflow states to identify systemic process bottlenecks.
- **Control Chart & Cycle Time**: Statistical process control displaying lead time, cycle time, and rolling averages with outlier detection.

### 6. Configurable Dashboards & Gadgets Framework
- **Multi-Dashboard Catalog**: Create, customize, and share project or personal dashboard layouts.
- **Rich Gadget Ecosystem**:
  - *Filter Results*: Live tabular list of issues matching any saved JQL filter.
  - *Two-Dimensional Pie Chart*: Issue distribution breakdown by Status, Priority, Issue Type, or Assignee.
  - *Created vs Resolved*: Trendline gadget monitoring defect inflow versus team throughput.
  - *Sprint Burndown Gadget*: Embedded live sprint burndown for active sprints.
  - *Assigned to Me*: Quick access widget for current user assignments.
  - *Activity Stream*: Real-time audit log of recent project events and transitions.

### 7. Time Tracking & Worklogs
- **Log Work Dialog**: Modal supporting time spent entry, date worked, and flexible remaining estimate adjustment (`auto`, `keep`, `manual`).
- **Worklog Audit Trail**: Detailed log history per work item with author, timestamp, comment, and formatted duration metrics.

### 8. Rich Text, Markdown WYSIWYG & Collaboration
- **WYSIWYG Markdown Editor**: Rich text editing with Markdown shortcuts, tables, task checklists, and code syntax highlighting.
- **@Mention Autocomplete**: Interactive user tagging dropdown with popover avatar, name, and email.
- **Clipboard & Drag-and-Drop Image Pasting**: Direct `Ctrl+V` pasting of clipboard images and file drop zones into descriptions and comments.

### 9. Security, Granular Permissions & Directory Management
- **Role-Based Access Control (RBAC)**: Fine-grained permission schemes covering `BROWSE_PROJECTS`, `CREATE_ISSUES`, `EDIT_ISSUES`, `TRANSITION_ISSUES`, `ASSIGN_ISSUES`, `CLOSE_ISSUES`, and `DELETE_ISSUES`.
- **Issue Security Schemes**: Project-level security schemes with discrete security levels to restrict visibility of confidential issues to authorized roles/groups.
- **Enterprise SSO (SAML 2.0 / OIDC)**: Single Sign-On integration, Service Provider (SP) SAML metadata XML generation (`/api/auth/sso/metadata`), domain auto-detection, and Just-In-Time (JIT) user provisioning.
- **Two-Factor Authentication (RFC 6238 TOTP)**: Multi-factor authentication with QR code base32 generation, ±30s drift tolerance, 2-step challenge authentication, and 10 one-time emergency backup recovery codes.

### 10. No-Code Automation Engine
- **Visual Rule Builder**: Custom workflow automation without writing code.
- **Triggers**: Issue Created, Field Value Changed, Issue Transitioned, Comment Added, Scheduled JQL Cron.
- **Conditions**: Issue Fields Match, JQL Condition Check, User Role/Permission Check.
- **Branching Rules**: Branch executions on Subtasks or Linked Issues.
- **Actions**: Edit Issue Fields, Transition Issue, Assign Issue, Add Comment, Dispatch In-App Notification, Fire Outbound Webhook.
- **Smart Values Parser**: Dynamic token replacement (`{{issue.key}}`, `{{issue.assignee.displayName}}`, `{{now}}`, `{{initiator}}`).
- **Automation Execution Audit Log**: Detailed execution history with status (`SUCCESS`, `FAILED`, `SKIPPED`) and execution duration metrics.

### 11. Notifications, In-App Drawer & Webhooks
- **Notification Schemes**: Event-driven notification dispatch for `ISSUE_CREATED`, `ISSUE_UPDATED`, `ISSUE_RESOLVED`, `COMMENT_ADDED`, and `WORKLOG_LOGGED`.
- **In-App Notification Center**: Bell icon drawer with live unread counter badge, mark-as-read, and direct issue link navigation.
- **Outbound Webhooks**: System event webhooks with HMAC SHA-256 payload signing and retry mechanisms.
- **Inbound Email Receiver**: Dedicated mailhook processor (`POST /api/mail/inbound`) accepting JSON and RFC 822 MIME raw emails, creating tickets, threading comments via regex subject matching, extracting attachments, and filtering spam.

### 12. Developer Tooling & Git Integration
- **VCS Webhooks**: Inbound integration (`POST /api/vcs/webhook`) supporting GitHub, GitLab, and Bitbucket push and pull request webhooks.
- **Smart Commits Parser**: Commit message commands for automatic processing:
  - `PROJ-123 #comment Fixed regression in parser`: Automatically appends comment.
  - `PROJ-123 #time 2h 30m`: Logs time spent to worklogs.
  - `PROJ-123 #resolve #close`: Triggers workflow state transitions.
- **Issue Detail Development Panel**: Interactive dev panel displaying associated branches, commit history, and pull request review states.

### 13. ITSM, Service Desk & Customer Portal
- **Dedicated Customer Portal (`/portal`)**: External customer-facing service portal with intuitive request catalog and non-technical request submission forms.
- **Customer Ticket Tracking**: Simplified status tracking with public vs internal private comment demarcation.
- **SLA (Service Level Agreement) Engine**:
  - SLA metrics: *Time to First Response* and *Time to Resolution*.
  - Configurable SLA calendars: Standard Business Hours (e.g., Mon–Fri 9:00–17:00) vs 24x7 Continuous.
  - Goal calculation, pause conditions (e.g., *Waiting on Customer*), and visual SLA countdown timers with breach warnings.

### 14. Bulk Operations, Issue Move/Clone & Trash Bin
- **Bulk Operations Wizard**: Multi-issue selection across search results and boards with bulk actions: Bulk Edit, Bulk Transition, Bulk Move, and Bulk Delete.
- **Issue Cloning**: 1-click issue duplication with toggles for copying attachments, subtasks, and links.
- **Issue Move Wizard**: Guided modal for moving issues between projects or retyping issue types with field mapping.
- **Trash Bin & Soft Delete (`/settings/trash`)**: Soft-delete safety net with 30-day retention countdown, single/bulk restore, and permanent purge.

### 15. Advanced Roadmaps, Gantt Dependencies & Critical Path
- **Interactive SVG Gantt Timeline (`/roadmap/advanced`)**: Visual timeline with drag-to-reschedule date bars and visual dependency link connectors.
- **Dependency Management**: Support for Finish-to-Start (FS), Start-to-Start (SS), Finish-to-Finish (FF), and Start-to-Finish (SF) dependencies with lead/lag day offsets.
- **Critical Path Method (CPM) Engine**: Native topological sort and CPM algorithm computing earliest/latest start/finish dates, identifying zero-float bottleneck tasks, and rendering visual critical path glowing overlays.
- **Portfolio Hierarchy Rollups**: Recursive aggregation of Story Points, Estimates, and Time Spent up through Subtasks $\rightarrow$ Stories/Tasks $\rightarrow$ Epics $\rightarrow$ Initiatives.
- **Baseline Snapshots**: Versioned schedule baselines (`timeline_baselines`) capturing planned vs actual dates with slippage indicators.
- **Team Capacity Heatmap**: Team workload distribution analyzer comparing assigned hours against working capacity per sprint window.

### 16. Internationalization (i18n) & User Preferences
- **Multi-Language Support**: Full translation bundles for English (`en`), German (`de`), French (`fr`), Spanish (`es`), Japanese (`ja`), and Chinese (`zh`).
- **Reactive Locale Switching**: Instant, zero-reload language switching via Angular `I18nService` and `I18nPipe`.
- **User Locale Preferences (`/settings/preferences`, `/profile`)**: User-configured display timezone, 12h/24h time formatting, and date format strings (`YYYY-MM-DD`, `DD/MM/YYYY`, `MM/DD/YYYY`) with live preview card.

### 17. Performance Governance, Rate Limiting & Developer Docs
- **Token-Bucket Rate Limiter**: High-throughput C++ middleware enforcing per-IP and per-API-token burst and replenishment limits, returning standard HTTP `429 Too Many Requests` with `Retry-After`, `X-RateLimit-Limit`, `X-RateLimit-Remaining: 0`, and `X-RateLimit-Reset` headers.
- **Rate Limit Administration**: Runtime telemetry and reset endpoints (`GET|PUT /api/rate-limits`, `POST /api/rate-limits/reset`).
- **OpenAPI 3.0.0 Specification**: Complete machine-readable OpenAPI specification documenting all endpoints (`GET /api/openapi.json`).
- **Interactive Developer Portal (`/developer/docs`)**: Interactive Swagger UI page (`GET /api/docs`), endpoint explorer with live execution console, rate-limiter telemetry, and burst stress simulator.

### 18. UI/UX Modernization & Real-Time Sync
- **Modern Angular 19+ Architecture**: Built with signals, reactive standalone components, and optimized change detection.
- **Theming & Dark Mode**: Persistent Light / Dark mode with OS system preference detection.
- **Real-Time Updates**: SSE (Server-Sent Events) and WebSocket event channels for collaborative board and issue updates.
- **First-Time Setup Wizard (`/setup`)**: Zero-configuration bootstrapping creating blank database, applying 24 sequential migrations, creating admin credentials, and seeding initial project data.

---

## Architecture & Tech Stack

```text
┌────────────────────────────────────────────────────────┐
│               Angular 19+ Frontend (SPA)               │
│  (Signals, CDK Drag & Drop, i18n, Dark Mode, Charts)   │
└───────────────────────────┬────────────────────────────┘
                            │ HTTP / REST / FastCGI / SSE
┌───────────────────────────▼────────────────────────────┐
│         cpp_ws_backend (C++23 Native Server)           │
│  ├── Token-Bucket Rate Limiter Middleware              │
│  ├── Thread-Safe SQLite Connection Pool                │
│  ├── JQL Lexer, AST Parser & FTS5 Query Engine         │
│  ├── Directed Graph Workflow & Automation Runner       │
│  ├── Critical Path & Dependency Scheduling Engine      │
│  ├── SAML 2.0 / TOTP 2FA Security Handlers             │
│  ├── Mailhook & Inbound VCS Commit Processors          │
│  └── OpenAPI 3.0 Spec & Swagger UI Generator           │
└──────────────┬──────────────────────────┬──────────────┘
               │                          │
┌──────────────▼─────────────┐   ┌────────▼─────────────┐
│   SQLite Database (WAL)    │   │  Local Blob Storage  │
│  (24 Schema Migrations)    │   │ (Attachments/Avatars)│
└────────────────────────────┘   └──────────────────────┘
```

### Technology Highlights
- **Backend**: C++23, `cpp_ws_backend` FastCGI framework, `sqlite3`, `libfcgi`, `json-c`, `pthread`.
- **Database**: SQLite 3 with WAL journal mode (`PRAGMA journal_mode=WAL`), persistent prepared statements, and thread-safe connection pooling (`sqlite_pool.h`).
- **Frontend**: Angular 19+, TypeScript, Angular CDK, Chart.js, Tailwind-inspired responsive CSS, standalone components.
- **Specification**: OpenAPI 3.0.0 with Swagger UI.

---

## Directory Structure

```text
demos/jiralike-demo/
├── run.sh                      # One-click launcher script (builds, starts backend & dev-gateway)
├── dev-gateway.js              # Node.js HTTP gateway (serves SPA + proxies /api to FastCGI)
├── CMakeLists.txt              # CMake build configuration for C++ backend
├── README.md                   # This documentation
├── main.cpp                    # FastCGI entry point, middleware & route definitions
├── db/
│   ├── schema.sql              # Base schema definition
│   ├── migrations/             # 24 sequential migration scripts (001_init.sql ... 024_...)
│   └── jiralike.db             # SQLite database file (created/migrated at startup)
├── blobs/                      # File attachment storage directory
├── src/                        # Modular C++ backend components
│   ├── db_migrator.h           # Atomic migration runner
│   ├── sqlite_pool.h           # RAII SQLite connection pool
│   ├── jql_engine.h            # C++ JQL AST lexer, parser, functions & FTS5 search
│   ├── workflow_engine.h       # Directed graph transitions, conditions & post-functions
│   ├── agile_engine.h          # Sprints, estimation triad, burndown/burnup/velocity
│   ├── dashboards_engine.h     # Dashboards catalog & gadget data generators
│   ├── permissions_engine.h    # Granular permission schemes & issue security levels
│   ├── automation_engine.h     # No-code rules runner, triggers, conditions & smart values
│   ├── notifications_engine.h  # Notification schemes & in-app drawer
│   ├── vcs_integration.h       # VCS webhooks & Smart Commits parser
│   ├── itsm_engine.h           # Customer portal, request types & SLA calendar engine
│   ├── bulk_trash_engine.h     # Bulk operations wizard, issue move/clone & soft delete
│   ├── roadmaps_engine.h       # Gantt dependency scheduling, CPM critical path & rollups
│   ├── enterprise_security.h   # SAML 2.0 SSO, RFC 6238 TOTP 2FA & inbound mailhooks
│   ├── rate_limiter.h          # Token-bucket rate limiter middleware & governance
│   └── openapi_spec.h          # OpenAPI 3.0 JSON specification & Swagger UI
├── nginx/                      # Nginx FastCGI + Angular SPA production configuration
├── docker/                     # Multi-stage Dockerfile and docker-compose.yml
├── Ubuntu/                     # Ubuntu/Debian (.deb) packaging scripts
├── Arch/                       # Arch Linux / AUR (PKGBUILD) package definition
└── web/                        # Angular 19+ Single-Page Application (SPA)
    ├── src/
    │   ├── app/
    │   │   ├── pages/          # Board, Backlog, JQL Search, Roadmaps, Dashboards, Portal, etc.
    │   │   ├── services/       # ApiService, I18nService, AuthService, ThemeService
    │   │   ├── models/         # TypeScript domain models and interfaces
    │   │   └── pipes/          # I18nPipe, LocaleDatePipe
    │   └── assets/             # Icons, i18n translation dictionaries
    ├── package.json
    └── angular.json
```

---

## REST / FastCGI API Overview

The backend exposes over 80 RESTful and streaming endpoints. Below is a high-level summary grouped by subsystem:

| Subsystem | Key Endpoints | Description |
| :--- | :--- | :--- |
| **Setup & Auth** | `GET /api/setup/status`<br>`POST /api/setup/init`<br>`POST /api/auth/login`<br>`GET /api/auth/me` | First-time instance setup, session login/logout, and profile retrieval |
| **Enterprise Security** | `GET /api/auth/sso/metadata`<br>`POST /api/auth/sso/login`<br>`POST /api/auth/2fa/setup`<br>`POST /api/auth/2fa/verify` | SAML 2.0 metadata, SSO redirect/callback, TOTP QR setup and verification |
| **User Preferences** | `GET\|PUT /api/user/preferences` | Retrieve and persist display language, timezone, date, and time formats |
| **Projects & Entities** | `GET\|POST /api/projects`<br>`GET /api/issue-types`<br>`GET /api/resolutions`<br>`GET /api/versions` | Project configurations, issue type schemes, resolutions, components, versions |
| **Work Items** | `GET\|POST /api/items`<br>`GET\|PUT\|DELETE /api/items/:id`<br>`PATCH /api/items/:id/state` | Core CRUD operations, dynamic custom field assignment, state transitions |
| **JQL & Search** | `POST /api/search/jql`<br>`GET\|POST /api/filters`<br>`GET /api/search/export` | AST-parsed JQL query execution, saved filters, CSV/JSON issue export |
| **Agile & Backlog** | `GET\|POST /api/boards`<br>`GET\|POST /api/sprints`<br>`POST /api/sprints/:id/start`<br>`POST /api/sprints/:id/complete` | Scrum/Kanban boards, backlog ordering, sprint lifecycle management |
| **Time Tracking** | `GET\|POST /api/items/:id/worklogs`<br>`DELETE /api/worklogs/:id` | Work item time logging, duration adjustment, worklog history |
| **Reports & Analytics** | `GET /api/reports/burndown`<br>`GET /api/reports/burnup`<br>`GET /api/reports/velocity`<br>`GET /api/reports/cfd` | Sprint Burndown, Burnup, Velocity, and Cumulative Flow Diagram datasets |
| **Dashboards** | `GET\|POST /api/dashboards`<br>`GET /api/gadgets/:type` | Dashboard layouts and gadget data generators |
| **Custom Fields & Screens** | `GET\|POST /api/custom-fields`<br>`GET\|POST /api/screens` | EAV field definitions, field schemes, screen tabs and layouts |
| **Workflows & Transitions** | `GET\|POST /api/workflows`<br>`GET /api/workflows/:id/transitions` | Directed workflow graphs, status mappings, condition/validator rules |
| **Automation Rules** | `GET\|POST /api/automation/rules`<br>`GET /api/automation/logs` | No-code rule configurations, trigger listeners, execution audit logs |
| **Notifications & Mail** | `GET /api/notifications`<br>`POST /api/mail/inbound` | In-app notification drawer and inbound MIME email ticket creation |
| **Dev Panel & VCS** | `POST /api/vcs/webhook`<br>`GET /api/items/:id/development` | GitHub/GitLab/Bitbucket webhooks, Smart Commits, development panels |
| **ITSM & Service Desk** | `GET /api/portal/requests`<br>`GET /api/slas` | Customer portal request catalog and SLA countdown tracking |
| **Bulk Operations & Trash** | `POST /api/bulk/execute`<br>`GET /api/trash`<br>`POST /api/trash/:id/restore` | Bulk Edit/Move/Transition, soft delete retention, and trash bin restore |
| **Roadmaps & CPM** | `GET /api/portfolio/rollups`<br>`GET /api/portfolio/critical-path`<br>`POST /api/portfolio/baselines` | Hierarchy rollups, Critical Path Method calculations, baseline comparisons |
| **Rate Limiter & Docs** | `GET /api/openapi.json`<br>`GET /api/docs`<br>`GET\|PUT /api/rate-limits`<br>`POST /api/rate-limits/reset` | OpenAPI 3.0 specification, Swagger UI, token-bucket limits and metrics |

> [!TIP]
> Visit **`/api/docs`** in your browser for the full, interactive Swagger UI documentation and live API console.

---

## Quick Start with `run.sh`

The fastest way to launch the demo (compiles C++ backend and Angular frontend automatically if not yet built, applies all 24 database migrations, and serves the web application and API proxy):

```bash
cd demos/jiralike-demo
./run.sh
```

Then open **[http://localhost:4200](http://localhost:4200)** in your browser!

### `run.sh` Options
```text
Usage: ./run.sh [OPTIONS]

Options:
  -p, --port <port>       HTTP gateway port for web app (default: 4200, env: PORT)
  -f, --fcgi-port <port>  FastCGI backend port (default: 9002, env: FCGI_PORT)
  -w, --workers <count>   FastCGI worker thread count (default: 4, env: WORKERS)
  -d, --db <path>         Path to SQLite database file (default: db/jiralike.db, env: DB_PATH)
  -u, --blobs <dir>       Path to blob attachments directory (default: blobs, env: BLOBS_DIR)
  -b, --build             Force rebuild backend and frontend before starting
  --backend-only          Start only the C++ FastCGI backend (e.g., if using system Nginx)
  -h, --help              Show usage help
```

---

## Building and Running from Source

### Prerequisites
- **C++ Compiler**: GCC $\ge$ 13 or Clang $\ge$ 16 (C++23 support required)
- **CMake**: $\ge$ 3.16
- **System Libraries**:
  - `libfcgi-dev`
  - `libsqlite3-dev`
  - `libjson-c-dev`
  - `pthread`
- **Frontend**:
  - `Node.js` $\ge$ 20 LTS
  - `npm` $\ge$ 10

### 1. Build the FastCGI Backend
```bash
# From repository root
./build.sh

# Or via CMake directly:
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### 2. Build the Angular Frontend
```bash
cd demos/jiralike-demo/web
npm install
npm run build
```

### 3. Run the Backend Server
```bash
cd demos/jiralike-demo
./jiralike-demo -n 127.0.0.1:9002 -w 4
```
*On initial startup, `jiralike-demo` checks `db/jiralike.db`, automatically applies all 24 schema migrations, and initializes the storage directories.*

### 4. Serve with Angular Dev Server (Proxying API to FastCGI/Nginx)
```bash
cd demos/jiralike-demo/web
npm start
```
Open [http://localhost:4200](http://localhost:4200) in your browser. If it is your first time launching, you will be welcomed by the interactive **Setup Wizard** to create your instance!

---

## Packaging & Deployment

### 1. Docker & Docker Compose (`docker/`)
Multi-stage build compiling backend and frontend, running with Nginx and persistent state volumes:
```bash
cd demos/jiralike-demo/docker
docker compose up --build -d
```

### 2. Ubuntu / Debian `.deb` Packages (`Ubuntu/`)
Build modular `.deb` packages for Ubuntu/Debian systems:
```bash
cd demos/jiralike-demo/Ubuntu
./build-all.sh
sudo dpkg -i *.deb
```

### 3. Arch Linux / AUR (`Arch/`)
Build and install using `makepkg`:
```bash
cd demos/jiralike-demo/Arch
makepkg -si
```
