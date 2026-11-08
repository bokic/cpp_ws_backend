#pragma once

#include <string>

namespace openapi {

inline const std::string &get_openapi_json() {
    static const std::string spec = R"JSON({
  "openapi": "3.0.0",
  "info": {
    "title": "JiraLike Enterprise REST API",
    "version": "1.0.0",
    "description": "Comprehensive REST and event streaming API for JiraLike issue tracking, agile boards, ITSM, and enterprise governance."
  },
  "servers": [
    {
      "url": "/",
      "description": "Current Server Instance"
    }
  ],
  "tags": [
    { "name": "System", "description": "Health, setup and runtime status" },
    { "name": "Authentication", "description": "Session auth, enterprise SSO (SAML/OIDC) and RFC 6238 2FA" },
    { "name": "User & Preferences", "description": "Users, personal access tokens and locale preferences" },
    { "name": "Projects", "description": "Project configurations, components and versions" },
    { "name": "Issue Configuration", "description": "Issue types, custom fields, screens and field configs" },
    { "name": "Workflows", "description": "Directed workflows, statuses, transitions and transition rules" },
    { "name": "Work Items", "description": "Core issue tracking, comments, worklogs, attachments and links" },
    { "name": "Agile", "description": "Scrum/Kanban boards, backlog management and sprints" },
    { "name": "Search & JQL", "description": "Full-text search (FTS5) and Jira Query Language engine" },
    { "name": "Analytics & Reports", "description": "Burndown, velocity, cumulative flow and sprint health" },
    { "name": "Dashboards", "description": "Custom dashboards and interactive gadgets" },
    { "name": "Permissions & Security", "description": "RBAC permission schemes and issue-level security schemes" },
    { "name": "Automation", "description": "No-code event-driven automation rules and execution logs" },
    { "name": "Notifications & SMTP", "description": "Notification schemes, in-app drawer and webhooks" },
    { "name": "VCS & Smart Commits", "description": "Git repository integration and smart commit message processing" },
    { "name": "ITSM & Service Desk", "description": "SLA calendar tracking, canned responses and customer portal" },
    { "name": "Real-Time Sync", "description": "Server-Sent Events (SSE) live updates and user presence" },
    { "name": "Bulk & Trash", "description": "Bulk operations and 30-day soft-delete trash bin retention" },
    { "name": "Portfolio & Gantt", "description": "Advanced roadmaps, Gantt dependencies, critical path & capacity" },
    { "name": "Mail Handlers", "description": "Inbound MIME email hook and issue routing" },
    { "name": "Governance & Rate Limiting", "description": "Token-bucket rate limiter inspection and OpenAPI docs" }
  ],
  "paths": {
    "/api/health": {
      "get": {
        "tags": ["System"],
        "summary": "System Health Check",
        "description": "Returns system health, database connectivity and connection pool metrics.",
        "responses": {
          "200": { "description": "System operational" },
          "503": { "description": "System degraded" }
        }
      }
    },
    "/api/setup/status": {
      "get": {
        "tags": ["System"],
        "summary": "Setup Status",
        "description": "Checks whether the workspace has been initialized.",
        "responses": {
          "200": { "description": "Setup state and schema version" }
        }
      }
    },
    "/api/setup/init": {
      "post": {
        "tags": ["System"],
        "summary": "Initialize Workspace",
        "description": "Seeds initial admin user, default project, and demo data.",
        "responses": {
          "200": { "description": "Initialization successful" }
        }
      }
    },
    "/api/auth/login": {
      "post": {
        "tags": ["Authentication"],
        "summary": "User Login",
        "description": "Authenticates user via username/password. If 2FA is active, returns an ephemeral challenge token.",
        "responses": {
          "200": { "description": "Authenticated session token or 2FA challenge requirement" },
          "401": { "description": "Invalid credentials" }
        }
      }
    },
    "/api/auth/logout": {
      "post": {
        "tags": ["Authentication"],
        "summary": "User Logout",
        "security": [{ "BearerAuth": [] }],
        "responses": {
          "200": { "description": "Session invalidated" }
        }
      }
    },
    "/api/auth/me": {
      "get": {
        "tags": ["Authentication"],
        "summary": "Current User Profile & Preferences",
        "security": [{ "BearerAuth": [] }],
        "responses": {
          "200": { "description": "Current user identity, role, and localization preferences" }
        }
      }
    },
    "/api/auth/sso/config": {
      "get": {
        "tags": ["Authentication"],
        "summary": "Get SSO Configuration",
        "responses": { "200": { "description": "Current SSO IdP settings" } }
      },
      "put": {
        "tags": ["Authentication"],
        "summary": "Update SSO Configuration",
        "security": [{ "BearerAuth": [] }],
        "responses": { "200": { "description": "SSO configuration saved" } }
      }
    },
    "/api/auth/sso/metadata": {
      "get": {
        "tags": ["Authentication"],
        "summary": "SAML 2.0 SP Metadata XML",
        "responses": { "200": { "description": "SAML Service Provider metadata XML" } }
      }
    },
    "/api/auth/sso/login": {
      "get": {
        "tags": ["Authentication"],
        "summary": "Initiate SSO Login",
        "responses": { "200": { "description": "SSO redirect URL generated" } }
      }
    },
    "/api/auth/sso/callback": {
      "post": {
        "tags": ["Authentication"],
        "summary": "Handle SSO Callback",
        "responses": { "200": { "description": "SSO assertion verified and session token issued" } }
      }
    },
    "/api/auth/2fa/status": {
      "get": {
        "tags": ["Authentication"],
        "summary": "2FA Status",
        "security": [{ "BearerAuth": [] }],
        "responses": { "200": { "description": "2FA activation state" } }
      }
    },
    "/api/auth/2fa/setup": {
      "post": {
        "tags": ["Authentication"],
        "summary": "Initiate 2FA Setup",
        "security": [{ "BearerAuth": [] }],
        "responses": { "200": { "description": "Base32 secret and inline SVG QR code" } }
      }
    },
    "/api/auth/2fa/verify": {
      "post": {
        "tags": ["Authentication"],
        "summary": "Verify 2FA TOTP Code",
        "responses": { "200": { "description": "Verification successful" } }
      }
    },
    "/api/auth/2fa/recovery": {
      "post": {
        "tags": ["Authentication"],
        "summary": "Verify Emergency Backup Code",
        "responses": { "200": { "description": "Recovery code consumed and login verified" } }
      }
    },
    "/api/auth/2fa/disable": {
      "post": {
        "tags": ["Authentication"],
        "summary": "Disable 2FA",
        "security": [{ "BearerAuth": [] }],
        "responses": { "200": { "description": "2FA disabled" } }
      }
    },
    "/api/user/preferences": {
      "get": {
        "tags": ["User & Preferences"],
        "summary": "Get User Preferences",
        "description": "Returns active user locale, language, timezone, date format and time format.",
        "security": [{ "BearerAuth": [] }],
        "responses": {
          "200": { "description": "User preferences retrieved" }
        }
      },
      "put": {
        "tags": ["User & Preferences"],
        "summary": "Update User Preferences",
        "description": "Updates language (en, de, fr, es, ja, zh), timezone, date format and time format.",
        "security": [{ "BearerAuth": [] }],
        "responses": {
          "200": { "description": "Preferences successfully saved" }
        }
      }
    },
    "/api/users": {
      "get": {
        "tags": ["User & Preferences"],
        "summary": "List Users",
        "security": [{ "BearerAuth": [] }],
        "responses": { "200": { "description": "List of system users" } }
      },
      "post": {
        "tags": ["User & Preferences"],
        "summary": "Create User",
        "security": [{ "BearerAuth": [] }],
        "responses": { "201": { "description": "User created" } }
      }
    },
    "/api/tokens": {
      "get": {
        "tags": ["User & Preferences"],
        "summary": "List Personal Access Tokens",
        "security": [{ "BearerAuth": [] }],
        "responses": { "200": { "description": "List of active PATs" } }
      },
      "post": {
        "tags": ["User & Preferences"],
        "summary": "Generate Personal Access Token",
        "security": [{ "BearerAuth": [] }],
        "responses": { "201": { "description": "PAT generated" } }
      }
    },
    "/api/projects": {
      "get": {
        "tags": ["Projects"],
        "summary": "List Projects",
        "security": [{ "BearerAuth": [] }],
        "responses": { "200": { "description": "List of accessible projects" } }
      },
      "post": {
        "tags": ["Projects"],
        "summary": "Create Project",
        "security": [{ "BearerAuth": [] }],
        "responses": { "201": { "description": "Project created" } }
      }
    },
    "/api/components": {
      "get": {
        "tags": ["Projects"],
        "summary": "List Components",
        "responses": { "200": { "description": "Project components" } }
      },
      "post": {
        "tags": ["Projects"],
        "summary": "Create Component",
        "responses": { "201": { "description": "Component created" } }
      }
    },
    "/api/versions": {
      "get": {
        "tags": ["Projects"],
        "summary": "List Versions / Releases",
        "responses": { "200": { "description": "Project versions" } }
      },
      "post": {
        "tags": ["Projects"],
        "summary": "Create Version",
        "responses": { "201": { "description": "Version created" } }
      }
    },
    "/api/issue-types": {
      "get": {
        "tags": ["Issue Configuration"],
        "summary": "List Issue Types",
        "responses": { "200": { "description": "Available issue types" } }
      }
    },
    "/api/custom-fields": {
      "get": {
        "tags": ["Issue Configuration"],
        "summary": "List Custom Fields",
        "responses": { "200": { "description": "Registered custom fields" } }
      }
    },
    "/api/workflows": {
      "get": {
        "tags": ["Workflows"],
        "summary": "List Workflows",
        "responses": { "200": { "description": "Defined directed workflows" } }
      }
    },
    "/api/items": {
      "get": {
        "tags": ["Work Items"],
        "summary": "List Work Items / Issues",
        "security": [{ "BearerAuth": [] }],
        "responses": { "200": { "description": "Work items matching criteria" } }
      },
      "post": {
        "tags": ["Work Items"],
        "summary": "Create Work Item",
        "security": [{ "BearerAuth": [] }],
        "responses": { "201": { "description": "Item created with unique key" } }
      }
    },
    "/api/items/{key}": {
      "get": {
        "tags": ["Work Items"],
        "summary": "Get Work Item Details",
        "parameters": [{ "name": "key", "in": "path", "required": true, "schema": { "type": "string" } }],
        "responses": { "200": { "description": "Complete work item model" } }
      },
      "put": {
        "tags": ["Work Items"],
        "summary": "Update Work Item",
        "parameters": [{ "name": "key", "in": "path", "required": true, "schema": { "type": "string" } }],
        "responses": { "200": { "description": "Work item updated" } }
      },
      "delete": {
        "tags": ["Work Items"],
        "summary": "Soft Delete Work Item (Send to Trash)",
        "parameters": [{ "name": "key", "in": "path", "required": true, "schema": { "type": "string" } }],
        "responses": { "200": { "description": "Item soft-deleted" } }
      }
    },
    "/api/items/{key}/comments": {
      "get": {
        "tags": ["Work Items"],
        "summary": "List Comments",
        "parameters": [{ "name": "key", "in": "path", "required": true, "schema": { "type": "string" } }],
        "responses": { "200": { "description": "Comments list" } }
      },
      "post": {
        "tags": ["Work Items"],
        "summary": "Add Comment",
        "parameters": [{ "name": "key", "in": "path", "required": true, "schema": { "type": "string" } }],
        "responses": { "201": { "description": "Comment posted" } }
      }
    },
    "/api/items/{key}/worklogs": {
      "get": {
        "tags": ["Work Items"],
        "summary": "List Worklogs",
        "parameters": [{ "name": "key", "in": "path", "required": true, "schema": { "type": "string" } }],
        "responses": { "200": { "description": "Worklogs list" } }
      },
      "post": {
        "tags": ["Work Items"],
        "summary": "Log Work",
        "parameters": [{ "name": "key", "in": "path", "required": true, "schema": { "type": "string" } }],
        "responses": { "201": { "description": "Time logged" } }
      }
    },
    "/api/items/{key}/transitions": {
      "get": {
        "tags": ["Work Items"],
        "summary": "Available Workflow Transitions",
        "parameters": [{ "name": "key", "in": "path", "required": true, "schema": { "type": "string" } }],
        "responses": { "200": { "description": "Permitted transition actions" } }
      },
      "post": {
        "tags": ["Work Items"],
        "summary": "Execute Workflow Transition",
        "parameters": [{ "name": "key", "in": "path", "required": true, "schema": { "type": "string" } }],
        "responses": { "200": { "description": "Transition executed and state updated" } }
      }
    },
    "/api/boards": {
      "get": {
        "tags": ["Agile"],
        "summary": "List Boards",
        "responses": { "200": { "description": "Scrum and Kanban boards" } }
      }
    },
    "/api/sprints": {
      "get": {
        "tags": ["Agile"],
        "summary": "List Sprints",
        "responses": { "200": { "description": "Sprints list" } }
      },
      "post": {
        "tags": ["Agile"],
        "summary": "Create Sprint",
        "responses": { "201": { "description": "Sprint created" } }
      }
    },
    "/api/search/jql": {
      "get": {
        "tags": ["Search & JQL"],
        "summary": "Execute JQL Query",
        "parameters": [
          { "name": "jql", "in": "query", "schema": { "type": "string" }, "description": "Jira Query Language clause" }
        ],
        "responses": { "200": { "description": "Issues matching JQL syntax" } }
      },
      "post": {
        "tags": ["Search & JQL"],
        "summary": "Execute JQL Query via POST",
        "responses": { "200": { "description": "Issues matching JQL syntax" } }
      }
    },
    "/api/reports/burndown": {
      "get": {
        "tags": ["Analytics & Reports"],
        "summary": "Sprint Burndown Chart Data",
        "responses": { "200": { "description": "Burndown series" } }
      }
    },
    "/api/dashboards": {
      "get": {
        "tags": ["Dashboards"],
        "summary": "List Dashboards",
        "responses": { "200": { "description": "Custom user dashboards" } }
      }
    },
    "/api/automation/rules": {
      "get": {
        "tags": ["Automation"],
        "summary": "List Automation Rules",
        "responses": { "200": { "description": "No-code automation rules" } }
      }
    },
    "/api/servicedesk/portal/requests": {
      "get": {
        "tags": ["ITSM & Service Desk"],
        "summary": "List Customer Service Desk Requests",
        "responses": { "200": { "description": "Customer tickets" } }
      },
      "post": {
        "tags": ["ITSM & Service Desk"],
        "summary": "Submit Service Desk Request",
        "responses": { "201": { "description": "Ticket raised in customer portal" } }
      }
    },
    "/api/portal/my-requests": {
      "get": {
        "tags": ["ITSM & Service Desk"],
        "summary": "Get Portal Requests Shared With Customer or Organization",
        "responses": { "200": { "description": "Customer tickets with customer organization segregation" } }
      }
    },
    "/api/portal/requests": {
      "post": {
        "tags": ["ITSM & Service Desk"],
        "summary": "Submit Customer Portal Request",
        "responses": { "201": { "description": "Created customer ticket" } }
      }
    },
    "/api/portal/requests/{id}/comments": {
      "post": {
        "tags": ["ITSM & Service Desk"],
        "summary": "Submit Public Comment on Customer Request",
        "responses": { "201": { "description": "Customer comment created with is_internal=0" } }
      }
    },
    "/api/servicedesk/desks/{id}/organizations": {
      "get": {
        "tags": ["ITSM & Service Desk"],
        "summary": "List Customer Organizations for Service Desk",
        "responses": { "200": { "description": "Customer organizations" } }
      },
      "post": {
        "tags": ["ITSM & Service Desk"],
        "summary": "Create Customer Organization",
        "responses": { "201": { "description": "Created customer organization" } }
      }
    },
    "/api/servicedesk/organizations/{id}": {
      "get": {
        "tags": ["ITSM & Service Desk"],
        "summary": "Get Customer Organization Details and Members",
        "responses": { "200": { "description": "Organization details" } }
      },
      "put": {
        "tags": ["ITSM & Service Desk"],
        "summary": "Update Customer Organization",
        "responses": { "200": { "description": "Updated organization" } }
      },
      "delete": {
        "tags": ["ITSM & Service Desk"],
        "summary": "Delete Customer Organization",
        "responses": { "200": { "description": "Deleted organization" } }
      }
    },
    "/api/events/live": {
      "get": {
        "tags": ["Real-Time Sync"],
        "summary": "Server-Sent Events (SSE) Live Feed",
        "description": "Streaming real-time SSE stream for instant board and issue refresh.",
        "responses": { "200": { "description": "text/event-stream connection established" } }
      }
    },
    "/api/presence": {
      "get": {
        "tags": ["Real-Time Sync"],
        "summary": "Get Active User Presence",
        "responses": { "200": { "description": "Users viewing and editing items" } }
      },
      "post": {
        "tags": ["Real-Time Sync"],
        "summary": "Heartbeat Presence Status",
        "responses": { "200": { "description": "Heartbeat acknowledged" } }
      }
    },
    "/api/trash": {
      "get": {
        "tags": ["Bulk & Trash"],
        "summary": "List Soft-Deleted Items in Trash",
        "responses": { "200": { "description": "Trash bin contents" } }
      }
    },
    "/api/trash/{id}/restore": {
      "post": {
        "tags": ["Bulk & Trash"],
        "summary": "Restore Item from Trash",
        "parameters": [{ "name": "id", "in": "path", "required": true, "schema": { "type": "integer" } }],
        "responses": { "200": { "description": "Item restored to active workspace" } }
      }
    },
    "/api/portfolio/critical-path": {
      "get": {
        "tags": ["Portfolio & Gantt"],
        "summary": "Calculate CPM Critical Path & Floats",
        "responses": { "200": { "description": "Critical path sequence and total float days" } }
      }
    },
    "/api/portfolio/capacity": {
      "get": {
        "tags": ["Portfolio & Gantt"],
        "summary": "Capacity Planning & Workload Heatmap",
        "responses": { "200": { "description": "Member allocations, availability and over-allocated days" } }
      }
    },
    "/api/mail/inbound": {
      "post": {
        "tags": ["Mail Handlers"],
        "summary": "Inbound Email Receiver Webhook",
        "description": "Accepts JSON or raw RFC 822 MIME mail payloads for ticket creation or comment threading.",
        "responses": { "200": { "description": "Email processed successfully" } }
      }
    },
    "/api/rate-limits": {
      "get": {
        "tags": ["Governance & Rate Limiting"],
        "summary": "Rate Limiting Status & Metrics",
        "description": "Inspects current token bucket status, capacity, refill rate, and remaining tokens.",
        "responses": {
          "200": { "description": "Current rate limit metrics and client bucket state" }
        }
      },
      "put": {
        "tags": ["Governance & Rate Limiting"],
        "summary": "Configure Rate Limits",
        "security": [{ "BearerAuth": [] }],
        "description": "Updates global IP / token burst capacity and refill rates.",
        "responses": {
          "200": { "description": "Rate limiting configuration updated" }
        }
      }
    },
    "/api/rate-limits/reset": {
      "post": {
        "tags": ["Governance & Rate Limiting"],
        "summary": "Reset Rate Limit Buckets",
        "description": "Clears token buckets for the active client or all clients (testing convenience).",
        "responses": {
          "200": { "description": "Buckets reset" }
        }
      }
    },
    "/api/openapi.json": {
      "get": {
        "tags": ["Governance & Rate Limiting"],
        "summary": "OpenAPI 3.0.0 Specification",
        "description": "Machine-readable OpenAPI 3.0 specification for all JiraLike endpoints.",
        "responses": {
          "200": { "description": "Complete OpenAPI 3.0.0 JSON schema" }
        }
      }
    },
    "/api/docs": {
      "get": {
        "tags": ["Governance & Rate Limiting"],
        "summary": "Interactive Swagger UI Documentation",
        "description": "Interactive HTML Swagger UI exploring the JiraLike API.",
        "responses": {
          "200": { "description": "HTML Swagger UI page" }
        }
      }
    }
  },
  "components": {
    "securitySchemes": {
      "BearerAuth": {
        "type": "http",
        "scheme": "bearer",
        "bearerFormat": "Token",
        "description": "User session token or Personal Access Token (PAT)"
      },
      "ApiKeyAuth": {
        "type": "apiKey",
        "in": "header",
        "name": "X-API-Key",
        "description": "Personal Access Token provided via HTTP header"
      }
    }
  }
})JSON";

    return spec;
}

inline std::string get_swagger_ui_html() {
    return R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>JiraLike API Documentation - Swagger UI</title>
  <link rel="stylesheet" type="text/css" href="https://cdn.jsdelivr.net/npm/swagger-ui-dist@5.11.0/swagger-ui.css" />
  <link rel="icon" type="image/png" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'><text y='.9em' font-size='90'>🚀</text></svg>">
  <style>
    html { box-sizing: border-box; overflow: -moz-scrollbars-vertical; overflow-y: scroll; }
    *, *:before, *:after { box-sizing: inherit; }
    body { margin:0; background: #fafafa; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; }
    .topbar-wrapper { display: flex; align-items: center; justify-content: space-between; padding: 12px 24px; background: #0747a6; color: #fff; }
    .topbar-wrapper h1 { font-size: 18px; margin: 0; font-weight: 600; display: flex; align-items: center; gap: 8px; }
    .topbar-links a { color: #fff; margin-left: 16px; text-decoration: none; font-size: 14px; opacity: 0.9; }
    .topbar-links a:hover { opacity: 1; text-decoration: underline; }
    #fallback-container { display: none; padding: 32px; max-width: 800px; margin: 40px auto; background: #fff; border: 1px solid #dfe1e6; border-radius: 8px; }
  </style>
</head>
<body>
  <div class="topbar-wrapper">
    <h1><span>🔷</span> JiraLike Enterprise REST API Documentation</h1>
    <div class="topbar-links">
      <a href="/api/openapi.json" target="_blank">Raw OpenAPI JSON ↗</a>
      <a href="/developer/docs">Developer Portal</a>
      <a href="/board">Back to App</a>
    </div>
  </div>

  <div id="swagger-ui"></div>
  <div id="fallback-container">
    <h2>API Documentation Ready</h2>
    <p>The interactive Swagger UI could not connect to external CDN. The complete OpenAPI 3.0 specification is available directly at:</p>
    <p><a href="/api/openapi.json" style="color: #0052cc; font-weight: 600;">/api/openapi.json</a></p>
  </div>

  <script src="https://cdn.jsdelivr.net/npm/swagger-ui-dist@5.11.0/swagger-ui-bundle.js"></script>
  <script src="https://cdn.jsdelivr.net/npm/swagger-ui-dist@5.11.0/swagger-ui-standalone-preset.js"></script>
  <script>
    window.onload = function() {
      if (typeof SwaggerUIBundle !== 'undefined') {
        const ui = SwaggerUIBundle({
          url: "/api/openapi.json",
          dom_id: '#swagger-ui',
          deepLinking: true,
          presets: [
            SwaggerUIBundle.presets.apis,
            SwaggerUIStandalonePreset
          ],
          plugins: [
            SwaggerUIBundle.plugins.DownloadUrl
          ],
          layout: "StandaloneLayout"
        });
        window.ui = ui;
      } else {
        document.getElementById('fallback-container').style.display = 'block';
      }
    };
  </script>
</body>
</html>)HTML";
}

} // namespace openapi
