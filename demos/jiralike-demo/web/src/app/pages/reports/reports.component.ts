import { Component, OnInit, computed, inject, signal } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { RouterLink } from '@angular/router';
import { ApiService } from '../../services/api.service';
import {
  BurndownReport,
  CumulativeFlowReport,
  CycleTimeReport,
  CreatedVsResolvedReport,
  EpicReport,
  EpicSummaryItem,
  ReleaseBurndownReport,
  ReleaseSummaryItem,
  Sprint,
  VelocityReport,
} from '../../models/jira.models';

export type ReportType =
  | 'burndown'
  | 'burnup'
  | 'velocity'
  | 'cfd'
  | 'cycle_time'
  | 'created_resolved'
  | 'epic_release';

@Component({
  selector: 'app-reports',
  standalone: true,
  imports: [CommonModule, FormsModule, RouterLink],
  template: `
    <div class="reports-container">
      <!-- Top Reports Header -->
      <header class="reports-header">
        <div class="header-left">
          <h1 class="page-title">Agile Analytics &amp; Reports</h1>
          <p class="page-subtitle">
            Sprint metrics, team velocity, cumulative flow, cycle time SLAs, and scope tracking
          </p>
        </div>

        <div class="header-actions">
          <button class="btn btn-secondary btn-sm" (click)="exportCurrentReportCsv()" title="Export active report as CSV">
            <span class="btn-icon">&#11015;</span> Export CSV
          </button>
          <button class="btn btn-outline btn-sm" (click)="loadActiveReport()" [disabled]="loading()">
            <span class="btn-icon">&#x21bb;</span> {{ loading() ? 'Refreshing...' : 'Refresh' }}
          </button>
        </div>
      </header>

      <!-- Reports Navigation Tabs -->
      <nav class="report-nav-tabs">
        <button
          type="button"
          class="report-tab"
          [class.active]="activeTab() === 'burndown'"
          (click)="selectTab('burndown')"
        >
          <span class="tab-icon">&#128201;</span>
          <span class="tab-title">Sprint Burndown</span>
        </button>

        <button
          type="button"
          class="report-tab"
          [class.active]="activeTab() === 'burnup'"
          (click)="selectTab('burnup')"
        >
          <span class="tab-icon">&#128200;</span>
          <span class="tab-title">Sprint Burnup</span>
        </button>

        <button
          type="button"
          class="report-tab"
          [class.active]="activeTab() === 'velocity'"
          (click)="selectTab('velocity')"
        >
          <span class="tab-icon">&#9889;</span>
          <span class="tab-title">Velocity Chart</span>
        </button>

        <button
          type="button"
          class="report-tab"
          [class.active]="activeTab() === 'cfd'"
          (click)="selectTab('cfd')"
        >
          <span class="tab-icon">&#127754;</span>
          <span class="tab-title">Cumulative Flow (CFD)</span>
        </button>

        <button
          type="button"
          class="report-tab"
          [class.active]="activeTab() === 'cycle_time'"
          (click)="selectTab('cycle_time')"
        >
          <span class="tab-icon">&#9201;</span>
          <span class="tab-title">Control Chart &amp; Cycle Time</span>
        </button>

        <button
          type="button"
          class="report-tab"
          [class.active]="activeTab() === 'created_resolved'"
          (click)="selectTab('created_resolved')"
        >
          <span class="tab-icon">&#9878;</span>
          <span class="tab-title">Created vs Resolved</span>
        </button>

        <button
          type="button"
          class="report-tab"
          [class.active]="activeTab() === 'epic_release'"
          (click)="selectTab('epic_release')"
        >
          <span class="tab-icon">&#127919;</span>
          <span class="tab-title">Epic &amp; Release Progress</span>
        </button>
      </nav>

      <!-- Global Filter Bar for Current Report -->
      <div class="report-filter-bar">
        @if (activeTab() === 'burndown' || activeTab() === 'burnup') {
          <div class="filter-group">
            <label class="filter-label">Sprint:</label>
            <select
              class="form-select form-select-sm"
              [ngModel]="selectedSprintId()"
              (ngModelChange)="onSprintChanged($event)"
            >
              @for (sp of sprints(); track sp.id) {
                <option [value]="sp.id">
                  {{ sp.name }} ({{ sp.status }}) {{ sp.is_current ? '★ Current' : '' }}
                </option>
              }
            </select>
          </div>

          <div class="filter-group">
            <label class="filter-label">Estimation Unit:</label>
            <div class="segmented-control">
              <button
                type="button"
                class="seg-btn"
                [class.active]="estimationUnit() === 'points'"
                (click)="setEstimationUnit('points')"
              >
                Story Points
              </button>
              <button
                type="button"
                class="seg-btn"
                [class.active]="estimationUnit() === 'hours'"
                (click)="setEstimationUnit('hours')"
              >
                Hours
              </button>
            </div>
          </div>

          @if (activeTab() === 'burndown') {
            <div class="filter-group check-group">
              <label class="checkbox-label">
                <input
                  type="checkbox"
                  [ngModel]="weekendFlattening()"
                  (ngModelChange)="toggleWeekendFlattening($event)"
                />
                <span>Weekend Flattening</span>
              </label>
            </div>
          }
        }

        @if (activeTab() === 'velocity') {
          <div class="filter-group">
            <label class="filter-label">Sprints Limit:</label>
            <select
              class="form-select form-select-sm"
              [ngModel]="velocityLimit()"
              (ngModelChange)="onVelocityLimitChanged($event)"
            >
              <option [value]="5">Last 5 Sprints</option>
              <option [value]="10">Last 10 Sprints</option>
              <option [value]="15">Last 15 Sprints</option>
            </select>
          </div>
        }

        @if (activeTab() === 'cfd' || activeTab() === 'cycle_time' || activeTab() === 'created_resolved') {
          <div class="filter-group">
            <label class="filter-label">Time Window:</label>
            <div class="segmented-control">
              <button
                type="button"
                class="seg-btn"
                [class.active]="timeWindowDays() === 14"
                (click)="setTimeWindowDays(14)"
              >
                14d
              </button>
              <button
                type="button"
                class="seg-btn"
                [class.active]="timeWindowDays() === 30"
                (click)="setTimeWindowDays(30)"
              >
                30d
              </button>
              <button
                type="button"
                class="seg-btn"
                [class.active]="timeWindowDays() === 60"
                (click)="setTimeWindowDays(60)"
              >
                60d
              </button>
              <button
                type="button"
                class="seg-btn"
                [class.active]="timeWindowDays() === 90"
                (click)="setTimeWindowDays(90)"
              >
                90d
              </button>
            </div>
          </div>
        }

        @if (activeTab() === 'epic_release') {
          <div class="filter-group">
            <label class="filter-label">Progress Sub-View:</label>
            <div class="segmented-control">
              <button
                type="button"
                class="seg-btn"
                [class.active]="epicReleaseSubView() === 'epics'"
                (click)="setEpicReleaseSubView('epics')"
              >
                Epics Report
              </button>
              <button
                type="button"
                class="seg-btn"
                [class.active]="epicReleaseSubView() === 'releases'"
                (click)="setEpicReleaseSubView('releases')"
              >
                Fix Versions / Releases
              </button>
            </div>
          </div>

          @if (epicReleaseSubView() === 'epics' && epicsList().length > 0) {
            <div class="filter-group">
              <label class="filter-label">Select Epic:</label>
              <select
                class="form-select form-select-sm"
                [ngModel]="selectedEpicId()"
                (ngModelChange)="onEpicSelected($event)"
              >
                @for (ep of epicsList(); track ep.id) {
                  <option [value]="ep.id">{{ ep.key }} - {{ ep.summary }}</option>
                }
              </select>
            </div>
          }

          @if (epicReleaseSubView() === 'releases' && releasesList().length > 0) {
            <div class="filter-group">
              <label class="filter-label">Select Version:</label>
              <select
                class="form-select form-select-sm"
                [ngModel]="selectedVersionId()"
                (ngModelChange)="onVersionSelected($event)"
              >
                @for (ver of releasesList(); track ver.id) {
                  <option [value]="ver.id">{{ ver.name }} ({{ ver.status }})</option>
                }
              </select>
            </div>
          }
        }
      </div>

      <!-- MAIN REPORT BODY -->
      <div class="report-content-body">
        @if (loading()) {
          <div class="report-loading-state">
            <div class="spinner"></div>
            <span>Calculating agile metrics &amp; generating chart...</span>
          </div>
        } @else {

          <!-- ============================================================= -->
          <!-- 1. SPRINT BURNDOWN REPORT -->
          <!-- ============================================================= -->
          @if (activeTab() === 'burndown' && burndownData(); as bd) {
            <div class="report-card">
              <div class="report-summary-ribbon">
                <div class="kpi-block">
                  <span class="kpi-label">Sprint</span>
                  <span class="kpi-value text-bold">{{ bd.sprint_name }}</span>
                  <span class="kpi-subtext">{{ bd.start_date }} &rarr; {{ bd.end_date }}</span>
                </div>
                <div class="kpi-block">
                  <span class="kpi-label">Committed Scope</span>
                  <span class="kpi-value">{{ estimationUnit() === 'points' ? bd.committed_points + ' pts' : bd.committed_hours + ' hrs' }}</span>
                  <span class="kpi-subtext">Baseline at start</span>
                </div>
                <div class="kpi-block">
                  <span class="kpi-label">Current Scope</span>
                  <span class="kpi-value" [class.text-warning]="bd.has_scope_creep">
                    {{ estimationUnit() === 'points' ? bd.current_scope_points + ' pts' : bd.current_scope_hours + ' hrs' }}
                  </span>
                  @if (bd.has_scope_creep) {
                    <span class="kpi-subtext badge-danger">+{{ bd.scope_creep_points }} pts Scope Creep</span>
                  } @else {
                    <span class="kpi-subtext badge-success">No Scope Creep</span>
                  }
                </div>
                <div class="kpi-block">
                  <span class="kpi-label">Remaining Work</span>
                  <span class="kpi-value text-primary">
                    {{ estimationUnit() === 'points' ? bd.remaining_points + ' pts' : bd.remaining_hours + ' hrs' }}
                  </span>
                  <span class="kpi-subtext">{{ bd.completed_points }} pts completed ({{ bd.completion_rate_percent | number:'1.0-1' }}%)</span>
                </div>
                <div class="kpi-block">
                  <span class="kpi-label">Completion Forecast</span>
                  <span class="kpi-value text-success">{{ bd.projected_completion_date }}</span>
                  <span class="kpi-subtext">{{ bd.points_burn_rate_per_day | number:'1.1-1' }} pts / day pace</span>
                </div>
              </div>

              <!-- Interactive Burndown Chart SVG -->
              <div class="chart-wrapper">
                <div class="chart-legend">
                  <span class="legend-item"><span class="legend-color ideal-guideline"></span> Ideal Guideline</span>
                  <span class="legend-item"><span class="legend-color remaining-line"></span> Remaining Work</span>
                  <span class="legend-item"><span class="legend-color scope-line"></span> Total Scope</span>
                  <span class="legend-item"><span class="legend-color weekend-fill"></span> Weekend</span>
                </div>

                <div class="svg-container">
                  <svg viewBox="0 0 900 320" class="responsive-svg" preserveAspectRatio="none">
                    <!-- Grid Lines -->
                    <line x1="60" y1="30" x2="880" y2="30" stroke="#EBECF0" stroke-width="1" />
                    <line x1="60" y1="90" x2="880" y2="90" stroke="#EBECF0" stroke-width="1" />
                    <line x1="60" y1="150" x2="880" y2="150" stroke="#EBECF0" stroke-width="1" />
                    <line x1="60" y1="210" x2="880" y2="210" stroke="#EBECF0" stroke-width="1" />
                    <line x1="60" y1="270" x2="880" y2="270" stroke="#42526E" stroke-width="1.5" />

                    <!-- Weekend Background Bars -->
                    @for (dp of bd.timeline; track dp.date; let idx = $index) {
                      @if (dp.is_weekend) {
                        <rect
                          [attr.x]="getTimelineX(idx, bd.timeline.length) - 15"
                          y="30"
                          width="30"
                          height="240"
                          fill="rgba(9, 30, 66, 0.04)"
                        />
                      }
                    }

                    <!-- Ideal Guideline Path (Dashed gray) -->
                    <path
                      [attr.d]="getBurndownIdealPath(bd)"
                      fill="none"
                      stroke="#8993A4"
                      stroke-width="2"
                      stroke-dasharray="6,4"
                    />

                    <!-- Total Scope Line (Solid gray) -->
                    <path
                      [attr.d]="getBurndownScopePath(bd)"
                      fill="none"
                      stroke="#6B778C"
                      stroke-width="1.5"
                    />

                    <!-- Actual Remaining Path (Bold Primary Blue) -->
                    <path
                      [attr.d]="getBurndownRemainingPath(bd)"
                      fill="none"
                      stroke="#0052CC"
                      stroke-width="3"
                    />

                    <!-- Data Points on Actual Remaining -->
                    @for (dp of bd.timeline; track dp.date; let idx = $index) {
                      <circle
                        [attr.cx]="getTimelineX(idx, bd.timeline.length)"
                        [attr.cy]="getBurndownY(estimationUnit() === 'points' ? dp.remaining_points : dp.remaining_hours, bd)"
                        r="4"
                        fill="#0052CC"
                        stroke="#fff"
                        stroke-width="1.5"
                        class="chart-dot"
                      >
                        <title>{{ dp.date }} (Day {{ dp.day_index }}): Remaining {{ estimationUnit() === 'points' ? dp.remaining_points + ' pts' : dp.remaining_hours + ' hrs' }} / Ideal {{ estimationUnit() === 'points' ? dp.ideal_points + ' pts' : dp.ideal_hours + ' hrs' }}</title>
                      </circle>
                    }

                    <!-- Y-Axis Labels -->
                    <text x="50" y="35" text-anchor="end" class="svg-axis-label">{{ getMaxBurndownVal(bd) }}</text>
                    <text x="50" y="155" text-anchor="end" class="svg-axis-label">{{ getMaxBurndownVal(bd) / 2 | number:'1.0-0' }}</text>
                    <text x="50" y="275" text-anchor="end" class="svg-axis-label">0</text>

                    <!-- X-Axis Date Labels -->
                    @for (dp of bd.timeline; track dp.date; let idx = $index) {
                      @if (idx % 2 === 0 || idx === bd.timeline.length - 1) {
                        <text
                          [attr.x]="getTimelineX(idx, bd.timeline.length)"
                          y="290"
                          text-anchor="middle"
                          class="svg-axis-label"
                        >
                          {{ dp.date.slice(5) }}
                        </text>
                      }
                    }
                  </svg>
                </div>
              </div>

              <!-- Scope Changes Timeline Table -->
              <div class="report-table-section">
                <h3 class="section-title">Sprint Scope Change Events &amp; Mid-Sprint Creep Log</h3>
                @if (bd.scope_changes.length === 0) {
                  <div class="empty-hint">
                    No mid-sprint scope changes detected. The sprint scope remained perfectly stable since commitment.
                  </div>
                } @else {
                  <table class="report-data-table">
                    <thead>
                      <tr>
                        <th>Date</th>
                        <th>Event Type</th>
                        <th>Issue Key</th>
                        <th>Summary</th>
                        <th class="text-right">Story Points Delta</th>
                      </tr>
                    </thead>
                    <tbody>
                      @for (sc of bd.scope_changes; track sc.item_id + sc.date) {
                        <tr>
                          <td>{{ sc.date }}</td>
                          <td>
                            <span class="badge" [class.badge-danger]="sc.event_type === 'added'" [class.badge-info]="sc.event_type !== 'added'">
                              {{ sc.event_type }}
                            </span>
                          </td>
                          <td>
                            <a [routerLink]="['/items', sc.item_key]" class="issue-key-link">{{ sc.item_key }}</a>
                          </td>
                          <td>{{ sc.summary }}</td>
                          <td class="text-right text-bold" [class.text-danger]="sc.delta_points > 0" [class.text-success]="sc.delta_points < 0">
                            {{ sc.delta_points > 0 ? '+' + sc.delta_points : sc.delta_points }} pts
                          </td>
                        </tr>
                      }
                    </tbody>
                  </table>
                }
              </div>
            </div>
          }

          <!-- ============================================================= -->
          <!-- 2. SPRINT BURNUP REPORT -->
          <!-- ============================================================= -->
          @if (activeTab() === 'burnup' && burndownData(); as bd) {
            <div class="report-card">
              <div class="report-summary-ribbon">
                <div class="kpi-block">
                  <span class="kpi-label">Sprint</span>
                  <span class="kpi-value text-bold">{{ bd.sprint_name }}</span>
                  <span class="kpi-subtext">{{ bd.status }}</span>
                </div>
                <div class="kpi-block">
                  <span class="kpi-label">Total Scope Line</span>
                  <span class="kpi-value text-bold">{{ bd.current_scope_points }} pts</span>
                  <span class="kpi-subtext">{{ bd.scope_changes.length }} scope adjustments</span>
                </div>
                <div class="kpi-block">
                  <span class="kpi-label">Completed Work Line</span>
                  <span class="kpi-value text-success text-bold">{{ bd.completed_points }} pts</span>
                  <span class="kpi-subtext">{{ bd.completion_rate_percent | number:'1.0-1' }}% finished</span>
                </div>
                <div class="kpi-block">
                  <span class="kpi-label">Forecast Status</span>
                  <span class="kpi-value text-primary">{{ bd.projected_completion_date }}</span>
                  <span class="kpi-subtext">Estimated completion</span>
                </div>
              </div>

              <!-- Interactive Burnup Chart SVG -->
              <div class="chart-wrapper">
                <div class="chart-legend">
                  <span class="legend-item"><span class="legend-color scope-line"></span> Total Scope (Upper Line)</span>
                  <span class="legend-item"><span class="legend-color completed-line"></span> Completed Work (Rising Line)</span>
                  <span class="legend-item"><span class="legend-color ideal-guideline"></span> Ideal Pace</span>
                </div>

                <div class="svg-container">
                  <svg viewBox="0 0 900 320" class="responsive-svg" preserveAspectRatio="none">
                    <!-- Grid Lines -->
                    <line x1="60" y1="30" x2="880" y2="30" stroke="#EBECF0" stroke-width="1" />
                    <line x1="60" y1="90" x2="880" y2="90" stroke="#EBECF0" stroke-width="1" />
                    <line x1="60" y1="150" x2="880" y2="150" stroke="#EBECF0" stroke-width="1" />
                    <line x1="60" y1="210" x2="880" y2="210" stroke="#EBECF0" stroke-width="1" />
                    <line x1="60" y1="270" x2="880" y2="270" stroke="#42526E" stroke-width="1.5" />

                    <!-- Total Scope Line (Red/Grey upper line) -->
                    <path
                      [attr.d]="getBurndownScopePath(bd)"
                      fill="none"
                      stroke="#FF5630"
                      stroke-width="2.5"
                    />

                    <!-- Ideal Pace Line (Dashed) -->
                    <path
                      [attr.d]="getBurnupIdealPath(bd)"
                      fill="none"
                      stroke="#8993A4"
                      stroke-width="2"
                      stroke-dasharray="5,5"
                    />

                    <!-- Completed Work Area & Line (Green) -->
                    <path
                      [attr.d]="getBurnupCompletedPath(bd)"
                      fill="none"
                      stroke="#36B37E"
                      stroke-width="3"
                    />

                    <!-- Points on Completed line -->
                    @for (dp of bd.timeline; track dp.date; let idx = $index) {
                      <circle
                        [attr.cx]="getTimelineX(idx, bd.timeline.length)"
                        [attr.cy]="getBurndownY(dp.completed_points, bd)"
                        r="4"
                        fill="#36B37E"
                        stroke="#fff"
                        stroke-width="1.5"
                      >
                        <title>{{ dp.date }}: Completed {{ dp.completed_points }} pts of {{ dp.total_scope_points }} pts</title>
                      </circle>
                    }

                    <!-- Axis Labels -->
                    <text x="50" y="35" text-anchor="end" class="svg-axis-label">{{ getMaxBurndownVal(bd) }}</text>
                    <text x="50" y="155" text-anchor="end" class="svg-axis-label">{{ getMaxBurndownVal(bd) / 2 | number:'1.0-0' }}</text>
                    <text x="50" y="275" text-anchor="end" class="svg-axis-label">0</text>

                    @for (dp of bd.timeline; track dp.date; let idx = $index) {
                      @if (idx % 2 === 0 || idx === bd.timeline.length - 1) {
                        <text
                          [attr.x]="getTimelineX(idx, bd.timeline.length)"
                          y="290"
                          text-anchor="middle"
                          class="svg-axis-label"
                        >
                          {{ dp.date.slice(5) }}
                        </text>
                      }
                    }
                  </svg>
                </div>
              </div>
            </div>
          }

          <!-- ============================================================= -->
          <!-- 3. VELOCITY CHART REPORT -->
          <!-- ============================================================= -->
          @if (activeTab() === 'velocity' && velocityData(); as vd) {
            <div class="report-card">
              <div class="report-summary-ribbon">
                <div class="kpi-block">
                  <span class="kpi-label">Sprints Analyzed</span>
                  <span class="kpi-value text-bold">{{ vd.sprints.length }}</span>
                  <span class="kpi-subtext">Historical iterations</span>
                </div>
                <div class="kpi-block">
                  <span class="kpi-label">Rolling Average Velocity</span>
                  <span class="kpi-value text-primary text-bold">{{ vd.rolling_average_velocity }} pts</span>
                  <span class="kpi-subtext">&plusmn;{{ vd.velocity_std_dev }} pts std dev</span>
                </div>
                <div class="kpi-block">
                  <span class="kpi-label">Recent Trend (Last 3)</span>
                  <span class="kpi-value text-success text-bold">{{ vd.last_3_sprints_average }} pts</span>
                  <span class="kpi-subtext">Recent 3-sprint average</span>
                </div>
                <div class="kpi-block">
                  <span class="kpi-label">Recommended Capacity</span>
                  <span class="kpi-value text-bold badge-capacity">{{ vd.suggested_capacity }} pts</span>
                  <span class="kpi-subtext">Next sprint target</span>
                </div>
              </div>

              <!-- Bar Chart SVG: Committed vs Completed -->
              <div class="chart-wrapper">
                <div class="chart-legend">
                  <span class="legend-item"><span class="legend-color bar-committed"></span> Committed Points</span>
                  <span class="legend-item"><span class="legend-color bar-completed"></span> Completed Points</span>
                  <span class="legend-item"><span class="legend-color avg-line"></span> Rolling Average Velocity</span>
                </div>

                <div class="svg-container">
                  <svg viewBox="0 0 900 320" class="responsive-svg" preserveAspectRatio="none">
                    <!-- Grid Lines -->
                    <line x1="60" y1="30" x2="880" y2="30" stroke="#EBECF0" stroke-width="1" />
                    <line x1="60" y1="90" x2="880" y2="90" stroke="#EBECF0" stroke-width="1" />
                    <line x1="60" y1="150" x2="880" y2="150" stroke="#EBECF0" stroke-width="1" />
                    <line x1="60" y1="210" x2="880" y2="210" stroke="#EBECF0" stroke-width="1" />
                    <line x1="60" y1="270" x2="880" y2="270" stroke="#42526E" stroke-width="1.5" />

                    <!-- Rolling Average Horizontal Line -->
                    <line
                      x1="60"
                      [attr.y1]="getVelocityY(vd.rolling_average_velocity, vd)"
                      x2="880"
                      [attr.y2]="getVelocityY(vd.rolling_average_velocity, vd)"
                      stroke="#FFAB00"
                      stroke-width="2"
                      stroke-dasharray="6,4"
                    />

                    <!-- Bars for Each Sprint -->
                    @for (sp of vd.sprints; track sp.sprint_id; let idx = $index) {
                      <!-- Committed Bar -->
                      <rect
                        [attr.x]="getVelocityBarX(idx, vd.sprints.length) - 18"
                        [attr.y]="getVelocityY(sp.committed_points, vd)"
                        width="16"
                        [attr.height]="270 - getVelocityY(sp.committed_points, vd)"
                        fill="#0052CC"
                        rx="2"
                      >
                        <title>{{ sp.name }}: Committed {{ sp.committed_points }} pts</title>
                      </rect>

                      <!-- Completed Bar -->
                      <rect
                        [attr.x]="getVelocityBarX(idx, vd.sprints.length) + 2"
                        [attr.y]="getVelocityY(sp.completed_points, vd)"
                        width="16"
                        [attr.height]="270 - getVelocityY(sp.completed_points, vd)"
                        fill="#36B37E"
                        rx="2"
                      >
                        <title>{{ sp.name }}: Completed {{ sp.completed_points }} pts ({{ sp.predictability_percent }}% predictability)</title>
                      </rect>

                      <!-- Sprint Label -->
                      <text
                        [attr.x]="getVelocityBarX(idx, vd.sprints.length)"
                        y="290"
                        text-anchor="middle"
                        class="svg-axis-label"
                      >
                        {{ sp.name.length > 15 ? sp.name.slice(0, 15) + '...' : sp.name }}
                      </text>
                    }

                    <!-- Axis Labels -->
                    <text x="50" y="35" text-anchor="end" class="svg-axis-label">{{ getMaxVelocityVal(vd) }}</text>
                    <text x="50" y="155" text-anchor="end" class="svg-axis-label">{{ getMaxVelocityVal(vd) / 2 | number:'1.0-0' }}</text>
                    <text x="50" y="275" text-anchor="end" class="svg-axis-label">0</text>
                  </svg>
                </div>
              </div>

              <!-- Velocity Breakdown Table -->
              <div class="report-table-section">
                <h3 class="section-title">Sprint Commitment &amp; Predictability History</h3>
                <table class="report-data-table">
                  <thead>
                    <tr>
                      <th>Sprint</th>
                      <th>Status</th>
                      <th>Dates</th>
                      <th class="text-right">Committed Points</th>
                      <th class="text-right">Completed Points</th>
                      <th class="text-right">Predictability %</th>
                    </tr>
                  </thead>
                  <tbody>
                    @for (sp of vd.sprints; track sp.sprint_id) {
                      <tr>
                        <td class="text-bold">{{ sp.name }}</td>
                        <td>
                          <span class="badge" [class.badge-success]="sp.status === 'closed'" [class.badge-primary]="sp.status === 'active'">
                            {{ sp.status }}
                          </span>
                        </td>
                        <td>{{ sp.start_date }} &rarr; {{ sp.end_date }}</td>
                        <td class="text-right">{{ sp.committed_points }} pts ({{ sp.committed_items }} items)</td>
                        <td class="text-right text-success text-bold">{{ sp.completed_points }} pts ({{ sp.completed_items }} items)</td>
                        <td class="text-right">
                          <span class="predictability-pill" [class.high]="sp.predictability_percent >= 80" [class.med]="sp.predictability_percent >= 60 && sp.predictability_percent < 80" [class.low]="sp.predictability_percent < 60">
                            {{ sp.predictability_percent | number:'1.0-0' }}%
                          </span>
                        </td>
                      </tr>
                    }
                  </tbody>
                </table>
              </div>
            </div>
          }

          <!-- ============================================================= -->
          <!-- 4. CUMULATIVE FLOW DIAGRAM (CFD) -->
          <!-- ============================================================= -->
          @if (activeTab() === 'cfd' && cfdData(); as cfd) {
            <div class="report-card">
              <div class="report-summary-ribbon">
                <div class="kpi-block">
                  <span class="kpi-label">Time Window</span>
                  <span class="kpi-value text-bold">{{ cfd.days_analyzed }} Days</span>
                  <span class="kpi-subtext">{{ cfd.start_date }} &rarr; {{ cfd.end_date }}</span>
                </div>
                <div class="kpi-block">
                  <span class="kpi-label">Current WIP</span>
                  <span class="kpi-value text-primary text-bold">{{ cfd.current_wip }} items</span>
                  <span class="kpi-subtext">Active in-progress work</span>
                </div>
                <div class="kpi-block">
                  <span class="kpi-label">Average WIP</span>
                  <span class="kpi-value text-bold">{{ cfd.average_wip | number:'1.1-1' }} items</span>
                  <span class="kpi-subtext">Daily throughput average</span>
                </div>
                <div class="kpi-block">
                  <span class="kpi-label">Bottleneck Status</span>
                  <span class="kpi-value badge-warning">{{ cfd.bottleneck_status }}</span>
                  <span class="kpi-subtext">Status with highest growth</span>
                </div>
              </div>

              <!-- CFD Stacked Area Chart SVG -->
              <div class="chart-wrapper">
                <div class="chart-legend">
                  @for (st of cfd.statuses; track st) {
                    <span class="legend-item">
                      <span class="legend-color" [style.background]="getStatusColor(st)"></span>
                      {{ st }}
                    </span>
                  }
                </div>

                <div class="svg-container">
                  <svg viewBox="0 0 900 320" class="responsive-svg" preserveAspectRatio="none">
                    <!-- Grid -->
                    <line x1="60" y1="30" x2="880" y2="30" stroke="#EBECF0" stroke-width="1" />
                    <line x1="60" y1="150" x2="880" y2="150" stroke="#EBECF0" stroke-width="1" />
                    <line x1="60" y1="270" x2="880" y2="270" stroke="#42526E" stroke-width="1.5" />

                    <!-- Render Stacked Area Polygons -->
                    @for (layer of getCfdLayers(cfd); track layer.status) {
                      <polygon
                        [attr.points]="layer.polygonPoints"
                        [attr.fill]="getStatusColor(layer.status)"
                        fill-opacity="0.8"
                        stroke="#fff"
                        stroke-width="0.5"
                      >
                        <title>{{ layer.status }}</title>
                      </polygon>
                    }

                    <!-- Date X-Axis -->
                    @for (d of cfd.dates; track d; let idx = $index) {
                      @if (idx % 4 === 0 || idx === cfd.dates.length - 1) {
                        <text
                          [attr.x]="getTimelineX(idx, cfd.dates.length)"
                          y="290"
                          text-anchor="middle"
                          class="svg-axis-label"
                        >
                          {{ d.slice(5) }}
                        </text>
                      }
                    }

                    <!-- Y-Axis -->
                    <text x="50" y="35" text-anchor="end" class="svg-axis-label">{{ getMaxCfdTotal(cfd) }}</text>
                    <text x="50" y="155" text-anchor="end" class="svg-axis-label">{{ getMaxCfdTotal(cfd) / 2 | number:'1.0-0' }}</text>
                    <text x="50" y="275" text-anchor="end" class="svg-axis-label">0</text>
                  </svg>
                </div>
              </div>
            </div>
          }

          <!-- ============================================================= -->
          <!-- 5. CONTROL CHART & CYCLE TIME SLA REPORT -->
          <!-- ============================================================= -->
          @if (activeTab() === 'cycle_time' && cycleTimeData(); as ct) {
            <div class="report-card">
              <div class="report-summary-ribbon">
                <div class="kpi-block">
                  <span class="kpi-label">Resolved Issues</span>
                  <span class="kpi-value text-bold">{{ ct.sample_size }}</span>
                  <span class="kpi-subtext">Completed items analyzed</span>
                </div>
                <div class="kpi-block">
                  <span class="kpi-label">Median Cycle Time (p50)</span>
                  <span class="kpi-value text-primary text-bold">{{ ct.median_cycle_time_days }} days</span>
                  <span class="kpi-subtext">50% completed faster</span>
                </div>
                <div class="kpi-block">
                  <span class="kpi-label">Kanban SLA (p85)</span>
                  <span class="kpi-value text-success text-bold">{{ ct.p85_cycle_time_days }} days</span>
                  <span class="kpi-subtext">85th percentile service level</span>
                </div>
                <div class="kpi-block">
                  <span class="kpi-label">95th Percentile (p95)</span>
                  <span class="kpi-value text-bold">{{ ct.p95_cycle_time_days }} days</span>
                  <span class="kpi-subtext">Long-tail boundary</span>
                </div>
                <div class="kpi-block">
                  <span class="kpi-label">Control Limits (&sigma;)</span>
                  <span class="kpi-value">&plusmn;{{ ct.std_dev_days }} d</span>
                  <span class="kpi-subtext">UCL2 = {{ ct.upper_control_limit_2_sigma }} d</span>
                </div>
              </div>

              <!-- Control Chart Scatter Plot SVG -->
              <div class="chart-wrapper">
                <div class="chart-legend">
                  <span class="legend-item"><span class="legend-color dot-cycle-time"></span> Issue Cycle Time</span>
                  <span class="legend-item"><span class="legend-color rolling-line"></span> Rolling Average</span>
                  <span class="legend-item"><span class="legend-color p85-line"></span> p85 SLA Target ({{ ct.p85_cycle_time_days }}d)</span>
                  <span class="legend-item"><span class="legend-color outlier-dot"></span> Outlier (&gt; 2&sigma;)</span>
                </div>

                <div class="svg-container">
                  <svg viewBox="0 0 900 320" class="responsive-svg" preserveAspectRatio="none">
                    <!-- Standard Deviation Bands -->
                    <rect
                      x="60"
                      [attr.y]="getCycleTimeY(ct.upper_control_limit_1_sigma, ct)"
                      width="820"
                      [attr.height]="Math.max(1, getCycleTimeY(ct.lower_control_limit_1_sigma, ct) - getCycleTimeY(ct.upper_control_limit_1_sigma, ct))"
                      fill="rgba(0, 82, 204, 0.06)"
                    />

                    <!-- p85 SLA Target Line -->
                    <line
                      x1="60"
                      [attr.y1]="getCycleTimeY(ct.p85_cycle_time_days, ct)"
                      x2="880"
                      [attr.y2]="getCycleTimeY(ct.p85_cycle_time_days, ct)"
                      stroke="#36B37E"
                      stroke-width="1.5"
                      stroke-dasharray="4,4"
                    />

                    <!-- Rolling Average Curve -->
                    <path
                      [attr.d]="getCycleTimeRollingPath(ct)"
                      fill="none"
                      stroke="#FFAB00"
                      stroke-width="2.5"
                    />

                    <!-- Scatter Points for Each Resolved Issue -->
                    @for (it of ct.issues; track it.id; let idx = $index) {
                      <circle
                        [attr.cx]="getTimelineX(idx, ct.issues.length)"
                        [attr.cy]="getCycleTimeY(it.cycle_time_days, ct)"
                        [attr.r]="it.is_outlier ? 6 : 4.5"
                        [attr.fill]="it.is_outlier ? '#FF5630' : '#0052CC'"
                        stroke="#fff"
                        stroke-width="1.5"
                        class="chart-dot"
                      >
                        <title>{{ it.key }}: Cycle Time {{ it.cycle_time_days }} days (Lead Time {{ it.lead_time_days }} days) - {{ it.summary }}</title>
                      </circle>
                    }

                    <!-- Axis -->
                    <line x1="60" y1="270" x2="880" y2="270" stroke="#42526E" stroke-width="1.5" />
                    <text x="50" y="35" text-anchor="end" class="svg-axis-label">{{ getMaxCycleTimeVal(ct) }}d</text>
                    <text x="50" y="155" text-anchor="end" class="svg-axis-label">{{ getMaxCycleTimeVal(ct) / 2 | number:'1.0-0' }}d</text>
                    <text x="50" y="275" text-anchor="end" class="svg-axis-label">0d</text>
                  </svg>
                </div>
              </div>

              <!-- Cycle Time Table -->
              <div class="report-table-section">
                <h3 class="section-title">Resolved Issues &amp; Lead/Cycle Time Metrics</h3>
                <table class="report-data-table">
                  <thead>
                    <tr>
                      <th>Issue Key</th>
                      <th>Summary</th>
                      <th>Type</th>
                      <th>Assignee</th>
                      <th>Resolved Date</th>
                      <th class="text-right">Cycle Time</th>
                      <th class="text-right">Lead Time</th>
                      <th class="text-center">SLA Compliance</th>
                    </tr>
                  </thead>
                  <tbody>
                    @for (it of ct.issues; track it.id) {
                      <tr>
                        <td><a [routerLink]="['/items', it.key]" class="issue-key-link">{{ it.key }}</a></td>
                        <td>{{ it.summary }}</td>
                        <td><span class="badge badge-subtle">{{ it.type }}</span></td>
                        <td>{{ it.assignee }}</td>
                        <td>{{ it.resolved_at.slice(0, 10) }}</td>
                        <td class="text-right text-bold" [class.text-danger]="it.is_outlier">
                          {{ it.cycle_time_days }} d
                        </td>
                        <td class="text-right">{{ it.lead_time_days }} d</td>
                        <td class="text-center">
                          @if (it.cycle_time_days <= ct.p85_cycle_time_days) {
                            <span class="badge badge-success">&#10003; Within SLA</span>
                          } @else if (it.is_outlier) {
                            <span class="badge badge-danger">&#9888; Outlier</span>
                          } @else {
                            <span class="badge badge-warning">&#9888; Over SLA</span>
                          }
                        </td>
                      </tr>
                    }
                  </tbody>
                </table>
              </div>
            </div>
          }

          <!-- ============================================================= -->
          <!-- 6. CREATED VS RESOLVED REPORT -->
          <!-- ============================================================= -->
          @if (activeTab() === 'created_resolved' && createdVsResolvedData(); as cvr) {
            <div class="report-card">
              <div class="report-summary-ribbon">
                <div class="kpi-block">
                  <span class="kpi-label">Issues Created</span>
                  <span class="kpi-value text-primary text-bold">{{ cvr.total_created }}</span>
                  <span class="kpi-subtext">Incoming volume</span>
                </div>
                <div class="kpi-block">
                  <span class="kpi-label">Issues Resolved</span>
                  <span class="kpi-value text-success text-bold">{{ cvr.total_resolved }}</span>
                  <span class="kpi-subtext">Throughput volume</span>
                </div>
                <div class="kpi-block">
                  <span class="kpi-label">Net Backlog Growth</span>
                  <span class="kpi-value text-bold" [class.text-danger]="cvr.net_backlog_delta > 0" [class.text-success]="cvr.net_backlog_delta <= 0">
                    {{ cvr.net_backlog_delta > 0 ? '+' + cvr.net_backlog_delta : cvr.net_backlog_delta }}
                  </span>
                  <span class="kpi-subtext">Backlog change</span>
                </div>
                <div class="kpi-block">
                  <span class="kpi-label">Resolution Rate</span>
                  <span class="kpi-value text-bold">{{ cvr.resolution_rate_percent | number:'1.0-0' }}%</span>
                  <span class="kpi-subtext">{{ cvr.health_status }}</span>
                </div>
              </div>

              <!-- Created vs Resolved Dual Line Chart -->
              <div class="chart-wrapper">
                <div class="chart-legend">
                  <span class="legend-item"><span class="legend-color created-line"></span> Created (Incoming)</span>
                  <span class="legend-item"><span class="legend-color completed-line"></span> Resolved (Completed)</span>
                </div>

                <div class="svg-container">
                  <svg viewBox="0 0 900 320" class="responsive-svg" preserveAspectRatio="none">
                    <line x1="60" y1="30" x2="880" y2="30" stroke="#EBECF0" stroke-width="1" />
                    <line x1="60" y1="150" x2="880" y2="150" stroke="#EBECF0" stroke-width="1" />
                    <line x1="60" y1="270" x2="880" y2="270" stroke="#42526E" stroke-width="1.5" />

                    <!-- Created Cumulative Line (Blue) -->
                    <path
                      [attr.d]="getCvrCreatedPath(cvr)"
                      fill="none"
                      stroke="#0052CC"
                      stroke-width="3"
                    />

                    <!-- Resolved Cumulative Line (Green) -->
                    <path
                      [attr.d]="getCvrResolvedPath(cvr)"
                      fill="none"
                      stroke="#36B37E"
                      stroke-width="3"
                    />

                    <!-- X-Axis Labels -->
                    @for (b of cvr.buckets; track b.period; let idx = $index) {
                      @if (idx % 4 === 0 || idx === cvr.buckets.length - 1) {
                        <text
                          [attr.x]="getTimelineX(idx, cvr.buckets.length)"
                          y="290"
                          text-anchor="middle"
                          class="svg-axis-label"
                        >
                          {{ b.period.slice(5) }}
                        </text>
                      }
                    }

                    <!-- Y-Axis -->
                    <text x="50" y="35" text-anchor="end" class="svg-axis-label">{{ getMaxCvrVal(cvr) }}</text>
                    <text x="50" y="155" text-anchor="end" class="svg-axis-label">{{ getMaxCvrVal(cvr) / 2 | number:'1.0-0' }}</text>
                    <text x="50" y="275" text-anchor="end" class="svg-axis-label">0</text>
                  </svg>
                </div>
              </div>
            </div>
          }

          <!-- ============================================================= -->
          <!-- 7. EPIC & RELEASE PROGRESS REPORT -->
          <!-- ============================================================= -->
          @if (activeTab() === 'epic_release') {
            @if (epicReleaseSubView() === 'epics') {
              @if (epicReportData(); as ep) {
                <div class="report-card">
                  <div class="report-summary-ribbon">
                    <div class="kpi-block">
                      <span class="kpi-label">Epic Key</span>
                      <span class="kpi-value text-bold">{{ ep.key }}</span>
                      <span class="kpi-subtext">{{ ep.summary }}</span>
                    </div>
                    <div class="kpi-block">
                      <span class="kpi-label">Story Points Progress</span>
                      <span class="kpi-value text-success text-bold">{{ ep.completed_points }} / {{ ep.total_points }} pts</span>
                      <span class="kpi-subtext">{{ ep.progress_percent }}% complete</span>
                    </div>
                    <div class="kpi-block">
                      <span class="kpi-label">Issue Breakdown</span>
                      <span class="kpi-value text-bold">{{ ep.completed_items }} / {{ ep.total_items }} done</span>
                      <span class="kpi-subtext">{{ ep.in_progress_items }} in progress, {{ ep.unstarted_items }} to do</span>
                    </div>
                    <div class="kpi-block">
                      <span class="kpi-label">Time Logged</span>
                      <span class="kpi-value text-primary text-bold">{{ ep.time_spent_hours }} hrs</span>
                      <span class="kpi-subtext">{{ ep.remaining_estimate_hours }} hrs remaining</span>
                    </div>
                  </div>

                  <!-- Visual Progress Bar -->
                  <div class="epic-progress-meter">
                    <div class="meter-bar-track">
                      <div class="meter-bar-fill done" [style.width.%]="ep.progress_percent"></div>
                    </div>
                    <span class="meter-text">{{ ep.progress_percent }}% Finished ({{ ep.completion_forecast }})</span>
                  </div>

                  <!-- Child Issues Table -->
                  <div class="report-table-section">
                    <h3 class="section-title">Child Tasks, Stories &amp; Deliverables</h3>
                    <table class="report-data-table">
                      <thead>
                        <tr>
                          <th>Key</th>
                          <th>Summary</th>
                          <th>Type</th>
                          <th>Status</th>
                          <th>Priority</th>
                          <th>Assignee</th>
                          <th class="text-right">Points</th>
                          <th class="text-right">Remaining</th>
                          <th class="text-right">Logged</th>
                        </tr>
                      </thead>
                      <tbody>
                        @for (ci of ep.child_issues; track ci.id) {
                          <tr>
                            <td><a [routerLink]="['/items', ci.key]" class="issue-key-link">{{ ci.key }}</a></td>
                            <td>{{ ci.summary }}</td>
                            <td><span class="badge badge-subtle">{{ ci.type }}</span></td>
                            <td><span class="badge" [class.badge-success]="ci.status === 'Done'" [class.badge-primary]="ci.status !== 'Done'">{{ ci.status }}</span></td>
                            <td>{{ ci.priority }}</td>
                            <td>{{ ci.assignee }}</td>
                            <td class="text-right">{{ ci.points }} pts</td>
                            <td class="text-right">{{ ci.remaining_estimate_hours }}h</td>
                            <td class="text-right">{{ ci.time_spent_hours }}h</td>
                          </tr>
                        }
                      </tbody>
                    </table>
                  </div>
                </div>
              }
            } @else {
              @if (releaseReportData(); as rel) {
                <div class="report-card">
                  <div class="report-summary-ribbon">
                    <div class="kpi-block">
                      <span class="kpi-label">Version</span>
                      <span class="kpi-value text-bold">{{ rel.version_name }}</span>
                      <span class="kpi-subtext">Target Date: {{ rel.release_date }}</span>
                    </div>
                    <div class="kpi-block">
                      <span class="kpi-label">Completion Status</span>
                      <span class="kpi-value text-bold" [class.text-success]="rel.forecast_status === 'On Track'" [class.text-warning]="rel.forecast_status === 'At Risk'">
                        {{ rel.forecast_status }}
                      </span>
                      <span class="kpi-subtext">{{ rel.completion_percent }}% complete</span>
                    </div>
                    <div class="kpi-block">
                      <span class="kpi-label">Deliverables</span>
                      <span class="kpi-value text-bold">{{ rel.completed_items }} / {{ rel.total_items }} items</span>
                      <span class="kpi-subtext">{{ rel.remaining_items }} items remaining</span>
                    </div>
                    <div class="kpi-block">
                      <span class="kpi-label">Story Points</span>
                      <span class="kpi-value text-primary text-bold">{{ rel.completed_points }} / {{ rel.total_points }} pts</span>
                      <span class="kpi-subtext">{{ rel.remaining_points }} pts to burn</span>
                    </div>
                  </div>

                  <div class="report-table-section">
                    <h3 class="section-title">Fix Version Issues</h3>
                    <table class="report-data-table">
                      <thead>
                        <tr>
                          <th>Key</th>
                          <th>Summary</th>
                          <th>Type</th>
                          <th>Status</th>
                          <th class="text-right">Story Points</th>
                        </tr>
                      </thead>
                      <tbody>
                        @for (it of rel.items; track it.id) {
                          <tr>
                            <td><a [routerLink]="['/items', it.key]" class="issue-key-link">{{ it.key }}</a></td>
                            <td>{{ it.summary }}</td>
                            <td><span class="badge badge-subtle">{{ it.type }}</span></td>
                            <td><span class="badge" [class.badge-success]="it.status === 'Done'">{{ it.status }}</span></td>
                            <td class="text-right">{{ it.points }} pts</td>
                          </tr>
                        }
                      </tbody>
                    </table>
                  </div>
                </div>
              }
            }
          }

        }
      </div>
    </div>
  `,
})
export class ReportsComponent implements OnInit {
  protected readonly Math = Math;
  private api = inject(ApiService);

  activeTab = signal<ReportType>('burndown');
  loading = signal<boolean>(false);

  // Filter signals
  sprints = signal<Sprint[]>([]);
  selectedSprintId = signal<number>(0);
  estimationUnit = signal<'points' | 'hours'>('points');
  weekendFlattening = signal<boolean>(true);
  velocityLimit = signal<number>(10);
  timeWindowDays = signal<number>(30);

  // Epic / Release signals
  epicReleaseSubView = signal<'epics' | 'releases'>('epics');
  epicsList = signal<EpicSummaryItem[]>([]);
  selectedEpicId = signal<number>(0);
  releasesList = signal<ReleaseSummaryItem[]>([]);
  selectedVersionId = signal<number>(0);

  // Report data signals
  burndownData = signal<BurndownReport | null>(null);
  velocityData = signal<VelocityReport | null>(null);
  cfdData = signal<CumulativeFlowReport | null>(null);
  cycleTimeData = signal<CycleTimeReport | null>(null);
  createdVsResolvedData = signal<CreatedVsResolvedReport | null>(null);
  epicReportData = signal<EpicReport | null>(null);
  releaseReportData = signal<ReleaseBurndownReport | null>(null);

  async ngOnInit(): Promise<void> {
    await this.loadSprintsList();
    await this.loadActiveReport();
  }

  async loadSprintsList(): Promise<void> {
    try {
      const sps = await this.api.getSprints();
      this.sprints.set(sps);
      const cur = sps.find((s) => s.is_current) || sps.find((s) => s.status === 'active') || sps[0];
      if (cur) {
        this.selectedSprintId.set(cur.id);
      }
    } catch (err) {
      console.error('Failed to load sprints list', err);
    }
  }

  selectTab(tab: ReportType): void {
    this.activeTab.set(tab);
    this.loadActiveReport();
  }

  onSprintChanged(sprintId: any): void {
    this.selectedSprintId.set(Number(sprintId));
    this.loadActiveReport();
  }

  setEstimationUnit(unit: 'points' | 'hours'): void {
    this.estimationUnit.set(unit);
    this.loadActiveReport();
  }

  toggleWeekendFlattening(checked: boolean): void {
    this.weekendFlattening.set(checked);
    this.loadActiveReport();
  }

  onVelocityLimitChanged(limit: any): void {
    this.velocityLimit.set(Number(limit));
    this.loadActiveReport();
  }

  setTimeWindowDays(days: number): void {
    this.timeWindowDays.set(days);
    this.loadActiveReport();
  }

  setEpicReleaseSubView(view: 'epics' | 'releases'): void {
    this.epicReleaseSubView.set(view);
    this.loadActiveReport();
  }

  onEpicSelected(id: any): void {
    this.selectedEpicId.set(Number(id));
    this.loadActiveReport();
  }

  onVersionSelected(id: any): void {
    this.selectedVersionId.set(Number(id));
    this.loadActiveReport();
  }

  async loadActiveReport(): Promise<void> {
    this.loading.set(true);
    try {
      const tab = this.activeTab();
      if (tab === 'burndown') {
        const rep = await this.api.getBurndownReport(
          this.selectedSprintId(),
          this.estimationUnit(),
          this.weekendFlattening()
        );
        this.burndownData.set(rep);
      } else if (tab === 'burnup') {
        const rep = await this.api.getBurnupReport(
          this.selectedSprintId(),
          this.estimationUnit()
        );
        this.burndownData.set(rep);
      } else if (tab === 'velocity') {
        const rep = await this.api.getVelocityReport(1, this.velocityLimit());
        this.velocityData.set(rep);
      } else if (tab === 'cfd') {
        const rep = await this.api.getCumulativeFlowReport(1, this.timeWindowDays());
        this.cfdData.set(rep);
      } else if (tab === 'cycle_time') {
        const rep = await this.api.getCycleTimeReport(1, this.timeWindowDays());
        this.cycleTimeData.set(rep);
      } else if (tab === 'created_resolved') {
        const rep = await this.api.getCreatedVsResolvedReport(1, this.timeWindowDays());
        this.createdVsResolvedData.set(rep);
      } else if (tab === 'epic_release') {
        if (this.epicReleaseSubView() === 'epics') {
          const list = await this.api.getEpicsList(1);
          this.epicsList.set(list.epics);
          if (this.selectedEpicId() === 0 && list.epics.length > 0) {
            this.selectedEpicId.set(list.epics[0].id);
          }
          if (this.selectedEpicId() > 0) {
            const ep = await this.api.getEpicReport(this.selectedEpicId());
            this.epicReportData.set(ep);
          }
        } else {
          const vlist = await this.api.getReleasesList(1);
          this.releasesList.set(vlist.releases);
          if (this.selectedVersionId() === 0 && vlist.releases.length > 0) {
            this.selectedVersionId.set(vlist.releases[0].id);
          }
          if (this.selectedVersionId() > 0) {
            const rel = await this.api.getReleaseBurndownReport(this.selectedVersionId());
            this.releaseReportData.set(rel);
          }
        }
      }
    } catch (err) {
      console.error('Failed to load active report', err);
    } finally {
      this.loading.set(false);
    }
  }

  exportCurrentReportCsv(): void {
    const tab = this.activeTab();
    if (tab === 'burndown') {
      this.api.downloadReportCsv(
        '/api/reports/burndown',
        { sprint_id: this.selectedSprintId(), unit: this.estimationUnit() },
        `burndown-sprint-${this.selectedSprintId()}.csv`
      );
    } else if (tab === 'burnup') {
      this.api.downloadReportCsv(
        '/api/reports/burnup',
        { sprint_id: this.selectedSprintId() },
        `burnup-sprint-${this.selectedSprintId()}.csv`
      );
    } else if (tab === 'velocity') {
      this.api.downloadReportCsv(
        '/api/reports/velocity',
        { project_id: 1, limit: this.velocityLimit() },
        `velocity-history.csv`
      );
    } else if (tab === 'cfd') {
      this.api.downloadReportCsv(
        '/api/reports/cumulative-flow',
        { project_id: 1, days: this.timeWindowDays() },
        `cumulative-flow.csv`
      );
    } else if (tab === 'cycle_time') {
      this.api.downloadReportCsv(
        '/api/reports/cycle-time',
        { project_id: 1, days: this.timeWindowDays() },
        `cycle-time-sla.csv`
      );
    } else if (tab === 'created_resolved') {
      this.api.downloadReportCsv(
        '/api/reports/created-vs-resolved',
        { project_id: 1, days: this.timeWindowDays() },
        `created-vs-resolved.csv`
      );
    } else if (tab === 'epic_release') {
      if (this.epicReleaseSubView() === 'epics') {
        this.api.downloadReportCsv(
          '/api/reports/epic-report',
          { epic_id: this.selectedEpicId() },
          `epic-report.csv`
        );
      } else {
        this.api.downloadReportCsv(
          '/api/reports/release-burndown',
          { version_id: this.selectedVersionId() },
          `release-burndown.csv`
        );
      }
    }
  }

  // ============================================================================
  // SVG Math & Path Coordinate Calculation Helpers
  // ============================================================================

  getTimelineX(index: number, total: number): number {
    if (total <= 1) return 470;
    const startX = 75;
    const endX = 865;
    return startX + (index / (total - 1)) * (endX - startX);
  }

  getMaxBurndownVal(bd: BurndownReport): number {
    const isHours = this.estimationUnit() === 'hours';
    const baseline = isHours ? bd.current_scope_hours : bd.current_scope_points;
    let maxV = baseline;
    for (const dp of bd.timeline) {
      const v = isHours ? dp.total_scope_hours : dp.total_scope_points;
      if (v > maxV) maxV = v;
    }
    return Math.max(10, Math.ceil(maxV * 1.15));
  }

  getBurndownY(val: number, bd: BurndownReport): number {
    const maxVal = this.getMaxBurndownVal(bd);
    const topY = 40;
    const bottomY = 270;
    const clamped = Math.max(0, Math.min(maxVal, val));
    return bottomY - (clamped / maxVal) * (bottomY - topY);
  }

  getBurndownIdealPath(bd: BurndownReport): string {
    const isHours = this.estimationUnit() === 'hours';
    return bd.timeline
      .map((dp, i) => {
        const x = this.getTimelineX(i, bd.timeline.length);
        const y = this.getBurndownY(isHours ? dp.ideal_hours : dp.ideal_points, bd);
        return `${i === 0 ? 'M' : 'L'} ${x} ${y}`;
      })
      .join(' ');
  }

  getBurndownScopePath(bd: BurndownReport): string {
    const isHours = this.estimationUnit() === 'hours';
    return bd.timeline
      .map((dp, i) => {
        const x = this.getTimelineX(i, bd.timeline.length);
        const y = this.getBurndownY(isHours ? dp.total_scope_hours : dp.total_scope_points, bd);
        return `${i === 0 ? 'M' : 'L'} ${x} ${y}`;
      })
      .join(' ');
  }

  getBurndownRemainingPath(bd: BurndownReport): string {
    const isHours = this.estimationUnit() === 'hours';
    return bd.timeline
      .map((dp, i) => {
        const x = this.getTimelineX(i, bd.timeline.length);
        const y = this.getBurndownY(isHours ? dp.remaining_hours : dp.remaining_points, bd);
        return `${i === 0 ? 'M' : 'L'} ${x} ${y}`;
      })
      .join(' ');
  }

  getBurnupIdealPath(bd: BurndownReport): string {
    return bd.timeline
      .map((dp, i) => {
        const x = this.getTimelineX(i, bd.timeline.length);
        const idealDone = Math.max(0, dp.total_scope_points - dp.ideal_points);
        const y = this.getBurndownY(idealDone, bd);
        return `${i === 0 ? 'M' : 'L'} ${x} ${y}`;
      })
      .join(' ');
  }

  getBurnupCompletedPath(bd: BurndownReport): string {
    return bd.timeline
      .map((dp, i) => {
        const x = this.getTimelineX(i, bd.timeline.length);
        const y = this.getBurndownY(dp.completed_points, bd);
        return `${i === 0 ? 'M' : 'L'} ${x} ${y}`;
      })
      .join(' ');
  }

  getMaxVelocityVal(vd: VelocityReport): number {
    let maxV = 10;
    for (const sp of vd.sprints) {
      if (sp.committed_points > maxV) maxV = sp.committed_points;
      if (sp.completed_points > maxV) maxV = sp.completed_points;
    }
    return Math.ceil(maxV * 1.25);
  }

  getVelocityBarX(idx: number, total: number): number {
    return this.getTimelineX(idx, total);
  }

  getVelocityY(val: number, vd: VelocityReport): number {
    const maxVal = this.getMaxVelocityVal(vd);
    const topY = 40;
    const bottomY = 270;
    const clamped = Math.max(0, Math.min(maxVal, val));
    return bottomY - (clamped / maxVal) * (bottomY - topY);
  }

  getStatusColor(status: string): string {
    switch (status) {
      case 'Done':
        return '#36B37E';
      case 'QA / Testing':
        return '#00B8D9';
      case 'In Review':
        return '#6554C0';
      case 'In Progress':
        return '#0052CC';
      case 'To Do':
        return '#FFAB00';
      case 'Backlog':
      default:
        return '#8993A4';
    }
  }

  getMaxCfdTotal(cfd: CumulativeFlowReport): number {
    let maxT = 5;
    for (let di = 0; di < cfd.dates.length; ++di) {
      let dailyTot = 0;
      for (const st of cfd.statuses) {
        dailyTot += (cfd.series[st] && cfd.series[st][di]) || 0;
      }
      if (dailyTot > maxT) maxT = dailyTot;
    }
    return Math.max(10, Math.ceil(maxT * 1.2));
  }

  getCfdLayers(cfd: CumulativeFlowReport): { status: string; polygonPoints: string }[] {
    const layers: { status: string; polygonPoints: string }[] = [];
    const maxT = this.getMaxCfdTotal(cfd);
    const topY = 40;
    const bottomY = 270;

    let prevCum = new Array(cfd.dates.length).fill(0);

    for (const st of cfd.statuses) {
      const counts = cfd.series[st] || new Array(cfd.dates.length).fill(0);
      const curCum = prevCum.map((val, i) => val + counts[i]);

      const topPoints: string[] = [];
      const bottomPoints: string[] = [];

      for (let di = 0; di < cfd.dates.length; ++di) {
        const x = this.getTimelineX(di, cfd.dates.length);
        const yTop = bottomY - (curCum[di] / maxT) * (bottomY - topY);
        const yBottom = bottomY - (prevCum[di] / maxT) * (bottomY - topY);
        topPoints.push(`${x},${yTop}`);
        bottomPoints.unshift(`${x},${yBottom}`);
      }

      layers.push({
        status: st,
        polygonPoints: [...topPoints, ...bottomPoints].join(' '),
      });

      prevCum = curCum;
    }

    return layers.reverse(); // Draw from top stack down
  }

  getMaxCycleTimeVal(ct: CycleTimeReport): number {
    let maxV = Math.max(ct.upper_control_limit_2_sigma, ct.p95_cycle_time_days);
    for (const it of ct.issues) {
      if (it.cycle_time_days > maxV) maxV = it.cycle_time_days;
    }
    return Math.max(5, Math.ceil(maxV * 1.2));
  }

  getCycleTimeY(val: number, ct: CycleTimeReport): number {
    const maxV = this.getMaxCycleTimeVal(ct);
    const topY = 40;
    const bottomY = 270;
    const clamped = Math.max(0, Math.min(maxV, val));
    return bottomY - (clamped / maxV) * (bottomY - topY);
  }

  getCycleTimeRollingPath(ct: CycleTimeReport): string {
    return ct.issues
      .map((it, i) => {
        const x = this.getTimelineX(i, ct.issues.length);
        const y = this.getCycleTimeY(it.rolling_avg_days, ct);
        return `${i === 0 ? 'M' : 'L'} ${x} ${y}`;
      })
      .join(' ');
  }

  getMaxCvrVal(cvr: CreatedVsResolvedReport): number {
    return Math.max(10, Math.ceil(Math.max(cvr.total_created, cvr.total_resolved) * 1.2));
  }

  getCvrCreatedPath(cvr: CreatedVsResolvedReport): string {
    const maxV = this.getMaxCvrVal(cvr);
    const topY = 40;
    const bottomY = 270;
    return cvr.buckets
      .map((b, i) => {
        const x = this.getTimelineX(i, cvr.buckets.length);
        const y = bottomY - (b.cumulative_created / maxV) * (bottomY - topY);
        return `${i === 0 ? 'M' : 'L'} ${x} ${y}`;
      })
      .join(' ');
  }

  getCvrResolvedPath(cvr: CreatedVsResolvedReport): string {
    const maxV = this.getMaxCvrVal(cvr);
    const topY = 40;
    const bottomY = 270;
    return cvr.buckets
      .map((b, i) => {
        const x = this.getTimelineX(i, cvr.buckets.length);
        const y = bottomY - (b.cumulative_resolved / maxV) * (bottomY - topY);
        return `${i === 0 ? 'M' : 'L'} ${x} ${y}`;
      })
      .join(' ');
  }
}
