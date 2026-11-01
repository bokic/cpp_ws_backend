import { Component, OnInit, inject, signal } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { RouterModule } from '@angular/router';
import { ApiService } from '../../services/api.service';
import { I18nService, I18nPipe } from '../../services/i18n.service';
import { RateLimitStatus } from '../../models/jira.models';

interface EndpointItem {
  path: string;
  method: 'GET' | 'POST' | 'PUT' | 'DELETE';
  tag: string;
  summary: string;
  description?: string;
  parameters?: any[];
  requestBody?: any;
}

@Component({
  selector: 'app-api-docs',
  standalone: true,
  imports: [CommonModule, FormsModule, RouterModule, I18nPipe],
  template: `
    <div class="api-docs-container" style="max-width: 1100px; margin: 24px auto; padding: 0 16px;">
      <!-- Header -->
      <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 24px; flex-wrap: wrap; gap: 12px;">
        <div>
          <h1 style="margin: 0 0 4px 0; font-size: 24px; font-weight: 700; color: var(--text-primary); display: flex; align-items: center; gap: 10px;">
            <span>🚀</span> {{ 'docs.title' | i18n }}
          </h1>
          <p style="margin: 0; font-size: 14px; color: var(--text-secondary);">
            {{ 'docs.subtitle' | i18n }}
          </p>
        </div>

        <div style="display: flex; gap: 8px;">
          <a href="/api/openapi.json" target="_blank" class="btn btn-secondary btn-sm" title="Download raw OpenAPI 3.0 JSON specification">
            📄 {{ 'docs.download' | i18n }} ↗
          </a>
          <a href="/api/docs" target="_blank" class="btn btn-primary btn-sm" title="Open standalone Swagger UI">
            ⚡ Standalone Swagger UI ↗
          </a>
        </div>
      </div>

      <!-- Navigation Tabs -->
      <div style="display: flex; gap: 8px; border-bottom: 1px solid var(--border-color); margin-bottom: 20px;">
        <button
          type="button"
          class="tab-btn"
          [class.active]="activeTab() === 'explorer'"
          (click)="activeTab.set('explorer')"
          style="padding: 10px 16px; border: none; background: none; font-size: 14px; font-weight: 600; cursor: pointer; border-bottom: 2px solid transparent;"
          [style.border-bottom-color]="activeTab() === 'explorer' ? 'var(--brand-primary)' : 'transparent'"
          [style.color]="activeTab() === 'explorer' ? 'var(--brand-primary)' : 'var(--text-secondary)'"
        >
          🔍 {{ 'docs.apiExplorer' | i18n }}
        </button>
        <button
          type="button"
          class="tab-btn"
          [class.active]="activeTab() === 'ratelimits'"
          (click)="loadRateLimits(); activeTab.set('ratelimits')"
          style="padding: 10px 16px; border: none; background: none; font-size: 14px; font-weight: 600; cursor: pointer; border-bottom: 2px solid transparent;"
          [style.border-bottom-color]="activeTab() === 'ratelimits' ? 'var(--brand-primary)' : 'transparent'"
          [style.color]="activeTab() === 'ratelimits' ? 'var(--brand-primary)' : 'var(--text-secondary)'"
        >
          ⏱️ {{ 'docs.rateLimitsTitle' | i18n }}
        </button>
        <button
          type="button"
          class="tab-btn"
          [class.active]="activeTab() === 'swagger'"
          (click)="activeTab.set('swagger')"
          style="padding: 10px 16px; border: none; background: none; font-size: 14px; font-weight: 600; cursor: pointer; border-bottom: 2px solid transparent;"
          [style.border-bottom-color]="activeTab() === 'swagger' ? 'var(--brand-primary)' : 'transparent'"
          [style.color]="activeTab() === 'swagger' ? 'var(--brand-primary)' : 'var(--text-secondary)'"
        >
          📘 Embedded Swagger UI
        </button>
        <button
          type="button"
          class="tab-btn"
          [class.active]="activeTab() === 'raw'"
          (click)="loadRawSpec(); activeTab.set('raw')"
          style="padding: 10px 16px; border: none; background: none; font-size: 14px; font-weight: 600; cursor: pointer; border-bottom: 2px solid transparent;"
          [style.border-bottom-color]="activeTab() === 'raw' ? 'var(--brand-primary)' : 'transparent'"
          [style.color]="activeTab() === 'raw' ? 'var(--brand-primary)' : 'var(--text-secondary)'"
        >
          {{ '{ }' }} {{ 'docs.rawSpec' | i18n }}
        </button>
      </div>

      <!-- TAB 1: API Explorer -->
      @if (activeTab() === 'explorer') {
        <div style="display: flex; flex-direction: column; gap: 16px;">
          <!-- Controls bar -->
          <div style="display: flex; gap: 12px; align-items: center; flex-wrap: wrap;">
            <input
              type="text"
              class="form-control"
              placeholder="Search endpoints by path, tag or summary..."
              [(ngModel)]="searchQuery"
              style="flex: 1; min-width: 260px; padding: 8px 12px; border: 1px solid var(--border-color); border-radius: 6px; background: var(--bg-surface); color: var(--text-primary); font-size: 14px;"
            />

            <select
              class="form-control"
              [(ngModel)]="selectedTag"
              style="padding: 8px 12px; border: 1px solid var(--border-color); border-radius: 6px; background: var(--bg-surface); color: var(--text-primary); font-size: 14px;"
            >
              <option value="ALL">All Categories ({{ endpoints.length }})</option>
              @for (tag of tags; track tag) {
                <option [value]="tag">{{ tag }}</option>
              }
            </select>
          </div>

          <!-- Endpoint List -->
          <div style="display: flex; flex-direction: column; gap: 10px;">
            @for (ep of filteredEndpoints(); track ep.path + ep.method) {
              <div
                class="endpoint-card"
                style="border: 1px solid var(--border-color); border-radius: 6px; background: var(--bg-surface); overflow: hidden; box-shadow: var(--shadow-sm);"
              >
                <div
                  (click)="toggleEndpoint(ep)"
                  style="display: flex; justify-content: space-between; align-items: center; padding: 12px 16px; cursor: pointer; user-select: none;"
                  [style.background]="isExpanded(ep) ? 'var(--bg-subtle)' : 'var(--bg-surface)'"
                >
                  <div style="display: flex; align-items: center; gap: 12px; font-family: monospace;">
                    <span
                      class="method-badge"
                      [style.background]="getMethodColor(ep.method)"
                      style="color: #fff; padding: 3px 8px; border-radius: 4px; font-size: 12px; font-weight: 700; width: 62px; text-align: center;"
                    >
                      {{ ep.method }}
                    </span>
                    <strong style="color: var(--text-primary); font-size: 14px;">{{ ep.path }}</strong>
                    <span style="color: var(--text-secondary); font-size: 13px; font-family: inherit;">
                      — {{ ep.summary }}
                    </span>
                  </div>

                  <div style="display: flex; align-items: center; gap: 8px;">
                    <span style="font-size: 11px; padding: 2px 6px; border-radius: 4px; background: var(--bg-subtle); color: var(--text-secondary);">
                      {{ ep.tag }}
                    </span>
                    <span style="color: var(--text-secondary); font-size: 14px;">
                      {{ isExpanded(ep) ? '▲' : '▼' }}
                    </span>
                  </div>
                </div>

                @if (isExpanded(ep)) {
                  <div style="padding: 16px; border-top: 1px solid var(--border-color); background: var(--bg-surface);">
                    <p style="margin: 0 0 12px 0; font-size: 13px; color: var(--text-secondary);">
                      {{ ep.description || ep.summary }}
                    </p>

                    <div style="display: flex; justify-content: space-between; align-items: center; margin-top: 12px;">
                      <span style="font-size: 12px; color: var(--text-secondary);">
                        Authentication: Requires active session token (Bearer Auth)
                      </span>
                      <button
                        type="button"
                        class="btn btn-sm btn-primary"
                        (click)="testEndpoint(ep)"
                        [disabled]="testingEp() === ep"
                      >
                        ⚡ {{ 'docs.tryItOut' | i18n }}
                      </button>
                    </div>

                    <!-- Test Output -->
                    @if (testResults[ep.path + ep.method]; as res) {
                      <div style="margin-top: 12px; border-radius: 6px; overflow: hidden; border: 1px solid var(--border-color);">
                        <div style="padding: 8px 12px; background: var(--bg-subtle); font-size: 12px; font-weight: 600; display: flex; justify-content: space-between;">
                          <span [style.color]="res.status < 400 ? '#36b37e' : '#de350b'">
                            Response: HTTP {{ res.status }} {{ res.statusText }}
                          </span>
                          <span style="color: var(--text-secondary);">{{ res.durationMs }}ms</span>
                        </div>
                        <pre style="margin: 0; padding: 12px; background: var(--bg-primary); color: var(--text-primary); font-size: 12px; max-height: 200px; overflow-y: auto; font-family: monospace;">{{ res.body }}</pre>
                      </div>
                    }
                  </div>
                }
              </div>
            }
          </div>
        </div>
      }

      <!-- TAB 2: Token-Bucket Rate Limiting Governance -->
      @if (activeTab() === 'ratelimits') {
        <div style="display: flex; flex-direction: column; gap: 20px;">
          <!-- Telemetry Status Card -->
          <div class="card" style="background: var(--bg-surface); border: 1px solid var(--border-color); border-radius: 8px; padding: 24px; box-shadow: var(--shadow-sm);">
            <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 16px;">
              <div>
                <h3 style="margin: 0 0 4px 0; font-size: 18px; font-weight: 600; color: var(--text-primary);">
                  {{ 'docs.rateLimitsTitle' | i18n }}
                </h3>
                <p style="margin: 0; font-size: 13px; color: var(--text-secondary);">
                  {{ 'docs.rateLimitsDesc' | i18n }}
                </p>
              </div>

              <div style="display: flex; gap: 8px;">
                <button type="button" class="btn btn-secondary btn-sm" (click)="loadRateLimits()">
                  🔄 Refresh Status
                </button>
                <button type="button" class="btn btn-danger btn-sm" (click)="resetBucket()">
                  🗑️ {{ 'docs.resetBuckets' | i18n }}
                </button>
              </div>
            </div>

            @if (rateLimitStatus(); as st) {
              <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 16px; margin-bottom: 20px;">
                <div style="background: var(--bg-subtle); padding: 12px 16px; border-radius: 6px; border: 1px solid var(--border-color);">
                  <div style="font-size: 12px; color: var(--text-secondary);">{{ 'docs.clientType' | i18n }}</div>
                  <div style="font-size: 16px; font-weight: 700; color: var(--text-primary); text-transform: uppercase;">
                    {{ st.entity_type }}
                  </div>
                  <small style="color: var(--text-secondary); font-size: 11px;">{{ st.identifier }}</small>
                </div>

                <div style="background: var(--bg-subtle); padding: 12px 16px; border-radius: 6px; border: 1px solid var(--border-color);">
                  <div style="font-size: 12px; color: var(--text-secondary);">{{ 'docs.tokensRemaining' | i18n }}</div>
                  <div style="font-size: 20px; font-weight: 700; color: var(--brand-primary);">
                    {{ st.remaining }} / {{ st.limit }}
                  </div>
                  <small style="color: var(--text-secondary); font-size: 11px;">{{ st.tokens.toFixed(2) }} tokens left</small>
                </div>

                <div style="background: var(--bg-subtle); padding: 12px 16px; border-radius: 6px; border: 1px solid var(--border-color);">
                  <div style="font-size: 12px; color: var(--text-secondary);">{{ 'docs.refillRate' | i18n }}</div>
                  <div style="font-size: 18px; font-weight: 700; color: var(--text-primary);">
                    {{ st.refill_rate_per_sec }} / sec
                  </div>
                  <small style="color: var(--text-secondary); font-size: 11px;">{{ st.refill_rate_per_sec * 60 }} req/min</small>
                </div>

                <div style="background: var(--bg-subtle); padding: 12px 16px; border-radius: 6px; border: 1px solid var(--border-color);">
                  <div style="font-size: 12px; color: var(--text-secondary);">Client Total Requests</div>
                  <div style="font-size: 18px; font-weight: 700; color: var(--text-primary);">
                    {{ st.client_total_requests }}
                  </div>
                  <small style="color: #de350b; font-size: 11px;">{{ st.client_blocked_requests }} blocked (429)</small>
                </div>
              </div>
            }
          </div>

          <!-- Interactive Burst Test Simulator -->
          <div class="card" style="background: var(--bg-surface); border: 1px solid var(--border-color); border-radius: 8px; padding: 24px; box-shadow: var(--shadow-sm);">
            <h4 style="margin: 0 0 8px 0; font-size: 16px; font-weight: 600; color: var(--text-primary);">
              ⚡ Interactive Burst Rate Limiting Simulator
            </h4>
            <p style="margin: 0 0 16px 0; font-size: 13px; color: var(--text-secondary);">
              Fire a rapid burst of concurrent FastCGI requests to test the token-bucket algorithm and verify standard HTTP 429 Too Many Requests responses and Retry-After headers.
            </p>

            <div style="display: flex; gap: 12px; align-items: center; margin-bottom: 16px;">
              <button
                type="button"
                class="btn btn-primary"
                (click)="simulateBurst(10)"
                [disabled]="burstTesting()"
              >
                Send 10 Rapid Requests
              </button>
              <button
                type="button"
                class="btn btn-secondary"
                (click)="simulateBurst(70)"
                [disabled]="burstTesting()"
              >
                Send 70 Rapid Requests (Exceed Limit)
              </button>
              @if (burstTesting()) {
                <span style="font-size: 13px; color: var(--text-secondary);">Running burst simulation...</span>
              }
            </div>

            <!-- Burst Test Log Output -->
            @if (burstLogs.length > 0) {
              <div style="max-height: 240px; overflow-y: auto; background: var(--bg-primary); border: 1px solid var(--border-color); border-radius: 6px; padding: 12px; font-family: monospace; font-size: 12px;">
                @for (log of burstLogs; track $index) {
                  <div [style.color]="log.status === 200 ? '#36b37e' : '#de350b'" style="margin-bottom: 4px;">
                    [{{ log.index }}] HTTP {{ log.status }} {{ log.statusText }} — {{ log.message }}
                  </div>
                }
              </div>
            }
          </div>
        </div>
      }

      <!-- TAB 3: Embedded Swagger UI -->
      @if (activeTab() === 'swagger') {
        <div class="card" style="background: var(--bg-surface); border: 1px solid var(--border-color); border-radius: 8px; overflow: hidden; box-shadow: var(--shadow-sm);">
          <div style="padding: 12px 16px; background: var(--bg-subtle); border-bottom: 1px solid var(--border-color); display: flex; justify-content: space-between; align-items: center;">
            <strong style="font-size: 14px; color: var(--text-primary);">Swagger UI (Live Frame)</strong>
            <a href="/api/docs" target="_blank" class="btn btn-xs btn-secondary">Open in New Tab ↗</a>
          </div>
          <iframe
            src="/api/docs"
            style="width: 100%; height: 750px; border: none; background: #fff;"
            title="Interactive Swagger UI Documentation"
          ></iframe>
        </div>
      }

      <!-- TAB 4: Raw OpenAPI 3.0 Specification -->
      @if (activeTab() === 'raw') {
        <div class="card" style="background: var(--bg-surface); border: 1px solid var(--border-color); border-radius: 8px; padding: 20px; box-shadow: var(--shadow-sm);">
          <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 12px;">
            <strong style="font-size: 14px; color: var(--text-primary);">OpenAPI 3.0.0 JSON Specification</strong>
            <button type="button" class="btn btn-secondary btn-sm" (click)="copyRawSpec()">
              📋 {{ copyButtonText() }}
            </button>
          </div>
          <pre style="margin: 0; padding: 16px; background: var(--bg-primary); color: var(--text-primary); border: 1px solid var(--border-color); border-radius: 6px; font-size: 12px; max-height: 600px; overflow-y: auto; font-family: monospace;">{{ rawSpecString() }}</pre>
        </div>
      }
    </div>
  `
})
export class ApiDocsComponent implements OnInit {
  readonly api = inject(ApiService);
  readonly i18n = inject(I18nService);

  readonly activeTab = signal<'explorer' | 'ratelimits' | 'swagger' | 'raw'>('explorer');
  readonly rateLimitStatus = signal<RateLimitStatus | null>(null);
  readonly rawSpecString = signal<string>('Loading OpenAPI specification...');
  readonly copyButtonText = signal<string>('Copy JSON');

  searchQuery = '';
  selectedTag = 'ALL';

  readonly expandedEndpoints = signal<Set<string>>(new Set());
  readonly testingEp = signal<EndpointItem | null>(null);
  testResults: Record<string, { status: number; statusText: string; body: string; durationMs: number }> = {};

  burstTesting = signal(false);
  burstLogs: Array<{ index: number; status: number; statusText: string; message: string }> = [];

  readonly tags = [
    'System',
    'Authentication',
    'User & Preferences',
    'Projects',
    'Issue Configuration',
    'Workflows',
    'Work Items',
    'Agile',
    'Search & JQL',
    'Analytics & Reports',
    'Dashboards',
    'Automation',
    'ITSM & Service Desk',
    'Real-Time Sync',
    'Bulk & Trash',
    'Portfolio & Gantt',
    'Mail Handlers',
    'Governance & Rate Limiting'
  ];

  readonly endpoints: EndpointItem[] = [
    { path: '/api/health', method: 'GET', tag: 'System', summary: 'Health check and DB connection pool stats' },
    { path: '/api/setup/status', method: 'GET', tag: 'System', summary: 'Database schema migration status' },
    { path: '/api/auth/login', method: 'POST', tag: 'Authentication', summary: 'User login with 2FA TOTP challenge support' },
    { path: '/api/auth/logout', method: 'POST', tag: 'Authentication', summary: 'Invalidate current session' },
    { path: '/api/auth/me', method: 'GET', tag: 'Authentication', summary: 'Current user profile and locale preferences' },
    { path: '/api/auth/sso/config', method: 'GET', tag: 'Authentication', summary: 'Enterprise SSO (SAML 2.0 / OIDC) configuration' },
    { path: '/api/auth/sso/metadata', method: 'GET', tag: 'Authentication', summary: 'SAML 2.0 SP metadata XML export' },
    { path: '/api/auth/2fa/status', method: 'GET', tag: 'Authentication', summary: '2FA TOTP status' },
    { path: '/api/auth/2fa/setup', method: 'POST', tag: 'Authentication', summary: 'Generate Base32 TOTP secret and SVG QR code' },
    { path: '/api/user/preferences', method: 'GET', tag: 'User & Preferences', summary: 'Get user display language, timezone and date format' },
    { path: '/api/user/preferences', method: 'PUT', tag: 'User & Preferences', summary: 'Update user display language, timezone and date format' },
    { path: '/api/users', method: 'GET', tag: 'User & Preferences', summary: 'List workspace users' },
    { path: '/api/tokens', method: 'GET', tag: 'User & Preferences', summary: 'List Personal Access Tokens' },
    { path: '/api/projects', method: 'GET', tag: 'Projects', summary: 'List projects' },
    { path: '/api/components', method: 'GET', tag: 'Projects', summary: 'List components' },
    { path: '/api/versions', method: 'GET', tag: 'Projects', summary: 'List project releases and versions' },
    { path: '/api/items', method: 'GET', tag: 'Work Items', summary: 'List and filter issues' },
    { path: '/api/items', method: 'POST', tag: 'Work Items', summary: 'Create new issue' },
    { path: '/api/items/{key}', method: 'GET', tag: 'Work Items', summary: 'Get full work item detail' },
    { path: '/api/items/{key}/comments', method: 'GET', tag: 'Work Items', summary: 'Get item comments' },
    { path: '/api/items/{key}/worklogs', method: 'GET', tag: 'Work Items', summary: 'Get item worklogs' },
    { path: '/api/items/{key}/transitions', method: 'GET', tag: 'Work Items', summary: 'Get available workflow transitions' },
    { path: '/api/boards', method: 'GET', tag: 'Agile', summary: 'List Scrum and Kanban boards' },
    { path: '/api/sprints', method: 'GET', tag: 'Agile', summary: 'List sprints' },
    { path: '/api/search/jql', method: 'GET', tag: 'Search & JQL', summary: 'Execute JQL query with FTS5 search' },
    { path: '/api/reports/burndown', method: 'GET', tag: 'Analytics & Reports', summary: 'Burndown chart data series' },
    { path: '/api/dashboards', method: 'GET', tag: 'Dashboards', summary: 'List user dashboards' },
    { path: '/api/automation/rules', method: 'GET', tag: 'Automation', summary: 'List no-code automation rules' },
    { path: '/api/servicedesk/portal/requests', method: 'GET', tag: 'ITSM & Service Desk', summary: 'Customer help desk requests' },
    { path: '/api/events/live', method: 'GET', tag: 'Real-Time Sync', summary: 'Server-Sent Events (SSE) live push stream' },
    { path: '/api/presence', method: 'GET', tag: 'Real-Time Sync', summary: 'Active user presence and viewing alerts' },
    { path: '/api/trash', method: 'GET', tag: 'Bulk & Trash', summary: 'Trash bin with 30-day retention' },
    { path: '/api/portfolio/critical-path', method: 'GET', tag: 'Portfolio & Gantt', summary: 'Calculate Gantt dependencies and CPM critical path' },
    { path: '/api/portfolio/capacity', method: 'GET', tag: 'Portfolio & Gantt', summary: 'Team capacity and workload planning' },
    { path: '/api/mail/handlers', method: 'GET', tag: 'Mail Handlers', summary: 'Inbound email-to-issue routing handlers' },
    { path: '/api/rate-limits', method: 'GET', tag: 'Governance & Rate Limiting', summary: 'Token-bucket rate limiter metrics and bucket status' },
    { path: '/api/rate-limits/reset', method: 'POST', tag: 'Governance & Rate Limiting', summary: 'Reset rate limit tokens for client' },
    { path: '/api/openapi.json', method: 'GET', tag: 'Governance & Rate Limiting', summary: 'OpenAPI 3.0.0 JSON specification' },
    { path: '/api/docs', method: 'GET', tag: 'Governance & Rate Limiting', summary: 'Swagger UI HTML documentation page' }
  ];

  async ngOnInit(): Promise<void> {
    await this.loadRateLimits();
  }

  filteredEndpoints(): EndpointItem[] {
    const q = this.searchQuery.toLowerCase().trim();
    const tag = this.selectedTag;

    return this.endpoints.filter(ep => {
      if (tag !== 'ALL' && ep.tag !== tag) return false;
      if (!q) return true;
      return (
        ep.path.toLowerCase().includes(q) ||
        ep.summary.toLowerCase().includes(q) ||
        ep.tag.toLowerCase().includes(q) ||
        ep.method.toLowerCase().includes(q)
      );
    });
  }

  getMethodColor(method: string): string {
    switch (method) {
      case 'GET': return '#0052cc';
      case 'POST': return '#00875a';
      case 'PUT': return '#ff8b00';
      case 'DELETE': return '#de350b';
      default: return '#42526e';
    }
  }

  toggleEndpoint(ep: EndpointItem): void {
    const key = ep.path + ep.method;
    const s = new Set(this.expandedEndpoints());
    if (s.has(key)) {
      s.delete(key);
    } else {
      s.add(key);
    }
    this.expandedEndpoints.set(s);
  }

  isExpanded(ep: EndpointItem): boolean {
    return this.expandedEndpoints().has(ep.path + ep.method);
  }

  async testEndpoint(ep: EndpointItem): Promise<void> {
    this.testingEp.set(ep);
    const key = ep.path + ep.method;
    const startTime = performance.now();

    try {
      let resolvedPath = ep.path.replace('{key}', 'PROJ-1').replace('{id}', '1');
      const res = await fetch(resolvedPath, {
        method: ep.method,
        headers: {
          'Authorization': `Bearer ${this.api.token()}`,
          'Accept': 'application/json'
        }
      });

      const text = await res.text();
      const durationMs = Math.round(performance.now() - startTime);

      let formattedBody = text;
      try {
        formattedBody = JSON.stringify(JSON.parse(text), null, 2);
      } catch {}

      this.testResults[key] = {
        status: res.status,
        statusText: res.statusText,
        body: formattedBody,
        durationMs
      };
    } catch (err: any) {
      this.testResults[key] = {
        status: 0,
        statusText: 'Error',
        body: err.message || 'Request failed',
        durationMs: Math.round(performance.now() - startTime)
      };
    } finally {
      this.testingEp.set(null);
    }
  }

  async loadRateLimits(): Promise<void> {
    try {
      const st = await this.api.getRateLimits();
      this.rateLimitStatus.set(st);
    } catch {
      // ignore
    }
  }

  async resetBucket(): Promise<void> {
    try {
      await this.api.resetRateLimits(false);
      await this.loadRateLimits();
    } catch {
      // ignore
    }
  }

  async simulateBurst(count: number): Promise<void> {
    this.burstTesting.set(true);
    this.burstLogs = [];

    const requests = Array.from({ length: count }, (_, i) => i + 1);
    for (const i of requests) {
      try {
        const res = await fetch('/api/users', {
          headers: {
            'Authorization': `Bearer ${this.api.token()}`
          }
        });
        const retryAfter = res.headers.get('Retry-After');
        const remaining = res.headers.get('X-RateLimit-Remaining');

        let msg = `Allowed (Remaining tokens: ${remaining ?? 'N/A'})`;
        if (res.status === 429) {
          msg = `THROTTLED (429 Too Many Requests) - Retry-After: ${retryAfter || '1'}s`;
        }
        this.burstLogs.unshift({
          index: i,
          status: res.status,
          statusText: res.statusText,
          message: msg
        });
      } catch (err: any) {
        this.burstLogs.unshift({
          index: i,
          status: 0,
          statusText: 'Network Error',
          message: err.message
        });
      }
    }

    await this.loadRateLimits();
    this.burstTesting.set(false);
  }

  async loadRawSpec(): Promise<void> {
    try {
      const spec = await this.api.getOpenApiSpec();
      this.rawSpecString.set(JSON.stringify(spec, null, 2));
    } catch (err: any) {
      this.rawSpecString.set(`Failed to load spec: ${err.message}`);
    }
  }

  async copyRawSpec(): Promise<void> {
    try {
      await navigator.clipboard.writeText(this.rawSpecString());
      this.copyButtonText.set('Copied! ✓');
      setTimeout(() => this.copyButtonText.set('Copy JSON'), 2000);
    } catch {
      this.copyButtonText.set('Failed to copy');
    }
  }
}
