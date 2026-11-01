import { Component, OnInit, computed, inject, signal } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { ActivatedRoute, Router, RouterLink } from '@angular/router';
import { ApiService } from '../../services/api.service';
import {
  AvailableTransition,
  BoardColumn,
  BoardConfig,
  CompleteSprintResponse,
  PRIORITIES,
  Sprint,
  SwimlaneStrategy,
  WORKFLOW_STATES,
  WORK_ITEM_CATEGORIES,
  WORK_ITEM_TYPES,
  WorkItem,
  WorkItemCategory,
  WorkItemType,
  WorkflowState,
} from '../../models/jira.models';

export interface SwimlaneRow {
  key: string;
  title: string;
  subtitle?: string;
  badgeColor?: string;
  items: WorkItem[];
  totalPoints: number;
}

@Component({
  selector: 'app-board',
  standalone: true,
  imports: [CommonModule, FormsModule, RouterLink],
  styles: [
    `
      .board-shell {
        display: flex;
        flex-direction: column;
        gap: 14px;
      }
      .board-top-bar {
        display: flex;
        justify-content: space-between;
        align-items: center;
        flex-wrap: wrap;
        gap: 12px;
        background: #fff;
        padding: 14px 18px;
        border-radius: 10px;
        border: 1px solid #dfe1e6;
        box-shadow: 0 1px 3px rgba(9, 30, 66, 0.05);
      }
      .board-selector-group {
        display: flex;
        align-items: center;
        gap: 10px;
        flex-wrap: wrap;
      }
      .board-select {
        font-size: 15px;
        font-weight: 700;
        color: #172b4d;
        padding: 7px 12px;
        border-radius: 6px;
        border: 2px solid #0052cc;
        background: #f4f5f7;
        cursor: pointer;
      }
      .board-type-tag {
        display: inline-flex;
        align-items: center;
        gap: 5px;
        padding: 4px 10px;
        border-radius: 999px;
        font-size: 11px;
        font-weight: 700;
        text-transform: uppercase;
        letter-spacing: 0.04em;
      }
      .board-type-tag.scrum {
        background: #deebff;
        color: #0747a6;
      }
      .board-type-tag.kanban {
        background: #eae6ff;
        color: #403294;
      }
      .mode-switcher {
        display: inline-flex;
        background: #ebecf0;
        padding: 3px;
        border-radius: 8px;
        gap: 4px;
      }
      .mode-btn {
        border: none;
        background: transparent;
        padding: 6px 14px;
        border-radius: 6px;
        font-size: 13px;
        font-weight: 600;
        color: #42526e;
        cursor: pointer;
        transition: all 0.15s;
      }
      .mode-btn.active {
        background: #0052cc;
        color: #fff;
        box-shadow: 0 1px 3px rgba(0, 82, 204, 0.3);
      }
      .quick-filters-bar {
        display: flex;
        align-items: center;
        justify-content: space-between;
        flex-wrap: wrap;
        gap: 10px;
        background: #fff;
        padding: 10px 16px;
        border-radius: 8px;
        border: 1px solid #dfe1e6;
      }
      .qf-pills {
        display: flex;
        align-items: center;
        gap: 6px;
        flex-wrap: wrap;
      }
      .qf-pill {
        padding: 5px 11px;
        border-radius: 999px;
        border: 1px solid #dfe1e6;
        background: #f4f5f7;
        color: #172b4d;
        font-size: 12px;
        font-weight: 600;
        cursor: pointer;
        transition: all 0.15s;
      }
      .qf-pill:hover {
        background: #ebecf0;
      }
      .qf-pill.active {
        background: #0052cc;
        color: #fff;
        border-color: #0052cc;
      }
      .wip-badge {
        display: inline-flex;
        align-items: center;
        gap: 4px;
        padding: 2px 7px;
        border-radius: 4px;
        font-size: 11px;
        font-weight: 700;
      }
      .wip-ok {
        background: #ebecf0;
        color: #42526e;
      }
      .wip-exceeded-header {
        background: #ffebe6 !important;
        border-bottom: 3px solid #de350b !important;
      }
      .wip-under-header {
        background: #fffae6 !important;
        border-bottom: 3px solid #ff991f !important;
      }
      .kanban-column.wip-exceeded-col {
        border: 2px solid #de350b !important;
        background: #fff8f6 !important;
      }
      .kanban-column.wip-under-col {
        border: 2px solid #ff991f !important;
      }
      .wip-alert-banner {
        background: #de350b;
        color: #fff;
        font-size: 11px;
        font-weight: 700;
        padding: 4px 8px;
        text-align: center;
        letter-spacing: 0.02em;
      }
      .wip-under-banner {
        background: #ff991f;
        color: #172b4d;
        font-size: 11px;
        font-weight: 700;
        padding: 4px 8px;
        text-align: center;
      }
      .mapped-status-chips {
        display: flex;
        gap: 4px;
        flex-wrap: wrap;
        padding: 4px 10px 6px;
        background: rgba(9, 30, 66, 0.03);
        border-bottom: 1px solid #ebecf0;
      }
      .mapped-chip {
        font-size: 10px;
        padding: 1px 6px;
        border-radius: 4px;
        background: #dfe1e6;
        color: #172b4d;
        font-weight: 600;
      }
      .swimlane-section {
        border: 1px solid #dfe1e6;
        border-radius: 10px;
        background: #fff;
        margin-bottom: 14px;
        overflow: hidden;
      }
      .swimlane-header {
        display: flex;
        align-items: center;
        justify-content: space-between;
        padding: 10px 16px;
        background: #f4f5f7;
        border-bottom: 1px solid #dfe1e6;
        cursor: pointer;
        user-select: none;
      }
      .swimlane-title {
        display: flex;
        align-items: center;
        gap: 10px;
        font-weight: 700;
        font-size: 14px;
        color: #172b4d;
      }
      .issue-card {
        border-left: 4px solid transparent;
      }
      /* Agile Backlog Split-Pane Layout */
      .backlog-workspace {
        display: grid;
        grid-template-columns: 270px 1fr;
        gap: 16px;
        align-items: start;
      }
      .backlog-workspace.no-sidepanel {
        grid-template-columns: 1fr;
      }
      .side-panels-col {
        display: flex;
        flex-direction: column;
        gap: 14px;
      }
      .side-panel-card {
        background: #fff;
        border: 1px solid #dfe1e6;
        border-radius: 10px;
        padding: 12px;
      }
      .side-panel-item {
        padding: 9px 10px;
        border: 1px solid #ebecf0;
        border-radius: 7px;
        margin-top: 8px;
        cursor: pointer;
        transition: all 0.15s;
        background: #fafbfc;
      }
      .side-panel-item:hover,
      .side-panel-item.drag-target {
        border-color: #0052cc;
        background: #deebff;
      }
      .side-panel-item.selected {
        border-color: #0052cc;
        background: #e9f2ff;
        box-shadow: inset 3px 0 0 #0052cc;
      }
      .sprint-bucket {
        background: #fff;
        border: 1px solid #dfe1e6;
        border-radius: 10px;
        margin-bottom: 16px;
        overflow: hidden;
      }
      .sprint-bucket.active-sprint {
        border: 2px solid #0052cc;
      }
      .sprint-bucket.drag-over-bucket {
        outline: 2px dashed #0052cc;
        background: #f4f8ff;
      }
      .sprint-bucket-header {
        display: flex;
        justify-content: space-between;
        align-items: center;
        flex-wrap: wrap;
        gap: 10px;
        padding: 12px 16px;
        background: #f4f5f7;
        border-bottom: 1px solid #dfe1e6;
      }
      .backlog-row {
        display: flex;
        align-items: center;
        justify-content: space-between;
        gap: 10px;
        padding: 9px 14px;
        border-bottom: 1px solid #f0f1f4;
        background: #fff;
        transition: background 0.12s;
      }
      .backlog-row:hover {
        background: #f8fafd;
      }
      .backlog-row:last-child {
        border-bottom: none;
      }
      .backlog-row-left {
        display: flex;
        align-items: center;
        gap: 10px;
        min-width: 0;
        flex: 1;
      }
      .backlog-row-right {
        display: flex;
        align-items: center;
        gap: 8px;
        flex-shrink: 0;
      }
      .rank-btn {
        border: 1px solid #dfe1e6;
        background: #fff;
        border-radius: 4px;
        padding: 2px 6px;
        font-size: 11px;
        cursor: pointer;
        color: #42526e;
      }
      .rank-btn:hover {
        background: #ebecf0;
        color: #172b4d;
      }
      .sp-pill-group {
        display: inline-flex;
        align-items: center;
        gap: 4px;
      }
      .sp-counter {
        padding: 2px 7px;
        border-radius: 999px;
        font-size: 11px;
        font-weight: 700;
      }
      .sp-todo {
        background: #dfe1e6;
        color: #42526e;
      }
      .sp-inprog {
        background: #deebff;
        color: #0747a6;
      }
      .sp-done {
        background: #e3fcef;
        color: #006644;
      }
    `,
  ],
  template: `
    <div class="page-container board-shell">
      <!-- Top Multi-Board Header & View Mode Switcher -->
      <div class="board-top-bar">
        <div class="board-selector-group">
          <label for="active-board-select" style="font-size: 12px; font-weight: 700; color: #5e6c84; text-transform: uppercase;">
            Agile Board:
          </label>
          <select
            id="active-board-select"
            name="active_board_id"
            class="board-select"
            [ngModel]="activeBoard()?.id || 1"
            (ngModelChange)="selectBoard($event)"
          >
            @for (b of api.boards(); track b.id) {
              <option [ngValue]="b.id">
                {{ b.board_type === 'scrum' ? '🏃 [Scrum] ' : '📋 [Kanban] ' }}{{ b.name }}
              </option>
            }
          </select>

          @if (activeBoard(); as board) {
            <span class="board-type-tag" [class.scrum]="board.board_type === 'scrum'" [class.kanban]="board.board_type === 'kanban'">
              {{ board.board_type === 'scrum' ? 'Scrum Board' : 'Kanban Flow' }}
            </span>
            @if (board.filter_jql) {
              <span class="badge" title="Powered by JQL Filter" style="font-family: monospace; font-size: 11px;">
                🔍 JQL: {{ board.filter_jql }}
              </span>
            }
          }

          <div class="mode-switcher">
            <button
              type="button"
              class="mode-btn"
              [class.active]="viewMode() === 'board'"
              (click)="setViewMode('board')"
            >
              📋 {{ activeBoard()?.board_type === 'scrum' ? 'Active Sprint Board' : 'Kanban Board' }}
            </button>
            <button
              type="button"
              class="mode-btn"
              [class.active]="viewMode() === 'backlog'"
              (click)="setViewMode('backlog')"
            >
              🗂 Agile Backlog &amp; Sprint Planning
            </button>
          </div>
        </div>

        <div class="toolbar-actions" style="display: flex; gap: 8px; flex-wrap: wrap;">
          @if (viewMode() === 'board' && activeBoard()?.board_type === 'scrum' && activeSprintForBoard(); as actSp) {
            <button type="button" class="btn btn-primary btn-sm" (click)="openCompleteSprintModal(actSp)">
              ✔ Complete Sprint ({{ actSp.name }})
            </button>
          }
          <button type="button" class="btn btn-secondary btn-sm" (click)="openCreateBoardModal()">
            + New Board
          </button>
          <button type="button" class="btn btn-secondary btn-sm" (click)="openBoardConfigModal()">
            ⚙ Configure Board
          </button>
          <a routerLink="/search" class="btn btn-secondary btn-sm">🔍 JQL Search</a>
          <a routerLink="/create" class="btn btn-primary btn-sm">+ Create Issue</a>
        </div>
      </div>

      <!-- Quick Filters, Swimlane Strategy & Search Bar -->
      <div class="quick-filters-bar">
        <div class="qf-pills">
          <span style="font-size: 11px; font-weight: 700; color: #5e6c84; text-transform: uppercase; margin-right: 4px;">
            ⚡ Quick Filters:
          </span>
          @if (activeBoard(); as board) {
            @for (qf of board.quick_filters; track qf.id) {
              <button
                type="button"
                class="qf-pill"
                [class.active]="isQuickFilterActive(qf.id)"
                [title]="qf.description + ' (' + qf.jql_query + ')'"
                (click)="toggleQuickFilter(qf.id)"
              >
                {{ qf.name }}
              </button>
            }
          }
          @if (activeQuickFilterIds().length > 0 || selectedEpicFilter() || selectedVersionFilter()) {
            <button
              type="button"
              class="btn btn-ghost btn-sm"
              style="font-size: 11px; padding: 3px 8px;"
              (click)="clearAllQuickFilters()"
            >
              ✕ Clear Active Filters
            </button>
          }
        </div>

        <div style="display: flex; align-items: center; gap: 8px; flex-wrap: wrap;">
          <input
            type="text"
            id="board-search"
            name="board_search"
            autocomplete="off"
            placeholder="Filter key, summary, labels..."
            style="padding: 5px 10px; font-size: 12px; border: 1px solid #dfe1e6; border-radius: 6px; width: 190px;"
            [ngModel]="searchQuery()"
            (ngModelChange)="searchQuery.set($event)"
          />

          <select
            name="board_assignee_filter"
            style="padding: 5px 8px; font-size: 12px; border: 1px solid #dfe1e6; border-radius: 6px;"
            [ngModel]="selectedAssignee()"
            (ngModelChange)="selectedAssignee.set($event)"
          >
            <option value="">All Assignees</option>
            @for (u of api.users(); track u.id) {
              <option [value]="u.id">{{ u.display_name }}</option>
            }
          </select>

          @if (viewMode() === 'board') {
            <label style="font-size: 12px; color: #5e6c84; font-weight: 600;">Swimlanes:</label>
            <select
              name="board_swimlane_strategy"
              style="padding: 5px 8px; font-size: 12px; border: 1px solid #dfe1e6; border-radius: 6px; font-weight: 600;"
              [ngModel]="currentSwimlaneStrategy()"
              (ngModelChange)="changeSwimlaneStrategy($event)"
            >
              <option value="none">None (Columns Only)</option>
              <option value="epic">By Epic</option>
              <option value="assignee">By Assignee</option>
              <option value="priority">By Priority</option>
              <option value="story">By Story (Subtasks)</option>
              <option value="custom_jql">By Custom JQL Swimlanes</option>
            </select>

            @if (activeBoard()?.board_type === 'scrum') {
              <select
                name="board_sprint_filter"
                style="padding: 5px 8px; font-size: 12px; border: 1px solid #dfe1e6; border-radius: 6px;"
                [ngModel]="selectedSprint()"
                (ngModelChange)="selectedSprint.set($event)"
              >
                <option value="">All Sprints</option>
                @for (s of api.sprints(); track s.id) {
                  <option [value]="s.id">
                    {{ s.is_current ? '★ Active: ' + s.name : s.name + ' (' + s.status + ')' }}
                  </option>
                }
              </select>
            }
          } @else {
            <button
              type="button"
              class="btn btn-secondary btn-sm"
              [class.active]="showSidePanels()"
              (click)="showSidePanels.set(!showSidePanels())"
            >
              {{ showSidePanels() ? '◀ Hide Epics & Versions Panels' : '▶ Show Epics & Versions Panels' }}
            </button>
          }
        </div>
      </div>

      @if (toastMsg()) {
        <div class="toast-banner">{{ toastMsg() }}</div>
      }

      <!-- ================================================================== -->
      <!-- VIEW MODE 1: ACTIVE SCRUM / KANBAN BOARD                           -->
      <!-- ================================================================== -->
      @if (viewMode() === 'board' && activeBoard(); as board) {
        <!-- Column Headers & WIP Status Overview when Swimlanes are active -->
        @if (currentSwimlaneStrategy() === 'none') {
          <div class="kanban-board">
            @for (col of visibleColumns(); track col.id) {
              @let colItems = itemsForColumn(col, boardFilteredItems());
              @let wipState = getColumnWipState(col, colItems.length);
              <div
                class="kanban-column"
                [class.drag-over]="dragOverColId() === col.id"
                [class.wip-exceeded-col]="wipState === 'exceeded'"
                [class.wip-under-col]="wipState === 'under'"
                (dragover)="onColDragOver($event, col)"
                (dragleave)="onColDragLeave(col)"
                (drop)="onColDrop($event, col)"
              >
                <div
                  class="column-header"
                  [class.wip-exceeded-header]="wipState === 'exceeded'"
                  [class.wip-under-header]="wipState === 'under'"
                  [style.border-top]="'3px solid ' + col.color"
                >
                  <div class="column-title">
                    <span
                      style="width: 9px; height: 9px; border-radius: 50%; display: inline-block;"
                      [style.background]="col.color"
                    ></span>
                    <span>{{ col.name }}</span>
                  </div>
                  <div style="display: flex; align-items: center; gap: 6px;">
                    @if (col.min_wip > 0 || col.max_wip > 0) {
                      <span class="wip-badge wip-ok" title="Configured WIP Limits">
                        WIP {{ col.min_wip > 0 ? 'min:' + col.min_wip + ' ' : '' }}{{ col.max_wip > 0 ? 'max:' + col.max_wip : '' }}
                      </span>
                    }
                    <span class="column-count">{{ colItems.length }}</span>
                  </div>
                </div>

                @if (wipState === 'exceeded') {
                  <div class="wip-alert-banner">
                    ⚠ MAX WIP LIMIT EXCEEDED ({{ colItems.length }} / {{ col.max_wip }})
                  </div>
                } @else if (wipState === 'under') {
                  <div class="wip-under-banner">
                    ⚠ BELOW MIN WIP ({{ colItems.length }} / {{ col.min_wip }})
                  </div>
                }

                @if (col.statuses.length > 1) {
                  <div class="mapped-status-chips" title="Multiple workflow statuses mapped to this column">
                    @for (st of col.statuses; track st) {
                      <span class="mapped-chip">{{ st }}</span>
                    }
                  </div>
                }

                <div class="column-body">
                  @for (item of colItems; track item.id) {
                    <ng-container *ngTemplateOutlet="issueCardTpl; context: { $implicit: item, col: col }" />
                  } @empty {
                    <div class="empty-column-drop">
                      Drop item here → <strong>{{ col.name }}</strong>
                      <div style="font-size: 11px; color: #6b778c; margin-top: 2px;">
                        ({{ col.statuses.join(' / ') }})
                      </div>
                    </div>
                  }
                </div>
              </div>
            }
          </div>
        } @else {
          <!-- SWIMLANE GROUPED BOARD VIEW -->
          @for (lane of swimlaneRows(); track lane.key) {
            <div class="swimlane-section">
              <div class="swimlane-header" (click)="toggleSwimlaneCollapsed(lane.key)">
                <div class="swimlane-title">
                  <span>{{ isSwimlaneCollapsed(lane.key) ? '▶' : '▼' }}</span>
                  <span>{{ lane.title }}</span>
                  @if (lane.subtitle) {
                    <span class="text-muted" style="font-weight: 400; font-size: 12px;">— {{ lane.subtitle }}</span>
                  }
                </div>
                <div style="display: flex; align-items: center; gap: 8px;">
                  <span class="badge">{{ lane.items.length }} issues</span>
                  <span class="points-badge">{{ lane.totalPoints }} SP</span>
                </div>
              </div>

              @if (!isSwimlaneCollapsed(lane.key)) {
                <div class="kanban-board" style="padding: 12px;">
                  @for (col of visibleColumns(); track col.id) {
                    @let laneColItems = itemsForColumn(col, lane.items);
                    @let globalColCount = itemsForColumn(col, boardFilteredItems()).length;
                    @let wipState = getColumnWipState(col, globalColCount);
                    <div
                      class="kanban-column"
                      [class.drag-over]="dragOverColId() === col.id"
                      [class.wip-exceeded-col]="wipState === 'exceeded'"
                      (dragover)="onColDragOver($event, col)"
                      (dragleave)="onColDragLeave(col)"
                      (drop)="onColDrop($event, col)"
                    >
                      <div
                        class="column-header"
                        [class.wip-exceeded-header]="wipState === 'exceeded'"
                        [style.border-top]="'3px solid ' + col.color"
                      >
                        <div class="column-title">
                          <span>{{ col.name }}</span>
                        </div>
                        <span class="column-count">{{ laneColItems.length }}</span>
                      </div>

                      <div class="column-body">
                        @for (item of laneColItems; track item.id) {
                          <ng-container *ngTemplateOutlet="issueCardTpl; context: { $implicit: item, col: col }" />
                        } @empty {
                          <div class="empty-column-drop" style="min-height: 55px; padding: 10px;">
                            Drop to <strong>{{ col.name }}</strong>
                          </div>
                        }
                      </div>
                    </div>
                  }
                </div>
              }
            </div>
          }
        }
      }

      <!-- Reusable Issue Card Template -->
      <ng-template #issueCardTpl let-item let-col="col">
        <div
          class="issue-card"
          draggable="true"
          [style.border-left-color]="getCardColorStrip(item)"
          [class.dragging]="draggedItem()?.id === item.id"
          (dragstart)="onDragStart($event, item)"
          (dragend)="onDragEnd()"
        >
          <div class="card-top">
            <a [routerLink]="['/', item.key]" class="issue-key">{{ item.key }}</a>
            <div style="display: flex; align-items: center; gap: 4px;">
              @if (col?.statuses?.length > 1) {
                <span class="state-pill" [attr.data-state]="item.status" style="font-size: 10px; padding: 1px 6px;">
                  {{ item.status }}
                </span>
              }
              <span class="priority-badge" [attr.data-priority]="item.priority">
                {{ item.priority }}
              </span>
            </div>
          </div>

          <div class="card-top">
            <span class="type-badge" [attr.data-type]="item.type">{{ item.type }}</span>
            <span class="cat-badge" [attr.data-cat]="item.category">{{ item.category }}</span>
          </div>

          @if (item.parent_key) {
            <div class="parent-chip">↳ {{ item.parent_key }}: {{ item.parent_title }}</div>
          }

          <a [routerLink]="['/', item.key]" class="card-title">{{ item.title }}</a>

          @if (shouldShowCardField('components') || shouldShowCardField('fix_versions') || shouldShowCardField('labels')) {
            <div class="card-labels">
              @if (item.resolution_name) {
                <span class="label-chip" style="background: #dcfce7; color: #166534;">✔ {{ item.resolution_name }}</span>
              }
              @if (shouldShowCardField('components')) {
                @for (comp of item.components || []; track comp.id) {
                  <span class="label-chip" style="background: #e0f2fe; color: #0369a1;">🧩 {{ comp.name }}</span>
                }
              }
              @if (shouldShowCardField('fix_versions')) {
                @for (fv of item.fix_versions || []; track fv.id) {
                  <span class="label-chip" style="background: #f3e8ff; color: #6b21a8;">🚀 {{ fv.name }}</span>
                }
              }
              @if (shouldShowCardField('labels')) {
                @for (lbl of splitLabels(item.labels); track lbl) {
                  <span class="label-chip">{{ lbl }}</span>
                }
              }
            </div>
          }

          @if (shouldShowCardField('due_date') && item.due_date) {
            <div style="font-size: 11px; color: #5e6c84; margin-top: 4px;">
              📅 Due: <strong>{{ item.due_date }}</strong>
            </div>
          }

          <div class="card-progress">
            <div class="progress-bar">
              <div class="progress-fill" [style.width.%]="item.progress"></div>
            </div>
            <span class="progress-text">{{ item.progress }}%</span>
          </div>

          <div class="card-footer">
            <div class="card-meta">
              @if (shouldShowCardField('story_points') && item.story_points > 0) {
                <span class="points-badge" title="Story Points">{{ item.story_points }} SP</span>
              }
              @if (item.time_spent_formatted || item.remaining_estimate_formatted || item.original_estimate_formatted) {
                <span
                  class="points-badge"
                  style="background: #eef2ff; color: #4338ca; border-color: #c7d2fe;"
                  [title]="'Time Spent: ' + (item.time_spent_formatted || '0m') + ' / Remaining: ' + (item.remaining_estimate_formatted || '0m')"
                >
                  ⏱ {{ item.time_spent_formatted || '0m' }}/{{ item.remaining_estimate_formatted || item.original_estimate_formatted || '0m' }}
                </span>
              }
              @if (item.tshirt_size) {
                <span class="label-chip" style="background: #fdf2f8; color: #be185d;" title="T-Shirt Size">
                  👕 {{ item.tshirt_size }}
                </span>
              }
              @if (item.subtask_count > 0) {
                <span class="meta-icon" title="Subtasks">⧉ {{ item.subtask_count }}</span>
              }
              @if (item.attachment_count > 0) {
                <span class="meta-icon" title="Blob Attachments">📎 {{ item.attachment_count }}</span>
              }
              @if (item.comment_count > 0) {
                <span class="meta-icon" title="Comments">💬 {{ item.comment_count }}</span>
              }
            </div>

            @if (item.assignee_name) {
              <div
                class="avatar-circle"
                [style.background]="item.assignee_color"
                [title]="'Assigned to ' + item.assignee_name"
              >
                {{ initials(item.assignee_name) }}
              </div>
            } @else {
              <div class="avatar-circle unassigned" title="Unassigned">?</div>
            }
          </div>
        </div>
      </ng-template>

      <!-- ================================================================== -->
      <!-- VIEW MODE 2: DEDICATED SPLIT-PANE AGILE BACKLOG & SPRINT PLANNING  -->
      <!-- ================================================================== -->
      @if (viewMode() === 'backlog') {
        <div class="backlog-workspace" [class.no-sidepanel]="!showSidePanels()">
          <!-- Left Side Panels: Epics & Versions (Drop Targets!) -->
          @if (showSidePanels()) {
            <div class="side-panels-col">
              <!-- EPICS PANEL -->
              <div class="side-panel-card">
                <div style="display: flex; justify-content: space-between; align-items: center;">
                  <h4 style="margin: 0; font-size: 13px; text-transform: uppercase; color: #5e6c84;">
                    ⚡ Epics ({{ epicItems().length }})
                  </h4>
                  @if (selectedEpicFilter()) {
                    <button
                      type="button"
                      class="btn btn-ghost btn-sm"
                      style="padding: 2px 6px; font-size: 11px;"
                      (click)="selectedEpicFilter.set(null)"
                    >
                      All
                    </button>
                  }
                </div>
                <small class="text-muted" style="display: block; margin-top: 2px; font-size: 11px;">
                  Click to filter or drag an issue onto an Epic to link it.
                </small>

                @for (ep of epicItems(); track ep.id) {
                  @let epStats = epicRollup(ep.id);
                  <div
                    class="side-panel-item"
                    [class.selected]="selectedEpicFilter() === ep.id"
                    [class.drag-target]="dragOverEpicId() === ep.id"
                    (click)="toggleEpicFilter(ep.id)"
                    (dragover)="onEpicDragOver($event, ep.id)"
                    (dragleave)="dragOverEpicId.set(null)"
                    (drop)="onEpicDrop($event, ep)"
                  >
                    <div style="display: flex; justify-content: space-between; align-items: center;">
                      <span class="issue-key" style="font-size: 11px;">{{ ep.key }}</span>
                      <span class="badge" style="font-size: 10px;">
                        {{ epStats.done }}/{{ epStats.total }} done · {{ epStats.points }} SP
                      </span>
                    </div>
                    <div style="font-weight: 600; font-size: 12px; color: #172b4d; margin: 4px 0;">
                      {{ ep.title }}
                    </div>
                    <div class="progress-bar" style="height: 5px;">
                      <div class="progress-fill" [style.width.%]="epStats.pct"></div>
                    </div>
                  </div>
                }
              </div>

              <!-- VERSIONS PANEL -->
              <div class="side-panel-card">
                <div style="display: flex; justify-content: space-between; align-items: center;">
                  <h4 style="margin: 0; font-size: 13px; text-transform: uppercase; color: #5e6c84;">
                    🚀 Versions ({{ api.versions().length }})
                  </h4>
                  @if (selectedVersionFilter()) {
                    <button
                      type="button"
                      class="btn btn-ghost btn-sm"
                      style="padding: 2px 6px; font-size: 11px;"
                      (click)="selectedVersionFilter.set(null)"
                    >
                      All
                    </button>
                  }
                </div>
                <small class="text-muted" style="display: block; margin-top: 2px; font-size: 11px;">
                  Click to filter or drag an issue onto a Version to assign Fix Version.
                </small>

                @for (ver of api.versions(); track ver.id) {
                  <div
                    class="side-panel-item"
                    [class.selected]="selectedVersionFilter() === ver.id"
                    [class.drag-target]="dragOverVersionId() === ver.id"
                    (click)="toggleVersionFilter(ver.id)"
                    (dragover)="onVersionDragOver($event, ver.id)"
                    (dragleave)="dragOverVersionId.set(null)"
                    (drop)="onVersionDrop($event, ver)"
                  >
                    <div style="display: flex; justify-content: space-between; align-items: center;">
                      <strong style="font-size: 12px; color: #172b4d;">{{ ver.name }}</strong>
                      <span class="badge" style="font-size: 10px;">{{ ver.status }}</span>
                    </div>
                    <div style="font-size: 11px; color: #5e6c84; margin-top: 3px;">
                      📅 Release: {{ ver.release_date || 'TBD' }} · {{ verIssueCount(ver.id) }} issues
                    </div>
                  </div>
                }
              </div>
            </div>
          }

          <!-- Main Split-Pane: Active/Planned Sprints (Top) + Backlog (Bottom) -->
          <div>
            <!-- Sprints Header Actions -->
            <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px;">
              <div>
                <h3 style="margin: 0; font-size: 16px;">Active &amp; Upcoming Sprints</h3>
                <small class="text-muted">
                  Drag work items between Sprints and Backlog, or use ▲ / ▼ to rank priority order.
                </small>
              </div>
              <button type="button" class="btn btn-secondary btn-sm" (click)="quickCreatePlannedSprint()">
                + Create Sprint
              </button>
            </div>

            <!-- SPRINT BUCKETS -->
            @for (sp of openAndPlannedSprints(); track sp.id) {
              @let spItems = itemsForSprintBucket(sp.id);
              @let spRollup = computeBucketRollup(spItems);
              <div
                class="sprint-bucket"
                [class.active-sprint]="sp.status === 'active' || sp.is_current"
                [class.drag-over-bucket]="dragOverBucketKey() === 'sprint_' + sp.id"
                (dragover)="onBucketDragOver($event, 'sprint_' + sp.id)"
                (dragleave)="dragOverBucketKey.set(null)"
                (drop)="onBucketDrop($event, sp.id)"
              >
                <div class="sprint-bucket-header">
                  <div style="display: flex; align-items: center; gap: 10px; flex-wrap: wrap;">
                    <button
                      type="button"
                      class="btn btn-ghost btn-sm"
                      style="padding: 2px 6px;"
                      (click)="toggleBucketCollapse('sprint_' + sp.id)"
                    >
                      {{ isBucketCollapsed('sprint_' + sp.id) ? '▶' : '▼' }}
                    </button>
                    <strong style="font-size: 14px; color: #172b4d;">{{ sp.name }}</strong>
                    @if (sp.status === 'active' || sp.is_current) {
                      <span class="current-pill">★ ACTIVE SPRINT</span>
                    } @else {
                      <span class="badge">PLANNED</span>
                    }
                    <span class="text-muted" style="font-size: 12px;">
                      {{ sp.start_date || 'TBD' }} → {{ sp.end_date || 'TBD' }} · {{ spItems.length }} issues
                    </span>
                    @if (sp.goal) {
                      <span class="text-muted" style="font-size: 12px; font-style: italic;">
                        🎯 "{{ sp.goal }}"
                      </span>
                    }
                  </div>

                  <div style="display: flex; align-items: center; gap: 10px;">
                    <div class="sp-pill-group" title="Story Points: To Do / In Progress / Done">
                      <span class="sp-counter sp-todo" title="To Do Story Points">{{ spRollup.todoPoints }} SP</span>
                      <span class="sp-counter sp-inprog" title="In Progress Story Points">{{ spRollup.inProgPoints }} SP</span>
                      <span class="sp-counter sp-done" title="Completed Story Points">{{ spRollup.donePoints }} SP</span>
                    </div>

                    @if (sp.status === 'planned' && !sp.is_current) {
                      <button
                        type="button"
                        class="btn btn-primary btn-sm"
                        [disabled]="spItems.length === 0"
                        (click)="openStartSprintModal(sp)"
                      >
                        ▶ Start Sprint
                      </button>
                    }
                    @if (sp.status === 'active' || sp.is_current) {
                      <button
                        type="button"
                        class="btn btn-secondary btn-sm"
                        (click)="openCompleteSprintModal(sp)"
                      >
                        ✔ Complete Sprint
                      </button>
                    }
                  </div>
                </div>

                @if (!isBucketCollapsed('sprint_' + sp.id)) {
                  <div>
                    @for (item of spItems; track item.id; let idx = $index) {
                      <ng-container
                        *ngTemplateOutlet="
                          backlogRowTpl;
                          context: { $implicit: item, bucketItems: spItems, idx: idx, sprintId: sp.id }
                        "
                      />
                    } @empty {
                      <div style="padding: 20px; text-align: center; color: #6b778c; font-size: 13px;">
                        Plan your sprint by dragging work items from the Backlog below into <strong>{{ sp.name }}</strong>.
                      </div>
                    }
                  </div>
                }
              </div>
            }

            <!-- BOTTOM SECTION: UNASSIGNED BACKLOG BUCKET -->
            @let blItems = backlogUnassignedItems();
            @let blRollup = computeBucketRollup(blItems);
            <div
              class="sprint-bucket"
              [class.drag-over-bucket]="dragOverBucketKey() === 'backlog'"
              (dragover)="onBucketDragOver($event, 'backlog')"
              (dragleave)="dragOverBucketKey.set(null)"
              (drop)="onBucketDrop($event, null)"
            >
              <div class="sprint-bucket-header">
                <div style="display: flex; align-items: center; gap: 10px;">
                  <strong style="font-size: 14px; color: #172b4d;">🗂 Backlog (Unassigned to Sprint)</strong>
                  <span class="badge">{{ blItems.length }} issues</span>
                </div>
                <div class="sp-pill-group">
                  <span class="sp-counter sp-todo">{{ blRollup.todoPoints }} SP</span>
                  <span class="sp-counter sp-inprog">{{ blRollup.inProgPoints }} SP</span>
                  <span class="sp-counter sp-done">{{ blRollup.donePoints }} SP</span>
                </div>
              </div>

              <div>
                @for (item of blItems; track item.id; let idx = $index) {
                  <ng-container
                    *ngTemplateOutlet="
                      backlogRowTpl;
                      context: { $implicit: item, bucketItems: blItems, idx: idx, sprintId: null }
                    "
                  />
                } @empty {
                  <div style="padding: 22px; text-align: center; color: #6b778c; font-size: 13px;">
                    Backlog is empty — drag items here to remove them from a sprint, or create a new backlog issue below.
                  </div>
                }
              </div>

              <!-- Quick Inline Backlog Item Creator -->
              <div style="padding: 10px 14px; background: #fafbfc; border-top: 1px solid #dfe1e6; display: flex; gap: 8px; align-items: center;">
                <select
                  name="quick_backlog_type"
                  style="width: 110px; padding: 6px 8px; font-size: 12px;"
                  [(ngModel)]="quickBacklogDraft.type"
                >
                  <option value="Story">Story</option>
                  <option value="Task">Task</option>
                  <option value="Epic">Epic</option>
                </select>
                <input
                  type="text"
                  name="quick_backlog_title"
                  placeholder="+ What needs to be done? (Press Enter to add to Backlog)"
                  style="flex: 1; padding: 6px 10px; font-size: 13px;"
                  [(ngModel)]="quickBacklogDraft.title"
                  (keydown.enter)="createQuickBacklogItem()"
                />
                <input
                  type="number"
                  name="quick_backlog_sp"
                  min="0"
                  max="100"
                  placeholder="SP"
                  style="width: 70px; padding: 6px 8px; font-size: 12px;"
                  [(ngModel)]="quickBacklogDraft.story_points"
                />
                <button type="button" class="btn btn-primary btn-sm" (click)="createQuickBacklogItem()">
                  + Add to Backlog
                </button>
              </div>
            </div>

            <!-- Closed Sprints Velocity History -->
            @if (closedSprints().length > 0) {
              <div class="card-panel" style="margin-top: 14px;">
                <h4 style="margin: 0 0 8px 0; font-size: 13px; color: #5e6c84; text-transform: uppercase;">
                  📊 Completed Sprints Velocity History ({{ closedSprints().length }})
                </h4>
                <div style="display: flex; gap: 10px; flex-wrap: wrap;">
                  @for (csp of closedSprints(); track csp.id) {
                    <div style="border: 1px solid #dfe1e6; border-radius: 8px; padding: 10px 14px; background: #fafbfc; min-width: 240px;">
                      <div style="display: flex; justify-content: space-between; align-items: center;">
                        <strong>{{ csp.name }}</strong>
                        <span class="badge" style="background: #e3fcef; color: #006644;">CLOSED</span>
                      </div>
                      <div style="font-size: 12px; color: #42526e; margin-top: 6px;">
                        Committed: <strong>{{ csp.committed_points || 0 }} SP</strong> ·
                        Completed: <strong style="color: #006644;">{{ csp.completed_points || 0 }} SP</strong>
                      </div>
                      <div style="font-size: 11px; color: #6b778c; margin-top: 2px;">
                        ✔ {{ csp.completed_items_count || 0 }} done · ↪ {{ csp.incomplete_items_count || 0 }} rolled over
                      </div>
                    </div>
                  }
                </div>
              </div>
            }
          </div>
        </div>
      }

      <!-- Reusable Backlog Row Template -->
      <ng-template #backlogRowTpl let-item let-bucketItems="bucketItems" let-idx="idx" let-sprintId="sprintId">
        <div
          class="backlog-row"
          draggable="true"
          (dragstart)="onDragStart($event, item)"
          (dragend)="onDragEnd()"
        >
          <div class="backlog-row-left">
            <span style="color: #a5adba; cursor: grab;" title="Drag to reorder or move between sprints">⋮⋮</span>
            <span class="type-badge" [attr.data-type]="item.type">{{ item.type }}</span>
            <a [routerLink]="['/', item.key]" class="issue-key">{{ item.key }}</a>
            <a [routerLink]="['/', item.key]" style="color: #172b4d; font-weight: 500; text-decoration: none; overflow: hidden; text-overflow: ellipsis; white-space: nowrap;">
              {{ item.title }}
            </a>
            @if (item.parent_key) {
              <span class="label-chip" style="background: #eae6ff; color: #403294; font-weight: 600;">
                ⚡ {{ item.parent_key }}
              </span>
            }
            @for (fv of item.fix_versions || []; track fv.id) {
              <span class="label-chip" style="background: #f3e8ff; color: #6b21a8;">
                🚀 {{ fv.name }}
              </span>
            }
          </div>

          <div class="backlog-row-right">
            <span class="priority-badge" [attr.data-priority]="item.priority">{{ item.priority }}</span>
            <span class="state-pill" [attr.data-state]="item.status">{{ item.status }}</span>
            <span class="points-badge" title="Story Points">{{ item.story_points || 0 }} SP</span>
            @if (item.remaining_estimate_formatted || item.original_estimate_formatted) {
              <span class="points-badge" style="background: #f0fdf4; color: #166534; border-color: #bbf7d0;" title="Remaining Estimate">
                ⏱ {{ item.remaining_estimate_formatted || item.original_estimate_formatted }}
              </span>
            }
            @if (item.tshirt_size) {
              <span class="label-chip" style="background: #fdf2f8; color: #be185d; padding: 2px 6px; font-size: 11px;">
                👕 {{ item.tshirt_size }}
              </span>
            }

            @if (item.assignee_name) {
              <div
                class="avatar-circle"
                style="width: 24px; height: 24px; font-size: 10px;"
                [style.background]="item.assignee_color"
                [title]="item.assignee_name"
              >
                {{ initials(item.assignee_name) }}
              </div>
            } @else {
              <div class="avatar-circle unassigned" style="width: 24px; height: 24px; font-size: 10px;">?</div>
            }

            <!-- Quick Sprint Mover Dropdown -->
            <select
              [name]="'move_sp_' + item.id"
              style="padding: 2px 6px; font-size: 11px; border: 1px solid #dfe1e6; border-radius: 4px; width: 115px;"
              [ngModel]="item.sprint_id || 0"
              (ngModelChange)="moveItemToSprint(item, $event)"
              title="Move to Sprint or Backlog"
            >
              <option [ngValue]="0">🗂 Backlog</option>
              @for (s of openAndPlannedSprints(); track s.id) {
                <option [ngValue]="s.id">{{ s.name }}</option>
              }
            </select>

            <button
              type="button"
              class="rank-btn"
              [disabled]="idx === 0"
              title="Rank Up"
              (click)="rankItemInBucket(bucketItems, idx, -1)"
            >
              ▲
            </button>
            <button
              type="button"
              class="rank-btn"
              [disabled]="idx === bucketItems.length - 1"
              title="Rank Down"
              (click)="rankItemInBucket(bucketItems, idx, 1)"
            >
              ▼
            </button>
          </div>
        </div>
      </ng-template>

      <!-- ================================================================== -->
      <!-- MODAL 1: START SPRINT DIALOG                                       -->
      <!-- ================================================================== -->
      @if (startSprintModal(); as sp) {
        <div
          style="position: fixed; inset: 0; background: rgba(15, 23, 42, 0.55); z-index: 1250; display: flex; align-items: center; justify-content: center; padding: 16px;"
        >
          <div class="card-panel" style="width: 100%; max-width: 520px; box-shadow: 0 20px 45px rgba(0,0,0,0.25);">
            <div class="section-header">
              <div>
                <h3>▶ Start Sprint: {{ sp.name }}</h3>
                <small class="text-muted">
                  {{ itemsForSprintBucket(sp.id).length }} issues ({{ computeBucketRollup(itemsForSprintBucket(sp.id)).totalPoints }} Story Points) will be committed to this sprint.
                </small>
              </div>
              <button type="button" class="btn btn-ghost btn-sm" (click)="startSprintModal.set(null)">✕</button>
            </div>

            <div class="form-group" style="margin-top: 12px;">
              <label>Sprint Name *</label>
              <input type="text" name="start_sp_name" [(ngModel)]="startSprintDraft.name" />
            </div>

            <div class="grid-3">
              <div class="form-group">
                <label>Duration</label>
                <select
                  name="start_sp_dur"
                  [(ngModel)]="startSprintDraft.duration_weeks"
                  (ngModelChange)="applyStartSprintDuration($event)"
                >
                  <option [ngValue]="1">1 week</option>
                  <option [ngValue]="2">2 weeks</option>
                  <option [ngValue]="3">3 weeks</option>
                  <option [ngValue]="4">4 weeks</option>
                  <option [ngValue]="0">Custom</option>
                </select>
              </div>
              <div class="form-group">
                <label>Start Date</label>
                <input type="date" name="start_sp_start" [(ngModel)]="startSprintDraft.start_date" />
              </div>
              <div class="form-group">
                <label>End Date</label>
                <input type="date" name="start_sp_end" [(ngModel)]="startSprintDraft.end_date" />
              </div>
            </div>

            <div class="form-group">
              <label>Sprint Goal</label>
              <textarea
                rows="3"
                name="start_sp_goal"
                [(ngModel)]="startSprintDraft.goal"
                placeholder="Describe what the team aims to achieve in this sprint..."
              ></textarea>
            </div>

            <div style="display: flex; justify-content: flex-end; gap: 8px; margin-top: 14px;">
              <button type="button" class="btn btn-secondary" (click)="startSprintModal.set(null)">Cancel</button>
              <button type="button" class="btn btn-primary" (click)="submitStartSprint()">
                ▶ Start Sprint
              </button>
            </div>
          </div>
        </div>
      }

      <!-- ================================================================== -->
      <!-- MODAL 2: COMPLETE SPRINT DIALOG (WITH INCOMPLETE ISSUE ROLLOVER)   -->
      <!-- ================================================================== -->
      @if (completeSprintModal(); as sp) {
        @let spItems = itemsForSprintBucket(sp.id);
        @let doneItems = getDoneItems(spItems);
        @let openItems = getIncompleteItems(spItems);
        <div
          style="position: fixed; inset: 0; background: rgba(15, 23, 42, 0.55); z-index: 1250; display: flex; align-items: center; justify-content: center; padding: 16px;"
        >
          <div class="card-panel" style="width: 100%; max-width: 560px; box-shadow: 0 20px 45px rgba(0,0,0,0.25);">
            <div class="section-header">
              <div>
                <h3>🏆 Complete Sprint: {{ sp.name }}</h3>
                <small class="text-muted">Review sprint velocity and choose where to roll over incomplete issues.</small>
              </div>
              <button type="button" class="btn btn-ghost btn-sm" (click)="completeSprintModal.set(null)">✕</button>
            </div>

            <div class="grid-2" style="margin: 14px 0;">
              <div style="background: #e3fcef; border: 1px solid #abf5d1; border-radius: 8px; padding: 12px;">
                <div style="font-size: 11px; font-weight: 700; color: #006644; text-transform: uppercase;">
                  ✔ Completed Issues
                </div>
                <div style="font-size: 22px; font-weight: 800; color: #006644; margin-top: 4px;">
                  {{ doneItems.length }} issues ({{ sumStoryPoints(doneItems) }} SP)
                </div>
              </div>

              <div style="background: #fffae6; border: 1px solid #ffe380; border-radius: 8px; padding: 12px;">
                <div style="font-size: 11px; font-weight: 700; color: #974f0c; text-transform: uppercase;">
                  ↪ Incomplete Issues
                </div>
                <div style="font-size: 22px; font-weight: 800; color: #974f0c; margin-top: 4px;">
                  {{ openItems.length }} issues ({{ sumStoryPoints(openItems) }} SP)
                </div>
              </div>
            </div>

            @if (openItems.length > 0) {
              <div class="form-group">
                <label>Move {{ openItems.length }} incomplete issues to:</label>
                <select name="complete_sp_rollover" [(ngModel)]="completeSprintDraft.rollover_choice">
                  <option value="backlog">🗂 Backlog</option>
                  @for (pl of otherPlannedSprints(sp.id); track pl.id) {
                    <option [value]="'sprint:' + pl.id">🏃 Planned Sprint: {{ pl.name }}</option>
                  }
                  <option value="new_sprint">✨ Create a New Follow-up Sprint</option>
                </select>
              </div>

              @if (completeSprintDraft.rollover_choice === 'new_sprint') {
                <div class="form-group">
                  <label>New Follow-up Sprint Name</label>
                  <input
                    type="text"
                    name="complete_sp_new_name"
                    [(ngModel)]="completeSprintDraft.next_sprint_name"
                    placeholder="e.g., Sprint 3 - Follow-up & Polish"
                  />
                </div>
              }
            }

            <div style="display: flex; justify-content: flex-end; gap: 8px; margin-top: 16px;">
              <button type="button" class="btn btn-secondary" (click)="completeSprintModal.set(null)">Cancel</button>
              <button type="button" class="btn btn-primary" (click)="submitCompleteSprint()">
                ✔ Complete Sprint &amp; Record Velocity
              </button>
            </div>
          </div>
        </div>
      }

      <!-- ================================================================== -->
      <!-- MODAL 3: CREATE NEW BOARD DIALOG                                   -->
      <!-- ================================================================== -->
      @if (createBoardModal()) {
        <div
          style="position: fixed; inset: 0; background: rgba(15, 23, 42, 0.55); z-index: 1250; display: flex; align-items: center; justify-content: center; padding: 16px;"
        >
          <div class="card-panel" style="width: 100%; max-width: 520px; box-shadow: 0 20px 45px rgba(0,0,0,0.25);">
            <div class="section-header">
              <h3>+ Create Scrum or Kanban Board</h3>
              <button type="button" class="btn btn-ghost btn-sm" (click)="createBoardModal.set(false)">✕</button>
            </div>

            <div class="form-group" style="margin-top: 10px;">
              <label>Board Name *</label>
              <input
                type="text"
                name="new_board_name"
                [(ngModel)]="newBoardDraft.name"
                placeholder="e.g., Platform Security Kanban Board"
              />
            </div>

            <div class="grid-2">
              <div class="form-group">
                <label>Board Type</label>
                <select name="new_board_type" [(ngModel)]="newBoardDraft.board_type">
                  <option value="scrum">🏃 Scrum Board (Sprints &amp; Velocity)</option>
                  <option value="kanban">📋 Kanban Board (Continuous Flow &amp; WIP Limits)</option>
                </select>
              </div>
              <div class="form-group">
                <label>Default Swimlane Strategy</label>
                <select name="new_board_swimlane" [(ngModel)]="newBoardDraft.swimlane_strategy">
                  <option value="none">None</option>
                  <option value="epic">By Epic</option>
                  <option value="assignee">By Assignee</option>
                  <option value="priority">By Priority</option>
                  <option value="story">By Story (Subtasks)</option>
                  <option value="custom_jql">By Custom JQL</option>
                </select>
              </div>
            </div>

            <div class="form-group">
              <label>Board JQL Filter Query (Optional)</label>
              <input
                type="text"
                name="new_board_jql"
                [(ngModel)]="newBoardDraft.filter_jql"
                placeholder='Leave empty for all project items, or e.g.: category = "Bug" ORDER BY priority DESC'
              />
            </div>

            <div style="display: flex; justify-content: flex-end; gap: 8px; margin-top: 14px;">
              <button type="button" class="btn btn-secondary" (click)="createBoardModal.set(false)">Cancel</button>
              <button type="button" class="btn btn-primary" (click)="submitCreateBoard()">+ Create Board</button>
            </div>
          </div>
        </div>
      }

      <!-- ================================================================== -->
      <!-- MODAL 4: BOARD CONFIGURATION & CUSTOMIZER (COLUMNS, WIP, SWIMLANES)-->
      <!-- ================================================================== -->
      @if (boardConfigModal() && activeBoard(); as board) {
        <div
          style="position: fixed; inset: 0; background: rgba(15, 23, 42, 0.55); z-index: 1250; display: flex; align-items: center; justify-content: center; padding: 16px;"
        >
          <div
            class="card-panel"
            style="width: 100%; max-width: 820px; max-height: 90vh; overflow-y: auto; box-shadow: 0 20px 45px rgba(0,0,0,0.25);"
          >
            <div class="section-header">
              <div>
                <h3>⚙ Configure Board: {{ board.name }}</h3>
                <small class="text-muted">
                  Customize Column-to-Status mappings, Min/Max WIP limits, Swimlanes, Card layout, and Quick Filters.
                </small>
              </div>
              <button type="button" class="btn btn-ghost btn-sm" (click)="boardConfigModal.set(false)">✕</button>
            </div>

            <div class="mode-switcher" style="margin: 12px 0;">
              <button
                type="button"
                class="mode-btn"
                [class.active]="configTab() === 'columns'"
                (click)="configTab.set('columns')"
              >
                📊 Columns, Status Mapping &amp; WIP Limits
              </button>
              <button
                type="button"
                class="mode-btn"
                [class.active]="configTab() === 'general'"
                (click)="configTab.set('general')"
              >
                🎨 General, Swimlanes &amp; Card Layout
              </button>
              <button
                type="button"
                class="mode-btn"
                [class.active]="configTab() === 'quick_filters'"
                (click)="configTab.set('quick_filters')"
              >
                ⚡ Quick Filters ({{ board.quick_filters.length }})
              </button>
              <button
                type="button"
                class="mode-btn"
                [class.active]="configTab() === 'swimlanes'"
                (click)="configTab.set('swimlanes')"
              >
                🏊 Custom JQL Swimlanes ({{ board.swimlanes.length }})
              </button>
            </div>

            <!-- TAB 1: COLUMNS & MANY-TO-ONE STATUS MAPPING + WIP LIMITS -->
            @if (configTab() === 'columns') {
              <div style="display: flex; flex-direction: column; gap: 10px;">
                @for (col of editableColumns; track $index; let idx = $index) {
                  <div style="border: 1px solid #dfe1e6; border-radius: 8px; padding: 12px; background: #fafbfc;">
                    <div class="grid-3" style="align-items: end;">
                      <div class="form-group" style="margin: 0;">
                        <label>Column Name</label>
                        <input type="text" [name]="'col_name_' + idx" [(ngModel)]="col.name" />
                      </div>
                      <div class="form-group" style="margin: 0;">
                        <label>Min WIP (0 = none)</label>
                        <input type="number" min="0" [name]="'col_min_' + idx" [(ngModel)]="col.min_wip" />
                      </div>
                      <div class="form-group" style="margin: 0; display: flex; gap: 8px; align-items: flex-end;">
                        <div style="flex: 1;">
                          <label>Max WIP (0 = none)</label>
                          <input type="number" min="0" [name]="'col_max_' + idx" [(ngModel)]="col.max_wip" />
                        </div>
                        <button
                          type="button"
                          class="btn btn-danger btn-sm"
                          (click)="removeEditableColumn(idx)"
                          [disabled]="editableColumns.length <= 1"
                        >
                          Delete
                        </button>
                      </div>
                    </div>

                    <div style="margin-top: 8px;">
                      <label style="font-size: 11px; font-weight: 700; color: #5e6c84;">
                        Mapped Workflow Statuses (Many-to-One Status-to-Column Mapping):
                      </label>
                      <div style="display: flex; gap: 8px; flex-wrap: wrap; margin-top: 4px;">
                        @for (st of states; track st) {
                          <label
                            style="display: inline-flex; align-items: center; gap: 4px; font-size: 12px; background: #fff; border: 1px solid #dfe1e6; padding: 3px 8px; border-radius: 6px; cursor: pointer;"
                          >
                            <input
                              type="checkbox"
                              [checked]="isStatusInEditableCol(col, st)"
                              (change)="toggleStatusInEditableCol(col, st)"
                            />
                            {{ st }}
                          </label>
                        }
                      </div>
                    </div>
                  </div>
                }

                <div style="display: flex; justify-content: space-between; margin-top: 8px;">
                  <button type="button" class="btn btn-secondary btn-sm" (click)="addEditableColumn()">
                    + Add Board Column
                  </button>
                  <button type="button" class="btn btn-primary" (click)="saveBoardConfiguration()">
                    💾 Save Columns &amp; WIP Configuration
                  </button>
                </div>
              </div>
            }

            <!-- TAB 2: GENERAL, SWIMLANES & CARD EXTRA FIELDS -->
            @if (configTab() === 'general') {
              <div class="grid-2">
                <div class="form-group">
                  <label>Board Name</label>
                  <input type="text" name="cfg_board_name" [(ngModel)]="boardConfigDraft.name" />
                </div>
                <div class="form-group">
                  <label>Board Type</label>
                  <select name="cfg_board_type" [(ngModel)]="boardConfigDraft.board_type">
                    <option value="scrum">Scrum Board</option>
                    <option value="kanban">Kanban Board</option>
                  </select>
                </div>
              </div>

              <div class="form-group">
                <label>Base JQL Filter Query</label>
                <input
                  type="text"
                  name="cfg_board_jql"
                  [(ngModel)]="boardConfigDraft.filter_jql"
                  placeholder='e.g. project = "JIRA" AND status != "Archived"'
                />
              </div>

              <div class="grid-2">
                <div class="form-group">
                  <label>Swimlane Grouping Strategy</label>
                  <select name="cfg_board_swimlane" [(ngModel)]="boardConfigDraft.swimlane_strategy">
                    <option value="none">None</option>
                    <option value="epic">By Epic</option>
                    <option value="assignee">By Assignee</option>
                    <option value="priority">By Priority</option>
                    <option value="story">By Story (Subtasks)</option>
                    <option value="custom_jql">By Custom JQL Swimlanes</option>
                  </select>
                </div>
                <div class="form-group">
                  <label>Card Left-Edge Color Strip Rule</label>
                  <select name="cfg_board_color" [(ngModel)]="boardConfigDraft.card_color_strategy">
                    <option value="priority">By Priority</option>
                    <option value="type">By Issue Type</option>
                    <option value="category">By Category</option>
                    <option value="none">None</option>
                  </select>
                </div>
              </div>

              <div class="form-group">
                <label>Card Extra Fields Displayed (<code>card_extra_fields</code>)</label>
                <input
                  type="text"
                  name="cfg_board_extra"
                  [(ngModel)]="boardConfigDraft.card_extra_fields"
                  placeholder="story_points,due_date,components,fix_versions,labels"
                />
              </div>

              <div style="display: flex; justify-content: flex-end; gap: 8px; margin-top: 12px;">
                <button type="button" class="btn btn-primary" (click)="saveBoardConfiguration()">
                  💾 Save Board Settings
                </button>
              </div>
            }

            <!-- TAB 3: QUICK FILTERS -->
            @if (configTab() === 'quick_filters') {
              <div style="display: flex; flex-direction: column; gap: 8px;">
                @for (qf of board.quick_filters; track qf.id) {
                  <div style="display: flex; justify-content: space-between; align-items: center; padding: 8px 12px; border: 1px solid #dfe1e6; border-radius: 6px;">
                    <div>
                      <strong>{{ qf.name }}</strong>
                      <code style="margin-left: 8px; font-size: 12px; background: #f4f5f7; padding: 2px 6px; border-radius: 4px;">
                        {{ qf.jql_query }}
                      </code>
                      <div class="text-muted" style="font-size: 11px;">{{ qf.description }}</div>
                    </div>
                    <button type="button" class="btn btn-danger btn-sm" (click)="deleteQuickFilter(qf.id)">
                      Delete
                    </button>
                  </div>
                }

                <div style="border-top: 1px solid #dfe1e6; padding-top: 12px; margin-top: 6px;">
                  <h4>+ Add Quick Filter</h4>
                  <div class="grid-3">
                    <input type="text" name="new_qf_name" placeholder="Button Label (e.g. Needs QA)" [(ngModel)]="newQfDraft.name" />
                    <input type="text" name="new_qf_jql" placeholder='JQL (e.g. status = "QA / Testing")' [(ngModel)]="newQfDraft.jql_query" />
                    <button type="button" class="btn btn-primary btn-sm" (click)="createQuickFilter()">+ Add Quick Filter</button>
                  </div>
                </div>
              </div>
            }

            <!-- TAB 4: CUSTOM JQL SWIMLANES -->
            @if (configTab() === 'swimlanes') {
              <div style="display: flex; flex-direction: column; gap: 8px;">
                @for (sw of board.swimlanes; track sw.id) {
                  <div style="display: flex; justify-content: space-between; align-items: center; padding: 8px 12px; border: 1px solid #dfe1e6; border-radius: 6px;">
                    <div>
                      <strong>{{ sw.name }}</strong>
                      <code style="margin-left: 8px; font-size: 12px; background: #f4f5f7; padding: 2px 6px; border-radius: 4px;">
                        {{ sw.jql_query }}
                      </code>
                      <div class="text-muted" style="font-size: 11px;">{{ sw.description }}</div>
                    </div>
                    <button type="button" class="btn btn-danger btn-sm" (click)="deleteSwimlane(sw.id)">
                      Delete
                    </button>
                  </div>
                }

                <div style="border-top: 1px solid #dfe1e6; padding-top: 12px; margin-top: 6px;">
                  <h4>+ Add Custom JQL Swimlane</h4>
                  <div class="grid-3">
                    <input type="text" name="new_sw_name" placeholder="Swimlane Title" [(ngModel)]="newSwDraft.name" />
                    <input type="text" name="new_sw_jql" placeholder='JQL (e.g. priority = "Critical")' [(ngModel)]="newSwDraft.jql_query" />
                    <button type="button" class="btn btn-primary btn-sm" (click)="createSwimlane()">+ Add Swimlane</button>
                  </div>
                </div>
              </div>
            }
          </div>
        </div>
      }

      <!-- ================================================================== -->
      <!-- MODAL 5: DIRECTED WORKFLOW TRANSITION SCREEN MODAL                 -->
      <!-- ================================================================== -->
      @if (boardTransitionModal(); as modal) {
        <div
          style="position: fixed; inset: 0; background: rgba(15, 23, 42, 0.55); z-index: 1300; display: flex; align-items: center; justify-content: center; padding: 16px;"
        >
          <div
            class="card-panel"
            style="width: 100%; max-width: 540px; max-height: 90vh; overflow-y: auto; box-shadow: 0 20px 45px rgba(0,0,0,0.25);"
          >
            <div class="section-header">
              <div>
                <h3>🗔 {{ modal.transition.name }} ({{ modal.item.key }})</h3>
                <small class="text-muted">
                  <span class="state-pill" [attr.data-state]="modal.item.status">{{ modal.item.status }}</span>
                  →
                  <span class="state-pill" [attr.data-state]="modal.transition.to_status">{{ modal.transition.to_status }}</span>
                  @if (modal.transition.screen_name) {
                    · Screen: <strong>{{ modal.transition.screen_name }}</strong>
                  }
                </small>
              </div>
              <button type="button" class="btn btn-ghost btn-sm" (click)="closeBoardTransitionModal()">✕</button>
            </div>

            @if (boardModalError()) {
              <div
                style="margin: 10px 0; padding: 10px 12px; background: #fef2f2; border: 1px solid #fecaca; color: #b91c1c; border-radius: 6px; font-size: 13px;"
              >
                ⚠ {{ boardModalError() }}
              </div>
            }

            @if (modal.candidateTransitions.length > 1) {
              <div class="form-group" style="margin-top: 10px;">
                <label>Target Column Status</label>
                <select
                  name="board_tr_choice"
                  [ngModel]="modal.transition.id"
                  (ngModelChange)="selectCandidateTransition($event)"
                >
                  @for (cand of modal.candidateTransitions; track cand.id) {
                    <option [ngValue]="cand.id">{{ cand.name }} → {{ cand.to_status }}</option>
                  }
                </select>
              </div>
            }

            @if (modal.transition.to_status === 'Done' || hasBoardValidator(modal.transition, 'resolution_id')) {
              <div class="form-group" style="margin-top: 10px;">
                <label>
                  Resolution
                  <span style="color: #de350b; font-weight: bold;">*</span>
                </label>
                <select name="board_tr_res" [(ngModel)]="boardTransitionDraft.resolution_id">
                  <option [ngValue]="0">— Select Resolution —</option>
                  @for (r of api.resolutions(); track r.id) {
                    <option [ngValue]="r.id">✔ {{ r.name }} — {{ r.description }}</option>
                  }
                </select>
              </div>
            }

            <div class="form-group" style="margin-top: 10px;">
              <label>
                Transition Comment
                @if (hasBoardCommentValidator(modal.transition)) {
                  <span style="color: #de350b; font-weight: bold;">* (Required)</span>
                }
              </label>
              <textarea
                rows="3"
                name="board_tr_comment"
                [(ngModel)]="boardTransitionDraft.comment"
                placeholder="Enter transition notes..."
              ></textarea>
            </div>

            <div style="display: flex; justify-content: flex-end; gap: 8px; margin-top: 14px;">
              <button type="button" class="btn btn-secondary" (click)="closeBoardTransitionModal()">
                Cancel
              </button>
              <button type="button" class="btn btn-primary" (click)="submitBoardTransitionModal()">
                Confirm '{{ modal.transition.name }}'
              </button>
            </div>
          </div>
        </div>
      }
    </div>
  `,
})
export class BoardComponent implements OnInit {
  readonly api = inject(ApiService);
  private readonly router = inject(Router);
  private readonly route = inject(ActivatedRoute);

  readonly states = WORKFLOW_STATES;
  readonly itemTypes = WORK_ITEM_TYPES;
  readonly itemCategories = WORK_ITEM_CATEGORIES;

  readonly viewMode = signal<'board' | 'backlog'>('board');
  readonly activeBoard = signal<BoardConfig | null>(null);
  readonly items = signal<WorkItem[]>([]);
  readonly swimlaneMatches = signal<Record<string, number[]>>({});

  readonly activeQuickFilterIds = signal<number[]>([]);
  readonly searchQuery = signal('');
  readonly selectedType = signal<WorkItemType | ''>('');
  readonly selectedCategory = signal<WorkItemCategory | ''>('');
  readonly selectedAssignee = signal<string>('');
  readonly selectedSprint = signal<string>('');
  readonly swimlaneStrategyOverride = signal<SwimlaneStrategy | null>(null);

  // Backlog side panel filters & state
  readonly showSidePanels = signal(true);
  readonly selectedEpicFilter = signal<number | null>(null);
  readonly selectedVersionFilter = signal<number | null>(null);
  readonly collapsedBuckets = signal<Record<string, boolean>>({});
  readonly collapsedSwimlanes = signal<Record<string, boolean>>({});

  // Drag & Drop state
  readonly draggedItem = signal<WorkItem | null>(null);
  readonly dragOverColId = signal<number | null>(null);
  readonly dragOverBucketKey = signal<string | null>(null);
  readonly dragOverEpicId = signal<number | null>(null);
  readonly dragOverVersionId = signal<number | null>(null);

  readonly toastMsg = signal('');

  // Modals
  readonly boardTransitionModal = signal<{
    item: WorkItem;
    transition: AvailableTransition;
    candidateTransitions: AvailableTransition[];
  } | null>(null);
  readonly boardModalError = signal('');

  readonly startSprintModal = signal<Sprint | null>(null);
  readonly completeSprintModal = signal<Sprint | null>(null);
  readonly createBoardModal = signal(false);
  readonly boardConfigModal = signal(false);
  readonly configTab = signal<'columns' | 'general' | 'quick_filters' | 'swimlanes'>('columns');

  boardTransitionDraft = {
    resolution_id: 1,
    comment: '',
  };

  startSprintDraft = {
    name: '',
    goal: '',
    duration_weeks: 2,
    start_date: '2026-11-01',
    end_date: '2026-11-15',
  };

  completeSprintDraft = {
    rollover_choice: 'backlog',
    next_sprint_name: '',
  };

  newBoardDraft = {
    name: '',
    board_type: 'scrum' as 'scrum' | 'kanban',
    swimlane_strategy: 'none' as SwimlaneStrategy,
    filter_jql: '',
  };

  boardConfigDraft = {
    name: '',
    board_type: 'scrum' as 'scrum' | 'kanban',
    filter_jql: '',
    swimlane_strategy: 'none' as SwimlaneStrategy,
    card_color_strategy: 'priority',
    card_extra_fields: 'story_points,due_date,components',
  };

  editableColumns: {
    name: string;
    statuses: string[];
    min_wip: number;
    max_wip: number;
    color: string;
    sort_order: number;
  }[] = [];

  newQfDraft = { name: '', jql_query: '', description: '' };
  newSwDraft = { name: '', jql_query: '', description: '' };

  quickBacklogDraft = {
    type: 'Story',
    title: '',
    story_points: 3,
  };

  readonly currentSwimlaneStrategy = computed<SwimlaneStrategy>(() => {
    return this.swimlaneStrategyOverride() || this.activeBoard()?.swimlane_strategy || 'none';
  });

  readonly visibleColumns = computed<BoardColumn[]>(() => {
    const b = this.activeBoard();
    if (!b) return [];
    return b.columns || [];
  });

  readonly activeSprintForBoard = computed<Sprint | null>(() => {
    const spId = Number(this.selectedSprint());
    if (spId > 0) {
      return this.api.sprints().find((s) => s.id === spId) || null;
    }
    return this.api.currentSprint();
  });

  readonly openAndPlannedSprints = computed<Sprint[]>(() => {
    return this.api.sprints().filter((s) => s.status !== 'closed');
  });

  readonly closedSprints = computed<Sprint[]>(() => {
    return this.api.sprints().filter((s) => s.status === 'closed');
  });

  readonly epicItems = computed<WorkItem[]>(() => {
    return this.items().filter((i) => i.type === 'Epic');
  });

  readonly filteredItems = computed<WorkItem[]>(() => {
    const q = this.searchQuery().trim().toLowerCase();
    const t = this.selectedType();
    const c = this.selectedCategory();
    const a = this.selectedAssignee();
    const epId = this.selectedEpicFilter();
    const verId = this.selectedVersionFilter();

    return this.items().filter((item) => {
      if (t && item.type !== t) return false;
      if (c && item.category !== c) return false;
      if (a && String(item.assignee_id || '') !== String(a)) return false;
      if (epId && item.parent_id !== epId && item.id !== epId) return false;
      if (verId && !(item.fix_versions || []).some((v) => v.id === verId)) return false;
      if (q) {
        const hay = `${item.key} ${item.title} ${item.description} ${item.labels} ${item.category}`.toLowerCase();
        if (!hay.includes(q)) return false;
      }
      return true;
    });
  });

  readonly boardFilteredItems = computed<WorkItem[]>(() => {
    const base = this.filteredItems();
    const b = this.activeBoard();
    if (b?.board_type === 'scrum') {
      const sp = this.selectedSprint();
      if (sp) {
        return base.filter((i) => String(i.sprint_id || '') === String(sp));
      }
    }
    return base;
  });

  readonly swimlaneRows = computed<SwimlaneRow[]>(() => {
    const strategy = this.currentSwimlaneStrategy();
    const list = this.boardFilteredItems();
    if (strategy === 'none') return [];

    if (strategy === 'epic') {
      const epics = new Map<number, WorkItem>();
      for (const i of this.items()) {
        if (i.type === 'Epic') epics.set(i.id, i);
      }
      const groups = new Map<string, SwimlaneRow>();
      for (const item of list) {
        if (item.type === 'Epic') continue;
        const ep = item.parent_id ? epics.get(item.parent_id) : undefined;
        const key = ep ? `epic_${ep.id}` : 'epic_none';
        if (!groups.has(key)) {
          groups.set(key, {
            key,
            title: ep ? `⚡ ${ep.key}: ${ep.title}` : '🗂 Issues Without Epic',
            items: [],
            totalPoints: 0,
          });
        }
        const row = groups.get(key)!;
        row.items.push(item);
        row.totalPoints += item.story_points || 0;
      }
      return Array.from(groups.values());
    }

    if (strategy === 'assignee') {
      const groups = new Map<string, SwimlaneRow>();
      for (const item of list) {
        const key = item.assignee_id ? `user_${item.assignee_id}` : 'user_unassigned';
        if (!groups.has(key)) {
          groups.set(key, {
            key,
            title: item.assignee_name ? `👤 ${item.assignee_name}` : '👤 Unassigned',
            items: [],
            totalPoints: 0,
          });
        }
        const row = groups.get(key)!;
        row.items.push(item);
        row.totalPoints += item.story_points || 0;
      }
      return Array.from(groups.values());
    }

    if (strategy === 'priority') {
      const order: string[] = ['Critical', 'Highest', 'High', 'Medium', 'Low', 'Lowest'];
      const rows: SwimlaneRow[] = [];
      for (const p of order) {
        const pItems = list.filter((i) => i.priority === p);
        if (pItems.length > 0) {
          rows.push({
            key: `prio_${p}`,
            title: `🔥 Priority: ${p}`,
            items: pItems,
            totalPoints: pItems.reduce((acc, x) => acc + (x.story_points || 0), 0),
          });
        }
      }
      return rows;
    }

    if (strategy === 'story') {
      const stories = list.filter((i) => i.type === 'Story' || i.type === 'Task' || i.type === 'Epic');
      const rows: SwimlaneRow[] = [];
      const assignedIds = new Set<number>();
      for (const st of stories) {
        const children = list.filter((i) => i.parent_id === st.id);
        if (children.length > 0) {
          const laneItems = [st, ...children];
          laneItems.forEach((x) => assignedIds.add(x.id));
          rows.push({
            key: `story_${st.id}`,
            title: `📘 ${st.key}: ${st.title}`,
            subtitle: `${children.length} subtasks`,
            items: laneItems,
            totalPoints: laneItems.reduce((acc, x) => acc + (x.story_points || 0), 0),
          });
        }
      }
      const remaining = list.filter((i) => !assignedIds.has(i.id));
      if (remaining.length > 0) {
        rows.push({
          key: 'story_other',
          title: '📋 Standalone Issues',
          items: remaining,
          totalPoints: remaining.reduce((acc, x) => acc + (x.story_points || 0), 0),
        });
      }
      return rows;
    }

    if (strategy === 'custom_jql') {
      const board = this.activeBoard();
      const matches = this.swimlaneMatches();
      const assignedIds = new Set<number>();
      const rows: SwimlaneRow[] = [];

      for (const sw of board?.swimlanes || []) {
        const matchSet = new Set<number>(matches[String(sw.id)] || []);
        const swItems = list.filter((i) => !assignedIds.has(i.id) && matchSet.has(i.id));
        swItems.forEach((x) => assignedIds.add(x.id));
        rows.push({
          key: `jql_${sw.id}`,
          title: sw.name,
          subtitle: sw.jql_query,
          items: swItems,
          totalPoints: swItems.reduce((acc, x) => acc + (x.story_points || 0), 0),
        });
      }

      const uncategorized = list.filter((i) => !assignedIds.has(i.id));
      if (uncategorized.length > 0) {
        rows.push({
          key: 'jql_other',
          title: '📋 Everything Else',
          items: uncategorized,
          totalPoints: uncategorized.reduce((acc, x) => acc + (x.story_points || 0), 0),
        });
      }
      return rows;
    }

    return [];
  });

  async ngOnInit(): Promise<void> {
    try {
      const st = await this.api.checkSetupStatus();
      if (!st.setup_complete) {
        await this.router.navigate(['/setup']);
        return;
      }
      if (!this.api.currentUser()) {
        await this.router.navigate(['/login']);
        return;
      }
      await this.api.loadReferenceData();

      // Check route path or query param for backlog view mode
      const path = this.route.snapshot.routeConfig?.path || '';
      const qView = this.route.snapshot.queryParamMap.get('view');
      if (path === 'backlog' || qView === 'backlog') {
        this.viewMode.set('backlog');
      }

      const cur = this.api.currentSprint();
      if (cur && !this.selectedSprint()) {
        this.selectedSprint.set(String(cur.id));
      }

      const boards = this.api.boards();
      if (boards.length > 0) {
        const defBoard = boards.find((b) => b.is_default) || boards[0];
        this.activeBoard.set(defBoard);
        await this.loadBoardItems();
      } else {
        await this.loadBoardItems();
      }
    } catch {
      // ignore
    }
  }

  async loadBoardItems(): Promise<void> {
    const board = this.activeBoard();
    if (!board) {
      const list = await this.api.getItems();
      this.items.set(list);
      return;
    }
    const payload = await this.api.getBoardItems(board.id, this.activeQuickFilterIds());
    this.activeBoard.set(payload.board);
    this.items.set(payload.items);
    this.swimlaneMatches.set(payload.swimlane_matches || {});
  }

  async selectBoard(boardId: number): Promise<void> {
    const found = this.api.boards().find((b) => b.id === Number(boardId));
    if (found) {
      this.activeBoard.set(found);
      this.swimlaneStrategyOverride.set(null);
      this.activeQuickFilterIds.set([]);
      if (found.board_type === 'scrum') {
        const cur = this.api.currentSprint();
        if (cur) this.selectedSprint.set(String(cur.id));
      } else {
        this.selectedSprint.set('');
      }
      await this.loadBoardItems();
    }
  }

  setViewMode(mode: 'board' | 'backlog'): void {
    this.viewMode.set(mode);
  }

  changeSwimlaneStrategy(strat: SwimlaneStrategy): void {
    this.swimlaneStrategyOverride.set(strat);
  }

  isQuickFilterActive(qfId: number): boolean {
    return this.activeQuickFilterIds().includes(qfId);
  }

  async toggleQuickFilter(qfId: number): Promise<void> {
    const cur = this.activeQuickFilterIds();
    if (cur.includes(qfId)) {
      this.activeQuickFilterIds.set(cur.filter((id) => id !== qfId));
    } else {
      this.activeQuickFilterIds.set([...cur, qfId]);
    }
    await this.loadBoardItems();
  }

  async clearAllQuickFilters(): Promise<void> {
    this.activeQuickFilterIds.set([]);
    this.selectedEpicFilter.set(null);
    this.selectedVersionFilter.set(null);
    await this.loadBoardItems();
  }

  itemsForColumn(col: BoardColumn, sourceItems: WorkItem[]): WorkItem[] {
    const statusSet = new Set(col.statuses || []);
    return sourceItems.filter((i) => statusSet.has(i.status));
  }

  getColumnWipState(col: BoardColumn, count: number): 'ok' | 'exceeded' | 'under' {
    if (col.max_wip > 0 && count > col.max_wip) return 'exceeded';
    if (col.min_wip > 0 && count < col.min_wip) return 'under';
    return 'ok';
  }

  shouldShowCardField(fieldKey: string): boolean {
    const b = this.activeBoard();
    if (!b || !b.card_extra_fields_list) return true;
    return b.card_extra_fields_list.includes(fieldKey);
  }

  getCardColorStrip(item: WorkItem): string {
    const strat = this.activeBoard()?.card_color_strategy || 'priority';
    if (strat === 'none') return 'transparent';
    if (strat === 'type') {
      const map: Record<string, string> = {
        Epic: '#6554C0',
        Story: '#00875A',
        Task: '#0052CC',
        Subtask: '#00B8D9',
      };
      return map[item.type] || '#42526E';
    }
    if (strat === 'category') {
      const map: Record<string, string> = {
        Bug: '#DE350B',
        'New Feature': '#00875A',
        Improvement: '#0052CC',
        Issue: '#FF991F',
      };
      return map[item.category] || '#42526E';
    }
    const prioMap: Record<string, string> = {
      Critical: '#BF2600',
      Highest: '#DE350B',
      High: '#FF5630',
      Medium: '#FF991F',
      Low: '#0065FF',
      Lowest: '#6B778C',
    };
    return prioMap[item.priority] || '#0052CC';
  }

  isSwimlaneCollapsed(key: string): boolean {
    return !!this.collapsedSwimlanes()[key];
  }

  toggleSwimlaneCollapsed(key: string): void {
    this.collapsedSwimlanes.update((m) => ({ ...m, [key]: !m[key] }));
  }

  isBucketCollapsed(key: string): boolean {
    return !!this.collapsedBuckets()[key];
  }

  toggleBucketCollapse(key: string): void {
    this.collapsedBuckets.update((m) => ({ ...m, [key]: !m[key] }));
  }

  // Agile Backlog Helpers
  itemsForSprintBucket(sprintId: number): WorkItem[] {
    return this.filteredItems().filter((i) => i.sprint_id === sprintId && i.type !== 'Epic');
  }

  backlogUnassignedItems(): WorkItem[] {
    return this.filteredItems().filter((i) => !i.sprint_id && i.type !== 'Epic');
  }

  computeBucketRollup(items: WorkItem[]): {
    todoPoints: number;
    inProgPoints: number;
    donePoints: number;
    totalPoints: number;
  } {
    let todoPoints = 0;
    let inProgPoints = 0;
    let donePoints = 0;
    for (const i of items) {
      const pts = i.story_points || 0;
      if (i.status === 'Done') donePoints += pts;
      else if (i.status === 'Backlog' || i.status === 'To Do') todoPoints += pts;
      else inProgPoints += pts;
    }
    return {
      todoPoints,
      inProgPoints,
      donePoints,
      totalPoints: todoPoints + inProgPoints + donePoints,
    };
  }

  epicRollup(epicId: number): { total: number; done: number; points: number; pct: number } {
    const children = this.items().filter((i) => i.parent_id === epicId);
    const total = children.length;
    const done = children.filter((i) => i.status === 'Done').length;
    const points = children.reduce((acc, x) => acc + (x.story_points || 0), 0);
    const pct = total > 0 ? Math.round((done * 100) / total) : 0;
    return { total, done, points, pct };
  }

  verIssueCount(verId: number): number {
    return this.items().filter((i) => (i.fix_versions || []).some((v) => v.id === verId)).length;
  }

  toggleEpicFilter(epicId: number): void {
    this.selectedEpicFilter.set(this.selectedEpicFilter() === epicId ? null : epicId);
  }

  toggleVersionFilter(verId: number): void {
    this.selectedVersionFilter.set(this.selectedVersionFilter() === verId ? null : verId);
  }

  // Backlog Drag & Drop + Ranking
  onEpicDragOver(event: DragEvent, epicId: number): void {
    event.preventDefault();
    this.dragOverEpicId.set(epicId);
  }

  async onEpicDrop(event: DragEvent, epic: WorkItem): Promise<void> {
    event.preventDefault();
    this.dragOverEpicId.set(null);
    const item = this.draggedItem();
    this.draggedItem.set(null);
    if (!item || item.id === epic.id) return;

    await this.api.rankItems({ item_id: item.id, parent_id: epic.id });
    await this.loadBoardItems();
    this.showToast(`Linked ${item.key} to Epic ${epic.key} (${epic.title})`);
  }

  onVersionDragOver(event: DragEvent, verId: number): void {
    event.preventDefault();
    this.dragOverVersionId.set(verId);
  }

  async onVersionDrop(event: DragEvent, ver: { id: number; name: string }): Promise<void> {
    event.preventDefault();
    this.dragOverVersionId.set(null);
    const item = this.draggedItem();
    this.draggedItem.set(null);
    if (!item) return;

    await this.api.rankItems({ item_id: item.id, fix_version_id: ver.id });
    await this.loadBoardItems();
    this.showToast(`Assigned Fix Version "${ver.name}" to ${item.key}`);
  }

  onBucketDragOver(event: DragEvent, key: string): void {
    event.preventDefault();
    this.dragOverBucketKey.set(key);
  }

  async onBucketDrop(event: DragEvent, targetSprintId: number | null): Promise<void> {
    event.preventDefault();
    this.dragOverBucketKey.set(null);
    const item = this.draggedItem();
    this.draggedItem.set(null);
    if (!item) return;

    await this.api.rankItems({ item_id: item.id, sprint_id: targetSprintId || 0 });
    await this.api.loadReferenceData();
    await this.loadBoardItems();
    const destName = targetSprintId
      ? this.api.sprints().find((s) => s.id === targetSprintId)?.name || `Sprint #${targetSprintId}`
      : 'Backlog';
    this.showToast(`Moved ${item.key} to ${destName}`);
  }

  async moveItemToSprint(item: WorkItem, newSprintId: number): Promise<void> {
    const target = Number(newSprintId) || 0;
    await this.api.rankItems({ item_id: item.id, sprint_id: target });
    await this.api.loadReferenceData();
    await this.loadBoardItems();
    const destName = target
      ? this.api.sprints().find((s) => s.id === target)?.name || `Sprint #${target}`
      : 'Backlog';
    this.showToast(`Moved ${item.key} to ${destName}`);
  }

  async rankItemInBucket(bucketItems: WorkItem[], index: number, delta: number): Promise<void> {
    const targetIdx = index + delta;
    if (targetIdx < 0 || targetIdx >= bucketItems.length) return;
    const reordered = [...bucketItems];
    const [moved] = reordered.splice(index, 1);
    reordered.splice(targetIdx, 0, moved);

    await this.api.rankItems({
      ordered_item_ids: reordered.map((x) => x.id),
    });
    await this.loadBoardItems();
    this.showToast(`Ranked ${moved.key} ${delta < 0 ? 'up ▲' : 'down ▼'}`);
  }

  async createQuickBacklogItem(): Promise<void> {
    const title = this.quickBacklogDraft.title.trim();
    if (!title) return;
    const created = await this.api.createItem({
      project_id: this.activeBoard()?.project_id || 1,
      type: this.quickBacklogDraft.type,
      category: 'New Feature',
      status: 'Backlog',
      priority: 'Medium',
      title,
      story_points: Number(this.quickBacklogDraft.story_points) || 0,
      sprint_id: null,
    });
    this.quickBacklogDraft.title = '';
    await this.loadBoardItems();
    this.showToast(`Created ${created.key} in Backlog`);
  }

  async quickCreatePlannedSprint(): Promise<void> {
    const nextNum = this.api.sprints().length + 1;
    const sp = await this.api.createSprint({
      project_id: this.activeBoard()?.project_id || 1,
      name: `Sprint ${nextNum} - Planned Iteration`,
      goal: 'Upcoming sprint deliverables',
      status: 'planned',
      start_date: '2026-11-21',
      end_date: '2026-12-05',
      is_current: false,
    });
    this.showToast(`Created planned sprint "${sp.name}"`);
  }

  // Sprint Lifecycle Modals (Start Sprint & Complete Sprint)
  openStartSprintModal(sp: Sprint): void {
    const start = sp.start_date || '2026-11-01';
    this.startSprintDraft = {
      name: sp.name,
      goal: sp.goal || '',
      duration_weeks: 2,
      start_date: start,
      end_date: sp.end_date || '2026-11-15',
    };
    this.startSprintModal.set(sp);
  }

  applyStartSprintDuration(weeks: number): void {
    const w = Number(weeks);
    if (!w || !this.startSprintDraft.start_date) return;
    const dt = new Date(this.startSprintDraft.start_date + 'T00:00:00');
    if (isNaN(dt.getTime())) return;
    const end = new Date(dt.getTime() + w * 7 * 86400000);
    this.startSprintDraft.end_date = end.toISOString().slice(0, 10);
  }

  async submitStartSprint(): Promise<void> {
    const sp = this.startSprintModal();
    if (!sp) return;
    await this.api.startSprint(sp.id, {
      name: this.startSprintDraft.name,
      goal: this.startSprintDraft.goal,
      start_date: this.startSprintDraft.start_date,
      end_date: this.startSprintDraft.end_date,
      move_backlog_to_todo: true,
    });
    this.startSprintModal.set(null);
    this.selectedSprint.set(String(sp.id));
    await this.loadBoardItems();
    this.showToast(`Started sprint "${this.startSprintDraft.name}"!`);
  }

  openCompleteSprintModal(sp: Sprint): void {
    const planned = this.otherPlannedSprints(sp.id);
    this.completeSprintDraft = {
      rollover_choice: planned.length > 0 ? `sprint:${planned[0].id}` : 'backlog',
      next_sprint_name: `Sprint ${this.api.sprints().length + 1} - Follow-up`,
    };
    this.completeSprintModal.set(sp);
  }

  otherPlannedSprints(excludeId: number): Sprint[] {
    return this.api.sprints().filter((s) => s.id !== excludeId && s.status !== 'closed');
  }

  getDoneItems(items: WorkItem[]): WorkItem[] {
    return items.filter((i) => i.status === 'Done');
  }

  getIncompleteItems(items: WorkItem[]): WorkItem[] {
    return items.filter((i) => i.status !== 'Done');
  }

  sumStoryPoints(items: WorkItem[]): number {
    return items.reduce((acc, x) => acc + (x.story_points || 0), 0);
  }

  async submitCompleteSprint(): Promise<void> {
    const sp = this.completeSprintModal();
    if (!sp) return;
    const choice = this.completeSprintDraft.rollover_choice;
    let rollover_target: 'backlog' | 'sprint' | 'new_sprint' = 'backlog';
    let target_sprint_id = 0;

    if (choice === 'new_sprint') {
      rollover_target = 'new_sprint';
    } else if (choice.startsWith('sprint:')) {
      rollover_target = 'sprint';
      target_sprint_id = Number(choice.split(':')[1]) || 0;
    }

    const res: CompleteSprintResponse = await this.api.completeSprint(sp.id, {
      rollover_target,
      target_sprint_id,
      next_sprint_name: this.completeSprintDraft.next_sprint_name,
      activate_target_sprint: true,
    });
    this.completeSprintModal.set(null);
    const cur = this.api.currentSprint();
    if (cur) {
      this.selectedSprint.set(String(cur.id));
    }
    await this.loadBoardItems();
    this.showToast(
      `Completed "${res.sprint_name}" (${res.completed_points}/${res.committed_points} SP done; ${res.incomplete_items_count} issues → ${res.target_sprint_name})`
    );
  }

  // Board Creation & Configuration Modals
  openCreateBoardModal(): void {
    this.newBoardDraft = {
      name: '',
      board_type: 'scrum',
      swimlane_strategy: 'none',
      filter_jql: '',
    };
    this.createBoardModal.set(true);
  }

  async submitCreateBoard(): Promise<void> {
    if (!this.newBoardDraft.name.trim()) return;
    const created = await this.api.createBoard({
      project_id: 1,
      name: this.newBoardDraft.name.trim(),
      board_type: this.newBoardDraft.board_type,
      swimlane_strategy: this.newBoardDraft.swimlane_strategy,
      filter_jql: this.newBoardDraft.filter_jql.trim(),
    });
    this.createBoardModal.set(false);
    this.activeBoard.set(created);
    await this.loadBoardItems();
    this.showToast(`Created ${created.board_type.toUpperCase()} board "${created.name}"`);
  }

  openBoardConfigModal(): void {
    const b = this.activeBoard();
    if (!b) return;
    this.boardConfigDraft = {
      name: b.name,
      board_type: b.board_type,
      filter_jql: b.filter_jql || '',
      swimlane_strategy: b.swimlane_strategy || 'none',
      card_color_strategy: b.card_color_strategy || 'priority',
      card_extra_fields: b.card_extra_fields || 'story_points,due_date,components',
    };
    this.editableColumns = (b.columns || []).map((c) => ({
      name: c.name,
      statuses: [...(c.statuses || [])],
      min_wip: c.min_wip || 0,
      max_wip: c.max_wip || 0,
      color: c.color || '#42526E',
      sort_order: c.sort_order || 10,
    }));
    this.configTab.set('columns');
    this.boardConfigModal.set(true);
  }

  isStatusInEditableCol(col: { statuses: string[] }, st: string): boolean {
    return col.statuses.includes(st);
  }

  toggleStatusInEditableCol(col: { statuses: string[] }, st: string): void {
    if (col.statuses.includes(st)) {
      if (col.statuses.length > 1) {
        col.statuses = col.statuses.filter((x) => x !== st);
      }
    } else {
      col.statuses = [...col.statuses, st];
    }
  }

  addEditableColumn(): void {
    this.editableColumns.push({
      name: 'New Column',
      statuses: ['In Review'],
      min_wip: 0,
      max_wip: 4,
      color: '#0052CC',
      sort_order: (this.editableColumns.length + 1) * 10,
    });
  }

  removeEditableColumn(idx: number): void {
    if (this.editableColumns.length <= 1) return;
    this.editableColumns.splice(idx, 1);
  }

  async saveBoardConfiguration(): Promise<void> {
    const b = this.activeBoard();
    if (!b) return;
    try {
      const updated = await this.api.updateBoard(b.id, {
        name: this.boardConfigDraft.name,
        board_type: this.boardConfigDraft.board_type,
        filter_jql: this.boardConfigDraft.filter_jql,
        swimlane_strategy: this.boardConfigDraft.swimlane_strategy,
        card_color_strategy: this.boardConfigDraft.card_color_strategy,
        card_extra_fields: this.boardConfigDraft.card_extra_fields,
        columns: this.editableColumns.map((c, idx) => ({
          id: 0,
          board_id: b.id,
          name: c.name,
          mapped_statuses: c.statuses.join(','),
          statuses: c.statuses,
          min_wip: Number(c.min_wip) || 0,
          max_wip: Number(c.max_wip) || 0,
          color: c.color || '#42526E',
          sort_order: (idx + 1) * 10,
        })),
      });
      this.activeBoard.set(updated);
      this.swimlaneStrategyOverride.set(null);
      this.boardConfigModal.set(false);
      await this.loadBoardItems();
      this.showToast(`Saved board configuration for "${updated.name}"`);
    } catch (err: any) {
      this.showToast(`Configuration error: ${err?.message}`);
    }
  }

  async createQuickFilter(): Promise<void> {
    const b = this.activeBoard();
    if (!b || !this.newQfDraft.name.trim() || !this.newQfDraft.jql_query.trim()) return;
    try {
      const updated = await this.api.addBoardQuickFilter(b.id, {
        name: this.newQfDraft.name.trim(),
        jql_query: this.newQfDraft.jql_query.trim(),
        description: this.newQfDraft.description.trim(),
      });
      this.activeBoard.set(updated);
      this.newQfDraft = { name: '', jql_query: '', description: '' };
      this.showToast('Added Quick Filter');
    } catch (err: any) {
      this.showToast(`Invalid Quick Filter JQL: ${err?.message}`);
    }
  }

  async deleteQuickFilter(qfId: number): Promise<void> {
    const updated = await this.api.deleteBoardQuickFilter(qfId);
    this.activeBoard.set(updated);
  }

  async createSwimlane(): Promise<void> {
    const b = this.activeBoard();
    if (!b || !this.newSwDraft.name.trim() || !this.newSwDraft.jql_query.trim()) return;
    try {
      const updated = await this.api.addBoardSwimlane(b.id, {
        name: this.newSwDraft.name.trim(),
        jql_query: this.newSwDraft.jql_query.trim(),
        description: this.newSwDraft.description.trim(),
      });
      this.activeBoard.set(updated);
      this.newSwDraft = { name: '', jql_query: '', description: '' };
      await this.loadBoardItems();
      this.showToast('Added Custom JQL Swimlane');
    } catch (err: any) {
      this.showToast(`Invalid Swimlane JQL: ${err?.message}`);
    }
  }

  async deleteSwimlane(swId: number): Promise<void> {
    const updated = await this.api.deleteBoardSwimlane(swId);
    this.activeBoard.set(updated);
    await this.loadBoardItems();
  }

  // Board Column Drag & Drop with Directed Workflow Transition Engine
  splitLabels(labels: string): string[] {
    return (labels || '')
      .split(',')
      .map((s) => s.trim())
      .filter(Boolean);
  }

  initials(name: string): string {
    return (name || '')
      .split(' ')
      .map((p) => p[0])
      .join('')
      .toUpperCase()
      .slice(0, 2);
  }

  onDragStart(event: DragEvent, item: WorkItem): void {
    this.draggedItem.set(item);
    if (event.dataTransfer) {
      event.dataTransfer.effectAllowed = 'move';
      event.dataTransfer.setData('text/plain', String(item.id));
    }
  }

  onColDragOver(event: DragEvent, col: BoardColumn): void {
    event.preventDefault();
    if (event.dataTransfer) {
      event.dataTransfer.dropEffect = 'move';
    }
    this.dragOverColId.set(col.id);
  }

  onColDragLeave(col: BoardColumn): void {
    if (this.dragOverColId() === col.id) {
      this.dragOverColId.set(null);
    }
  }

  onDragEnd(): void {
    this.draggedItem.set(null);
    this.dragOverColId.set(null);
    this.dragOverBucketKey.set(null);
    this.dragOverEpicId.set(null);
    this.dragOverVersionId.set(null);
  }

  hasBoardValidator(tr: AvailableTransition, fieldName: string): boolean {
    return tr.validators.some((v) => v.rule_type === 'field_required' && v.field_name === fieldName);
  }

  hasBoardCommentValidator(tr: AvailableTransition): boolean {
    return tr.validators.some((v) => v.rule_type === 'comment_required');
  }

  selectCandidateTransition(trId: number): void {
    const modal = this.boardTransitionModal();
    if (!modal) return;
    const found = modal.candidateTransitions.find((t) => t.id === Number(trId));
    if (found) {
      this.boardTransitionModal.set({ ...modal, transition: found });
    }
  }

  closeBoardTransitionModal(): void {
    this.boardTransitionModal.set(null);
    this.boardModalError.set('');
  }

  async submitBoardTransitionModal(): Promise<void> {
    const modal = this.boardTransitionModal();
    if (!modal) return;
    try {
      const updated = await this.api.transitionItemState(modal.item.key, modal.transition.to_status as WorkflowState, {
        transition_id: modal.transition.id,
        resolution_id: this.boardTransitionDraft.resolution_id || undefined,
        comment: this.boardTransitionDraft.comment.trim() || undefined,
      });
      this.items.update((list) => list.map((i) => (i.id === modal.item.id ? updated : i)));
      this.closeBoardTransitionModal();
      this.showToast(`${modal.item.key} transitioned via "${modal.transition.name}" → "${modal.transition.to_status}"`);
    } catch (err: any) {
      this.boardModalError.set(err?.message || 'Transition validation failed');
    }
  }

  async onColDrop(event: DragEvent, targetCol: BoardColumn): Promise<void> {
    event.preventDefault();
    this.dragOverColId.set(null);
    const item = this.draggedItem();
    this.draggedItem.set(null);

    if (!item || (targetCol.statuses || []).includes(item.status)) {
      return;
    }

    try {
      const wfPayload = await this.api.getItemTransitions(item.key);
      const matchingTransitions = wfPayload.transitions.filter((t) =>
        (targetCol.statuses || []).includes(String(t.to_status))
      );

      if (matchingTransitions.length === 0) {
        this.showToast(
          `No directed transition from "${item.status}" to column "${targetCol.name}" (${targetCol.statuses.join(', ')})`
        );
        return;
      }

      const allowedCandidates = matchingTransitions.filter((t) => t.allowed);
      if (allowedCandidates.length === 0) {
        this.showToast(
          matchingTransitions[0].blocked_reasons[0] || `Transition to "${targetCol.name}" blocked by workflow condition`
        );
        return;
      }

      const tr = allowedCandidates[0];
      if (tr.requires_screen || allowedCandidates.length > 1) {
        const defRes = item.resolution_id || this.api.resolutions().find((r) => r.is_default)?.id || 1;
        this.boardTransitionDraft = {
          resolution_id: tr.to_status === 'Done' ? defRes : item.resolution_id || 0,
          comment: '',
        };
        this.boardModalError.set('');
        this.boardTransitionModal.set({
          item,
          transition: tr,
          candidateTransitions: allowedCandidates,
        });
        return;
      }

      const prevStatus = item.status;
      const updated = await this.api.transitionItemState(item.key, tr.to_status as WorkflowState, {
        transition_id: tr.id,
        comment: `Moved from '${prevStatus}' to '${tr.to_status}' (${targetCol.name}) on Board`,
      });
      this.items.update((list) => list.map((i) => (i.id === item.id ? updated : i)));
      this.showToast(`${item.key} moved from "${prevStatus}" → "${tr.to_status}" (${targetCol.name})`);
    } catch (err) {
      this.showToast(`Error moving ${item.key}: ${(err as Error).message}`);
    }
  }

  private showToast(msg: string): void {
    this.toastMsg.set(msg);
    setTimeout(() => {
      if (this.toastMsg() === msg) this.toastMsg.set('');
    }, 3500);
  }
}
