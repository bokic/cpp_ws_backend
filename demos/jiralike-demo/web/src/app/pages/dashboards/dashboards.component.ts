import { Component, OnInit, computed, inject, signal } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { RouterLink } from '@angular/router';
import { ApiService } from '../../services/api.service';
import {
  Dashboard,
  DashboardGadget,
  DashboardLayout,
  DashboardShareType,
  GadgetCatalogItem,
  SavedFilter,
} from '../../models/jira.models';

@Component({
  selector: 'app-dashboards',
  standalone: true,
  imports: [CommonModule, FormsModule, RouterLink],
  template: `
    <div class="dashboards-view">
      <!-- Top Control Bar -->
      <header class="dashboards-topbar">
        <div class="topbar-left">
          <div class="dash-selector-wrap">
            <span class="dash-icon">&#128202;</span>
            <select
              class="dash-select"
              [ngModel]="activeDashboard()?.id"
              (ngModelChange)="onSelectDashboard($event)"
            >
              @for (d of dashboards(); track d.id) {
                <option [value]="d.id">
                  {{ d.is_favorite ? '★ ' : '' }}{{ d.name }} ({{ d.layout }})
                </option>
              }
            </select>

            @if (activeDashboard(); as cur) {
              <button
                type="button"
                class="btn-star"
                [class.starred]="cur.is_favorite"
                (click)="toggleFavorite(cur)"
                title="Toggle favorite dashboard"
              >
                {{ cur.is_favorite ? '★' : '☆' }}
              </button>
            }
          </div>

          @if (activeDashboard(); as cur) {
            <div class="dash-meta-badge">
              <span class="share-badge" [class.badge-public]="cur.share_type === 'public'">
                {{ cur.share_type | uppercase }}
              </span>
              <span class="gadget-count-badge">
                {{ cur.gadgets?.length || 0 }} Gadgets
              </span>
            </div>
          }
        </div>

        <div class="topbar-actions">
          <!-- Layout Switcher Button Group -->
          <div class="layout-picker" title="Change layout columns">
            <button
              type="button"
              class="layout-btn"
              [class.active]="activeDashboard()?.layout === '1_column'"
              (click)="changeLayout('1_column')"
              title="1 Column (100%)"
            >
              <span class="layout-icon">&#9646;</span> 1 Col
            </button>
            <button
              type="button"
              class="layout-btn"
              [class.active]="activeDashboard()?.layout === '2_column_equal'"
              (click)="changeLayout('2_column_equal')"
              title="2 Columns Equal (50% / 50%)"
            >
              <span class="layout-icon">&#9646;&#9646;</span> 2 Col (50/50)
            </button>
            <button
              type="button"
              class="layout-btn"
              [class.active]="activeDashboard()?.layout === '2_column_split'"
              (click)="changeLayout('2_column_split')"
              title="2 Columns Split (67% / 33%)"
            >
              <span class="layout-icon">&#9646;&#9647;</span> 2 Col Split
            </button>
            <button
              type="button"
              class="layout-btn"
              [class.active]="activeDashboard()?.layout === '3_column'"
              (click)="changeLayout('3_column')"
              title="3 Columns (33% / 33% / 33%)"
            >
              <span class="layout-icon">&#9646;&#9646;&#9646;</span> 3 Col
            </button>
          </div>

          <button
            type="button"
            class="btn btn-primary btn-sm"
            (click)="openCatalogModal()"
          >
            <span class="btn-icon">+</span> Add Gadget
          </button>

          <button
            type="button"
            class="btn btn-secondary btn-sm"
            (click)="refreshAllGadgets()"
            [disabled]="loading()"
            title="Refresh all gadgets on this dashboard"
          >
            <span class="btn-icon">&#x21bb;</span> Refresh All
          </button>

          <div class="dash-dropdown-menu">
            <button
              type="button"
              class="btn btn-outline btn-sm"
              (click)="showDashActions = !showDashActions"
            >
              Tools &#9662;
            </button>
            @if (showDashActions) {
              <div class="dropdown-popover" (click)="showDashActions = false">
                <button type="button" class="dropdown-item" (click)="openCreateDashboard()">
                  &#10010; Create New Dashboard
                </button>
                <button type="button" class="dropdown-item" (click)="openEditDashboard()">
                  &#9998; Edit Dashboard Details
                </button>
                <button type="button" class="dropdown-item" (click)="copyCurrentDashboard()">
                  &#128203; Duplicate Dashboard
                </button>
                <hr class="dropdown-divider" />
                <button
                  type="button"
                  class="dropdown-item text-danger"
                  (click)="deleteCurrentDashboard()"
                  [disabled]="dashboards().length <= 1"
                >
                  &#128465; Delete Dashboard
                </button>
              </div>
            }
          </div>
        </div>
      </header>

      <!-- Dashboard Description Banner -->
      @if (activeDashboard(); as cur) {
        @if (cur.description) {
          <div class="dash-desc-bar">
            <span>{{ cur.description }}</span>
            <span class="dash-owner">Owner: <strong>{{ cur.owner_name || 'Admin' }}</strong></span>
          </div>
        }
      }

      <!-- Main Dashboard Multi-Column Grid -->
      @if (activeDashboard(); as cur) {
        <div class="dash-grid" [ngClass]="'layout-' + cur.layout">
          @for (colIdx of columnsList(); track colIdx) {
            <div class="dash-column" [attr.data-col]="colIdx">
              <div class="column-header">
                <span class="col-title">Column {{ colIdx + 1 }}</span>
                <button
                  type="button"
                  class="btn-col-add"
                  (click)="openCatalogModal(colIdx)"
                  title="Add gadget to this column"
                >
                  + Add Gadget
                </button>
              </div>

              <div class="column-gadgets-stack">
                @for (gadget of getGadgetsForColumn(colIdx); track gadget.id) {
                  <article class="gadget-card">
                    <!-- Gadget Header -->
                    <header class="gadget-header">
                      <div class="gadget-title-group">
                        <span class="gadget-icon-badge" [ngSwitch]="gadget.gadget_type">
                          <span *ngSwitchCase="'filter_results'">&#128203;</span>
                          <span *ngSwitchCase="'pie_chart'">&#9684;</span>
                          <span *ngSwitchCase="'created_vs_resolved'">&#128200;</span>
                          <span *ngSwitchCase="'sprint_burndown'">&#9889;</span>
                          <span *ngSwitchCase="'two_dimensional_stats'">&#9638;</span>
                          <span *ngSwitchCase="'assigned_to_me'">&#128100;</span>
                          <span *ngSwitchCase="'activity_stream'">&#9201;</span>
                          <span *ngSwitchCase="'roadmap_summary'">&#128506;</span>
                          <span *ngSwitchDefault>&#128202;</span>
                        </span>
                        <h3 class="gadget-title" title="{{ gadget.title }}">{{ gadget.title }}</h3>
                      </div>

                      <div class="gadget-header-actions">
                        <!-- Column Move Left/Right -->
                        @if (colIdx > 0) {
                          <button
                            type="button"
                            class="gadget-btn"
                            (click)="moveGadget(gadget, 'left')"
                            title="Move to left column"
                          >
                            &larr;
                          </button>
                        }
                        @if (colIdx < columnsList().length - 1) {
                          <button
                            type="button"
                            class="gadget-btn"
                            (click)="moveGadget(gadget, 'right')"
                            title="Move to right column"
                          >
                            &rarr;
                          </button>
                        }
                        <!-- Row Move Up/Down -->
                        <button
                          type="button"
                          class="gadget-btn"
                          (click)="moveGadget(gadget, 'up')"
                          title="Move up"
                        >
                          &uarr;
                        </button>
                        <button
                          type="button"
                          class="gadget-btn"
                          (click)="moveGadget(gadget, 'down')"
                          title="Move down"
                        >
                          &darr;
                        </button>

                        <!-- Config Gear -->
                        <button
                          type="button"
                          class="gadget-btn"
                          (click)="openConfigGadget(gadget)"
                          title="Configure gadget preferences"
                        >
                          &#9881;
                        </button>

                        <!-- Refresh Single Gadget -->
                        <button
                          type="button"
                          class="gadget-btn"
                          (click)="refreshGadget(gadget)"
                          [disabled]="gadgetLoadingMap()[gadget.id]"
                          title="Refresh gadget data"
                        >
                          &#x21bb;
                        </button>

                        <!-- Delete Gadget -->
                        <button
                          type="button"
                          class="gadget-btn btn-trash"
                          (click)="deleteGadget(gadget)"
                          title="Remove gadget from dashboard"
                        >
                          &times;
                        </button>
                      </div>
                    </header>

                    <!-- Gadget Body -->
                    <div class="gadget-body">
                      @if (gadgetLoadingMap()[gadget.id]) {
                        <div class="gadget-loading-state">
                          <span class="spinner">&#x21bb;</span> Loading gadget data...
                        </div>
                      } @else if (gadgetDataMap()[gadget.id]; as data) {
                        <!-- 1. Filter Results Gadget -->
                        @if (gadget.gadget_type === 'filter_results') {
                          <div class="gadget-filter-results">
                            <div class="filter-results-meta">
                              <span class="filter-name"><strong>Filter:</strong> {{ data.filter_name || 'All Active' }}</span>
                              <span class="filter-count">Total: {{ data.total_count }} issues</span>
                            </div>
                            <div class="table-responsive">
                              <table class="gadget-table">
                                <thead>
                                  <tr>
                                    <th>Key</th>
                                    <th>Summary</th>
                                    <th>Status</th>
                                    <th>Priority</th>
                                    <th>Assignee</th>
                                    <th>Pts</th>
                                  </tr>
                                </thead>
                                <tbody>
                                  @for (item of data.items; track item.id) {
                                    <tr>
                                      <td class="cell-key">
                                        <a [routerLink]="['/items', item.key]" class="issue-link">{{ item.key }}</a>
                                      </td>
                                      <td class="cell-title" title="{{ item.title }}">{{ item.title }}</td>
                                      <td>
                                        <span class="status-pill status-{{ item.status | lowercase }}">{{ item.status }}</span>
                                      </td>
                                      <td>
                                        <span class="priority-pill">{{ item.priority }}</span>
                                      </td>
                                      <td>
                                        @if (item.assignee) {
                                          <div class="assignee-cell">
                                            <span class="avatar-xs" [style.background]="item.assignee.avatar_color">
                                              {{ item.assignee.display_name.slice(0, 1).toUpperCase() }}
                                            </span>
                                            <span>{{ item.assignee.display_name }}</span>
                                          </div>
                                        } @else {
                                          <span class="text-muted">Unassigned</span>
                                        }
                                      </td>
                                      <td>
                                        <span class="pts-badge">{{ item.story_points || '-' }}</span>
                                      </td>
                                    </tr>
                                  } @empty {
                                    <tr>
                                      <td colspan="6" class="text-center text-muted py-3">No issues match this filter.</td>
                                    </tr>
                                  }
                                </tbody>
                              </table>
                            </div>
                          </div>
                        }

                        <!-- 2. Pie Chart Gadget -->
                        @else if (gadget.gadget_type === 'pie_chart') {
                          <div class="gadget-pie-chart">
                            <div class="pie-container">
                              <svg viewBox="0 0 220 220" class="pie-svg">
                                <g transform="translate(110, 110)">
                                  @for (slice of getPieSlices(data.slices, data.total); track slice.label) {
                                    <path
                                      [attr.d]="slice.pathD"
                                      [attr.fill]="slice.color"
                                      class="pie-slice"
                                      [title]="slice.label + ': ' + slice.count + ' (' + slice.percentage + '%)'"
                                    />
                                  }
                                  <!-- Center cutout for donut style -->
                                  <circle cx="0" cy="0" r="48" fill="#ffffff" />
                                  <text x="0" y="-4" text-anchor="middle" class="donut-total">{{ data.total }}</text>
                                  <text x="0" y="14" text-anchor="middle" class="donut-label">Total</text>
                                </g>
                              </svg>

                              <div class="pie-legend">
                                @for (slice of data.slices; track slice.label) {
                                  <div class="legend-row">
                                    <span class="legend-dot" [style.background]="slice.color"></span>
                                    <span class="legend-label" title="{{ slice.label }}">{{ slice.label }}</span>
                                    <span class="legend-count">{{ slice.count }}</span>
                                    <span class="legend-pct">({{ slice.percentage }}%)</span>
                                  </div>
                                }
                              </div>
                            </div>
                          </div>
                        }

                        <!-- 3. Created vs Resolved Gadget -->
                        @else if (gadget.gadget_type === 'created_vs_resolved') {
                          <div class="gadget-created-resolved">
                            <div class="chart-summary-bar">
                              <span class="stat-pill">Created: <strong>{{ data.total_created || 0 }}</strong></span>
                              <span class="stat-pill">Resolved: <strong>{{ data.total_resolved || 0 }}</strong></span>
                              <span class="stat-pill" [class.text-danger]="data.net_backlog_delta > 0" [class.text-success]="data.net_backlog_delta <= 0">
                                Net Delta: <strong>{{ data.net_backlog_delta > 0 ? '+' : '' }}{{ data.net_backlog_delta || 0 }}</strong>
                              </span>
                            </div>

                            <svg viewBox="0 0 450 180" class="trend-svg">
                              <!-- Gridlines -->
                              <line x1="40" y1="20" x2="430" y2="20" stroke="#EBECF0" stroke-width="1" />
                              <line x1="40" y1="70" x2="430" y2="70" stroke="#EBECF0" stroke-width="1" />
                              <line x1="40" y1="120" x2="430" y2="120" stroke="#EBECF0" stroke-width="1" />
                              <line x1="40" y1="150" x2="430" y2="150" stroke="#DFE1E6" stroke-width="1.5" />

                              <!-- Created Line (Red/Orange) -->
                              <path [attr.d]="buildTrendLine(data.buckets, 'created_count', 450, 180)" fill="none" stroke="#FF5630" stroke-width="2.5" />
                              <!-- Resolved Line (Green) -->
                              <path [attr.d]="buildTrendLine(data.buckets, 'resolved_count', 450, 180)" fill="none" stroke="#00875A" stroke-width="2.5" />
                            </svg>

                            <div class="chart-legend-inline">
                              <span class="legend-inline-item"><span class="line-dot color-created"></span> Created Trend</span>
                              <span class="legend-inline-item"><span class="line-dot color-resolved"></span> Resolved Trend</span>
                            </div>
                          </div>
                        }

                        <!-- 4. Sprint Burndown Gadget -->
                        @else if (gadget.gadget_type === 'sprint_burndown') {
                          <div class="gadget-burndown">
                            <div class="burndown-meta">
                              <span class="sprint-name"><strong>{{ data.sprint_name || 'Current Sprint' }}</strong></span>
                              <span class="days-left">{{ data.days_remaining ?? data.timeline?.length ?? 0 }} days</span>
                              <span class="pts-rem">{{ data.remaining_points ?? data.points_remaining ?? 0 }} pts remaining</span>
                            </div>

                            <svg viewBox="0 0 450 180" class="burndown-svg">
                              <line x1="40" y1="20" x2="430" y2="20" stroke="#EBECF0" stroke-width="1" />
                              <line x1="40" y1="80" x2="430" y2="80" stroke="#EBECF0" stroke-width="1" />
                              <line x1="40" y1="150" x2="430" y2="150" stroke="#DFE1E6" stroke-width="1.5" />

                              <!-- Ideal Guideline (Dashed grey) -->
                              <path [attr.d]="buildBurndownIdeal(data.timeline, 450, 180)" fill="none" stroke="#6B778C" stroke-dasharray="4,4" stroke-width="2" />
                              <!-- Actual Remaining (Blue) -->
                              <path [attr.d]="buildBurndownActual(data.timeline, 450, 180)" fill="none" stroke="#0052CC" stroke-width="2.5" />
                            </svg>

                            <div class="chart-legend-inline">
                              <span class="legend-inline-item"><span class="line-dot color-ideal"></span> Ideal Burndown</span>
                              <span class="legend-inline-item"><span class="line-dot color-actual"></span> Remaining Work</span>
                            </div>
                          </div>
                        }

                        <!-- 5. Two-Dimensional Filter Statistics Gadget -->
                        @else if (gadget.gadget_type === 'two_dimensional_stats') {
                          <div class="gadget-two-dim">
                            <div class="two-dim-axes-header">
                              <span><strong>Y-Axis:</strong> {{ data.y_axis | uppercase }} &times; <strong>X-Axis:</strong> {{ data.x_axis | uppercase }}</span>
                              <span class="grand-total">Total: {{ data.grand_total }}</span>
                            </div>
                            <div class="table-responsive">
                              <table class="gadget-table two-dim-table">
                                <thead>
                                  <tr>
                                    <th class="corner-header">{{ data.y_axis | titlecase }}</th>
                                    @for (xl of data.x_labels; track xl) {
                                      <th class="text-center">{{ xl }}</th>
                                    }
                                    <th class="text-center col-total-header">Total</th>
                                  </tr>
                                </thead>
                                <tbody>
                                  @for (row of data.rows; track row.label) {
                                    <tr>
                                      <td class="row-label">{{ row.label }}</td>
                                      @for (cell of row.values; track $index) {
                                        <td class="text-center cell-num" [class.cell-nonzero]="cell > 0">
                                          {{ cell }}
                                        </td>
                                      }
                                      <td class="text-center row-total-cell">{{ row.total }}</td>
                                    </tr>
                                  }
                                </tbody>
                                <tfoot>
                                  <tr class="footer-total-row">
                                    <td><strong>Total</strong></td>
                                    @for (ct of data.col_totals; track $index) {
                                      <td class="text-center"><strong>{{ ct }}</strong></td>
                                    }
                                    <td class="text-center grand-total-cell"><strong>{{ data.grand_total }}</strong></td>
                                  </tr>
                                </tfoot>
                              </table>
                            </div>
                          </div>
                        }

                        <!-- 6. Assigned to Me Gadget -->
                        @else if (gadget.gadget_type === 'assigned_to_me') {
                          <div class="gadget-assigned-to-me">
                            <div class="assigned-header-meta">
                              <span>Open issues assigned to you ({{ data.count || 0 }})</span>
                            </div>
                            <div class="assigned-list">
                              @for (item of data.items; track item.id) {
                                <div class="assigned-item-card">
                                  <div class="assigned-item-left">
                                    <span class="priority-indicator priority-{{ item.priority | lowercase }}"></span>
                                    <a [routerLink]="['/items', item.key]" class="issue-link">{{ item.key }}</a>
                                    <span class="item-summary" title="{{ item.title }}">{{ item.title }}</span>
                                  </div>
                                  <div class="assigned-item-right">
                                    <span class="status-pill status-{{ item.status | lowercase }}">{{ item.status }}</span>
                                    @if (item.story_points) {
                                      <span class="pts-badge">{{ item.story_points }} pt</span>
                                    }
                                  </div>
                                </div>
                              } @empty {
                                <div class="empty-state-notice">
                                  &#10003; You have no unresolved issues assigned to you!
                                </div>
                              }
                            </div>
                          </div>
                        }

                        <!-- 7. Activity Stream Gadget -->
                        @else if (gadget.gadget_type === 'activity_stream') {
                          <div class="gadget-activity-stream">
                            <div class="activity-timeline">
                              @for (act of data.activities; track act.timestamp + act.event_type + act.item_key) {
                                <div class="timeline-row">
                                  <div class="avatar-col">
                                    <span class="avatar-xs" [style.background]="act.avatar_color">
                                      {{ act.user_name.slice(0, 1).toUpperCase() }}
                                    </span>
                                  </div>
                                  <div class="timeline-content">
                                    <div class="timeline-line-1">
                                      <strong class="user-name">{{ act.user_name }}</strong>
                                      <span class="action-desc">
                                        @if (act.event_type === 'transition') { transitioned }
                                        @else if (act.event_type === 'comment') { commented on }
                                        @else if (act.event_type === 'worklog') { logged work on }
                                        @else { updated }
                                      </span>
                                      <a [routerLink]="['/items', act.item_key]" class="issue-link">{{ act.item_key }}</a>
                                    </div>
                                    <div class="timeline-details">{{ act.details }}</div>
                                    <div class="timeline-time">{{ formatTimeAgo(act.timestamp) }}</div>
                                  </div>
                                </div>
                              } @empty {
                                <div class="empty-state-notice">No recent activity recorded.</div>
                              }
                            </div>
                          </div>
                        }

                        <!-- 8. Roadmap Summary Gadget -->
                        @else if (gadget.gadget_type === 'roadmap_summary') {
                          <div class="gadget-roadmap-summary">
                            <h4 class="section-subheading">Active Versions &amp; Releases</h4>
                            <div class="roadmap-versions-list">
                              @for (v of data.versions; track v.id) {
                                <div class="version-progress-card">
                                  <div class="vcard-header">
                                    <span class="vcard-name">{{ v.name }}</span>
                                    <span class="vcard-status status-{{ v.status | lowercase }}">{{ v.status }}</span>
                                    <span class="vcard-pct">{{ v.progress_percentage }}%</span>
                                  </div>
                                  <div class="progress-bar-track">
                                    <div class="progress-bar-fill" [style.width.%]="v.progress_percentage"></div>
                                  </div>
                                  <div class="vcard-sub">
                                    <span>{{ v.done_issues }}/{{ v.total_issues }} issues completed</span>
                                    <span>{{ v.release_date || 'No release date' }}</span>
                                  </div>
                                </div>
                              } @empty {
                                <div class="empty-state-notice">No versions found for project.</div>
                              }
                            </div>

                            <h4 class="section-subheading mt-3">Epics Progress</h4>
                            <div class="roadmap-epics-list">
                              @for (ep of data.epics; track ep.id) {
                                <div class="epic-progress-row">
                                  <div class="epic-info">
                                    <a [routerLink]="['/items', ep.key]" class="issue-link">{{ ep.key }}</a>
                                    <span class="epic-title" title="{{ ep.title }}">{{ ep.title }}</span>
                                  </div>
                                  <div class="epic-bar-wrap">
                                    <div class="progress-bar-track">
                                      <div class="progress-bar-fill" [style.width.%]="ep.progress_percentage"></div>
                                    </div>
                                    <span class="epic-pct">{{ ep.progress_percentage }}% ({{ ep.done_points }}/{{ ep.total_points }} pts)</span>
                                  </div>
                                </div>
                              }
                            </div>
                          </div>
                        }
                      } @else {
                        <div class="gadget-empty-state">
                          <span>No data available. Click &#x21bb; to load.</span>
                        </div>
                      }
                    </div>
                  </article>
                } @empty {
                  <div class="empty-column-placeholder">
                    <p>No gadgets in Column {{ colIdx + 1 }}</p>
                    <button
                      type="button"
                      class="btn btn-outline btn-sm"
                      (click)="openCatalogModal(colIdx)"
                    >
                      + Add Gadget Here
                    </button>
                  </div>
                }
              </div>
            </div>
          }
        </div>
      }

      <!-- Gadget Directory Catalog Modal -->
      @if (showCatalogModal()) {
        <div class="modal-backdrop" (click)="showCatalogModal.set(false)">
          <div class="modal-card modal-lg" (click)="$event.stopPropagation()">
            <header class="modal-header">
              <h2>Gadget Directory Catalog</h2>
              <button type="button" class="btn-close" (click)="showCatalogModal.set(false)">&times;</button>
            </header>

            <div class="modal-body">
              <p class="catalog-intro">
                Choose from Jira standard gadgets to add real-time charts, issue tables, and metrics to Column {{ targetColumnIndex + 1 }}.
              </p>

              <div class="catalog-grid">
                @for (item of gadgetCatalog(); track item.gadget_type) {
                  <div class="catalog-card" [class.selected]="selectedCatalogGadget()?.gadget_type === item.gadget_type">
                    <div class="catalog-card-icon">
                      <span class="cat-icon-badge">&#128202;</span>
                    </div>
                    <div class="catalog-card-content">
                      <span class="catalog-category-tag">{{ item.category }}</span>
                      <h4 class="catalog-card-title">{{ item.title }}</h4>
                      <p class="catalog-card-desc">{{ item.description }}</p>
                    </div>
                    <div class="catalog-card-action">
                      <button
                        type="button"
                        class="btn btn-primary btn-sm"
                        (click)="addGadgetFromCatalog(item)"
                      >
                        + Add to Column {{ targetColumnIndex + 1 }}
                      </button>
                    </div>
                  </div>
                }
              </div>
            </div>

            <footer class="modal-footer">
              <button type="button" class="btn btn-secondary" (click)="showCatalogModal.set(false)">
                Close
              </button>
            </footer>
          </div>
        </div>
      }

      <!-- Create / Edit Dashboard Modal -->
      @if (showCreateDashModal() || showEditDashModal()) {
        <div class="modal-backdrop" (click)="closeDashModal()">
          <div class="modal-card" (click)="$event.stopPropagation()">
            <header class="modal-header">
              <h2>{{ showCreateDashModal() ? 'Create New Dashboard' : 'Edit Dashboard Details' }}</h2>
              <button type="button" class="btn-close" (click)="closeDashModal()">&times;</button>
            </header>

            <div class="modal-body">
              <div class="form-group mb-3">
                <label class="form-label">Dashboard Name *</label>
                <input
                  type="text"
                  class="form-control"
                  [(ngModel)]="dashFormName"
                  placeholder="e.g. Sprint Executive Overview"
                />
              </div>

              <div class="form-group mb-3">
                <label class="form-label">Description</label>
                <textarea
                  class="form-control"
                  rows="2"
                  [(ngModel)]="dashFormDesc"
                  placeholder="Brief description of the dashboard's purpose"
                ></textarea>
              </div>

              <div class="form-group mb-3">
                <label class="form-label">Grid Layout</label>
                <select class="form-control" [(ngModel)]="dashFormLayout">
                  <option value="1_column">1 Column (100% full width)</option>
                  <option value="2_column_equal">2 Columns Equal (50% / 50%)</option>
                  <option value="2_column_split">2 Columns Split (67% / 33%)</option>
                  <option value="3_column">3 Columns (33% / 33% / 33%)</option>
                </select>
              </div>

              <div class="form-group mb-3">
                <label class="form-label">Sharing &amp; Visibility</label>
                <select class="form-control" [(ngModel)]="dashFormShareType">
                  <option value="private">Private (Only me)</option>
                  <option value="project">Project Team</option>
                  <option value="public">Public (All workspace members)</option>
                </select>
              </div>
            </div>

            <footer class="modal-footer">
              <button type="button" class="btn btn-secondary" (click)="closeDashModal()">Cancel</button>
              <button
                type="button"
                class="btn btn-primary"
                [disabled]="!dashFormName.trim()"
                (click)="saveDashboardForm()"
              >
                {{ showCreateDashModal() ? 'Create Dashboard' : 'Save Changes' }}
              </button>
            </footer>
          </div>
        </div>
      }

      <!-- Gadget Configuration Modal -->
      @if (showConfigGadgetModal(); as g) {
        <div class="modal-backdrop" (click)="showConfigGadgetModal.set(null)">
          <div class="modal-card" (click)="$event.stopPropagation()">
            <header class="modal-header">
              <h2>Configure Gadget: {{ g.title }}</h2>
              <button type="button" class="btn-close" (click)="showConfigGadgetModal.set(null)">&times;</button>
            </header>

            <div class="modal-body">
              <div class="form-group mb-3">
                <label class="form-label">Gadget Title</label>
                <input type="text" class="form-control" [(ngModel)]="configTitle" />
              </div>

              <!-- Type-Specific Controls -->
              @if (g.gadget_type === 'filter_results') {
                <div class="form-group mb-3">
                  <label class="form-label">Saved Filter</label>
                  <select class="form-control" [(ngModel)]="configFilterId">
                    <option [value]="0">Ad-hoc query / Default</option>
                    @for (f of savedFilters(); track f.id) {
                      <option [value]="f.id">{{ f.name }}</option>
                    }
                  </select>
                </div>
                <div class="form-group mb-3">
                  <label class="form-label">Item Limit (Pagination)</label>
                  <select class="form-control" [(ngModel)]="configLimit">
                    <option [value]="5">5 items</option>
                    <option [value]="10">10 items</option>
                    <option [value]="20">20 items</option>
                  </select>
                </div>
              }

              @if (g.gadget_type === 'pie_chart') {
                <div class="form-group mb-3">
                  <label class="form-label">Group By Dimension</label>
                  <select class="form-control" [(ngModel)]="configStatType">
                    <option value="status">Status</option>
                    <option value="priority">Priority</option>
                    <option value="assignee">Assignee</option>
                    <option value="issue_type">Issue Type</option>
                    <option value="component">Component</option>
                  </select>
                </div>
              }

              @if (g.gadget_type === 'created_vs_resolved') {
                <div class="form-group mb-3">
                  <label class="form-label">Time Window</label>
                  <select class="form-control" [(ngModel)]="configDays">
                    <option [value]="14">Last 14 Days</option>
                    <option [value]="30">Last 30 Days</option>
                    <option [value]="60">Last 60 Days</option>
                    <option [value]="90">Last 90 Days</option>
                  </select>
                </div>
              }

              @if (g.gadget_type === 'two_dimensional_stats') {
                <div class="form-group mb-3">
                  <label class="form-label">X-Axis Dimension</label>
                  <select class="form-control" [(ngModel)]="configXAxis">
                    <option value="status">Status</option>
                    <option value="priority">Priority</option>
                    <option value="assignee">Assignee</option>
                    <option value="issue_type">Issue Type</option>
                  </select>
                </div>
                <div class="form-group mb-3">
                  <label class="form-label">Y-Axis Dimension</label>
                  <select class="form-control" [(ngModel)]="configYAxis">
                    <option value="assignee">Assignee</option>
                    <option value="status">Status</option>
                    <option value="priority">Priority</option>
                    <option value="component">Component</option>
                  </select>
                </div>
              }

              @if (g.gadget_type === 'assigned_to_me') {
                <div class="form-group mb-3">
                  <label class="form-label">Display Limit</label>
                  <select class="form-control" [(ngModel)]="configLimit">
                    <option [value]="5">5 items</option>
                    <option [value]="10">10 items</option>
                    <option [value]="25">25 items</option>
                  </select>
                </div>
              }

              @if (g.gadget_type === 'activity_stream') {
                <div class="form-group mb-3">
                  <label class="form-label">Activity Entries Limit</label>
                  <select class="form-control" [(ngModel)]="configLimit">
                    <option [value]="10">10 entries</option>
                    <option [value]="15">15 entries</option>
                    <option [value]="30">30 entries</option>
                  </select>
                </div>
              }
            </div>

            <footer class="modal-footer">
              <button type="button" class="btn btn-secondary" (click)="showConfigGadgetModal.set(null)">Cancel</button>
              <button type="button" class="btn btn-primary" (click)="saveGadgetConfig()">Save &amp; Refresh</button>
            </footer>
          </div>
        </div>
      }
    </div>
  `,
})
export class DashboardsComponent implements OnInit {
  api = inject(ApiService);

  dashboards = signal<Dashboard[]>([]);
  activeDashboard = signal<Dashboard | null>(null);
  loading = signal<boolean>(false);

  gadgetCatalog = signal<GadgetCatalogItem[]>([]);
  savedFilters = signal<SavedFilter[]>([]);

  showDashActions = false;
  showCatalogModal = signal<boolean>(false);
  showCreateDashModal = signal<boolean>(false);
  showEditDashModal = signal<boolean>(false);
  showConfigGadgetModal = signal<DashboardGadget | null>(null);
  selectedCatalogGadget = signal<GadgetCatalogItem | null>(null);

  targetColumnIndex = 0;

  // Form states for Dashboard create/edit
  dashFormName = '';
  dashFormDesc = '';
  dashFormLayout: DashboardLayout = '2_column_equal';
  dashFormShareType: DashboardShareType = 'public';

  // Form states for Gadget config
  configTitle = '';
  configFilterId = 0;
  configLimit = 10;
  configStatType = 'status';
  configDays = 30;
  configXAxis = 'status';
  configYAxis = 'assignee';

  // Per-gadget data cache and loading flags
  gadgetDataMap = signal<Record<number, any>>({});
  gadgetLoadingMap = signal<Record<number, boolean>>({});

  columnsList = computed(() => {
    const layout = this.activeDashboard()?.layout || '2_column_equal';
    if (layout === '1_column') return [0];
    if (layout === '3_column') return [0, 1, 2];
    return [0, 1]; // 2_column_equal or 2_column_split
  });

  ngOnInit(): void {
    this.loadDashboards();
    this.loadCatalog();
    this.loadSavedFilters();
  }

  async loadDashboards(selectId?: number): Promise<void> {
    this.loading.set(true);
    try {
      const res = await this.api.getDashboards();
      const list = res.dashboards || [];
      this.dashboards.set(list);

      if (list.length > 0) {
        let targetId = selectId;
        if (!targetId && this.activeDashboard()) {
          targetId = this.activeDashboard()!.id;
        }
        if (!targetId || !list.some((d) => d.id === targetId)) {
          // Select favorite or first
          const fav = list.find((d) => d.is_favorite);
          targetId = fav ? fav.id : list[0].id;
        }
        await this.loadDashboardDetail(targetId);
      } else {
        this.activeDashboard.set(null);
      }
    } catch (err) {
      console.error('Failed to load dashboards:', err);
    } finally {
      this.loading.set(false);
    }
  }

  async loadDashboardDetail(id: number): Promise<void> {
    try {
      const dash = await this.api.getDashboard(id);
      this.activeDashboard.set(dash);
      this.refreshAllGadgets();
    } catch (err) {
      console.error('Failed to load dashboard detail:', err);
    }
  }

  async loadCatalog(): Promise<void> {
    try {
      const cat = await this.api.getGadgetCatalog();
      this.gadgetCatalog.set(cat);
    } catch (err) {
      console.error('Failed to load gadget catalog:', err);
    }
  }

  async loadSavedFilters(): Promise<void> {
    try {
      const filters = await this.api.getSavedFilters();
      this.savedFilters.set(filters);
    } catch (err) {
      console.error('Failed to load saved filters:', err);
    }
  }

  onSelectDashboard(idStr: any): void {
    const id = Number(idStr);
    if (id > 0) {
      this.loadDashboardDetail(id);
    }
  }

  getGadgetsForColumn(colIdx: number): DashboardGadget[] {
    const gadgets = this.activeDashboard()?.gadgets || [];
    return gadgets
      .filter((g) => g.column_index === colIdx)
      .sort((a, b) => a.row_index - b.row_index);
  }

  async refreshGadget(gadget: DashboardGadget): Promise<void> {
    this.gadgetLoadingMap.update((m) => ({ ...m, [gadget.id]: true }));
    try {
      const res = await this.api.getDashboardGadgetData(gadget.id);
      this.gadgetDataMap.update((m) => ({ ...m, [gadget.id]: res.data }));
    } catch (err) {
      console.error(`Failed to refresh gadget ${gadget.id}:`, err);
    } finally {
      this.gadgetLoadingMap.update((m) => ({ ...m, [gadget.id]: false }));
    }
  }

  refreshAllGadgets(): void {
    const gadgets = this.activeDashboard()?.gadgets || [];
    for (const g of gadgets) {
      this.refreshGadget(g);
    }
  }

  async toggleFavorite(dash: Dashboard): Promise<void> {
    try {
      const res = await this.api.toggleDashboardStar(dash.id);
      dash.is_favorite = res.is_favorite;
      this.dashboards.update((list) =>
        list.map((d) => (d.id === dash.id ? { ...d, is_favorite: res.is_favorite } : d))
      );
    } catch (err) {
      console.error('Failed to star dashboard:', err);
    }
  }

  async changeLayout(layout: DashboardLayout): Promise<void> {
    const cur = this.activeDashboard();
    if (!cur || cur.layout === layout) return;
    try {
      await this.api.updateDashboard(cur.id, { layout });
      cur.layout = layout;
      this.activeDashboard.set({ ...cur });
    } catch (err) {
      console.error('Failed to change layout:', err);
    }
  }

  async moveGadget(gadget: DashboardGadget, direction: 'left' | 'right' | 'up' | 'down'): Promise<void> {
    const maxCols = this.columnsList().length;
    let newCol = gadget.column_index;
    let newRow = gadget.row_index;

    if (direction === 'left' && newCol > 0) newCol--;
    else if (direction === 'right' && newCol < maxCols - 1) newCol++;
    else if (direction === 'up' && newRow > 0) newRow--;
    else if (direction === 'down') newRow++;

    try {
      await this.api.updateDashboardGadgetPosition(gadget.id, {
        column_index: newCol,
        row_index: newRow,
      });
      gadget.column_index = newCol;
      gadget.row_index = newRow;
      if (this.activeDashboard()) {
        await this.loadDashboardDetail(this.activeDashboard()!.id);
      }
    } catch (err) {
      console.error('Failed to move gadget:', err);
    }
  }

  async deleteGadget(gadget: DashboardGadget): Promise<void> {
    if (!confirm(`Are you sure you want to remove "${gadget.title}" from this dashboard?`)) {
      return;
    }
    try {
      await this.api.deleteDashboardGadget(gadget.id);
      const cur = this.activeDashboard();
      if (cur && cur.gadgets) {
        cur.gadgets = cur.gadgets.filter((g) => g.id !== gadget.id);
        this.activeDashboard.set({ ...cur });
      }
    } catch (err) {
      console.error('Failed to delete gadget:', err);
    }
  }

  openCatalogModal(colIdx = 0): void {
    this.targetColumnIndex = colIdx;
    this.showCatalogModal.set(true);
  }

  async addGadgetFromCatalog(item: GadgetCatalogItem): Promise<void> {
    const cur = this.activeDashboard();
    if (!cur) return;
    try {
      let prefs = {};
      try {
        prefs = JSON.parse(item.default_preferences || '{}');
      } catch (_) {}

      const created = await this.api.addDashboardGadget(cur.id, {
        gadget_type: item.gadget_type,
        title: item.title,
        column_index: this.targetColumnIndex,
        row_index: 99,
        preferences: prefs,
      });

      this.showCatalogModal.set(false);
      await this.loadDashboardDetail(cur.id);
      this.refreshGadget(created);
    } catch (err) {
      console.error('Failed to add gadget:', err);
    }
  }

  openConfigGadget(gadget: DashboardGadget): void {
    this.configTitle = gadget.title;
    const p = gadget.preferences || {};
    this.configFilterId = p.filter_id || 0;
    this.configLimit = p.limit || 10;
    this.configStatType = p.statistic_type || 'status';
    this.configDays = p.days || 30;
    this.configXAxis = p.x_axis || 'status';
    this.configYAxis = p.y_axis || 'assignee';
    this.showConfigGadgetModal.set(gadget);
  }

  async saveGadgetConfig(): Promise<void> {
    const g = this.showConfigGadgetModal();
    if (!g) return;

    const newPrefs: any = { ...(g.preferences || {}) };
    if (g.gadget_type === 'filter_results') {
      newPrefs.filter_id = Number(this.configFilterId);
      newPrefs.limit = Number(this.configLimit);
    } else if (g.gadget_type === 'pie_chart') {
      newPrefs.statistic_type = this.configStatType;
    } else if (g.gadget_type === 'created_vs_resolved') {
      newPrefs.days = Number(this.configDays);
    } else if (g.gadget_type === 'two_dimensional_stats') {
      newPrefs.x_axis = this.configXAxis;
      newPrefs.y_axis = this.configYAxis;
    } else if (g.gadget_type === 'assigned_to_me' || g.gadget_type === 'activity_stream') {
      newPrefs.limit = Number(this.configLimit);
    }

    try {
      await this.api.updateDashboardGadget(g.id, {
        title: this.configTitle.trim() || g.title,
        preferences: newPrefs,
      });
      g.title = this.configTitle.trim() || g.title;
      g.preferences = newPrefs;
      this.showConfigGadgetModal.set(null);
      this.refreshGadget(g);
    } catch (err) {
      console.error('Failed to save gadget config:', err);
    }
  }

  openCreateDashboard(): void {
    this.dashFormName = '';
    this.dashFormDesc = '';
    this.dashFormLayout = '2_column_equal';
    this.dashFormShareType = 'public';
    this.showCreateDashModal.set(true);
  }

  openEditDashboard(): void {
    const cur = this.activeDashboard();
    if (!cur) return;
    this.dashFormName = cur.name;
    this.dashFormDesc = cur.description || '';
    this.dashFormLayout = cur.layout;
    this.dashFormShareType = cur.share_type;
    this.showEditDashModal.set(true);
  }

  closeDashModal(): void {
    this.showCreateDashModal.set(false);
    this.showEditDashModal.set(false);
  }

  async saveDashboardForm(): Promise<void> {
    if (!this.dashFormName.trim()) return;

    if (this.showCreateDashModal()) {
      try {
        const created = await this.api.createDashboard({
          name: this.dashFormName.trim(),
          description: this.dashFormDesc.trim(),
          layout: this.dashFormLayout,
          share_type: this.dashFormShareType,
          project_id: 1,
        });
        this.closeDashModal();
        await this.loadDashboards(created.id);
      } catch (err) {
        console.error('Failed to create dashboard:', err);
      }
    } else if (this.showEditDashModal()) {
      const cur = this.activeDashboard();
      if (!cur) return;
      try {
        await this.api.updateDashboard(cur.id, {
          name: this.dashFormName.trim(),
          description: this.dashFormDesc.trim(),
          layout: this.dashFormLayout,
          share_type: this.dashFormShareType,
        });
        this.closeDashModal();
        await this.loadDashboards(cur.id);
      } catch (err) {
        console.error('Failed to edit dashboard:', err);
      }
    }
  }

  async copyCurrentDashboard(): Promise<void> {
    const cur = this.activeDashboard();
    if (!cur) return;
    try {
      const copy = await this.api.copyDashboard(cur.id);
      await this.loadDashboards(copy.id);
    } catch (err) {
      console.error('Failed to copy dashboard:', err);
    }
  }

  async deleteCurrentDashboard(): Promise<void> {
    const cur = this.activeDashboard();
    if (!cur || !confirm(`Are you sure you want to delete dashboard "${cur.name}"?`)) {
      return;
    }
    try {
      await this.api.deleteDashboard(cur.id);
      await this.loadDashboards();
    } catch (err) {
      console.error('Failed to delete dashboard:', err);
    }
  }

  // SVG Chart Geometry Helpers
  getPieSlices(slices: Array<{ label: string; count: number; percentage: number; color: string }>, total: number): any[] {
    if (!slices || slices.length === 0 || !total) return [];
    let currentAngle = -Math.PI / 2; // Start from top
    const radius = 95;

    return slices.map((s) => {
      const sliceAngle = (s.count / total) * 2 * Math.PI;
      const startAngle = currentAngle;
      const endAngle = currentAngle + sliceAngle;
      currentAngle = endAngle;

      const x1 = radius * Math.cos(startAngle);
      const y1 = radius * Math.sin(startAngle);
      const x2 = radius * Math.cos(endAngle);
      const y2 = radius * Math.sin(endAngle);

      const largeArc = sliceAngle > Math.PI ? 1 : 0;
      const pathD = `M 0 0 L ${x1} ${y1} A ${radius} ${radius} 0 ${largeArc} 1 ${x2} ${y2} Z`;

      return {
        ...s,
        pathD,
      };
    });
  }

  buildTrendLine(buckets: any[], key: 'created_count' | 'resolved_count', width: number, height: number): string {
    if (!buckets || buckets.length === 0) return '';
    const values = buckets.map((b) => b[key] || 0);
    const maxVal = Math.max(...values, 5);
    const paddingLeft = 40;
    const paddingRight = 20;
    const paddingTop = 20;
    const paddingBottom = 30;

    const plotW = width - paddingLeft - paddingRight;
    const plotH = height - paddingTop - paddingBottom;

    return values
      .map((v, i) => {
        const x = paddingLeft + (i / Math.max(values.length - 1, 1)) * plotW;
        const y = height - paddingBottom - (v / maxVal) * plotH;
        return `${i === 0 ? 'M' : 'L'} ${x.toFixed(1)} ${y.toFixed(1)}`;
      })
      .join(' ');
  }

  buildBurndownIdeal(timeline: any[], width: number, height: number): string {
    if (!timeline || timeline.length === 0) return '';
    const paddingLeft = 40;
    const paddingRight = 20;
    const paddingTop = 20;
    const paddingBottom = 30;
    const plotW = width - paddingLeft - paddingRight;
    const plotH = height - paddingTop - paddingBottom;

    const maxPts = Math.max(...timeline.map((t) => t.ideal_points ?? t.ideal_remaining ?? 0), 10);

    return timeline
      .map((t, i) => {
        const val = t.ideal_points ?? t.ideal_remaining ?? 0;
        const x = paddingLeft + (i / Math.max(timeline.length - 1, 1)) * plotW;
        const y = height - paddingBottom - (val / maxPts) * plotH;
        return `${i === 0 ? 'M' : 'L'} ${x.toFixed(1)} ${y.toFixed(1)}`;
      })
      .join(' ');
  }

  buildBurndownActual(timeline: any[], width: number, height: number): string {
    if (!timeline || timeline.length === 0) return '';
    const paddingLeft = 40;
    const paddingRight = 20;
    const paddingTop = 20;
    const paddingBottom = 30;
    const plotW = width - paddingLeft - paddingRight;
    const plotH = height - paddingTop - paddingBottom;

    const maxPts = Math.max(
      ...timeline.map((t) => Math.max(t.ideal_points ?? t.ideal_remaining ?? 0, t.remaining_points ?? t.actual_remaining ?? 0)),
      10
    );

    return timeline
      .map((t, i) => {
        const val = t.remaining_points ?? t.actual_remaining ?? t.ideal_points ?? t.ideal_remaining ?? 0;
        const x = paddingLeft + (i / Math.max(timeline.length - 1, 1)) * plotW;
        const y = height - paddingBottom - (val / maxPts) * plotH;
        return `${i === 0 ? 'M' : 'L'} ${x.toFixed(1)} ${y.toFixed(1)}`;
      })
      .join(' ');
  }

  formatTimeAgo(ts: string): string {
    if (!ts) return '';
    const d = new Date(ts.replace(' ', 'T') + 'Z');
    const now = new Date();
    const sec = Math.floor((now.getTime() - d.getTime()) / 1000);
    if (sec < 60) return 'Just now';
    if (sec < 3600) return `${Math.floor(sec / 60)}m ago`;
    if (sec < 86400) return `${Math.floor(sec / 3600)}h ago`;
    return `${Math.floor(sec / 86400)}d ago`;
  }
}
