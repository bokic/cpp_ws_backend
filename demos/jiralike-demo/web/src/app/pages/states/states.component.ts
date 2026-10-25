import { Component, OnInit, computed, inject, signal } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { Router, RouterLink } from '@angular/router';
import { ApiService } from '../../services/api.service';
import {
  WORKFLOW_STATES,
  WorkItem,
  WorkflowState,
} from '../../models/jira.models';

interface GanttDay {
  iso: string;
  label: string;
  dayNum: string;
  isWeekend: boolean;
}

@Component({
  selector: 'app-states',
  standalone: true,
  imports: [CommonModule, FormsModule, RouterLink],
  host: {
    '(window:mouseup)': 'onSliderEnd()',
    '(window:pointerup)': 'onSliderEnd()',
    '(window:touchend)': 'onSliderEnd()',
  },
  template: `
    <div class="page-container">
      <div class="page-toolbar">
        <div>
          <div style="display: flex; align-items: center; gap: 10px; flex-wrap: wrap;">
            <h2>Workflow States Matrix</h2>
            @if (api.currentSprint(); as cur) {
              <span class="current-pill">
                ★ Current Sprint: {{ cur.name }} ({{ cur.start_date }} → {{ cur.end_date }})
              </span>
            }
          </div>
          <p class="text-muted">
            Inspect what item is in what state across the project schedule. Drag &amp; drop any work item into another state lane or adjust its state directly.
          </p>
        </div>
        <div class="toolbar-actions">
          <select
            id="states-sprint-filter"
            name="states_sprint_filter"
            style="min-width: 240px;"
            [ngModel]="selectedSprint()"
            (ngModelChange)="selectedSprint.set($event)"
          >
            <option value="">All Sprints</option>
            @for (s of api.sprints(); track s.id) {
              <option [value]="s.id">
                {{ s.is_current ? '★ Current: ' + s.name : s.name }}
              </option>
            }
          </select>
          <a routerLink="/gantt" class="btn btn-secondary" style="text-align: center; line-height: 1.2;">Switch to<br />Gantt</a>
          <a routerLink="/board" class="btn btn-secondary" style="text-align: center; line-height: 1.2;">Switch to<br />Kanban</a>
          <a routerLink="/create" class="btn btn-primary">+ Create Work Item</a>
        </div>
      </div>

      @if (toastMsg()) {
        <div class="toast-banner">{{ toastMsg() }}</div>
      }

      <!-- Interactive State Drop Zones Bar -->
      <div class="state-drop-strip">
        <div class="strip-label">Drag &amp; Drop Target States:</div>
        <div class="strip-targets">
          @for (st of states; track st) {
            <div
              class="state-drop-target"
              [attr.data-state]="st"
              [class.drag-over]="dragOverState() === st"
              (dragover)="onDragOver($event, st)"
              (dragleave)="onDragLeave(st)"
              (drop)="onDropToState($event, st)"
            >
              <span class="state-dot" [attr.data-state]="st"></span>
              <span class="target-name">{{ st }}</span>
              <span class="target-count">{{ countInState(st) }}</span>
            </div>
          }
        </div>

        <div style="display: flex; align-items: center; gap: 8px; margin-left: auto;">
          <span class="strip-label">Visible Columns:</span>
          <label class="state-check-pill" [class.checked]="showStateCol()" for="states-col-state">
            <input
              type="checkbox"
              id="states-col-state"
              name="states_col_state"
              [checked]="showStateCol()"
              (change)="showStateCol.set(!showStateCol())"
            />
            <span>Current State</span>
          </label>
          <label class="state-check-pill" [class.checked]="showProgressCol()" for="states-col-progress">
            <input
              type="checkbox"
              id="states-col-progress"
              name="states_col_progress"
              [checked]="showProgressCol()"
              (change)="showProgressCol.set(!showProgressCol())"
            />
            <span>Progress</span>
          </label>
        </div>
      </div>

      <!-- States Matrix Grouped by Workflow State -->
      <div class="gantt-wrapper">
        <div class="gantt-table">
          <div class="gantt-header-row" [style.gridTemplateColumns]="gridTemplateColumns()">
            <div class="gantt-col-info">Work Element (Drag to Move State)</div>
            @if (showStateCol()) {
              <div class="gantt-col-state">Current State</div>
            }
            @if (showProgressCol()) {
              <div class="gantt-col-progress">Progress</div>
            }
            <div class="gantt-col-timeline">
              <div class="timeline-days-header">
                @for (d of timelineDays(); track d.iso) {
                  <div class="day-cell" [class.weekend]="d.isWeekend" [title]="d.iso">
                    <span class="day-month">{{ d.label }}</span>
                    <span class="day-num">{{ d.dayNum }}</span>
                  </div>
                }
              </div>
            </div>
          </div>

          @for (state of states; track state) {
            <div
              class="gantt-state-group"
              [class.drag-over]="dragOverState() === state"
              (dragover)="onDragOver($event, state)"
              (dragleave)="onDragLeave(state)"
              (drop)="onDropToState($event, state)"
            >
              <div class="gantt-group-banner" [attr.data-state]="state">
                <span class="state-dot" [attr.data-state]="state"></span>
                <strong>{{ state }}</strong>
                <span class="group-count">({{ itemsByState(state).length }} items)</span>
                <span class="group-hint">Drop work item here to transition to {{ state }}</span>
              </div>

              @for (item of itemsByState(state); track item.id) {
                <div
                  class="gantt-row"
                  [style.gridTemplateColumns]="gridTemplateColumns()"
                  [attr.draggable]="!sliderActive()"
                  [class.dragging]="draggedItem()?.id === item.id"
                  (dragstart)="onDragStart($event, item)"
                  (dragend)="onDragEnd()"
                >
                  <div class="gantt-col-info">
                    <span class="drag-handle" title="Drag to another state">⋮⋮</span>
                    <div class="states-badges-stack">
                      <span class="type-badge" [attr.data-type]="item.type">{{ item.type }}</span>
                      <span class="cat-badge" [attr.data-cat]="item.category">{{ item.category }}</span>
                    </div>
                    <a [routerLink]="['/', item.key]" class="issue-key">{{ item.key }}</a>
                    <a [routerLink]="['/', item.key]" class="gantt-item-title" [title]="item.title">
                      {{ item.title }}
                    </a>
                  </div>

                  @if (showStateCol()) {
                    <div class="gantt-col-state">
                      <select
                        class="state-select"
                        [id]="'states-item-status-' + item.id"
                        [name]="'states_item_status_' + item.id"
                        [attr.name]="'states_item_status_' + item.id"
                        [attr.data-state]="item.status"
                        [ngModel]="item.status"
                        (ngModelChange)="onQuickStateChange(item, $event)"
                      >
                        @for (st of states; track st) {
                          <option [value]="st">{{ st }}</option>
                        }
                      </select>
                    </div>
                  }

                  @if (showProgressCol()) {
                    <div
                      class="gantt-col-progress"
                      (mousedown)="onSliderStart()"
                      (pointerdown)="onSliderStart()"
                      (touchstart)="onSliderStart()"
                    >
                      <div class="gantt-progress-control">
                        <input
                          type="range"
                          draggable="false"
                          [id]="'states-item-progress-' + item.id"
                          [name]="'states_item_progress_' + item.id"
                          [attr.name]="'states_item_progress_' + item.id"
                          min="0"
                          max="100"
                          step="5"
                          [ngModel]="item.progress"
                          (mousedown)="onSliderStart()"
                          (pointerdown)="onSliderStart()"
                          (touchstart)="onSliderStart()"
                          (dragstart)="$event.preventDefault(); $event.stopPropagation()"
                          (input)="onProgressInput(item, $any($event.target).value)"
                          (change)="onProgressChange(item, $any($event.target).value)"
                        />
                        <span>{{ item.progress }}%</span>
                      </div>
                    </div>
                  }

                  <div class="gantt-col-timeline">
                    <div class="timeline-grid-bg">
                      @for (d of timelineDays(); track d.iso) {
                        <div class="grid-cell" [class.weekend]="d.isWeekend"></div>
                      }
                    </div>
                    <div
                      class="gantt-bar"
                      [attr.data-state]="item.status"
                      [style.left.%]="barLeftPercent(item)"
                      [style.width.%]="barWidthPercent(item)"
                      [title]="item.key + ': ' + (item.start_date || 'now') + ' → ' + (item.due_date || 'ongoing') + ' (' + item.status + ', ' + item.progress + '%)'"
                    >
                      <div class="gantt-bar-fill" [style.width.%]="item.progress"></div>
                      <span class="gantt-bar-label">
                        {{ item.key }} · {{ item.assignee_name || 'Unassigned' }} ({{ item.progress }}%)
                      </span>
                    </div>
                  </div>
                </div>
              } @empty {
                <div class="gantt-empty-lane">
                  No items currently in <strong>{{ state }}</strong> — drag any item row here to move it into {{ state }}.
                </div>
              }
            </div>
          }
        </div>
      </div>
    </div>
  `,
})
export class StatesComponent implements OnInit {
  readonly api = inject(ApiService);
  private readonly router = inject(Router);

  readonly states = WORKFLOW_STATES;
  readonly items = signal<WorkItem[]>([]);
  readonly selectedSprint = signal<string>('');
  readonly showStateCol = signal<boolean>(false);
  readonly showProgressCol = signal<boolean>(false);
  readonly draggedItem = signal<WorkItem | null>(null);
  readonly dragOverState = signal<WorkflowState | null>(null);
  readonly sliderActive = signal<boolean>(false);
  readonly toastMsg = signal('');

  readonly gridTemplateColumns = computed(() => {
    const cols = ['380px'];
    if (this.showStateCol()) cols.push('150px');
    if (this.showProgressCol()) cols.push('140px');
    cols.push('1fr');
    return cols.join(' ');
  });

  readonly windowStart = new Date('2026-10-14T00:00:00');
  readonly totalDays = 28;

  readonly timelineDays = computed<GanttDay[]>(() => {
    const out: GanttDay[] = [];
    const months = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
    for (let i = 0; i < this.totalDays; i++) {
      const dt = new Date(this.windowStart.getTime() + i * 86400000);
      const iso = dt.toISOString().slice(0, 10);
      const dayOfWeek = dt.getDay();
      out.push({
        iso,
        label: months[dt.getMonth()],
        dayNum: String(dt.getDate()),
        isWeekend: dayOfWeek === 0 || dayOfWeek === 6,
      });
    }
    return out;
  });

  async ngOnInit(): Promise<void> {
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
    const cur = this.api.currentSprint();
    if (cur && !this.selectedSprint()) {
      this.selectedSprint.set(String(cur.id));
    }
    const list = await this.api.getItems();
    this.items.set(list);
  }

  itemsByState(state: WorkflowState): WorkItem[] {
    const sp = this.selectedSprint();
    return this.items().filter((i) => {
      if (i.status !== state) return false;
      if (sp && String(i.sprint_id || '') !== String(sp)) return false;
      return true;
    });
  }

  countInState(state: WorkflowState): number {
    return this.itemsByState(state).length;
  }

  barLeftPercent(item: WorkItem): number {
    const s = item.start_date ? new Date(item.start_date + 'T00:00:00') : new Date('2026-10-18T00:00:00');
    const diffDays = (s.getTime() - this.windowStart.getTime()) / 86400000;
    const clamped = Math.max(0, Math.min(this.totalDays - 2, diffDays));
    return (clamped / this.totalDays) * 100;
  }

  barWidthPercent(item: WorkItem): number {
    const s = item.start_date ? new Date(item.start_date + 'T00:00:00') : new Date('2026-10-18T00:00:00');
    const e = item.due_date ? new Date(item.due_date + 'T00:00:00') : new Date(s.getTime() + 5 * 86400000);
    const spanDays = Math.max(2, (e.getTime() - s.getTime()) / 86400000 + 1);
    const leftPct = this.barLeftPercent(item);
    const rawPct = (spanDays / this.totalDays) * 100;
    return Math.min(100 - leftPct, Math.max(7, rawPct));
  }

  onSliderStart(): void {
    this.sliderActive.set(true);
  }

  onSliderEnd(): void {
    if (this.sliderActive()) {
      this.sliderActive.set(false);
    }
  }

  onDragStart(event: DragEvent, item: WorkItem): void {
    const target = event.target as HTMLElement | null;
    if (
      this.sliderActive() ||
      target?.tagName === 'INPUT' ||
      target?.tagName === 'SELECT' ||
      target?.closest('.gantt-col-progress') ||
      target?.closest('.gantt-col-state')
    ) {
      event.preventDefault();
      event.stopPropagation();
      return;
    }
    this.draggedItem.set(item);
    if (event.dataTransfer) {
      event.dataTransfer.effectAllowed = 'move';
      event.dataTransfer.setData('text/plain', String(item.id));
    }
  }

  onDragOver(event: DragEvent, state: WorkflowState): void {
    if (this.sliderActive() || !this.draggedItem()) {
      return;
    }
    event.preventDefault();
    this.dragOverState.set(state);
  }

  onDragLeave(state: WorkflowState): void {
    if (this.dragOverState() === state) {
      this.dragOverState.set(null);
    }
  }

  onDragEnd(): void {
    this.draggedItem.set(null);
    this.dragOverState.set(null);
    this.sliderActive.set(false);
  }

  async onDropToState(event: DragEvent, targetState: WorkflowState): Promise<void> {
    event.preventDefault();
    this.dragOverState.set(null);
    const item = this.draggedItem();
    this.draggedItem.set(null);
    if (!item || item.status === targetState) return;
    await this.onQuickStateChange(item, targetState);
  }

  async onQuickStateChange(item: WorkItem, newState: WorkflowState): Promise<void> {
    const oldState = item.status;
    if (oldState === newState) return;
    try {
      const updated = await this.api.transitionItemState(item.key, newState, {
        comment: `Moved from '${oldState}' to '${newState}' on States Matrix`,
      });
      this.items.update((list) => list.map((i) => (i.id === item.id ? updated : i)));
      this.showToast(`${item.key} state changed: ${oldState} → ${newState}`);
    } catch (err) {
      this.showToast(`Error updating ${item.key}: ${(err as Error).message}`);
    }
  }

  onProgressInput(item: WorkItem, valStr: string): void {
    const progress = Number(valStr) || 0;
    this.items.update((list) =>
      list.map((i) => (i.id === item.id ? { ...i, progress } : i))
    );
  }

  async onProgressChange(item: WorkItem, valStr: string): Promise<void> {
    this.sliderActive.set(false);
    const progress = Number(valStr) || 0;
    try {
      const updated = await this.api.transitionItemState(item.key, item.status, {
        progress,
        comment: `Progress updated to ${progress}% on States Matrix`,
      });
      this.items.update((list) => list.map((i) => (i.id === item.id ? updated : i)));
      this.showToast(`${item.key} progress set to ${progress}%`);
    } catch (err) {
      this.showToast(`Error updating progress: ${(err as Error).message}`);
    }
  }

  private showToast(msg: string): void {
    this.toastMsg.set(msg);
    setTimeout(() => {
      if (this.toastMsg() === msg) this.toastMsg.set('');
    }, 3500);
  }
}
