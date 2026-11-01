import { Component, OnInit, computed, inject, signal } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { ActivatedRoute, Router, RouterLink } from '@angular/router';
import { ApiService } from '../../services/api.service';
import {
  JqlSearchResponse,
  JqlSuggestions,
  JqlToken,
  JqlValidateResponse,
  SavedFilter,
  WorkItem,
  WorkItemDetail,
} from '../../models/jira.models';

@Component({
  selector: 'app-search',
  standalone: true,
  imports: [CommonModule, FormsModule, RouterLink],
  template: `
    <div class="search-workspace">
      @if (toastMessage()) {
        <div class="toast-banner" [class.toast-error]="toastIsError()">
          <span>{{ toastMessage() }}</span>
          <button type="button" class="toast-close" (click)="toastMessage.set('')">&times;</button>
        </div>
      }

      <!-- Left Sidebar: Saved Filters Drawer -->
      <aside class="filters-sidebar" [class.collapsed]="!sidebarOpen()">
        <div class="sidebar-header">
          @if (sidebarOpen()) {
            <div class="sidebar-title-wrap">
              <h3>Saved Filters</h3>
              <span class="filter-count-pill">{{ filteredSavedFilters().length }}</span>
            </div>
          }
          <button
            type="button"
            class="btn-icon"
            (click)="sidebarOpen.set(!sidebarOpen())"
            [title]="sidebarOpen() ? 'Collapse Filters Drawer' : 'Expand Filters Drawer'"
          >
            {{ sidebarOpen() ? '◀' : '▶' }}
          </button>
        </div>

        @if (sidebarOpen()) {
          <div class="sidebar-tabs">
            <button
              type="button"
              [class.active]="filterDrawerTab() === 'all'"
              (click)="filterDrawerTab.set('all')"
            >
              All
            </button>
            <button
              type="button"
              [class.active]="filterDrawerTab() === 'favorites'"
              (click)="filterDrawerTab.set('favorites')"
            >
              ★ Starred
            </button>
            <button
              type="button"
              [class.active]="filterDrawerTab() === 'mine'"
              (click)="filterDrawerTab.set('mine')"
            >
              My Filters
            </button>
          </div>

          <div class="sidebar-actions">
            <button type="button" class="btn btn-primary btn-sm w-full" (click)="openSaveFilterModal()">
              + Save Current JQL as Filter
            </button>
          </div>

          <div class="saved-filter-list">
            @for (sf of filteredSavedFilters(); track sf.id) {
              <div
                class="saved-filter-card"
                [class.active]="activeFilter()?.id === sf.id"
                (click)="selectSavedFilter(sf)"
              >
                <div class="sf-top">
                  <button
                    type="button"
                    class="star-btn"
                    [class.starred]="sf.is_favorite"
                    (click)="toggleStar(sf, $event)"
                    title="Toggle Favorite Filter"
                  >
                    {{ sf.is_favorite ? '★' : '☆' }}
                  </button>
                  <span class="sf-name">{{ sf.name }}</span>
                  @if (sf.is_system) {
                    <span class="sf-badge">System</span>
                  } @else {
                    <span class="sf-badge share">{{ sf.share_type }}</span>
                  }
                </div>
                @if (sf.description) {
                  <div class="sf-desc">{{ sf.description }}</div>
                }
                <code class="sf-jql-preview">{{ sf.jql_query }}</code>
                <div class="sf-footer" (click)="$event.stopPropagation()">
                  <button
                    type="button"
                    class="btn-link-xs"
                    (click)="openSubscriptionsModal(sf)"
                    title="Manage Scheduled Email Digest Subscriptions"
                  >
                    🔔 Subscriptions ({{ sf.subscriptions.length || 0 }})
                  </button>
                  @if (!sf.is_system) {
                    <div class="sf-owner-btns">
                      <button type="button" class="btn-link-xs" (click)="openEditFilterModal(sf)">
                        Edit
                      </button>
                      <button
                        type="button"
                        class="btn-link-xs danger"
                        (click)="deleteFilter(sf)"
                      >
                        Delete
                      </button>
                    </div>
                  }
                </div>
              </div>
            }
          </div>
        }
      </aside>

      <!-- Main Search & JQL Navigator Content -->
      <div class="search-main">
        <!-- Top Header Bar -->
        <div class="search-header">
          <div class="header-left">
            <div>
              <div class="breadcrumb-row">
                <span class="crumb">Search &amp; JQL Navigator</span>
                @if (activeFilter(); as af) {
                  <span class="crumb-sep">/</span>
                  <span class="crumb-active">{{ af.name }}</span>
                }
              </div>
              <h1 class="search-title">
                {{ activeFilter()?.name || 'Issue Search & JQL Query Engine' }}
              </h1>
            </div>
          </div>

          <div class="header-controls">
            <!-- Mode Switcher: Basic vs Advanced JQL -->
            <div class="mode-toggle-group">
              <button
                type="button"
                class="mode-btn"
                [class.active]="searchMode() === 'basic'"
                (click)="switchMode('basic')"
              >
                Basic Filter Mode
              </button>
              <button
                type="button"
                class="mode-btn"
                [class.active]="searchMode() === 'jql'"
                (click)="switchMode('jql')"
              >
                Advanced JQL Editor
              </button>
            </div>

            <!-- View Switcher: List Table vs Split Detail -->
            <div class="mode-toggle-group">
              <button
                type="button"
                class="mode-btn"
                [class.active]="viewLayout() === 'list'"
                (click)="viewLayout.set('list')"
              >
                ☰ List View
              </button>
              <button
                type="button"
                class="mode-btn"
                [class.active]="viewLayout() === 'split'"
                (click)="setSplitLayout()"
              >
                ◫ Detail View
              </button>
            </div>

            <!-- Export Dropdown / Actions -->
            <div class="export-group">
              <button type="button" class="btn btn-secondary btn-sm" (click)="exportResults('csv')">
                ⬇ CSV
              </button>
              <button type="button" class="btn btn-secondary btn-sm" (click)="exportJson()">
                ⬇ JSON
              </button>
              <button type="button" class="btn btn-secondary btn-sm" (click)="exportResults('markdown')">
                ⬇ Markdown
              </button>
              <button type="button" class="btn btn-secondary btn-sm" (click)="exportResults('html')">
                🖨 Printable HTML
              </button>
            </div>
          </div>
        </div>

        <!-- BASIC FILTER MODE -->
        @if (searchMode() === 'basic') {
          <div class="basic-search-panel card">
            <div class="basic-filters-grid">
              <div class="filter-field">
                <label>Project</label>
                <select [(ngModel)]="basicFilters.project" (change)="applyBasicFilters()">
                  <option value="">All Projects</option>
                  @for (p of api.projects(); track p.id) {
                    <option [value]="p.key">{{ p.name }} ({{ p.key }})</option>
                  }
                </select>
              </div>

              <div class="filter-field">
                <label>Issue Type</label>
                <select [(ngModel)]="basicFilters.type" (change)="applyBasicFilters()">
                  <option value="">All Types</option>
                  @for (t of api.issueTypes(); track t.id) {
                    <option [value]="t.name">{{ t.name }}</option>
                  }
                </select>
              </div>

              <div class="filter-field">
                <label>Category</label>
                <select [(ngModel)]="basicFilters.category" (change)="applyBasicFilters()">
                  <option value="">All Categories</option>
                  <option value="Bug">Bug</option>
                  <option value="New Feature">New Feature</option>
                  <option value="Improvement">Improvement</option>
                </select>
              </div>

              <div class="filter-field">
                <label>Status</label>
                <select [(ngModel)]="basicFilters.status" (change)="applyBasicFilters()">
                  <option value="">All Statuses</option>
                  @for (st of suggestions()?.values?.['status'] || ['Backlog','To Do','In Progress','In Review','QA / Testing','Blocked','Done']; track st) {
                    <option [value]="st">{{ st }}</option>
                  }
                </select>
              </div>

              <div class="filter-field">
                <label>Priority</label>
                <select [(ngModel)]="basicFilters.priority" (change)="applyBasicFilters()">
                  <option value="">All Priorities</option>
                  <option value="Critical">Critical</option>
                  <option value="Highest">Highest</option>
                  <option value="High">High</option>
                  <option value="Medium">Medium</option>
                  <option value="Low">Low</option>
                  <option value="Lowest">Lowest</option>
                </select>
              </div>

              <div class="filter-field">
                <label>Assignee</label>
                <select [(ngModel)]="basicFilters.assignee" (change)="applyBasicFilters()">
                  <option value="">Anyone</option>
                  <option value="currentUser()">Current User (currentUser())</option>
                  <option value="EMPTY">Unassigned (IS EMPTY)</option>
                  @for (u of api.users(); track u.id) {
                    <option [value]="u.username">{{ u.display_name }} (@{{ u.username }})</option>
                  }
                </select>
              </div>

              <div class="filter-field">
                <label>Sprint</label>
                <select [(ngModel)]="basicFilters.sprint" (change)="applyBasicFilters()">
                  <option value="">Any Sprint</option>
                  <option value="openSprints()">Active Sprints (openSprints())</option>
                  <option value="EMPTY">Backlog / No Sprint</option>
                  @for (sp of api.sprints(); track sp.id) {
                    <option [value]="sp.name">{{ sp.name }}</option>
                  }
                </select>
              </div>

              <div class="filter-field">
                <label>Resolution</label>
                <select [(ngModel)]="basicFilters.resolution" (change)="applyBasicFilters()">
                  <option value="">Any Resolution</option>
                  <option value="EMPTY">Unresolved (IS EMPTY)</option>
                  <option value="NOT_EMPTY">Resolved (IS NOT EMPTY)</option>
                  @for (r of api.resolutions(); track r.id) {
                    <option [value]="r.name">{{ r.name }}</option>
                  }
                </select>
              </div>

              <div class="filter-field">
                <label>Component</label>
                <select [(ngModel)]="basicFilters.component" (change)="applyBasicFilters()">
                  <option value="">Any Component</option>
                  @for (c of api.components(); track c.id) {
                    <option [value]="c.name">{{ c.name }}</option>
                  }
                </select>
              </div>

              <div class="filter-field">
                <label>Fix Version</label>
                <select [(ngModel)]="basicFilters.fixVersion" (change)="applyBasicFilters()">
                  <option value="">Any Version</option>
                  @for (v of api.versions(); track v.id) {
                    <option [value]="v.name">{{ v.name }}</option>
                  }
                </select>
              </div>

              <div class="filter-field filter-field-wide">
                <label>Full-Text Search (SQLite FTS5 BM25)</label>
                <div class="basic-text-row">
                  <input
                    type="text"
                    placeholder="Search title, description, comments, steps..."
                    [(ngModel)]="basicFilters.text"
                    (keyup.enter)="applyBasicFilters()"
                  />
                  <button type="button" class="btn btn-primary btn-sm" (click)="applyBasicFilters()">
                    Search
                  </button>
                  <button type="button" class="btn btn-ghost btn-sm" (click)="resetBasicFilters()">
                    Reset
                  </button>
                </div>
              </div>
            </div>

            <div class="generated-jql-strip">
              <span class="strip-label">Generated JQL:</span>
              <code>{{ jqlQuery() || '(All Issues — ORDER BY board_order ASC)' }}</code>
              <button type="button" class="btn-link-xs" (click)="switchMode('jql')">
                Edit in Advanced JQL →
              </button>
            </div>
          </div>
        }

        <!-- ADVANCED JQL EDITOR MODE -->
        @if (searchMode() === 'jql') {
          <div class="jql-editor-panel card">
            <div class="jql-toolbar">
              <div class="jql-status-badges">
                @if (validationState()?.valid !== false) {
                  <span class="jql-badge valid">✓ Valid JQL Syntax</span>
                } @else {
                  <span class="jql-badge invalid">✕ JQL Syntax Error</span>
                }

                @if (searchResult()?.execution_time_ms !== undefined) {
                  <span class="jql-badge timing" title="C++ Pratt Parser + SQLite Execution Time">
                    ⚡ {{ searchResult()!.execution_time_ms!.toFixed(2) }} ms
                  </span>
                }

                @if (searchResult()?.uses_fts5 || validationState()?.uses_fts5) {
                  <span class="jql-badge fts5" title="Powered by SQLite FTS5 Virtual Table & BM25 Ranking">
                    🔍 FTS5 Index Active
                  </span>
                }
              </div>

              <div class="jql-toolbar-right">
                <button
                  type="button"
                  class="btn btn-ghost btn-xs"
                  (click)="showSqlInspector.set(!showSqlInspector())"
                >
                  {{ showSqlInspector() ? 'Hide AST & SQL Inspector' : '⚙ Inspect AST & Compiled SQL' }}
                </button>
                <button
                  type="button"
                  class="btn btn-ghost btn-xs"
                  (click)="showAutocomplete.set(!showAutocomplete())"
                >
                  {{ showAutocomplete() ? 'Hide JQL Reference' : '💡 JQL Autocomplete & Functions' }}
                </button>
              </div>
            </div>

            <!-- CodeMirror-style JQL Input Box -->
            <div class="jql-input-wrapper" [class.has-error]="validationState()?.valid === false">
              <span class="jql-prompt-label">JQL</span>
              <textarea
                class="jql-textarea"
                rows="2"
                [(ngModel)]="jqlInputText"
                (ngModelChange)="onJqlInputChange($event)"
                (keydown.control.enter)="executeJqlSearch()"
                placeholder='e.g. project = "JIRA" AND status IN ("In Progress", "In Review") AND summary ~ "auth" ORDER BY priority DESC'
                spellcheck="false"
              ></textarea>
              <div class="jql-run-actions">
                <button type="button" class="btn btn-primary" (click)="executeJqlSearch()">
                  Search JQL
                </button>
                <button
                  type="button"
                  class="btn btn-secondary btn-sm"
                  (click)="clearJqlQuery()"
                  title="Clear Query"
                >
                  Clear
                </button>
              </div>
            </div>

            <!-- Live Syntax-Highlighted Token Stream -->
            @if (activeTokens().length > 0) {
              <div class="token-stream-bar">
                <span class="token-stream-label">Tokens:</span>
                @for (tok of activeTokens(); track $index) {
                  <span
                    class="jql-tok"
                    [attr.data-tok-type]="tok.type"
                    [title]="tok.type + ' (col ' + tok.col + ')'"
                  >
                    {{ tok.raw_text }}
                  </span>
                }
              </div>
            }

            <!-- Inline Error Marker Alert -->
            @if (validationState()?.valid === false && validationState()?.error) {
              <div class="jql-error-banner">
                <strong>Syntax Error (Line {{ validationState()?.error_line || 1 }}, Col {{ validationState()?.error_col || 1 }}):</strong>
                <span>{{ validationState()?.error }}</span>
              </div>
            }

            <!-- Quick-Insert JQL Snippets & Functions Bar -->
            <div class="jql-quick-chips">
              <span class="chips-label">Quick Insert:</span>
              <button type="button" class="jql-chip" (click)="appendJqlSnippet('assignee = currentUser()')">
                assignee = currentUser()
              </button>
              <button type="button" class="jql-chip" (click)="appendJqlSnippet('sprint IN openSprints()')">
                sprint IN openSprints()
              </button>
              <button type="button" class="jql-chip" (click)="appendJqlSnippet('text ~ &quot;SQLite&quot;')">
                text ~ "SQLite" (FTS5)
              </button>
              <button type="button" class="jql-chip" (click)="appendJqlSnippet('status WAS &quot;In Progress&quot;')">
                status WAS "In Progress"
              </button>
              <button type="button" class="jql-chip" (click)="appendJqlSnippet('status CHANGED')">
                status CHANGED
              </button>
              <button type="button" class="jql-chip" (click)="appendJqlSnippet('cf[environment] = &quot;Production&quot;')">
                cf[environment] = "Production"
              </button>
              <button type="button" class="jql-chip" (click)="appendJqlSnippet('updated >= startOfWeek()')">
                updated &gt;= startOfWeek()
              </button>
              <button type="button" class="jql-chip" (click)="appendJqlOrderBy('ORDER BY priority DESC, updated DESC')">
                ORDER BY priority DESC
              </button>
            </div>

            <!-- Context-Aware Autocomplete & JQL Reference Drawer -->
            @if (showAutocomplete() && suggestions(); as sug) {
              <div class="jql-autocomplete-drawer">
                <div class="ac-col">
                  <h4>Fields &amp; Custom Fields (Click to Insert)</h4>
                  <div class="ac-pill-list">
                    @for (f of sug.fields; track f.name) {
                      <button
                        type="button"
                        class="ac-pill field"
                        (click)="insertTokenAtEnd(f.cf_syntax || f.name)"
                        [title]="f.description"
                      >
                        <strong>{{ f.cf_syntax || f.name }}</strong>
                        <small>{{ f.type }}</small>
                      </button>
                    }
                  </div>
                </div>

                <div class="ac-col">
                  <h4>Operators &amp; History Predicates</h4>
                  <div class="ac-pill-list">
                    @for (op of sug.operators; track op) {
                      <button
                        type="button"
                        class="ac-pill operator"
                        (click)="insertTokenAtEnd(op)"
                      >
                        {{ op }}
                      </button>
                    }
                    @for (kw of sug.keywords; track kw) {
                      <button
                        type="button"
                        class="ac-pill keyword"
                        (click)="insertTokenAtEnd(kw)"
                      >
                        {{ kw }}
                      </button>
                    }
                  </div>
                </div>

                <div class="ac-col">
                  <h4>Built-in JQL Functions</h4>
                  <div class="ac-pill-list">
                    @for (fn of sug.functions; track fn.signature) {
                      <button
                        type="button"
                        class="ac-pill func"
                        (click)="insertTokenAtEnd(fn.signature)"
                        [title]="fn.description"
                      >
                        <code>{{ fn.signature }}</code>
                      </button>
                    }
                  </div>
                </div>
              </div>
            }

            <!-- Compiled AST & Parameterized SQLite Query Inspector -->
            @if (showSqlInspector() && (validationState() || searchResult()); as info) {
              <div class="sql-inspector-box">
                <div class="inspector-grid">
                  <div>
                    <span class="insp-label">Pratt Parser AST Summary:</span>
                    <pre class="insp-pre">{{ info.ast_summary || '(Empty AST)' }}</pre>
                  </div>
                  <div>
                    <span class="insp-label">Compiled Parameterized SQLite WHERE + ORDER BY:</span>
                    <pre class="insp-pre">WHERE {{ info.where_sql || '1 = 1' }}
ORDER BY {{ info.order_by_sql || 'w.board_order ASC, w.id ASC' }}</pre>
                  </div>
                </div>
                @if (info.bind_params && info.bind_params.length > 0) {
                  <div class="bound-params-row">
                    <span class="insp-label">Bound SQL Parameters (?):</span>
                    @for (bp of info.bind_params; track $index) {
                      <code class="param-pill">?{{ $index + 1 }} = "{{ bp }}"</code>
                    }
                  </div>
                }
              </div>
            }
          </div>
        }

        <!-- RESULTS SUMMARY BAR -->
        <div class="results-meta-bar">
          <div class="results-count">
            Showing <strong>{{ items().length }}</strong> of <strong>{{ totalMatches() }}</strong> matching work items
            @if (searchResult()?.uses_fts5) {
              <span class="fts-tag">BM25 Full-Text Ranked</span>
            }
          </div>
          <div class="results-sort-quick">
            <span>Quick Sort:</span>
            <button type="button" class="btn-link-xs" (click)="quickOrderBy('priority', 'DESC')">
              Priority ↓
            </button>
            <button type="button" class="btn-link-xs" (click)="quickOrderBy('updated', 'DESC')">
              Updated ↓
            </button>
            <button type="button" class="btn-link-xs" (click)="quickOrderBy('created', 'DESC')">
              Created ↓
            </button>
            <button type="button" class="btn-link-xs" (click)="quickOrderBy('storyPoints', 'DESC')">
              Story Points ↓
            </button>
            <button type="button" class="btn-link-xs" (click)="quickOrderBy('key', 'ASC')">
              Key ↑
            </button>
          </div>
        </div>

        <!-- VIEW MODE 1: LIST TABLE VIEW -->
        @if (viewLayout() === 'list') {
          <div class="results-table-card card">
            <table class="jql-results-table">
              <thead>
                <tr>
                  <th>Key</th>
                  <th>Type</th>
                  <th>Category</th>
                  <th>Summary</th>
                  <th>Status</th>
                  <th>Priority</th>
                  <th>Resolution</th>
                  <th>Assignee</th>
                  <th>Sprint</th>
                  <th>Components / Fix Ver</th>
                  <th>Custom Fields</th>
                  <th>Pts</th>
                  <th>Updated</th>
                </tr>
              </thead>
              <tbody>
                @for (item of items(); track item.id) {
                  <tr
                    class="result-row"
                    [class.selected]="selectedItem()?.id === item.id"
                    (click)="selectItemForSplit(item)"
                  >
                    <td class="col-key">
                      <a [routerLink]="['/items', item.key]" class="issue-key-link" (click)="$event.stopPropagation()">
                        {{ item.key }}
                      </a>
                      @if (item.bm25_score !== undefined) {
                        <span class="bm25-pill" title="SQLite FTS5 BM25 Relevance Score">
                          {{ item.bm25_score.toFixed(2) }}
                        </span>
                      }
                    </td>
                    <td>
                      <span class="type-pill" [attr.data-type]="item.type">{{ item.type }}</span>
                    </td>
                    <td>
                      <span class="cat-pill" [attr.data-cat]="item.category">{{ item.category }}</span>
                    </td>
                    <td class="col-summary">
                      <a [routerLink]="['/items', item.key]" class="summary-link" (click)="$event.stopPropagation()">
                        {{ item.title }}
                      </a>
                      @if (item.labels) {
                        <div class="row-labels">
                          @for (lbl of item.labels.split(','); track lbl) {
                            @if (lbl.trim()) {
                              <span class="lbl-chip">{{ lbl.trim() }}</span>
                            }
                          }
                        </div>
                      }
                    </td>
                    <td>
                      <span class="status-badge" [attr.data-status]="item.status">{{ item.status }}</span>
                    </td>
                    <td>
                      <span class="prio-badge" [attr.data-prio]="item.priority">{{ item.priority }}</span>
                    </td>
                    <td>
                      @if (item.resolution_name) {
                        <span class="res-resolved">✓ {{ item.resolution_name }}</span>
                      } @else {
                        <span class="res-unresolved">Unresolved</span>
                      }
                    </td>
                    <td>
                      @if (item.assignee_name) {
                        <div class="user-cell">
                          <span class="avatar-xs" [style.background]="item.assignee_color">
                            {{ item.assignee_name.slice(0, 2).toUpperCase() }}
                          </span>
                          <span>{{ item.assignee_name }}</span>
                        </div>
                      } @else {
                        <span class="muted">Unassigned</span>
                      }
                    </td>
                    <td>
                      <span class="sprint-tag">{{ item.sprint_name || 'Backlog' }}</span>
                    </td>
                    <td>
                      <div class="comp-ver-cell">
                        @for (c of item.components || []; track c.id) {
                          <span class="mini-tag comp">⚙ {{ c.name }}</span>
                        }
                        @for (v of item.fix_versions || []; track v.id) {
                          <span class="mini-tag ver">🏷 {{ v.name }}</span>
                        }
                      </div>
                    </td>
                    <td>
                      <div class="cf-summary-cell">
                        @if (item.custom_fields?.['environment']) {
                          <span class="mini-tag cf">{{ item.custom_fields?.['environment'] }}</span>
                        }
                        @if (item.custom_fields?.['customer_tier']) {
                          <span class="mini-tag cf">{{ item.custom_fields?.['customer_tier'] }}</span>
                        }
                      </div>
                    </td>
                    <td class="col-pts">
                      <span class="pts-circle">{{ item.story_points }}</span>
                    </td>
                    <td class="col-date">{{ item.updated_at.slice(0, 10) }}</td>
                  </tr>
                } @empty {
                  <tr>
                    <td colspan="13" class="empty-results">
                      No work items matched the JQL query <code>{{ jqlQuery() }}</code>.
                    </td>
                  </tr>
                }
              </tbody>
            </table>
          </div>
        }

        <!-- VIEW MODE 2: SPLIT DETAIL NAVIGATOR VIEW -->
        @if (viewLayout() === 'split') {
          <div class="split-navigator">
            <div class="split-list-pane card">
              @for (item of items(); track item.id) {
                <div
                  class="split-card-item"
                  [class.active]="selectedItem()?.id === item.id"
                  (click)="selectItemForSplit(item)"
                >
                  <div class="sc-top">
                    <span class="sc-key">{{ item.key }}</span>
                    <span class="type-pill" [attr.data-type]="item.type">{{ item.type }}</span>
                    <span class="status-badge" [attr.data-status]="item.status">{{ item.status }}</span>
                  </div>
                  <div class="sc-title">{{ item.title }}</div>
                  <div class="sc-meta">
                    <span class="prio-badge" [attr.data-prio]="item.priority">{{ item.priority }}</span>
                    <span>{{ item.assignee_name || 'Unassigned' }}</span>
                    <span class="pts-circle">{{ item.story_points }} pts</span>
                  </div>
                </div>
              } @empty {
                <div class="empty-results">No matching issues.</div>
              }
            </div>

            <div class="split-detail-pane card">
              @if (selectedItemDetail(); as detail) {
                <div class="detail-pane-header">
                  <div>
                    <div class="dp-crumbs">
                      <span class="sc-key">{{ detail.key }}</span>
                      <span class="type-pill" [attr.data-type]="detail.type">{{ detail.type }}</span>
                      <span class="cat-pill" [attr.data-cat]="detail.category">{{ detail.category }}</span>
                      <span class="status-badge" [attr.data-status]="detail.status">{{ detail.status }}</span>
                    </div>
                    <h2>{{ detail.title }}</h2>
                  </div>
                  <a [routerLink]="['/items', detail.key]" class="btn btn-primary btn-sm">
                    Open Full Issue View ↗
                  </a>
                </div>

                <div class="dp-meta-grid">
                  <div><strong>Priority:</strong> {{ detail.priority }}</div>
                  <div><strong>Assignee:</strong> {{ detail.assignee_name || 'Unassigned' }}</div>
                  <div><strong>Reporter:</strong> {{ detail.reporter_name || 'System' }}</div>
                  <div><strong>Sprint:</strong> {{ detail.sprint_name || 'Backlog' }}</div>
                  <div><strong>Resolution:</strong> {{ detail.resolution_name || 'Unresolved' }}</div>
                  <div><strong>Story Points:</strong> {{ detail.story_points }} ({{ detail.progress }}%)</div>
                </div>

                @if (detail.description) {
                  <div class="dp-section">
                    <h4>Description</h4>
                    <p class="dp-desc">{{ detail.description }}</p>
                  </div>
                }

                @if (detail.custom_field_entries && detail.custom_field_entries.length > 0) {
                  <div class="dp-section">
                    <h4>Custom Fields</h4>
                    <div class="dp-cf-grid">
                      @for (cf of detail.custom_field_entries; track cf.custom_field_id) {
                        @if (cf.display_value) {
                          <div class="dp-cf-item">
                            <span class="dp-cf-name">{{ cf.name }}:</span>
                            <span class="dp-cf-val">{{ cf.display_value }}</span>
                          </div>
                        }
                      }
                    </div>
                  </div>
                }

                @if (detail.comments && detail.comments.length > 0) {
                  <div class="dp-section">
                    <h4>Comments ({{ detail.comments.length }})</h4>
                    @for (c of detail.comments; track c.id) {
                      <div class="dp-comment">
                        <strong>{{ c.author_name }}</strong> <small>{{ c.created_at }}</small>
                        <div>{{ c.body }}</div>
                      </div>
                    }
                  </div>
                }
              } @else {
                <div class="empty-results">Select an issue from the list to inspect its details.</div>
              }
            </div>
          </div>
        }
      </div>
    </div>

    <!-- MODAL 1: SAVE / EDIT SAVED FILTER -->
    @if (showFilterModal()) {
      <div class="modal-backdrop" (click)="showFilterModal.set(false)">
        <div class="modal-dialog" (click)="$event.stopPropagation()">
          <div class="modal-header">
            <h3>{{ editingFilterId() ? 'Edit Saved Filter' : 'Save JQL Filter' }}</h3>
            <button type="button" class="btn-close" (click)="showFilterModal.set(false)">&times;</button>
          </div>
          <div class="modal-body">
            <div class="form-group">
              <label>Filter Name *</label>
              <input
                type="text"
                [(ngModel)]="filterForm.name"
                placeholder="e.g. Q4 Release Blockers"
              />
            </div>
            <div class="form-group">
              <label>Description</label>
              <input
                type="text"
                [(ngModel)]="filterForm.description"
                placeholder="Brief summary of what this JQL filter tracks"
              />
            </div>
            <div class="form-group">
              <label>JQL Query *</label>
              <textarea rows="3" class="mono-input" [(ngModel)]="filterForm.jql_query"></textarea>
            </div>
            <div class="form-row">
              <div class="form-group">
                <label>Share Permission Scope</label>
                <select [(ngModel)]="filterForm.share_type">
                  <option value="public">Public (Everyone in Workspace)</option>
                  <option value="private">Private (Only Me)</option>
                  <option value="role">Project Role</option>
                </select>
              </div>
              @if (filterForm.share_type === 'role') {
                <div class="form-group">
                  <label>Target Role</label>
                  <select [(ngModel)]="filterForm.share_target">
                    <option value="Admin">Admin</option>
                    <option value="Project Manager">Project Manager</option>
                    <option value="Developer">Developer</option>
                    <option value="QA Engineer">QA Engineer</option>
                  </select>
                </div>
              }
            </div>
            <label class="checkbox-row">
              <input type="checkbox" [(ngModel)]="filterForm.is_favorite" />
              <span>★ Pin to Starred Favorites</span>
            </label>
          </div>
          <div class="modal-footer">
            <button type="button" class="btn btn-ghost" (click)="showFilterModal.set(false)">
              Cancel
            </button>
            <button type="button" class="btn btn-primary" (click)="saveFilterSubmit()">
              {{ editingFilterId() ? 'Update Filter' : 'Create Filter' }}
            </button>
          </div>
        </div>
      </div>
    }

    <!-- MODAL 2: FILTER SUBSCRIPTIONS (SCHEDULED CRON DIGEST) -->
    @if (subscriptionTargetFilter(); as subFilter) {
      <div class="modal-backdrop" (click)="subscriptionTargetFilter.set(null)">
        <div class="modal-dialog" (click)="$event.stopPropagation()">
          <div class="modal-header">
            <h3>🔔 Filter Subscriptions — {{ subFilter.name }}</h3>
            <button type="button" class="btn-close" (click)="subscriptionTargetFilter.set(null)">
              &times;
            </button>
          </div>
          <div class="modal-body">
            <p class="sub-help">
              Schedule automated JQL filter execution and digest notifications for
              <code>{{ subFilter.jql_query }}</code>
            </p>

            @if (subFilter.subscriptions && subFilter.subscriptions.length > 0) {
              <div class="existing-subs">
                @for (sub of subFilter.subscriptions; track sub.id) {
                  <div class="sub-item">
                    <div>
                      <strong>{{ sub.frequency_label }}</strong>
                      <code>{{ sub.cron_schedule }}</code>
                      <span class="sf-badge">Scope: {{ sub.recipient_scope }}</span>
                    </div>
                    <button
                      type="button"
                      class="btn-link-xs danger"
                      (click)="removeSubscription(sub.id)"
                    >
                      Remove
                    </button>
                  </div>
                }
              </div>
            }

            <div class="new-sub-box">
              <h4>Add New Subscription</h4>
              <div class="form-row">
                <div class="form-group">
                  <label>Schedule Preset</label>
                  <select [(ngModel)]="subForm.preset" (change)="onSubPresetChange()">
                    <option value="daily">Daily (Weekdays at 08:00)</option>
                    <option value="weekly">Weekly (Mondays at 09:00)</option>
                    <option value="hourly">Hourly Digest</option>
                  </select>
                </div>
                <div class="form-group">
                  <label>Cron Expression</label>
                  <input type="text" [(ngModel)]="subForm.cron_schedule" />
                </div>
              </div>
              <div class="form-group">
                <label>Recipients</label>
                <select [(ngModel)]="subForm.recipient_scope">
                  <option value="self">Only Me (Personal Digest)</option>
                  <option value="role:Developer">Role: Developers</option>
                  <option value="role:QA Engineer">Role: QA Engineers</option>
                  <option value="all">All Project Members</option>
                </select>
              </div>
              <label class="checkbox-row">
                <input type="checkbox" [(ngModel)]="subForm.email_empty" />
                <span>Send digest even if JQL query returns 0 matching issues</span>
              </label>
            </div>
          </div>
          <div class="modal-footer">
            <button type="button" class="btn btn-ghost" (click)="subscriptionTargetFilter.set(null)">
              Close
            </button>
            <button type="button" class="btn btn-primary" (click)="createSubscriptionSubmit()">
              + Subscribe to Filter
            </button>
          </div>
        </div>
      </div>
    }
  `,
  styles: [
    `
      .search-workspace {
        display: flex;
        min-height: calc(100vh - 60px);
        background: #f4f5f7;
      }
      .toast-banner {
        position: fixed;
        bottom: 20px;
        right: 24px;
        background: #0052cc;
        color: #fff;
        padding: 10px 16px;
        border-radius: 6px;
        box-shadow: 0 4px 12px rgba(9, 30, 66, 0.25);
        display: flex;
        align-items: center;
        gap: 12px;
        z-index: 2000;
        font-size: 13px;
        font-weight: 500;
      }
      .toast-banner.toast-error {
        background: #de350b;
      }
      .toast-close {
        background: none;
        border: none;
        color: #fff;
        font-size: 16px;
        cursor: pointer;
      }
      .filters-sidebar {
        width: 300px;
        background: #ffffff;
        border-right: 1px solid #dfe1e6;
        display: flex;
        flex-direction: column;
        transition: width 0.15s ease;
        flex-shrink: 0;
      }
      .filters-sidebar.collapsed {
        width: 44px;
      }
      .sidebar-header {
        display: flex;
        align-items: center;
        justify-content: space-between;
        padding: 14px 14px;
        border-bottom: 1px solid #ebecf0;
      }
      .sidebar-title-wrap {
        display: flex;
        align-items: center;
        gap: 8px;
      }
      .sidebar-title-wrap h3 {
        margin: 0;
        font-size: 14px;
        font-weight: 700;
        color: #172b4d;
      }
      .filter-count-pill {
        background: #ebecf0;
        color: #42526e;
        font-size: 11px;
        font-weight: 600;
        padding: 2px 7px;
        border-radius: 10px;
      }
      .btn-icon {
        background: #f4f5f7;
        border: 1px solid #dfe1e6;
        border-radius: 4px;
        cursor: pointer;
        padding: 4px 8px;
        font-size: 11px;
      }
      .sidebar-tabs {
        display: flex;
        padding: 8px 10px 0;
        gap: 4px;
        border-bottom: 1px solid #ebecf0;
      }
      .sidebar-tabs button {
        flex: 1;
        background: none;
        border: none;
        border-bottom: 2px solid transparent;
        padding: 6px 4px;
        font-size: 12px;
        font-weight: 600;
        color: #5e6c84;
        cursor: pointer;
      }
      .sidebar-tabs button.active {
        color: #0052cc;
        border-bottom-color: #0052cc;
      }
      .sidebar-actions {
        padding: 10px 12px;
      }
      .w-full {
        width: 100%;
      }
      .saved-filter-list {
        flex: 1;
        overflow-y: auto;
        padding: 4px 10px 16px;
        display: flex;
        flex-direction: column;
        gap: 8px;
      }
      .saved-filter-card {
        border: 1px solid #dfe1e6;
        border-radius: 6px;
        padding: 10px;
        background: #fafbfc;
        cursor: pointer;
        transition: all 0.12s ease;
      }
      .saved-filter-card:hover {
        border-color: #4c9aff;
        background: #ffffff;
      }
      .saved-filter-card.active {
        border-color: #0052cc;
        background: #deebff;
      }
      .sf-top {
        display: flex;
        align-items: center;
        gap: 6px;
      }
      .star-btn {
        background: none;
        border: none;
        cursor: pointer;
        font-size: 15px;
        color: #8993a4;
        padding: 0;
      }
      .star-btn.starred {
        color: #ff991f;
      }
      .sf-name {
        font-size: 13px;
        font-weight: 600;
        color: #172b4d;
        flex: 1;
      }
      .sf-badge {
        font-size: 10px;
        background: #ebecf0;
        color: #42526e;
        padding: 1px 6px;
        border-radius: 4px;
        text-transform: uppercase;
        font-weight: 600;
      }
      .sf-desc {
        font-size: 11px;
        color: #5e6c84;
        margin: 4px 0;
      }
      .sf-jql-preview {
        display: block;
        font-size: 11px;
        background: rgba(9, 30, 66, 0.05);
        padding: 4px 6px;
        border-radius: 4px;
        color: #0747a6;
        white-space: nowrap;
        overflow: hidden;
        text-overflow: ellipsis;
        margin-top: 4px;
      }
      .sf-footer {
        display: flex;
        align-items: center;
        justify-content: space-between;
        margin-top: 6px;
        padding-top: 6px;
        border-top: 1px dashed #dfe1e6;
      }
      .sf-owner-btns {
        display: flex;
        gap: 8px;
      }
      .btn-link-xs {
        background: none;
        border: none;
        color: #0052cc;
        font-size: 11px;
        font-weight: 600;
        cursor: pointer;
        padding: 0;
      }
      .btn-link-xs.danger {
        color: #de350b;
      }
      .search-main {
        flex: 1;
        padding: 18px 24px;
        display: flex;
        flex-direction: column;
        gap: 14px;
        min-width: 0;
      }
      .search-header {
        display: flex;
        align-items: center;
        justify-content: space-between;
        flex-wrap: wrap;
        gap: 12px;
      }
      .breadcrumb-row {
        font-size: 12px;
        color: #5e6c84;
        display: flex;
        gap: 6px;
      }
      .crumb-active {
        color: #0052cc;
        font-weight: 600;
      }
      .search-title {
        margin: 2px 0 0;
        font-size: 20px;
        font-weight: 700;
        color: #172b4d;
      }
      .header-controls {
        display: flex;
        align-items: center;
        gap: 10px;
        flex-wrap: wrap;
      }
      .mode-toggle-group {
        display: inline-flex;
        background: #ebecf0;
        border-radius: 6px;
        padding: 2px;
      }
      .mode-btn {
        border: none;
        background: transparent;
        padding: 6px 12px;
        font-size: 12px;
        font-weight: 600;
        color: #42526e;
        border-radius: 4px;
        cursor: pointer;
      }
      .mode-btn.active {
        background: #ffffff;
        color: #0052cc;
        box-shadow: 0 1px 3px rgba(9, 30, 66, 0.15);
      }
      .export-group {
        display: flex;
        gap: 6px;
      }
      .card {
        background: #ffffff;
        border: 1px solid #dfe1e6;
        border-radius: 8px;
      }
      .basic-search-panel {
        padding: 14px 16px;
      }
      .basic-filters-grid {
        display: grid;
        grid-template-columns: repeat(auto-fill, minmax(175px, 1fr));
        gap: 10px;
      }
      .filter-field {
        display: flex;
        flex-direction: column;
        gap: 4px;
      }
      .filter-field-wide {
        grid-column: span 2;
      }
      .filter-field label {
        font-size: 11px;
        font-weight: 700;
        color: #5e6c84;
        text-transform: uppercase;
      }
      .filter-field select,
      .filter-field input {
        padding: 6px 8px;
        border: 1px solid #dfe1e6;
        border-radius: 4px;
        font-size: 12px;
        background: #fafbfc;
      }
      .basic-text-row {
        display: flex;
        gap: 6px;
      }
      .basic-text-row input {
        flex: 1;
      }
      .generated-jql-strip {
        margin-top: 12px;
        padding-top: 10px;
        border-top: 1px solid #ebecf0;
        display: flex;
        align-items: center;
        gap: 10px;
        font-size: 12px;
      }
      .strip-label {
        font-weight: 600;
        color: #5e6c84;
      }
      .generated-jql-strip code {
        flex: 1;
        background: #f4f5f7;
        padding: 4px 8px;
        border-radius: 4px;
        color: #0747a6;
      }
      .jql-editor-panel {
        padding: 12px 16px;
        display: flex;
        flex-direction: column;
        gap: 10px;
      }
      .jql-toolbar {
        display: flex;
        align-items: center;
        justify-content: space-between;
        flex-wrap: wrap;
        gap: 8px;
      }
      .jql-status-badges {
        display: flex;
        align-items: center;
        gap: 8px;
      }
      .jql-badge {
        font-size: 11px;
        font-weight: 700;
        padding: 3px 8px;
        border-radius: 12px;
      }
      .jql-badge.valid {
        background: #e3fcef;
        color: #006644;
      }
      .jql-badge.invalid {
        background: #ffebe6;
        color: #bf2600;
      }
      .jql-badge.timing {
        background: #deebff;
        color: #0747a6;
      }
      .jql-badge.fts5 {
        background: #eae6ff;
        color: #403294;
      }
      .jql-input-wrapper {
        display: flex;
        align-items: stretch;
        border: 2px solid #0052cc;
        border-radius: 6px;
        background: #fafbfc;
        overflow: hidden;
      }
      .jql-input-wrapper.has-error {
        border-color: #de350b;
      }
      .jql-prompt-label {
        background: #0747a6;
        color: #ffffff;
        font-weight: 800;
        font-size: 12px;
        padding: 0 12px;
        display: flex;
        align-items: center;
        letter-spacing: 0.5px;
      }
      .jql-textarea {
        flex: 1;
        border: none;
        padding: 10px 12px;
        font-family: 'JetBrains Mono', 'Fira Code', monospace;
        font-size: 13px;
        line-height: 1.45;
        color: #172b4d;
        background: #ffffff;
        resize: vertical;
        outline: none;
      }
      .jql-run-actions {
        display: flex;
        align-items: center;
        gap: 6px;
        padding: 6px 10px;
        background: #f4f5f7;
        border-left: 1px solid #dfe1e6;
      }
      .token-stream-bar {
        display: flex;
        align-items: center;
        flex-wrap: wrap;
        gap: 5px;
        padding: 6px 10px;
        background: #f4f5f7;
        border-radius: 4px;
        font-family: monospace;
        font-size: 12px;
      }
      .token-stream-label {
        font-size: 11px;
        font-weight: 700;
        color: #5e6c84;
        margin-right: 4px;
      }
      .jql-tok {
        padding: 1px 6px;
        border-radius: 4px;
        font-weight: 600;
        background: #ebecf0;
        color: #172b4d;
      }
      .jql-tok[data-tok-type='IDENTIFIER'] {
        background: #deebff;
        color: #0747a6;
      }
      .jql-tok[data-tok-type='CUSTOM_FIELD'] {
        background: #eae6ff;
        color: #403294;
      }
      .jql-tok[data-tok-type='STRING'] {
        background: #e3fcef;
        color: #006644;
      }
      .jql-tok[data-tok-type='NUMBER'] {
        background: #fff0b3;
        color: #172b4d;
      }
      .jql-tok[data-tok-type='OPERATOR'],
      .jql-tok[data-tok-type='IN'],
      .jql-tok[data-tok-type='NOT_IN'],
      .jql-tok[data-tok-type='IS'],
      .jql-tok[data-tok-type='IS_NOT'],
      .jql-tok[data-tok-type='WAS'],
      .jql-tok[data-tok-type='WAS_IN'],
      .jql-tok[data-tok-type='CHANGED'] {
        background: #fffae6;
        color: #974f0c;
        border: 1px solid #ffe380;
      }
      .jql-tok[data-tok-type='AND'],
      .jql-tok[data-tok-type='OR'],
      .jql-tok[data-tok-type='NOT'],
      .jql-tok[data-tok-type='ORDER_BY'],
      .jql-tok[data-tok-type='ASC'],
      .jql-tok[data-tok-type='DESC'] {
        background: #172b4d;
        color: #ffffff;
      }
      .jql-error-banner {
        background: #ffebe6;
        border: 1px solid #ffbdad;
        color: #bf2600;
        padding: 8px 12px;
        border-radius: 6px;
        font-size: 12px;
        display: flex;
        gap: 8px;
      }
      .jql-quick-chips {
        display: flex;
        align-items: center;
        flex-wrap: wrap;
        gap: 6px;
      }
      .chips-label {
        font-size: 11px;
        font-weight: 700;
        color: #5e6c84;
      }
      .jql-chip {
        background: #f4f5f7;
        border: 1px solid #dfe1e6;
        border-radius: 12px;
        padding: 3px 9px;
        font-family: monospace;
        font-size: 11px;
        color: #172b4d;
        cursor: pointer;
      }
      .jql-chip:hover {
        background: #deebff;
        border-color: #4c9aff;
        color: #0747a6;
      }
      .jql-autocomplete-drawer {
        display: grid;
        grid-template-columns: repeat(3, 1fr);
        gap: 12px;
        padding: 12px;
        background: #fafbfc;
        border: 1px solid #dfe1e6;
        border-radius: 6px;
      }
      .ac-col h4 {
        margin: 0 0 8px;
        font-size: 11px;
        text-transform: uppercase;
        color: #5e6c84;
      }
      .ac-pill-list {
        display: flex;
        flex-wrap: wrap;
        gap: 5px;
        max-height: 135px;
        overflow-y: auto;
      }
      .ac-pill {
        border: 1px solid #dfe1e6;
        background: #ffffff;
        border-radius: 4px;
        padding: 3px 7px;
        font-size: 11px;
        cursor: pointer;
        display: inline-flex;
        align-items: center;
        gap: 5px;
      }
      .ac-pill:hover {
        border-color: #0052cc;
        background: #deebff;
      }
      .ac-pill small {
        color: #6b778c;
        font-size: 10px;
      }
      .sql-inspector-box {
        background: #091e42;
        color: #ebecf0;
        padding: 12px;
        border-radius: 6px;
        font-size: 12px;
      }
      .inspector-grid {
        display: grid;
        grid-template-columns: 1fr 1fr;
        gap: 12px;
      }
      .insp-label {
        font-size: 11px;
        font-weight: 700;
        color: #79f2c0;
        text-transform: uppercase;
      }
      .insp-pre {
        margin: 4px 0 0;
        background: rgba(255, 255, 255, 0.06);
        padding: 8px;
        border-radius: 4px;
        white-space: pre-wrap;
        word-break: break-word;
        font-size: 11px;
      }
      .bound-params-row {
        margin-top: 8px;
        display: flex;
        align-items: center;
        flex-wrap: wrap;
        gap: 6px;
      }
      .param-pill {
        background: rgba(121, 242, 192, 0.15);
        color: #79f2c0;
        padding: 2px 6px;
        border-radius: 4px;
        font-size: 11px;
      }
      .results-meta-bar {
        display: flex;
        align-items: center;
        justify-content: space-between;
        font-size: 13px;
        color: #42526e;
      }
      .fts-tag {
        margin-left: 8px;
        background: #eae6ff;
        color: #403294;
        padding: 2px 8px;
        border-radius: 10px;
        font-size: 11px;
        font-weight: 700;
      }
      .results-sort-quick {
        display: flex;
        align-items: center;
        gap: 10px;
        font-size: 12px;
      }
      .results-table-card {
        overflow-x: auto;
      }
      .jql-results-table {
        width: 100%;
        border-collapse: collapse;
        font-size: 13px;
      }
      .jql-results-table th {
        text-align: left;
        padding: 10px 12px;
        background: #fafbfc;
        border-bottom: 2px solid #dfe1e6;
        font-size: 11px;
        font-weight: 700;
        color: #5e6c84;
        text-transform: uppercase;
        white-space: nowrap;
      }
      .jql-results-table td {
        padding: 10px 12px;
        border-bottom: 1px solid #ebecf0;
        vertical-align: middle;
      }
      .result-row:hover {
        background: #f4f5f7;
      }
      .issue-key-link {
        font-weight: 700;
        color: #0052cc;
        text-decoration: none;
        white-space: nowrap;
      }
      .bm25-pill {
        margin-left: 5px;
        font-size: 10px;
        background: #eae6ff;
        color: #403294;
        padding: 1px 5px;
        border-radius: 4px;
        font-weight: 700;
      }
      .summary-link {
        color: #172b4d;
        font-weight: 600;
        text-decoration: none;
      }
      .summary-link:hover {
        color: #0052cc;
      }
      .row-labels {
        display: flex;
        gap: 4px;
        margin-top: 3px;
      }
      .lbl-chip {
        font-size: 10px;
        background: #ebecf0;
        color: #42526e;
        padding: 1px 5px;
        border-radius: 3px;
      }
      .type-pill,
      .cat-pill,
      .status-badge,
      .prio-badge {
        display: inline-block;
        padding: 2px 7px;
        border-radius: 4px;
        font-size: 11px;
        font-weight: 700;
        white-space: nowrap;
      }
      .type-pill {
        background: #deebff;
        color: #0747a6;
      }
      .cat-pill[data-cat='Bug'] {
        background: #ffebe6;
        color: #bf2600;
      }
      .cat-pill[data-cat='New Feature'] {
        background: #e3fcef;
        color: #006644;
      }
      .cat-pill[data-cat='Improvement'] {
        background: #eae6ff;
        color: #403294;
      }
      .status-badge {
        background: #dfe1e6;
        color: #172b4d;
      }
      .status-badge[data-status='Done'] {
        background: #e3fcef;
        color: #006644;
      }
      .status-badge[data-status='In Progress'],
      .status-badge[data-status='In Review'],
      .status-badge[data-status='QA / Testing'] {
        background: #deebff;
        color: #0052cc;
      }
      .status-badge[data-status='Blocked'] {
        background: #ffebe6;
        color: #bf2600;
      }
      .prio-badge[data-prio='Critical'],
      .prio-badge[data-prio='Highest'],
      .prio-badge[data-prio='High'] {
        background: #ffebe6;
        color: #bf2600;
      }
      .prio-badge[data-prio='Medium'] {
        background: #fffae6;
        color: #974f0c;
      }
      .prio-badge[data-prio='Low'],
      .prio-badge[data-prio='Lowest'] {
        background: #f4f5f7;
        color: #5e6c84;
      }
      .res-resolved {
        color: #006644;
        font-weight: 600;
        font-size: 12px;
      }
      .res-unresolved {
        color: #6b778c;
        font-size: 12px;
      }
      .user-cell {
        display: flex;
        align-items: center;
        gap: 6px;
        white-space: nowrap;
      }
      .avatar-xs {
        width: 20px;
        height: 20px;
        border-radius: 50%;
        color: #fff;
        font-size: 10px;
        font-weight: 700;
        display: inline-flex;
        align-items: center;
        justify-content: center;
      }
      .comp-ver-cell,
      .cf-summary-cell {
        display: flex;
        flex-wrap: wrap;
        gap: 4px;
      }
      .mini-tag {
        font-size: 10px;
        padding: 1px 6px;
        border-radius: 4px;
        background: #f4f5f7;
        color: #42526e;
        white-space: nowrap;
      }
      .mini-tag.cf {
        background: #eae6ff;
        color: #403294;
      }
      .pts-circle {
        background: #ebecf0;
        color: #172b4d;
        padding: 2px 7px;
        border-radius: 10px;
        font-size: 11px;
        font-weight: 700;
      }
      .split-navigator {
        display: grid;
        grid-template-columns: 360px 1fr;
        gap: 14px;
        min-height: 540px;
      }
      .split-list-pane {
        overflow-y: auto;
        max-height: 700px;
        padding: 8px;
        display: flex;
        flex-direction: column;
        gap: 6px;
      }
      .split-card-item {
        padding: 10px;
        border: 1px solid #dfe1e6;
        border-radius: 6px;
        cursor: pointer;
      }
      .split-card-item.active {
        border-color: #0052cc;
        background: #deebff;
      }
      .sc-top {
        display: flex;
        align-items: center;
        gap: 6px;
        margin-bottom: 4px;
      }
      .sc-key {
        font-weight: 700;
        color: #0052cc;
        font-size: 12px;
      }
      .sc-title {
        font-weight: 600;
        font-size: 13px;
        color: #172b4d;
        margin-bottom: 6px;
      }
      .sc-meta {
        display: flex;
        align-items: center;
        justify-content: space-between;
        font-size: 11px;
        color: #5e6c84;
      }
      .split-detail-pane {
        padding: 20px;
      }
      .detail-pane-header {
        display: flex;
        justify-content: space-between;
        align-items: flex-start;
        border-bottom: 1px solid #ebecf0;
        padding-bottom: 12px;
        margin-bottom: 14px;
      }
      .dp-crumbs {
        display: flex;
        gap: 6px;
        margin-bottom: 6px;
      }
      .detail-pane-header h2 {
        margin: 0;
        font-size: 18px;
      }
      .dp-meta-grid {
        display: grid;
        grid-template-columns: repeat(3, 1fr);
        gap: 10px;
        background: #fafbfc;
        padding: 12px;
        border-radius: 6px;
        font-size: 13px;
        margin-bottom: 14px;
      }
      .dp-section {
        margin-top: 14px;
      }
      .dp-section h4 {
        margin: 0 0 6px;
        font-size: 12px;
        text-transform: uppercase;
        color: #5e6c84;
      }
      .dp-cf-grid {
        display: grid;
        grid-template-columns: repeat(2, 1fr);
        gap: 8px;
        font-size: 12px;
      }
      .dp-comment {
        padding: 8px 10px;
        background: #f4f5f7;
        border-radius: 6px;
        margin-bottom: 6px;
        font-size: 12px;
      }
      .empty-results {
        padding: 32px;
        text-align: center;
        color: #5e6c84;
      }
      .modal-backdrop {
        position: fixed;
        inset: 0;
        background: rgba(9, 30, 66, 0.54);
        display: flex;
        align-items: center;
        justify-content: center;
        z-index: 1500;
      }
      .modal-dialog {
        background: #ffffff;
        border-radius: 8px;
        width: 540px;
        max-width: 94vw;
        box-shadow: 0 12px 32px rgba(9, 30, 66, 0.25);
        overflow: hidden;
      }
      .modal-header {
        display: flex;
        align-items: center;
        justify-content: space-between;
        padding: 14px 18px;
        border-bottom: 1px solid #ebecf0;
      }
      .modal-header h3 {
        margin: 0;
        font-size: 16px;
      }
      .modal-body {
        padding: 16px 18px;
        display: flex;
        flex-direction: column;
        gap: 12px;
      }
      .modal-footer {
        padding: 12px 18px;
        border-top: 1px solid #ebecf0;
        display: flex;
        justify-content: flex-end;
        gap: 8px;
      }
      .form-group {
        display: flex;
        flex-direction: column;
        gap: 4px;
      }
      .form-group label {
        font-size: 12px;
        font-weight: 600;
        color: #42526e;
      }
      .form-group input,
      .form-group select,
      .form-group textarea {
        padding: 8px 10px;
        border: 1px solid #dfe1e6;
        border-radius: 4px;
        font-size: 13px;
      }
      .mono-input {
        font-family: monospace;
      }
      .form-row {
        display: grid;
        grid-template-columns: 1fr 1fr;
        gap: 10px;
      }
      .checkbox-row {
        display: flex;
        align-items: center;
        gap: 8px;
        font-size: 13px;
        cursor: pointer;
      }
      .existing-subs {
        display: flex;
        flex-direction: column;
        gap: 6px;
      }
      .sub-item {
        display: flex;
        align-items: center;
        justify-content: space-between;
        padding: 8px 10px;
        background: #f4f5f7;
        border-radius: 6px;
        font-size: 12px;
      }
    `,
  ],
})
export class SearchComponent implements OnInit {
  readonly api = inject(ApiService);
  private readonly route = inject(ActivatedRoute);
  private readonly router = inject(Router);

  readonly sidebarOpen = signal<boolean>(true);
  readonly filterDrawerTab = signal<'all' | 'favorites' | 'mine'>('all');
  readonly searchMode = signal<'basic' | 'jql'>('jql');
  readonly viewLayout = signal<'list' | 'split'>('list');

  readonly jqlQuery = signal<string>('');
  jqlInputText = '';

  readonly searchResult = signal<JqlSearchResponse | null>(null);
  readonly validationState = signal<JqlValidateResponse | null>(null);
  readonly suggestions = signal<JqlSuggestions | null>(null);

  readonly activeFilter = signal<SavedFilter | null>(null);
  readonly selectedItem = signal<WorkItem | null>(null);
  readonly selectedItemDetail = signal<WorkItemDetail | null>(null);

  readonly showSqlInspector = signal<boolean>(false);
  readonly showAutocomplete = signal<boolean>(false);

  readonly toastMessage = signal<string>('');
  readonly toastIsError = signal<boolean>(false);

  // Basic filter state
  basicFilters = {
    project: '',
    type: '',
    category: '',
    status: '',
    priority: '',
    assignee: '',
    sprint: '',
    resolution: '',
    component: '',
    fixVersion: '',
    text: '',
  };

  // Save/Edit Filter Modal state
  readonly showFilterModal = signal<boolean>(false);
  readonly editingFilterId = signal<number | null>(null);
  filterForm = {
    name: '',
    description: '',
    jql_query: '',
    share_type: 'public',
    share_target: '*',
    is_favorite: true,
  };

  // Subscriptions Modal state
  readonly subscriptionTargetFilter = signal<SavedFilter | null>(null);
  subForm = {
    preset: 'daily',
    cron_schedule: '0 8 * * 1-5',
    frequency_label: 'Daily (Weekdays 08:00)',
    recipient_scope: 'self',
    email_empty: false,
  };

  private validateTimer: any = null;

  readonly items = computed<WorkItem[]>(() => this.searchResult()?.items || []);
  readonly totalMatches = computed<number>(() => this.searchResult()?.total || 0);
  readonly activeTokens = computed<JqlToken[]>(
    () => this.validationState()?.tokens || this.searchResult()?.tokens || []
  );

  readonly filteredSavedFilters = computed<SavedFilter[]>(() => {
    const all = this.api.savedFilters();
    const tab = this.filterDrawerTab();
    const uid = this.api.currentUser()?.id;
    if (tab === 'favorites') {
      return all.filter((f) => f.is_favorite);
    }
    if (tab === 'mine') {
      return all.filter((f) => !f.is_system && f.owner_id === uid);
    }
    return all;
  });

  async ngOnInit(): Promise<void> {
    await this.api.loadReferenceData();
    try {
      const sug = await this.api.getJqlSuggestions();
      this.suggestions.set(sug);
    } catch {
      // ignore
    }

    const qParam = this.route.snapshot.queryParamMap.get('jql');
    const filterIdParam = this.route.snapshot.queryParamMap.get('filter');
    if (filterIdParam) {
      const target = this.api.savedFilters().find((f) => f.id === Number(filterIdParam));
      if (target) {
        await this.selectSavedFilter(target);
        return;
      }
    }
    if (qParam !== null) {
      this.jqlInputText = qParam;
      this.jqlQuery.set(qParam);
    }
    await this.executeJqlSearch();
  }

  public showToast(msg: string, isError = false): void {
    this.toastMessage.set(msg);
    this.toastIsError.set(isError);
    setTimeout(() => {
      if (this.toastMessage() === msg) {
        this.toastMessage.set('');
      }
    }, 4000);
  }

  switchMode(mode: 'basic' | 'jql'): void {
    this.searchMode.set(mode);
  }

  async setSplitLayout(): Promise<void> {
    this.viewLayout.set('split');
    if (!this.selectedItem() && this.items().length > 0) {
      await this.selectItemForSplit(this.items()[0]);
    }
  }

  async selectItemForSplit(item: WorkItem): Promise<void> {
    this.selectedItem.set(item);
    if (this.viewLayout() === 'split') {
      try {
        const det = await this.api.getItemDetail(item.key);
        this.selectedItemDetail.set(det);
      } catch {
        // ignore
      }
    }
  }

  onJqlInputChange(val: string): void {
    if (this.validateTimer) {
      clearTimeout(this.validateTimer);
    }
    this.validateTimer = setTimeout(async () => {
      try {
        const res = await this.api.validateJql(val);
        this.validationState.set(res);
      } catch {
        // ignore
      }
    }, 140);
  }

  async executeJqlSearch(): Promise<void> {
    const q = this.jqlInputText.trim();
    this.jqlQuery.set(q);
    try {
      const res = await this.api.searchJql(q);
      this.searchResult.set(res);
      this.validationState.set(res);
      if (this.viewLayout() === 'split' && res.items.length > 0) {
        await this.selectItemForSplit(res.items[0]);
      }
    } catch (err: any) {
      try {
        const valRes = await this.api.validateJql(q);
        this.validationState.set(valRes);
      } catch {
        // ignore
      }
      this.showToast(err?.message || 'JQL query execution failed', true);
    }
  }

  clearJqlQuery(): void {
    this.activeFilter.set(null);
    this.jqlInputText = '';
    this.jqlQuery.set('');
    this.executeJqlSearch();
  }

  appendJqlSnippet(snippet: string): void {
    const cur = this.jqlInputText.trim();
    if (!cur) {
      this.jqlInputText = snippet;
    } else {
      const orderIdx = cur.toUpperCase().indexOf('ORDER BY');
      if (orderIdx >= 0) {
        const before = cur.slice(0, orderIdx).trim();
        const orderPart = cur.slice(orderIdx).trim();
        this.jqlInputText = before ? `${before} AND ${snippet} ${orderPart}` : `${snippet} ${orderPart}`;
      } else {
        this.jqlInputText = `${cur} AND ${snippet}`;
      }
    }
    this.executeJqlSearch();
  }

  appendJqlOrderBy(orderClause: string): void {
    const cur = this.jqlInputText.trim();
    const orderIdx = cur.toUpperCase().indexOf('ORDER BY');
    if (orderIdx >= 0) {
      this.jqlInputText = `${cur.slice(0, orderIdx).trim()} ${orderClause}`.trim();
    } else {
      this.jqlInputText = cur ? `${cur} ${orderClause}` : orderClause;
    }
    this.executeJqlSearch();
  }

  insertTokenAtEnd(token: string): void {
    const cur = this.jqlInputText.trim();
    this.jqlInputText = cur ? `${cur} ${token}` : token;
    this.onJqlInputChange(this.jqlInputText);
  }

  quickOrderBy(field: string, dir: 'ASC' | 'DESC'): void {
    this.appendJqlOrderBy(`ORDER BY ${field} ${dir}`);
  }

  applyBasicFilters(): void {
    const clauses: string[] = [];
    if (this.basicFilters.project) {
      clauses.push(`project = "${this.basicFilters.project}"`);
    }
    if (this.basicFilters.type) {
      clauses.push(`type = "${this.basicFilters.type}"`);
    }
    if (this.basicFilters.category) {
      clauses.push(`category = "${this.basicFilters.category}"`);
    }
    if (this.basicFilters.status) {
      clauses.push(`status = "${this.basicFilters.status}"`);
    }
    if (this.basicFilters.priority) {
      clauses.push(`priority = "${this.basicFilters.priority}"`);
    }
    if (this.basicFilters.assignee) {
      if (this.basicFilters.assignee === 'currentUser()') {
        clauses.push(`assignee = currentUser()`);
      } else if (this.basicFilters.assignee === 'EMPTY') {
        clauses.push(`assignee IS EMPTY`);
      } else {
        clauses.push(`assignee = "${this.basicFilters.assignee}"`);
      }
    }
    if (this.basicFilters.sprint) {
      if (this.basicFilters.sprint === 'openSprints()') {
        clauses.push(`sprint IN openSprints()`);
      } else if (this.basicFilters.sprint === 'EMPTY') {
        clauses.push(`sprint IS EMPTY`);
      } else {
        clauses.push(`sprint = "${this.basicFilters.sprint}"`);
      }
    }
    if (this.basicFilters.resolution) {
      if (this.basicFilters.resolution === 'EMPTY') {
        clauses.push(`resolution IS EMPTY`);
      } else if (this.basicFilters.resolution === 'NOT_EMPTY') {
        clauses.push(`resolution IS NOT EMPTY`);
      } else {
        clauses.push(`resolution = "${this.basicFilters.resolution}"`);
      }
    }
    if (this.basicFilters.component) {
      clauses.push(`component = "${this.basicFilters.component}"`);
    }
    if (this.basicFilters.fixVersion) {
      clauses.push(`fixVersion = "${this.basicFilters.fixVersion}"`);
    }
    if (this.basicFilters.text.trim()) {
      const escaped = this.basicFilters.text.trim().replace(/"/g, '\\"');
      clauses.push(`text ~ "${escaped}"`);
    }

    this.jqlInputText = clauses.join(' AND ');
    this.executeJqlSearch();
  }

  resetBasicFilters(): void {
    this.basicFilters = {
      project: '',
      type: '',
      category: '',
      status: '',
      priority: '',
      assignee: '',
      sprint: '',
      resolution: '',
      component: '',
      fixVersion: '',
      text: '',
    };
    this.applyBasicFilters();
  }

  async selectSavedFilter(sf: SavedFilter): Promise<void> {
    this.activeFilter.set(sf);
    this.jqlInputText = sf.jql_query;
    this.jqlQuery.set(sf.jql_query);
    await this.executeJqlSearch();
  }

  async toggleStar(sf: SavedFilter, event: Event): Promise<void> {
    event.stopPropagation();
    try {
      const updated = await this.api.toggleSavedFilterStar(sf.id);
      if (this.activeFilter()?.id === sf.id) {
        this.activeFilter.set(updated);
      }
    } catch (err: any) {
      this.showToast(err?.message || 'Failed to star filter', true);
    }
  }

  openSaveFilterModal(): void {
    this.editingFilterId.set(null);
    this.filterForm = {
      name: '',
      description: '',
      jql_query: this.jqlInputText.trim() || 'ORDER BY priority DESC',
      share_type: 'public',
      share_target: '*',
      is_favorite: true,
    };
    this.showFilterModal.set(true);
  }

  openEditFilterModal(sf: SavedFilter): void {
    this.editingFilterId.set(sf.id);
    this.filterForm = {
      name: sf.name,
      description: sf.description,
      jql_query: sf.jql_query,
      share_type: sf.share_type,
      share_target: sf.share_target || '*',
      is_favorite: sf.is_favorite,
    };
    this.showFilterModal.set(true);
  }

  async saveFilterSubmit(): Promise<void> {
    if (!this.filterForm.name.trim()) {
      this.showToast('Filter name is required', true);
      return;
    }
    try {
      const editId = this.editingFilterId();
      if (editId) {
        const updated = await this.api.updateSavedFilter(editId, this.filterForm);
        this.activeFilter.set(updated);
        this.showToast(`Updated filter "${updated.name}"`);
      } else {
        const created = await this.api.createSavedFilter(this.filterForm);
        this.activeFilter.set(created);
        this.showToast(`Saved filter "${created.name}"`);
      }
      this.showFilterModal.set(false);
    } catch (err: any) {
      this.showToast(err?.message || 'Failed to save filter', true);
    }
  }

  async deleteFilter(sf: SavedFilter): Promise<void> {
    try {
      await this.api.deleteSavedFilter(sf.id);
      if (this.activeFilter()?.id === sf.id) {
        this.activeFilter.set(null);
      }
      this.showToast(`Deleted filter "${sf.name}"`);
    } catch (err: any) {
      this.showToast(err?.message || 'Failed to delete filter', true);
    }
  }

  openSubscriptionsModal(sf: SavedFilter): void {
    this.subscriptionTargetFilter.set(sf);
  }

  onSubPresetChange(): void {
    if (this.subForm.preset === 'daily') {
      this.subForm.cron_schedule = '0 8 * * 1-5';
      this.subForm.frequency_label = 'Daily (Weekdays 08:00)';
    } else if (this.subForm.preset === 'weekly') {
      this.subForm.cron_schedule = '0 9 * * 1';
      this.subForm.frequency_label = 'Weekly (Mondays 09:00)';
    } else if (this.subForm.preset === 'hourly') {
      this.subForm.cron_schedule = '0 * * * *';
      this.subForm.frequency_label = 'Hourly Digest';
    }
  }

  async createSubscriptionSubmit(): Promise<void> {
    const sf = this.subscriptionTargetFilter();
    if (!sf) return;
    try {
      const updated = await this.api.createFilterSubscription(sf.id, {
        cron_schedule: this.subForm.cron_schedule,
        frequency_label: this.subForm.frequency_label,
        recipient_scope: this.subForm.recipient_scope,
        email_empty: this.subForm.email_empty,
      });
      this.subscriptionTargetFilter.set(updated);
      this.showToast('Added filter subscription schedule');
    } catch (err: any) {
      this.showToast(err?.message || 'Failed to create subscription', true);
    }
  }

  async removeSubscription(subId: number): Promise<void> {
    const sf = this.subscriptionTargetFilter();
    if (!sf) return;
    try {
      await this.api.deleteFilterSubscription(subId);
      const refreshed = this.api.savedFilters().find((f) => f.id === sf.id) || null;
      this.subscriptionTargetFilter.set(refreshed);
      this.showToast('Removed subscription');
    } catch (err: any) {
      this.showToast(err?.message || 'Failed to remove subscription', true);
    }
  }

  exportResults(format: 'csv' | 'markdown' | 'html'): void {
    const url = this.api.getJqlExportUrl(this.jqlQuery(), format);
    window.open(url, '_blank');
  }

  exportJson(): void {
    const payload = JSON.stringify(this.searchResult() || { items: this.items() }, null, 2);
    const blob = new Blob([payload], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 'jira-jql-export.json';
    a.click();
    URL.revokeObjectURL(url);
  }
}
