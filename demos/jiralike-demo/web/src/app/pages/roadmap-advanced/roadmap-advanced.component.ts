import {
  Component,
  OnInit,
  computed,
  inject,
  signal,
} from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { RouterLink } from '@angular/router';
import { ApiService } from '../../services/api.service';
import {
  BaselineDetailResponse,
  CapacityResponse,
  CriticalPathNode,
  CriticalPathResponse,
  DependencyLink,
  PortfolioRollupsResponse,
  Project,
  RollupItem,
  TimelineBaseline,
} from '../../models/jira.models';

interface TimelineDay {
  iso: string;
  dayNum: number;
  dayName: string;
  isWeekend: boolean;
  isToday: boolean;
}

interface FlattenedHierarchyRow {
  item: RollupItem;
  depth: number;
  hasChildren: boolean;
  expanded: boolean;
  visible: boolean;
  barLeftPx: number;
  barWidthPx: number;
  isCritical: boolean;
  baselineItem?: any;
}

@Component({
  selector: 'app-roadmap-advanced',
  standalone: true,
  imports: [CommonModule, FormsModule, RouterLink],
  template: `
    <div class="roadmap-container">
      <!-- Toast Alert Banner -->
      @if (toastMessage()) {
        <div class="toast-banner" [class.toast-error]="toastIsError()">
          <span>{{ toastMessage() }}</span>
          <button type="button" class="toast-close" (click)="toastMessage.set('')">&times;</button>
        </div>
      }

      <!-- Page Header -->
      <header class="roadmap-header">
        <div class="header-titles">
          <div class="header-breadcrumb">
            <a routerLink="/board">Boards</a> &rsaquo;
            <span>Advanced Roadmaps &amp; Portfolio Gantt</span>
          </div>
          <div class="header-row">
            <h1 class="page-title">🗺️ Advanced Roadmaps &amp; Gantt CPM</h1>
            <div class="header-stats-chips">
              <span class="stat-chip">
                <strong>{{ summary()?.initiatives_count || 0 }}</strong> Initiatives
              </span>
              <span class="stat-chip">
                <strong>{{ summary()?.epics_count || 0 }}</strong> Epics
              </span>
              <span class="stat-chip">
                <strong>{{ summary()?.stories_count || 0 }}</strong> Stories &amp; Tasks
              </span>
              <span class="stat-chip">
                <strong>{{ summary()?.total_story_points || 0 }}</strong> SP Total
              </span>
              <span class="stat-chip progress-chip">
                Progress: <strong>{{ summary()?.overall_progress || 0 }}%</strong>
              </span>
            </div>
          </div>
        </div>

        <div class="header-actions">
          <!-- Project Filter -->
          <select
            class="form-control select-project"
            [ngModel]="selectedProjectId()"
            (ngModelChange)="onProjectChange($event)"
            title="Filter by Project"
          >
            <option [value]="0">All Projects</option>
            @for (p of projects(); track p.id) {
              <option [value]="p.id">{{ p.name }} ({{ p.key }})</option>
            }
          </select>

          <!-- View Switcher Tabs -->
          <div class="btn-group view-switcher">
            <button
              type="button"
              class="btn btn-sm"
              [class.btn-primary]="activeView() === 'timeline'"
              [class.btn-outline]="activeView() !== 'timeline'"
              (click)="activeView.set('timeline')"
            >
              📅 Gantt &amp; Hierarchy
            </button>
            <button
              type="button"
              class="btn btn-sm"
              [class.btn-primary]="activeView() === 'capacity'"
              [class.btn-outline]="activeView() !== 'capacity'"
              (click)="activeView.set('capacity')"
            >
              👥 Team Capacity Heatmap
            </button>
          </div>

          <!-- Refresh Button -->
          <button
            type="button"
            class="btn btn-secondary btn-sm"
            (click)="reloadAll()"
            [disabled]="loading()"
            title="Reload Data"
          >
            🔄 {{ loading() ? 'Loading...' : 'Refresh' }}
          </button>
        </div>
      </header>

      <!-- Subheader Controls Toolbar (When in Timeline View) -->
      @if (activeView() === 'timeline') {
        <div class="roadmap-toolbar">
          <div class="toolbar-left">
            <!-- Critical Path Toggle -->
            <button
              type="button"
              class="btn btn-sm cpm-toggle-btn"
              [class.active]="showCriticalPath()"
              (click)="toggleCriticalPath()"
              title="Highlight zero-float bottleneck tasks on the Critical Path"
            >
              <span class="glow-indicator" [class.on]="showCriticalPath()"></span>
              🔥 Critical Path (CPM)
            </button>

            <!-- Baseline Comparison Dropdown -->
            <div class="baseline-control">
              <label class="toolbar-label" for="baseline-select">Baseline:</label>
              <select
                id="baseline-select"
                class="form-control select-baseline"
                [ngModel]="selectedBaselineId()"
                (ngModelChange)="onBaselineChange($event)"
              >
                <option [value]="0">Live Progress (No Baseline)</option>
                @for (b of baselines(); track b.id) {
                  <option [value]="b.id">📌 {{ b.name }} ({{ b.created_at | date:'shortDate' }})</option>
                }
              </select>

              <button
                type="button"
                class="btn btn-outline btn-sm"
                (click)="openBaselineModal()"
                title="Save current schedule as a new point-in-time baseline snapshot"
              >
                + Snapshot
              </button>

              @if (selectedBaselineId() > 0) {
                <button
                  type="button"
                  class="btn btn-danger-outline btn-sm"
                  (click)="deleteSelectedBaseline()"
                  title="Delete this baseline snapshot"
                >
                  ✕
                </button>
              }
            </div>

            <!-- Add Dependency Link Button -->
            <button
              type="button"
              class="btn btn-secondary btn-sm"
              (click)="openDependencyModal()"
              title="Add directed dependency between work items"
            >
              🔗 Add Dependency
            </button>
          </div>

          <div class="toolbar-right">
            <!-- Hierarchy Expand/Collapse All -->
            <button type="button" class="btn btn-ghost btn-sm" (click)="toggleAllHierarchy(true)">
              Expand All
            </button>
            <button type="button" class="btn btn-ghost btn-sm" (click)="toggleAllHierarchy(false)">
              Collapse All
            </button>

            <!-- Date Window Zoom / Paging -->
            <div class="zoom-controls">
              <button type="button" class="btn btn-sm btn-ghost" (click)="shiftWindowDays(-7)" title="Shift 1 week back">◀</button>
              <button type="button" class="btn btn-sm btn-ghost" (click)="resetWindowToToday()" title="Jump to Today">Today</button>
              <button type="button" class="btn btn-sm btn-ghost" (click)="shiftWindowDays(7)" title="Shift 1 week forward">▶</button>
            </div>
          </div>
        </div>
      }

      <!-- Baseline Variance Summary Banner (when baseline active) -->
      @if (activeView() === 'timeline' && baselineDetail(); as base) {
        <div class="baseline-summary-banner">
          <div class="banner-title">
            <strong>Baseline Comparison: {{ base.name }}</strong>
            <span class="created-info">Created {{ base.created_at | date:'medium' }}</span>
          </div>
          <div class="banner-stats">
            <span class="badge badge-success">✓ {{ base.on_track_items_count }} On Track</span>
            <span class="badge badge-danger">⚠️ {{ base.slipped_items_count }} Slipped (Late)</span>
            <span class="badge badge-info">⚡ {{ base.ahead_items_count }} Ahead of Schedule</span>
            <span class="badge badge-warning">Avg Slip: {{ base.average_slip_days }} days</span>
          </div>
        </div>
      }

      <!-- Critical Path CPM Summary Banner (when CPM active) -->
      @if (activeView() === 'timeline' && showCriticalPath() && criticalPathData(); as cpm) {
        <div class="cpm-summary-banner">
          <div class="cpm-title">
            <span class="pulse-flame">🔥</span>
            <strong>Critical Path Computed (CPM)</strong>: Total Duration:
            <strong>{{ cpm.total_duration_days }} days</strong> ({{ cpm.project_start_date }} &rarr; {{ cpm.project_end_date }})
          </div>
          <div class="cpm-bottlenecks">
            <span>Critical Bottlenecks:</span>
            @for (bn of cpm.bottlenecks; track bn.id) {
              <a [routerLink]="['/', bn.key]" class="cpm-badge" [title]="bn.title">
                {{ bn.key }} (0d float)
              </a>
            }
          </div>
        </div>
      }

      <!-- MAIN VIEW 1: Gantt Timeline & Deep Hierarchy Tree -->
      @if (activeView() === 'timeline') {
        <div class="timeline-viewport">
          <div class="gantt-split-view">
            <!-- Left Pane: Deep Hierarchy Tree Grid -->
            <div class="hierarchy-panel" [style.width.px]="hierarchyWidthPx">
              <div class="hierarchy-header">
                <div class="th-title">Hierarchy &amp; Work Items</div>
                <div class="th-rollup">Rollups (SP / Hours)</div>
                <div class="th-progress">Progress</div>
              </div>

              <div class="hierarchy-rows-container">
                @for (row of flattenedRows(); track row.item.id) {
                  @if (row.visible) {
                    <div
                      class="hierarchy-row"
                      [class.critical-row]="row.isCritical && showCriticalPath()"
                      [style.paddingLeft.px]="12 + row.depth * 20"
                    >
                      <!-- Expand / Collapse chevron -->
                      @if (row.hasChildren) {
                        <button
                          type="button"
                          class="chevron-btn"
                          (click)="toggleRow(row.item.id)"
                          [title]="row.expanded ? 'Collapse children' : 'Expand children'"
                        >
                          {{ row.expanded ? '▼' : '▶' }}
                        </button>
                      } @else {
                        <span class="chevron-spacer"></span>
                      }

                      <!-- Issue Type Icon Badge -->
                      <span
                        class="type-icon-pill"
                        [style.backgroundColor]="getTypeColor(row.item.type)"
                        [title]="row.item.type"
                      >
                        {{ row.item.type.substring(0, 1) }}
                      </span>

                      <!-- Issue Key & Title -->
                      <div class="item-info">
                        <a [routerLink]="['/', row.item.key]" class="item-key" [title]="row.item.title">
                          {{ row.item.key }}
                        </a>
                        <span class="item-title" [title]="row.item.title">
                          {{ row.item.title }}
                        </span>
                      </div>

                      <!-- Progress Rollup Pill -->
                      <div class="rollup-stats" title="Rollup Story Points / Time Spent">
                        @if (row.hasChildren) {
                          <span class="sp-rollup-badge" title="Rolled up Story Points from children">
                            Σ {{ row.item.rollup_story_points }} SP
                          </span>
                        } @else {
                          <span class="sp-badge" title="Direct Story Points">
                            {{ row.item.story_points }} SP
                          </span>
                        }
                      </div>

                      <!-- Progress percentage -->
                      <div class="item-progress-cell">
                        <div class="mini-progress-bar">
                          <div
                            class="mini-progress-fill"
                            [style.width.%]="row.hasChildren ? row.item.rollup_progress : row.item.progress"
                          ></div>
                        </div>
                        <span class="prog-label">
                          {{ row.hasChildren ? row.item.rollup_progress : row.item.progress }}%
                        </span>
                      </div>
                    </div>
                  }
                }
              </div>
            </div>

            <!-- Right Pane: SVG / Canvas Gantt Timeline -->
            <div class="timeline-panel" #timelineContainer (scroll)="onTimelineScroll($event)">
              <!-- Days Header Row -->
              <div class="timeline-header" [style.width.px]="timelineTotalWidthPx()">
                @for (d of timelineDays(); track d.iso) {
                  <div
                    class="day-header-cell"
                    [class.weekend]="d.isWeekend"
                    [class.today]="d.isToday"
                    [style.width.px]="dayColWidth"
                  >
                    <div class="day-num">{{ d.dayNum }}</div>
                    <div class="day-name">{{ d.dayName }}</div>
                  </div>
                }
              </div>

              <!-- Timeline Body with Grid Columns & Work Item Bars -->
              <div class="timeline-body" [style.width.px]="timelineTotalWidthPx()">
                <!-- Weekend vertical column stripes -->
                <div class="stripes-overlay">
                  @for (d of timelineDays(); track d.iso; let idx = $index) {
                    @if (d.isWeekend) {
                      <div
                        class="weekend-stripe"
                        [style.left.px]="idx * dayColWidth"
                        [style.width.px]="dayColWidth"
                      ></div>
                    }
                    @if (d.isToday) {
                      <div
                        class="today-line"
                        [style.left.px]="idx * dayColWidth + (dayColWidth / 2)"
                        title="Today"
                      ></div>
                    }
                  }
                </div>

                <!-- SVG Dependency Lines Layer -->
                <svg class="dependencies-svg" [style.width.px]="timelineTotalWidthPx()">
                  <defs>
                    <marker
                      id="dep-arrow-default"
                      viewBox="0 0 10 10"
                      refX="8"
                      refY="5"
                      markerWidth="6"
                      markerHeight="6"
                      orient="auto-start-reverse"
                    >
                      <path d="M 0 1 L 10 5 L 0 9 z" fill="#0052cc" />
                    </marker>
                    <marker
                      id="dep-arrow-critical"
                      viewBox="0 0 10 10"
                      refX="8"
                      refY="5"
                      markerWidth="7"
                      markerHeight="7"
                      orient="auto-start-reverse"
                    >
                      <path d="M 0 1 L 10 5 L 0 9 z" fill="#de350b" />
                    </marker>
                  </defs>

                  @for (line of computedDependencyLines(); track line.id) {
                    <path
                      [attr.d]="line.svgPath"
                      [attr.stroke]="line.isCritical ? '#de350b' : '#0052cc'"
                      [attr.stroke-width]="line.isCritical ? '3' : '2'"
                      [attr.stroke-dasharray]="line.isCritical ? 'none' : '4 2'"
                      fill="none"
                      [attr.marker-end]="line.isCritical ? 'url(#dep-arrow-critical)' : 'url(#dep-arrow-default)'"
                    />
                  }
                </svg>

                <!-- Gantt Rows & Bars -->
                @for (row of flattenedRows(); track row.item.id; let rowIdx = $index) {
                  @if (row.visible) {
                    <div class="timeline-row" [style.height.px]="rowHeightPx">
                      <!-- Live Progress Bar -->
                      @if (row.barWidthPx > 0) {
                        <div
                          class="gantt-bar"
                          [class.critical-bar]="row.isCritical && showCriticalPath()"
                          [class.is-epic]="row.item.type === 'Epic'"
                          [class.is-initiative]="row.item.type === 'Initiative'"
                          [style.left.px]="row.barLeftPx"
                          [style.width.px]="row.barWidthPx"
                          (mousedown)="startDragBar($event, row.item)"
                          title="Drag to reschedule ({{ row.item.start_date }} -> {{ row.item.due_date }})"
                        >
                          <!-- Bar Progress Fill -->
                          <div
                            class="gantt-bar-fill"
                            [style.width.%]="row.hasChildren ? row.item.rollup_progress : row.item.progress"
                          ></div>

                          <div class="gantt-bar-content">
                            <span class="bar-key">{{ row.item.key }}</span>
                            <span class="bar-title">{{ row.item.title }}</span>
                          </div>

                          <!-- Right Edge Resize Handle to Extend Duration -->
                          <div
                            class="resize-handle right"
                            (mousedown)="startResizeRight($event, row.item)"
                            title="Drag to extend due date"
                          ></div>
                        </div>

                        <!-- Baseline Ghost Bar (when baseline is selected) -->
                        @if (selectedBaselineId() > 0 && row.baselineItem; as bi) {
                          @if (getBaselineBarLeftPx(bi); as bLeft) {
                            @if (getBaselineBarWidthPx(bi); as bWidth) {
                              <div
                                class="baseline-ghost-bar"
                                [style.left.px]="bLeft"
                                [style.width.px]="bWidth"
                                [title]="'Baseline Planned: ' + bi.baseline_start_date + ' to ' + bi.baseline_due_date"
                              >
                                <span class="baseline-label">Baseline</span>
                              </div>
                            }
                          }

                          <!-- Slippage Badge -->
                          @if (bi.due_variance_days !== 0) {
                            <div
                              class="slippage-pill"
                              [class.slipped]="bi.due_variance_days > 0"
                              [class.ahead]="bi.due_variance_days < 0"
                              [style.left.px]="row.barLeftPx + row.barWidthPx + 8"
                            >
                              {{ bi.due_variance_days > 0 ? '+' + bi.due_variance_days + 'd slip' : bi.due_variance_days + 'd early' }}
                            </div>
                          }
                        }
                      }
                    </div>
                  }
                }
              </div>
            </div>
          </div>
        </div>
      }

      <!-- MAIN VIEW 2: Team Capacity & Workload Heatmap -->
      @if (activeView() === 'capacity') {
        <div class="capacity-viewport">
          <div class="capacity-header-card">
            <div class="card-title-group">
              <h2>👥 Team Capacity &amp; Resource Allocation Matrix</h2>
              <p class="card-subtitle">
                Compare allocated task hours against weekly working capacities to balance workloads and eliminate burnout bottlenecks.
              </p>
            </div>
            <div class="capacity-window-badge">
              <span>Sprint / Planning Window: <strong>{{ capacityData()?.weeks_count || 2 }} Weeks</strong></span>
              <span>Team Capacity: <strong>{{ capacityData()?.team_total_capacity_hours || 0 }} hrs</strong></span>
              <span>Allocated: <strong>{{ capacityData()?.team_total_allocated_hours || 0 }} hrs</strong></span>
              <span class="utilization-chip" [class]="getUtilizationClass(capacityData()?.team_utilization_percentage || 0)">
                Team Load: <strong>{{ capacityData()?.team_utilization_percentage || 0 }}%</strong>
              </span>
            </div>
          </div>

          <!-- Heatmap Users Grid -->
          <div class="capacity-grid">
            @for (u of capacityData()?.users || []; track u.user_id) {
              <div class="capacity-card" [class]="u.allocation_status">
                <div class="user-card-header">
                  <div class="user-profile">
                    <div class="user-avatar" [style.backgroundColor]="u.avatar_color || '#0052cc'">
                      {{ u.display_name.substring(0, 1) }}
                    </div>
                    <div class="user-info">
                      <div class="user-name">{{ u.display_name }}</div>
                      <div class="user-username">&#64;{{ u.username }}</div>
                    </div>
                  </div>

                  <div class="user-actions">
                    <span class="status-pill" [class]="u.allocation_status">
                      @if (u.allocation_status === 'over_allocated') { ⚠️ Over-Allocated }
                      @else if (u.allocation_status === 'balanced') { ✓ Balanced }
                      @else { 🟢 Under-Allocated }
                    </span>
                    <button
                      type="button"
                      class="btn btn-outline btn-xs"
                      (click)="openCapacityModal(u)"
                      title="Configure weekly hours capacity"
                    >
                      ⚙️ Edit Capacity
                    </button>
                  </div>
                </div>

                <!-- Hours Breakdown & Progress Bar -->
                <div class="user-metrics-row">
                  <div class="metric-item">
                    <span class="metric-label">Allocated / Capacity:</span>
                    <span class="metric-val"><strong>{{ u.allocated_hours }}h</strong> / {{ u.total_capacity_hours }}h</span>
                  </div>
                  <div class="metric-item">
                    <span class="metric-label">Completed:</span>
                    <span class="metric-val">{{ u.completed_hours }}h</span>
                  </div>
                  <div class="metric-item">
                    <span class="metric-label">Utilization:</span>
                    <span class="metric-val"><strong>{{ u.utilization_percentage }}%</strong></span>
                  </div>
                </div>

                <!-- Utilization Bar -->
                <div class="utilization-bar-container">
                  <div
                    class="utilization-bar-fill"
                    [class]="u.allocation_status"
                    [style.width.%]="mathMin(u.utilization_percentage, 100)"
                  ></div>
                  <!-- 100% threshold marker -->
                  <div class="bar-limit-marker" title="100% Target Capacity Threshold"></div>
                </div>

                <!-- Assigned Items Mini Table -->
                <div class="assigned-items-section">
                  <div class="assigned-header">
                    <span>Assigned Items ({{ u.assigned_items.length }})</span>
                  </div>
                  <div class="assigned-list">
                    @for (item of u.assigned_items; track item.id) {
                      <div class="assigned-item-row">
                        <a [routerLink]="['/', item.key]" class="item-link">{{ item.key }}</a>
                        <span class="item-title-mini" [title]="item.title">{{ item.title }}</span>
                        <span class="item-stat-badge">{{ item.status }}</span>
                        <span class="item-hrs-badge">{{ item.estimate_hours }}h</span>
                      </div>
                    }
                    @if (u.assigned_items.length === 0) {
                      <div class="no-items-text">No active items assigned in this window.</div>
                    }
                  </div>
                </div>
              </div>
            }
          </div>
        </div>
      }

      <!-- MODAL 1: Create Baseline Snapshot -->
      @if (showBaselineModal()) {
        <div class="modal-backdrop" (click)="closeBaselineModal()">
          <div class="modal-dialog" (click)="$event.stopPropagation()">
            <div class="modal-header">
              <h3>📌 Create Timeline Baseline Snapshot</h3>
              <button type="button" class="modal-close" (click)="closeBaselineModal()">&times;</button>
            </div>
            <div class="modal-body">
              <p class="modal-intro">
                Snapshots freeze all current start dates, due dates, estimates, and statuses.
                You can later select this baseline to visually track schedule variance and slippage.
              </p>
              <div class="form-group">
                <label for="base-name">Baseline Name *</label>
                <input
                  id="base-name"
                  type="text"
                  class="form-control"
                  [(ngModel)]="newBaselineName"
                  placeholder="e.g., Initial Q4 Scope / Sprint 1 Baseline"
                />
              </div>
              <div class="form-group">
                <label for="base-desc">Description</label>
                <textarea
                  id="base-desc"
                  class="form-control"
                  rows="3"
                  [(ngModel)]="newBaselineDesc"
                  placeholder="Optional notes or milestone release reference..."
                ></textarea>
              </div>
            </div>
            <div class="modal-footer">
              <button type="button" class="btn btn-ghost" (click)="closeBaselineModal()">Cancel</button>
              <button
                type="button"
                class="btn btn-primary"
                (click)="submitBaseline()"
                [disabled]="!newBaselineName.trim() || submittingBaseline()"
              >
                {{ submittingBaseline() ? 'Saving...' : 'Save Snapshot' }}
              </button>
            </div>
          </div>
        </div>
      }

      <!-- MODAL 2: Create Directed Dependency Link -->
      @if (showDependencyModal()) {
        <div class="modal-backdrop" (click)="closeDependencyModal()">
          <div class="modal-dialog" (click)="$event.stopPropagation()">
            <div class="modal-header">
              <h3>🔗 Add Gantt Dependency Link</h3>
              <button type="button" class="modal-close" (click)="closeDependencyModal()">&times;</button>
            </div>
            <div class="modal-body">
              <p class="modal-intro">
                Establish directed scheduling constraints with automatic topological cycle prevention.
              </p>

              @if (depModalError()) {
                <div class="alert alert-danger">{{ depModalError() }}</div>
              }

              <div class="form-group">
                <label for="dep-predecessor">Predecessor (Source Item) *</label>
                <select id="dep-predecessor" class="form-control" [(ngModel)]="newDepSourceId">
                  <option [value]="0">Select predecessor item...</option>
                  @for (it of allFlatItems(); track it.id) {
                    <option [value]="it.id">{{ it.key }} - {{ it.title }}</option>
                  }
                </select>
              </div>

              <div class="form-group">
                <label for="dep-type">Dependency Relationship Type *</label>
                <select id="dep-type" class="form-control" [(ngModel)]="newDepType">
                  <option value="FS">Finish-to-Start (FS) - Task B starts after Task A finishes</option>
                  <option value="SS">Start-to-Start (SS) - Task B starts after Task A starts</option>
                  <option value="FF">Finish-to-Finish (FF) - Task B finishes after Task A finishes</option>
                  <option value="SF">Start-to-Finish (SF) - Task B finishes after Task A starts</option>
                </select>
              </div>

              <div class="form-group">
                <label for="dep-successor">Successor (Target Item) *</label>
                <select id="dep-successor" class="form-control" [(ngModel)]="newDepTargetId">
                  <option [value]="0">Select successor item...</option>
                  @for (it of allFlatItems(); track it.id) {
                    <option [value]="it.id">{{ it.key }} - {{ it.title }}</option>
                  }
                </select>
              </div>

              <div class="form-group">
                <label for="dep-lag">Lead / Lag Offset (Days)</label>
                <input
                  id="dep-lag"
                  type="number"
                  class="form-control"
                  [(ngModel)]="newDepLagDays"
                  placeholder="0 (Positive for Lag delay, Negative for Lead time)"
                />
                <small class="form-hint">e.g., +2 introduces a 2-day delay buffer; -1 allows 1 day overlap.</small>
              </div>
            </div>
            <div class="modal-footer">
              <button type="button" class="btn btn-ghost" (click)="closeDependencyModal()">Cancel</button>
              <button
                type="button"
                class="btn btn-primary"
                (click)="submitDependency()"
                [disabled]="!newDepSourceId || !newDepTargetId || submittingDep()"
              >
                {{ submittingDep() ? 'Adding Link...' : 'Create Dependency' }}
              </button>
            </div>
          </div>
        </div>
      }

      <!-- MODAL 3: Configure User Weekly Capacity -->
      @if (showCapacityModal()) {
        <div class="modal-backdrop" (click)="closeCapacityModal()">
          <div class="modal-dialog" (click)="$event.stopPropagation()">
            <div class="modal-header">
              <h3>⚙️ Configure Working Capacity: {{ editingCapacityUser?.display_name }}</h3>
              <button type="button" class="modal-close" (click)="closeCapacityModal()">&times;</button>
            </div>
            <div class="modal-body">
              <div class="form-group">
                <label for="cap-hours">Weekly Working Capacity (Hours) *</label>
                <input
                  id="cap-hours"
                  type="number"
                  step="0.5"
                  class="form-control"
                  [(ngModel)]="editingCapacityHours"
                  placeholder="40.0"
                />
                <small class="form-hint">Standard full-time is 40.0 hours/week. Part-time or contracted is typically 20-30 hours.</small>
              </div>
            </div>
            <div class="modal-footer">
              <button type="button" class="btn btn-ghost" (click)="closeCapacityModal()">Cancel</button>
              <button
                type="button"
                class="btn btn-primary"
                (click)="submitCapacityConfig()"
                [disabled]="editingCapacityHours <= 0 || submittingCapacity()"
              >
                {{ submittingCapacity() ? 'Saving...' : 'Save Capacity' }}
              </button>
            </div>
          </div>
        </div>
      }
    </div>
  `,
  styles: [`
    .roadmap-container {
      display: flex;
      flex-direction: column;
      height: calc(100vh - 56px);
      background-color: var(--color-bg, #ffffff);
      color: var(--color-text, #172b4d);
      overflow: hidden;
    }

    .toast-banner {
      display: flex;
      align-items: center;
      justify-content: space-between;
      padding: 10px 16px;
      background-color: #e3fcef;
      color: #006644;
      font-weight: 500;
      border-bottom: 1px solid #abf5d1;
    }
    .toast-error {
      background-color: #ffebe6;
      color: #bf2600;
      border-color: #ffbdad;
    }
    .toast-close {
      background: none;
      border: none;
      font-size: 18px;
      cursor: pointer;
      color: inherit;
    }

    .roadmap-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding: 14px 20px 10px 20px;
      border-bottom: 1px solid var(--color-border, #ebecf0);
      background-color: var(--color-surface, #ffffff);
    }
    .header-breadcrumb {
      font-size: 12px;
      color: #6b778c;
      margin-bottom: 4px;
    }
    .header-row {
      display: flex;
      align-items: center;
      gap: 16px;
    }
    .page-title {
      font-size: 20px;
      font-weight: 700;
      margin: 0;
    }
    .header-stats-chips {
      display: flex;
      align-items: center;
      gap: 8px;
    }
    .stat-chip {
      background-color: #f4f5f7;
      padding: 3px 8px;
      border-radius: 4px;
      font-size: 12px;
      color: #42526e;
    }
    .progress-chip {
      background-color: #deebff;
      color: #0747a6;
    }

    .header-actions {
      display: flex;
      align-items: center;
      gap: 10px;
    }
    .select-project {
      max-width: 200px;
      padding: 5px 8px;
      font-size: 13px;
    }

    .roadmap-toolbar {
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding: 8px 20px;
      background-color: #fafbfc;
      border-bottom: 1px solid var(--color-border, #ebecf0);
    }
    .toolbar-left, .toolbar-right {
      display: flex;
      align-items: center;
      gap: 12px;
    }
    .cpm-toggle-btn {
      display: inline-flex;
      align-items: center;
      gap: 6px;
      font-weight: 600;
      border: 1px solid #dfe1e6;
      background-color: #ffffff;
      color: #42526e;
    }
    .cpm-toggle-btn.active {
      background-color: #ffebe6;
      border-color: #ff8f73;
      color: #de350b;
      box-shadow: 0 0 6px rgba(222, 53, 11, 0.25);
    }
    .glow-indicator {
      width: 8px;
      height: 8px;
      border-radius: 50%;
      background-color: #a5b2c6;
    }
    .glow-indicator.on {
      background-color: #de350b;
      box-shadow: 0 0 8px #de350b;
    }

    .baseline-control {
      display: flex;
      align-items: center;
      gap: 6px;
    }
    .toolbar-label {
      font-size: 12px;
      font-weight: 600;
      color: #5e6c84;
    }
    .select-baseline {
      font-size: 12px;
      padding: 4px 8px;
      max-width: 220px;
    }

    .baseline-summary-banner {
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding: 8px 20px;
      background-color: #e6fcff;
      border-bottom: 1px solid #b3f5ff;
      font-size: 12px;
    }
    .banner-stats {
      display: flex;
      gap: 8px;
    }
    .badge {
      padding: 3px 6px;
      border-radius: 3px;
      font-weight: 600;
    }
    .badge-success { background-color: #e3fcef; color: #006644; }
    .badge-danger { background-color: #ffebe6; color: #bf2600; }
    .badge-info { background-color: #deebff; color: #0747a6; }
    .badge-warning { background-color: #fff0b3; color: #172b4d; }

    .cpm-summary-banner {
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding: 8px 20px;
      background-color: #ffebe6;
      border-bottom: 1px solid #ffbdad;
      color: #bf2600;
      font-size: 12px;
    }
    .cpm-bottlenecks {
      display: flex;
      align-items: center;
      gap: 6px;
    }
    .cpm-badge {
      background-color: #de350b;
      color: #ffffff;
      padding: 2px 6px;
      border-radius: 3px;
      font-size: 11px;
      text-decoration: none;
      font-weight: 600;
    }

    /* Split View Container */
    .timeline-viewport {
      display: flex;
      flex: 1;
      overflow: hidden;
    }
    .gantt-split-view {
      display: flex;
      width: 100%;
      height: 100%;
    }

    /* Left Hierarchy Tree Pane */
    .hierarchy-panel {
      display: flex;
      flex-direction: column;
      border-right: 2px solid var(--color-border, #dfe1e6);
      background-color: #ffffff;
      flex-shrink: 0;
      overflow-y: auto;
    }
    .hierarchy-header {
      display: flex;
      align-items: center;
      height: 44px;
      background-color: #f4f5f7;
      border-bottom: 1px solid #dfe1e6;
      font-weight: 700;
      font-size: 12px;
      color: #5e6c84;
      padding: 0 12px;
    }
    .th-title { flex: 1; }
    .th-rollup { width: 90px; text-align: right; }
    .th-progress { width: 80px; text-align: right; }

    .hierarchy-rows-container {
      display: flex;
      flex-direction: column;
    }
    .hierarchy-row {
      display: flex;
      align-items: center;
      height: 40px;
      border-bottom: 1px solid #f0f1f4;
      font-size: 13px;
      padding-right: 12px;
      box-sizing: border-box;
      gap: 8px;
    }
    .hierarchy-row:hover {
      background-color: #fafbfc;
    }
    .hierarchy-row.critical-row {
      background-color: #fff5f2;
      border-left: 3px solid #de350b;
    }
    .chevron-btn {
      background: none;
      border: none;
      cursor: pointer;
      font-size: 10px;
      color: #6b778c;
      width: 16px;
      padding: 0;
    }
    .chevron-spacer {
      width: 16px;
    }
    .type-icon-pill {
      display: inline-flex;
      align-items: center;
      justify-content: center;
      width: 18px;
      height: 18px;
      border-radius: 3px;
      color: #ffffff;
      font-size: 10px;
      font-weight: 700;
      flex-shrink: 0;
    }
    .item-info {
      display: flex;
      align-items: center;
      gap: 6px;
      flex: 1;
      overflow: hidden;
      white-space: nowrap;
    }
    .item-key {
      font-weight: 600;
      color: #0052cc;
      text-decoration: none;
      flex-shrink: 0;
    }
    .item-title {
      overflow: hidden;
      text-overflow: ellipsis;
      color: #172b4d;
    }
    .sp-rollup-badge {
      background-color: #dfe1e6;
      color: #172b4d;
      font-size: 11px;
      font-weight: 600;
      padding: 2px 6px;
      border-radius: 10px;
    }
    .sp-badge {
      font-size: 11px;
      color: #5e6c84;
    }
    .item-progress-cell {
      display: flex;
      align-items: center;
      gap: 4px;
      width: 80px;
      justify-content: flex-end;
    }
    .mini-progress-bar {
      width: 40px;
      height: 6px;
      background-color: #dfe1e6;
      border-radius: 3px;
      overflow: hidden;
    }
    .mini-progress-fill {
      height: 100%;
      background-color: #36b37e;
    }
    .prog-label {
      font-size: 11px;
      color: #6b778c;
      width: 28px;
      text-align: right;
    }

    /* Right Timeline Pane */
    .timeline-panel {
      flex: 1;
      overflow-x: auto;
      overflow-y: auto;
      position: relative;
      background-color: #ffffff;
    }
    .timeline-header {
      display: flex;
      height: 44px;
      background-color: #f4f5f7;
      border-bottom: 1px solid #dfe1e6;
      position: sticky;
      top: 0;
      z-index: 10;
    }
    .day-header-cell {
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      border-right: 1px solid #ebecf0;
      font-size: 11px;
      flex-shrink: 0;
      color: #5e6c84;
    }
    .day-header-cell.weekend {
      background-color: #ebecf0;
    }
    .day-header-cell.today {
      background-color: #deebff;
      font-weight: 700;
      color: #0747a6;
    }
    .day-num { font-weight: 700; }
    .day-name { font-size: 9px; text-transform: uppercase; }

    .timeline-body {
      position: relative;
    }
    .stripes-overlay {
      position: absolute;
      top: 0;
      bottom: 0;
      left: 0;
      right: 0;
      pointer-events: none;
      z-index: 1;
    }
    .weekend-stripe {
      position: absolute;
      top: 0;
      bottom: 0;
      background-color: rgba(9, 30, 66, 0.03);
      border-right: 1px solid #ebecf0;
    }
    .today-line {
      position: absolute;
      top: 0;
      bottom: 0;
      width: 2px;
      background-color: #0052cc;
      z-index: 5;
    }

    .dependencies-svg {
      position: absolute;
      top: 0;
      left: 0;
      height: 100%;
      pointer-events: none;
      z-index: 4;
    }

    .timeline-row {
      display: flex;
      align-items: center;
      position: relative;
      border-bottom: 1px solid #f4f5f7;
      z-index: 2;
    }

    /* Gantt Bars */
    .gantt-bar {
      position: absolute;
      height: 24px;
      border-radius: 4px;
      background-color: #4c9aff;
      color: #ffffff;
      cursor: grab;
      user-select: none;
      display: flex;
      align-items: center;
      box-shadow: 0 1px 3px rgba(0, 0, 0, 0.12);
      transition: box-shadow 0.2s;
    }
    .gantt-bar:hover {
      box-shadow: 0 2px 6px rgba(0, 0, 0, 0.2);
    }
    .gantt-bar:active {
      cursor: grabbing;
    }
    .gantt-bar.is-epic {
      background-color: #6554c0;
    }
    .gantt-bar.is-initiative {
      background-color: #403294;
    }
    .gantt-bar.critical-bar {
      background-color: #ff5630;
      border: 2px solid #de350b;
      box-shadow: 0 0 8px rgba(222, 53, 11, 0.5);
    }
    .gantt-bar-fill {
      position: absolute;
      top: 0;
      bottom: 0;
      left: 0;
      background-color: rgba(0, 0, 0, 0.18);
      border-radius: 4px 0 0 4px;
      pointer-events: none;
    }
    .gantt-bar-content {
      position: relative;
      display: flex;
      align-items: center;
      gap: 6px;
      padding: 0 8px;
      font-size: 11px;
      font-weight: 600;
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
      z-index: 2;
    }
    .bar-key {
      opacity: 0.9;
    }
    .bar-title {
      font-weight: 500;
    }
    .resize-handle {
      position: absolute;
      top: 0;
      bottom: 0;
      right: 0;
      width: 6px;
      cursor: col-resize;
      background-color: rgba(255, 255, 255, 0.3);
      border-radius: 0 4px 4px 0;
    }
    .resize-handle:hover {
      background-color: #ffffff;
    }

    /* Baseline Ghost Overlay */
    .baseline-ghost-bar {
      position: absolute;
      bottom: 2px;
      height: 6px;
      background-color: #7a869a;
      border-radius: 2px;
      opacity: 0.75;
      pointer-events: none;
    }
    .baseline-label {
      display: none;
    }
    .slippage-pill {
      position: absolute;
      font-size: 10px;
      font-weight: 700;
      padding: 1px 5px;
      border-radius: 3px;
      white-space: nowrap;
    }
    .slippage-pill.slipped {
      background-color: #ffebe6;
      color: #bf2600;
      border: 1px solid #ffbdad;
    }
    .slippage-pill.ahead {
      background-color: #e3fcef;
      color: #006644;
      border: 1px solid #abf5d1;
    }

    /* Capacity View Styles */
    .capacity-viewport {
      flex: 1;
      padding: 20px;
      overflow-y: auto;
      background-color: #fafbfc;
    }
    .capacity-header-card {
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding: 16px 20px;
      background-color: #ffffff;
      border-radius: 6px;
      border: 1px solid #dfe1e6;
      margin-bottom: 20px;
    }
    .card-title-group h2 {
      margin: 0 0 4px 0;
      font-size: 18px;
    }
    .card-subtitle {
      margin: 0;
      font-size: 13px;
      color: #6b778c;
    }
    .capacity-window-badge {
      display: flex;
      align-items: center;
      gap: 12px;
      font-size: 13px;
    }
    .utilization-chip {
      padding: 4px 8px;
      border-radius: 4px;
      font-weight: 600;
    }
    .utilization-chip.under_allocated { background-color: #e3fcef; color: #006644; }
    .utilization-chip.balanced { background-color: #deebff; color: #0747a6; }
    .utilization-chip.over_allocated { background-color: #ffebe6; color: #bf2600; }

    .capacity-grid {
      display: grid;
      grid-template-columns: repeat(auto-fill, minmax(360px, 1fr));
      gap: 16px;
    }
    .capacity-card {
      background-color: #ffffff;
      border: 1px solid #dfe1e6;
      border-radius: 6px;
      padding: 16px;
      display: flex;
      flex-direction: column;
      gap: 12px;
      box-shadow: 0 1px 3px rgba(0, 0, 0, 0.05);
    }
    .capacity-card.over_allocated {
      border-left: 4px solid #de350b;
    }
    .capacity-card.balanced {
      border-left: 4px solid #0052cc;
    }
    .capacity-card.under_allocated {
      border-left: 4px solid #36b37e;
    }

    .user-card-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
    }
    .user-profile {
      display: flex;
      align-items: center;
      gap: 10px;
    }
    .user-avatar {
      width: 36px;
      height: 36px;
      border-radius: 50%;
      color: #ffffff;
      display: flex;
      align-items: center;
      justify-content: center;
      font-weight: 700;
      font-size: 14px;
    }
    .user-name {
      font-weight: 700;
      font-size: 14px;
    }
    .user-username {
      font-size: 12px;
      color: #6b778c;
    }
    .status-pill {
      font-size: 11px;
      font-weight: 700;
      padding: 3px 8px;
      border-radius: 12px;
    }
    .status-pill.under_allocated { background-color: #e3fcef; color: #006644; }
    .status-pill.balanced { background-color: #deebff; color: #0747a6; }
    .status-pill.over_allocated { background-color: #ffebe6; color: #bf2600; }

    .user-metrics-row {
      display: flex;
      justify-content: space-between;
      font-size: 12px;
    }
    .metric-label {
      color: #5e6c84;
      display: block;
    }

    .utilization-bar-container {
      position: relative;
      height: 10px;
      background-color: #ebecf0;
      border-radius: 5px;
      overflow: hidden;
    }
    .utilization-bar-fill {
      height: 100%;
      border-radius: 5px;
      transition: width 0.3s;
    }
    .utilization-bar-fill.under_allocated { background-color: #36b37e; }
    .utilization-bar-fill.balanced { background-color: #0052cc; }
    .utilization-bar-fill.over_allocated { background-color: #de350b; }
    .bar-limit-marker {
      position: absolute;
      top: 0;
      bottom: 0;
      left: 100%;
      width: 2px;
      background-color: #bf2600;
    }

    .assigned-items-section {
      border-top: 1px solid #ebecf0;
      padding-top: 10px;
    }
    .assigned-header {
      font-size: 11px;
      font-weight: 700;
      color: #6b778c;
      text-transform: uppercase;
      margin-bottom: 6px;
    }
    .assigned-list {
      display: flex;
      flex-direction: column;
      gap: 6px;
      max-height: 160px;
      overflow-y: auto;
    }
    .assigned-item-row {
      display: flex;
      align-items: center;
      gap: 6px;
      font-size: 12px;
      padding: 3px 6px;
      background-color: #fafbfc;
      border-radius: 3px;
    }
    .item-link {
      font-weight: 600;
      color: #0052cc;
      text-decoration: none;
    }
    .item-title-mini {
      flex: 1;
      overflow: hidden;
      text-overflow: ellipsis;
      white-space: nowrap;
    }
    .item-stat-badge {
      font-size: 10px;
      color: #5e6c84;
    }
    .item-hrs-badge {
      font-weight: 700;
      font-size: 11px;
      color: #172b4d;
    }
    .no-items-text {
      font-size: 12px;
      color: #8993a4;
      font-style: italic;
    }

    /* Modal Styles */
    .modal-backdrop {
      position: fixed;
      top: 0;
      left: 0;
      right: 0;
      bottom: 0;
      background-color: rgba(9, 30, 66, 0.54);
      display: flex;
      align-items: center;
      justify-content: center;
      z-index: 1000;
    }
    .modal-dialog {
      background-color: #ffffff;
      border-radius: 6px;
      width: 520px;
      max-width: 90vw;
      box-shadow: 0 4px 16px rgba(0, 0, 0, 0.2);
      display: flex;
      flex-direction: column;
    }
    .modal-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding: 16px 20px;
      border-bottom: 1px solid #dfe1e6;
    }
    .modal-header h3 { margin: 0; font-size: 16px; }
    .modal-close {
      background: none;
      border: none;
      font-size: 20px;
      cursor: pointer;
    }
    .modal-body {
      padding: 20px;
      display: flex;
      flex-direction: column;
      gap: 14px;
    }
    .modal-intro {
      margin: 0;
      font-size: 13px;
      color: #6b778c;
    }
    .modal-footer {
      display: flex;
      justify-content: flex-end;
      gap: 10px;
      padding: 12px 20px;
      border-top: 1px solid #dfe1e6;
      background-color: #fafbfc;
    }
    .form-group {
      display: flex;
      flex-direction: column;
      gap: 4px;
    }
    .form-group label {
      font-size: 12px;
      font-weight: 600;
      color: #344563;
    }
    .form-control {
      padding: 6px 10px;
      border: 1px solid #dfe1e6;
      border-radius: 4px;
      font-size: 13px;
    }
    .form-hint {
      font-size: 11px;
      color: #6b778c;
    }
  `],
})
export class RoadmapAdvancedComponent implements OnInit {
  readonly api = inject(ApiService);

  // Active view: 'timeline' or 'capacity'
  readonly activeView = signal<'timeline' | 'capacity'>('timeline');

  readonly loading = signal<boolean>(false);
  readonly toastMessage = signal<string>('');
  readonly toastIsError = signal<boolean>(false);

  // Projects list
  readonly projects = signal<Project[]>([]);
  readonly selectedProjectId = signal<number>(0);

  // Portfolio Rollup Data
  readonly rollupsData = signal<PortfolioRollupsResponse | null>(null);
  readonly summary = computed(() => this.rollupsData()?.summary || null);

  // Dependencies
  readonly dependencies = signal<DependencyLink[]>([]);

  // Critical Path Method Data
  readonly showCriticalPath = signal<boolean>(false);
  readonly criticalPathData = signal<CriticalPathResponse | null>(null);

  // Baselines
  readonly baselines = signal<TimelineBaseline[]>([]);
  readonly selectedBaselineId = signal<number>(0);
  readonly baselineDetail = signal<BaselineDetailResponse | null>(null);

  // Capacity Plan
  readonly capacityData = signal<CapacityResponse | null>(null);

  // Hierarchy expand / collapse state
  readonly collapsedNodeIds = signal<Set<number>>(new Set());

  // Timeline view geometry
  readonly hierarchyWidthPx = 420;
  readonly dayColWidth = 36;
  readonly rowHeightPx = 40;
  readonly totalWindowDays = signal<number>(60);
  readonly windowStartIso = signal<string>(this.computeInitialWindowStart());

  // Drag-to-reschedule tracking
  private isDragging = false;
  private isResizing = false;
  private dragItem: RollupItem | null = null;
  private dragStartX = 0;
  private dragOriginalStart = '';
  private dragOriginalDue = '';

  // Modals state
  readonly showBaselineModal = signal<boolean>(false);
  newBaselineName = '';
  newBaselineDesc = '';
  readonly submittingBaseline = signal<boolean>(false);

  readonly showDependencyModal = signal<boolean>(false);
  newDepSourceId = 0;
  newDepTargetId = 0;
  newDepType = 'FS';
  newDepLagDays = 0;
  readonly depModalError = signal<string>('');
  readonly submittingDep = signal<boolean>(false);

  readonly showCapacityModal = signal<boolean>(false);
  editingCapacityUser: any = null;
  editingCapacityHours = 40.0;
  readonly submittingCapacity = signal<boolean>(false);

  ngOnInit(): void {
    this.loadProjects();
    this.reloadAll();
  }

  computeInitialWindowStart(): string {
    const d = new Date();
    d.setDate(d.getDate() - 10);
    return d.toISOString().substring(0, 10);
  }

  async loadProjects(): Promise<void> {
    try {
      const projs = await this.api.getProjects();
      this.projects.set(projs);
    } catch (e: any) {
      console.error('Failed to load projects', e);
    }
  }

  async reloadAll(): Promise<void> {
    this.loading.set(true);
    const pid = this.selectedProjectId();
    try {
      const [rollups, deps, bases, cap] = await Promise.all([
        this.api.getPortfolioRollups(pid > 0 ? pid : undefined),
        this.api.getPortfolioDependencies(pid > 0 ? pid : undefined),
        this.api.getTimelineBaselines(pid > 0 ? pid : undefined),
        this.api.getTeamCapacity({ project_id: pid > 0 ? pid : undefined }),
      ]);

      this.rollupsData.set(rollups);
      this.dependencies.set(deps);
      this.baselines.set(bases);
      this.capacityData.set(cap);

      // If CPM toggle is on, reload critical path
      if (this.showCriticalPath()) {
        await this.loadCriticalPath();
      }

      // If baseline snapshot is selected, reload baseline detail
      if (this.selectedBaselineId() > 0) {
        await this.loadBaselineDetail(this.selectedBaselineId());
      }
    } catch (e: any) {
      this.showToast(e.message || 'Failed to load portfolio roadmap data', true);
    } finally {
      this.loading.set(false);
    }
  }

  async loadCriticalPath(): Promise<void> {
    const pid = this.selectedProjectId();
    try {
      const cpm = await this.api.getCriticalPath(pid > 0 ? pid : undefined);
      this.criticalPathData.set(cpm);
    } catch (e: any) {
      console.error('Failed to load critical path', e);
    }
  }

  async loadBaselineDetail(baselineId: number): Promise<void> {
    try {
      const detail = await this.api.getTimelineBaselineDetail(baselineId);
      this.baselineDetail.set(detail);
    } catch (e: any) {
      this.baselineDetail.set(null);
    }
  }

  onProjectChange(newPid: number): void {
    this.selectedProjectId.set(Number(newPid));
    this.selectedBaselineId.set(0);
    this.baselineDetail.set(null);
    this.reloadAll();
  }

  toggleCriticalPath(): void {
    const next = !this.showCriticalPath();
    this.showCriticalPath.set(next);
    if (next) {
      this.loadCriticalPath();
    }
  }

  onBaselineChange(newBaseId: number): void {
    const bid = Number(newBaseId);
    this.selectedBaselineId.set(bid);
    if (bid > 0) {
      this.loadBaselineDetail(bid);
    } else {
      this.baselineDetail.set(null);
    }
  }

  // --- Days Array & Timeline Math ---

  readonly timelineDays = computed<TimelineDay[]>(() => {
    const startStr = this.windowStartIso();
    const total = this.totalWindowDays();
    const days: TimelineDay[] = [];
    const baseDate = new Date(startStr + 'T00:00:00');
    const todayStr = new Date().toISOString().substring(0, 10);

    const dayNames = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];

    for (let i = 0; i < total; i++) {
      const cur = new Date(baseDate.getTime() + i * 86400000);
      const iso = cur.toISOString().substring(0, 10);
      const dayOfWeek = cur.getDay();
      days.push({
        iso,
        dayNum: cur.getDate(),
        dayName: dayNames[dayOfWeek],
        isWeekend: dayOfWeek === 0 || dayOfWeek === 6,
        isToday: iso === todayStr,
      });
    }
    return days;
  });

  timelineTotalWidthPx(): number {
    return this.timelineDays().length * this.dayColWidth;
  }

  // All flat items for dropdowns
  readonly allFlatItems = computed<RollupItem[]>(() => {
    return this.rollupsData()?.items || [];
  });

  // Flattened rows with hierarchy indentations & visibility
  readonly flattenedRows = computed<FlattenedHierarchyRow[]>(() => {
    const roots = this.rollupsData()?.tree || [];
    const collapsed = this.collapsedNodeIds();
    const criticalMap = new Set(this.criticalPathData()?.critical_path.map(c => c.id) || []);
    const baselineItemsMap = new Map<number, any>();
    if (this.baselineDetail()) {
      for (const bi of this.baselineDetail()!.items) {
        baselineItemsMap.set(bi.work_item_id, bi);
      }
    }

    const rows: FlattenedHierarchyRow[] = [];
    const windowStart = this.windowStartIso();

    const traverse = (item: RollupItem, depth: number, parentVisible: boolean) => {
      const hasChildren = !!(item.children && item.children.length > 0);
      const isExpanded = !collapsed.has(item.id);

      // Compute Gantt bar geometry
      let barLeftPx = 0;
      let barWidthPx = 0;
      const effectiveStart = item.start_date || item.rollup_start_date;
      const effectiveDue = item.due_date || item.rollup_due_date;

      if (effectiveStart && effectiveDue) {
        const offsetDays = this.getDaysDifference(windowStart, effectiveStart);
        const durationDays = Math.max(1, this.getDaysDifference(effectiveStart, effectiveDue) + 1);
        barLeftPx = offsetDays * this.dayColWidth;
        barWidthPx = durationDays * this.dayColWidth;
      }

      rows.push({
        item,
        depth,
        hasChildren,
        expanded: isExpanded,
        visible: parentVisible,
        barLeftPx,
        barWidthPx,
        isCritical: criticalMap.has(item.id),
        baselineItem: baselineItemsMap.get(item.id),
      });

      if (hasChildren) {
        for (const child of item.children!) {
          traverse(child, depth + 1, parentVisible && isExpanded);
        }
      }
    };

    for (const r of roots) {
      traverse(r, 0, true);
    }
    return rows;
  });

  toggleRow(nodeId: number): void {
    const s = new Set(this.collapsedNodeIds());
    if (s.has(nodeId)) {
      s.delete(nodeId);
    } else {
      s.add(nodeId);
    }
    this.collapsedNodeIds.set(s);
  }

  toggleAllHierarchy(expand: boolean): void {
    if (expand) {
      this.collapsedNodeIds.set(new Set());
    } else {
      const allIds = new Set(this.allFlatItems().map(i => i.id));
      this.collapsedNodeIds.set(allIds);
    }
  }

  shiftWindowDays(days: number): void {
    const cur = new Date(this.windowStartIso() + 'T00:00:00');
    cur.setDate(cur.getDate() + days);
    this.windowStartIso.set(cur.toISOString().substring(0, 10));
  }

  resetWindowToToday(): void {
    this.windowStartIso.set(this.computeInitialWindowStart());
  }

  // --- SVG Dependency Lines Calculation ---

  readonly computedDependencyLines = computed<any[]>(() => {
    const deps = this.dependencies();
    const rows = this.flattenedRows();
    const rowMap = new Map<number, { rowIdx: number; row: FlattenedHierarchyRow }>();

    let visibleIdx = 0;
    for (const r of rows) {
      if (r.visible) {
        rowMap.set(r.item.id, { rowIdx: visibleIdx, row: r });
        visibleIdx++;
      }
    }

    const lines: any[] = [];
    const criticalEdges = new Set(this.criticalPathData()?.critical_path.map(c => c.id) || []);

    for (const d of deps) {
      const src = rowMap.get(d.source_item_id);
      const tgt = rowMap.get(d.target_item_id);
      if (!src || !tgt) continue;

      const srcRow = src.row;
      const tgtRow = tgt.row;
      if (srcRow.barWidthPx <= 0 || tgtRow.barWidthPx <= 0) continue;

      const y1 = src.rowIdx * this.rowHeightPx + (this.rowHeightPx / 2);
      const y2 = tgt.rowIdx * this.rowHeightPx + (this.rowHeightPx / 2);

      let x1 = srcRow.barLeftPx + srcRow.barWidthPx; // End of predecessor
      let x2 = tgtRow.barLeftPx; // Start of successor

      // Handle SS, FF, SF relationships
      if (d.link_type === 'SS') {
        x1 = srcRow.barLeftPx;
        x2 = tgtRow.barLeftPx;
      } else if (d.link_type === 'FF') {
        x1 = srcRow.barLeftPx + srcRow.barWidthPx;
        x2 = tgtRow.barLeftPx + tgtRow.barWidthPx;
      } else if (d.link_type === 'SF') {
        x1 = srcRow.barLeftPx;
        x2 = tgtRow.barLeftPx + tgtRow.barWidthPx;
      }

      // Bezier curve connector
      const dx = Math.max(20, Math.abs(x2 - x1) / 2);
      const svgPath = `M ${x1} ${y1} C ${x1 + dx} ${y1}, ${x2 - dx} ${y2}, ${x2} ${y2}`;

      const isCritical = criticalEdges.has(d.source_item_id) && criticalEdges.has(d.target_item_id);

      lines.push({
        id: d.id,
        svgPath,
        isCritical,
        linkType: d.link_type,
        lag: d.lag_days,
      });
    }

    return lines;
  });

  // --- Drag-to-Reschedule Event Handlers ---

  startDragBar(event: MouseEvent, item: RollupItem): void {
    if ((event.target as HTMLElement).classList.contains('resize-handle')) return;
    event.preventDefault();
    this.isDragging = true;
    this.dragItem = item;
    this.dragStartX = event.clientX;
    this.dragOriginalStart = item.start_date;
    this.dragOriginalDue = item.due_date;

    const onMouseMove = (e: MouseEvent) => {
      if (!this.isDragging || !this.dragItem) return;
      const deltaPx = e.clientX - this.dragStartX;
      const deltaDays = Math.round(deltaPx / this.dayColWidth);
      if (deltaDays !== 0 && this.dragOriginalStart && this.dragOriginalDue) {
        this.dragItem.start_date = this.addDays(this.dragOriginalStart, deltaDays);
        this.dragItem.due_date = this.addDays(this.dragOriginalDue, deltaDays);
      }
    };

    const onMouseUp = async (e: MouseEvent) => {
      window.removeEventListener('mousemove', onMouseMove);
      window.removeEventListener('mouseup', onMouseUp);
      if (this.isDragging && this.dragItem) {
        this.isDragging = false;
        try {
          await this.api.rescheduleItem({
            item_id: this.dragItem.id,
            start_date: this.dragItem.start_date,
            due_date: this.dragItem.due_date,
          });
          this.showToast(`Rescheduled ${this.dragItem.key} to ${this.dragItem.start_date} -> ${this.dragItem.due_date}`);
          this.reloadAll();
        } catch (err: any) {
          this.showToast('Failed to save reschedule update: ' + err.message, true);
        }
      }
    };

    window.addEventListener('mousemove', onMouseMove);
    window.addEventListener('mouseup', onMouseUp);
  }

  startResizeRight(event: MouseEvent, item: RollupItem): void {
    event.stopPropagation();
    event.preventDefault();
    this.isResizing = true;
    this.dragItem = item;
    this.dragStartX = event.clientX;
    this.dragOriginalDue = item.due_date;

    const onMouseMove = (e: MouseEvent) => {
      if (!this.isResizing || !this.dragItem) return;
      const deltaPx = e.clientX - this.dragStartX;
      const deltaDays = Math.round(deltaPx / this.dayColWidth);
      if (deltaDays !== 0 && this.dragOriginalDue) {
        const candidateDue = this.addDays(this.dragOriginalDue, deltaDays);
        if (candidateDue >= this.dragItem.start_date) {
          this.dragItem.due_date = candidateDue;
        }
      }
    };

    const onMouseUp = async (e: MouseEvent) => {
      window.removeEventListener('mousemove', onMouseMove);
      window.removeEventListener('mouseup', onMouseUp);
      if (this.isResizing && this.dragItem) {
        this.isResizing = false;
        try {
          await this.api.rescheduleItem({
            item_id: this.dragItem.id,
            start_date: this.dragItem.start_date,
            due_date: this.dragItem.due_date,
          });
          this.showToast(`Updated ${this.dragItem.key} due date to ${this.dragItem.due_date}`);
          this.reloadAll();
        } catch (err: any) {
          this.showToast('Failed to extend duration: ' + err.message, true);
        }
      }
    };

    window.addEventListener('mousemove', onMouseMove);
    window.addEventListener('mouseup', onMouseUp);
  }

  // --- Baseline Overlay Calculations ---

  getBaselineBarLeftPx(bi: any): number {
    if (!bi.baseline_start_date) return 0;
    const offset = this.getDaysDifference(this.windowStartIso(), bi.baseline_start_date);
    return offset * this.dayColWidth;
  }

  getBaselineBarWidthPx(bi: any): number {
    if (!bi.baseline_start_date || !bi.baseline_due_date) return 0;
    const dur = Math.max(1, this.getDaysDifference(bi.baseline_start_date, bi.baseline_due_date) + 1);
    return dur * this.dayColWidth;
  }

  // --- Modals Logic ---

  openBaselineModal(): void {
    const proj = this.projects().find(p => p.id === this.selectedProjectId());
    this.newBaselineName = `Baseline ${new Date().toLocaleDateString()} (${proj ? proj.key : 'Global'})`;
    this.newBaselineDesc = '';
    this.showBaselineModal.set(true);
  }

  closeBaselineModal(): void {
    this.showBaselineModal.set(false);
  }

  async submitBaseline(): Promise<void> {
    const pid = this.selectedProjectId() || (this.projects().length > 0 ? this.projects()[0].id : 1);
    this.submittingBaseline.set(true);
    try {
      const created = await this.api.createTimelineBaseline({
        project_id: pid,
        name: this.newBaselineName.trim(),
        description: this.newBaselineDesc.trim(),
      });
      this.showToast(`Baseline "${created.name}" created with ${created.total_items} snapshot items`);
      this.closeBaselineModal();
      this.selectedBaselineId.set(created.id);
      await this.reloadAll();
    } catch (e: any) {
      this.showToast('Failed to save baseline: ' + e.message, true);
    } finally {
      this.submittingBaseline.set(false);
    }
  }

  async deleteSelectedBaseline(): Promise<void> {
    const bid = this.selectedBaselineId();
    if (!bid || !confirm('Permanently delete this baseline snapshot?')) return;
    try {
      await this.api.deleteTimelineBaseline(bid);
      this.showToast('Baseline deleted successfully.');
      this.selectedBaselineId.set(0);
      this.baselineDetail.set(null);
      await this.reloadAll();
    } catch (e: any) {
      this.showToast('Failed to delete baseline: ' + e.message, true);
    }
  }

  openDependencyModal(): void {
    this.newDepSourceId = 0;
    this.newDepTargetId = 0;
    this.newDepType = 'FS';
    this.newDepLagDays = 0;
    this.depModalError.set('');
    this.showDependencyModal.set(true);
  }

  closeDependencyModal(): void {
    this.showDependencyModal.set(false);
  }

  async submitDependency(): Promise<void> {
    this.depModalError.set('');
    if (this.newDepSourceId === this.newDepTargetId) {
      this.depModalError.set('An issue cannot depend on itself.');
      return;
    }

    this.submittingDep.set(true);
    try {
      await this.api.createPortfolioDependency({
        source_item_id: Number(this.newDepSourceId),
        target_item_id: Number(this.newDepTargetId),
        link_type: this.newDepType,
        lag_days: Number(this.newDepLagDays || 0),
      });
      this.showToast('Dependency link established successfully.');
      this.closeDependencyModal();
      await this.reloadAll();
    } catch (e: any) {
      this.depModalError.set(e.message || 'Failed to create dependency');
    } finally {
      this.submittingDep.set(false);
    }
  }

  openCapacityModal(u: any): void {
    this.editingCapacityUser = u;
    this.editingCapacityHours = u.weekly_capacity_hours || 40.0;
    this.showCapacityModal.set(true);
  }

  closeCapacityModal(): void {
    this.showCapacityModal.set(false);
    this.editingCapacityUser = null;
  }

  async submitCapacityConfig(): Promise<void> {
    if (!this.editingCapacityUser) return;
    this.submittingCapacity.set(true);
    try {
      await this.api.setUserCapacityConfig({
        user_id: this.editingCapacityUser.user_id,
        project_id: this.selectedProjectId() || undefined,
        weekly_capacity_hours: Number(this.editingCapacityHours),
      });
      this.showToast(`Updated capacity for ${this.editingCapacityUser.display_name} to ${this.editingCapacityHours}h/week`);
      this.closeCapacityModal();
      await this.reloadAll();
    } catch (e: any) {
      this.showToast('Failed to update capacity: ' + e.message, true);
    } finally {
      this.submittingCapacity.set(false);
    }
  }

  // --- Helper Date / Color Utilities ---

  getTypeColor(type: string): string {
    switch (type) {
      case 'Initiative': return '#403294';
      case 'Epic': return '#6554c0';
      case 'Story': return '#36b37e';
      case 'Task': return '#0052cc';
      case 'Subtask': return '#00b8d9';
      case 'Bug': return '#de350b';
      default: return '#5e6c84';
    }
  }

  getUtilizationClass(pct: number): string {
    if (pct > 105) return 'over_allocated';
    if (pct >= 80) return 'balanced';
    return 'under_allocated';
  }

  getDaysDifference(d1: string, d2: string): number {
    const t1 = new Date(d1 + 'T00:00:00').getTime();
    const t2 = new Date(d2 + 'T00:00:00').getTime();
    return Math.round((t2 - t1) / 86400000);
  }

  addDays(dateStr: string, days: number): string {
    const d = new Date(dateStr + 'T00:00:00');
    d.setDate(d.getDate() + days);
    return d.toISOString().substring(0, 10);
  }

  mathMin(a: number, b: number): number {
    return Math.min(a, b);
  }

  showToast(msg: string, isError = false): void {
    this.toastMessage.set(msg);
    this.toastIsError.set(isError);
    setTimeout(() => {
      if (this.toastMessage() === msg) {
        this.toastMessage.set('');
      }
    }, 4500);
  }

  onTimelineScroll(_event: Event): void {
    // Keep header & body scroll synchronised
  }
}
