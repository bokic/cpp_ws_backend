import { Component, OnInit, computed, inject, signal } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { Router, RouterLink } from '@angular/router';
import { ApiService } from '../../services/api.service';
import {
  WORKFLOW_STATES,
  WorkItem,
  WorkItemLink,
  WorkflowState,
} from '../../models/jira.models';

interface GanttDay {
  iso: string;
  label: string;
  dayNum: string;
  isWeekend: boolean;
}

interface TreeGridRow {
  item: WorkItem;
  depth: number;
  hasChildren: boolean;
  expanded: boolean;
  isLastChild: boolean;
  dependencies: {
    id: number;
    relation: string;
    otherId: number;
    otherKey: string;
    otherTitle: string;
    isBlocker: boolean;
  }[];
}

interface DependencyLine {
  id: number;
  path: string;
  color: string;
  label: string;
}

@Component({
  selector: 'app-gantt',
  standalone: true,
  imports: [CommonModule, FormsModule, RouterLink],
  template: `
    <div class="page-container">
      <div class="page-toolbar">
        <div>
          <h2>Gantt Tree-Grid &amp; Dependencies</h2>
          <p class="text-muted">
            Hierarchical tree-grid of Epics, Stories, Features, Tasks, Subtasks, and their blocking/dependency chains across the timeline.
          </p>
        </div>
        <div class="toolbar-actions">
          <button type="button" class="btn btn-secondary btn-sm" (click)="expandAll()">
            ⊞ Expand All
          </button>
          <button type="button" class="btn btn-secondary btn-sm" (click)="collapseAll()">
            ⊟ Collapse All
          </button>
          <a routerLink="/states" class="btn btn-secondary">Open States Matrix</a>
          <a routerLink="/board" class="btn btn-secondary">Open Kanban Board</a>
          <a routerLink="/create" class="btn btn-primary">+ Create Work Item</a>
        </div>
      </div>

      <!-- Filter Bar: Sprints Filter (Default: All) & States Filter (Default: All Checked) -->
      <div class="gantt-filter-panel">
        <div class="gantt-filter-row">
          <div class="sprint-filter-box">
            <label class="filter-label" for="gantt-sprint-filter">Sprint Filter:</label>
            <select
              id="gantt-sprint-filter"
              name="gantt_sprint_filter"
              [ngModel]="selectedSprint()"
              (ngModelChange)="selectedSprint.set($event)"
            >
              <option value="">All Sprints (Default)</option>
              @for (s of api.sprints(); track s.id) {
                <option [value]="s.id">
                  {{ s.is_current ? '★ Current: ' + s.name : s.name }}
                </option>
              }
            </select>
          </div>

          <div class="states-checkbox-group">
            <div class="states-filter-header">
              <span class="filter-label">Workflow States Filter:</span>
              <button type="button" class="btn-link-xs" (click)="checkAllStates()">Check All</button>
              <span>·</span>
              <button type="button" class="btn-link-xs" (click)="uncheckAllStates()">Uncheck All</button>
            </div>
            <div class="states-checkboxes">
              @for (st of states; track st; let idx = $index) {
                <label
                  class="state-check-pill"
                  [class.checked]="isStateChecked(st)"
                  [attr.for]="'gantt-state-' + idx"
                >
                  <input
                    type="checkbox"
                    [id]="'gantt-state-' + idx"
                    [attr.name]="'gantt_state_' + idx"
                    [checked]="isStateChecked(st)"
                    (change)="toggleState(st)"
                  />
                  <span class="state-dot" [attr.data-state]="st"></span>
                  <span>{{ st }}</span>
                </label>
              }
            </div>

            <!-- Column Visibility Checkboxes (Unchecked by Default) -->
            <div style="margin-top: 10px; padding-top: 10px; border-top: 1px solid #ebecf0;">
              <div class="states-filter-header">
                <span class="filter-label">Visible Tree-Grid Columns:</span>
              </div>
              <div class="states-checkboxes">
                <label class="state-check-pill" [class.checked]="showDepsCol()" for="gantt-col-deps">
                  <input
                    type="checkbox"
                    id="gantt-col-deps"
                    name="gantt_col_deps"
                    [checked]="showDepsCol()"
                    (change)="showDepsCol.set(!showDepsCol())"
                  />
                  <span>Dependencies &amp; Links</span>
                </label>

                <label class="state-check-pill" [class.checked]="showSprintCol()" for="gantt-col-sprint">
                  <input
                    type="checkbox"
                    id="gantt-col-sprint"
                    name="gantt_col_sprint"
                    [checked]="showSprintCol()"
                    (change)="showSprintCol.set(!showSprintCol())"
                  />
                  <span>Sprint</span>
                </label>

                <label class="state-check-pill" [class.checked]="showStateCol()" for="gantt-col-state">
                  <input
                    type="checkbox"
                    id="gantt-col-state"
                    name="gantt_col_state"
                    [checked]="showStateCol()"
                    (change)="showStateCol.set(!showStateCol())"
                  />
                  <span>State</span>
                </label>
              </div>
            </div>
          </div>
        </div>
      </div>

      @if (toastMsg()) {
        <div class="toast-banner">{{ toastMsg() }}</div>
      }

      <!-- Tree-Grid + Gantt Chart Widget -->
      <div class="gantt-wrapper">
        <div class="tree-gantt-table">
          <!-- Header Row -->
          <div class="tree-gantt-header" [style.gridTemplateColumns]="gridTemplateColumns()">
            <div class="tg-col-tree">Work Item Hierarchy (Tree-Grid)</div>
            @if (showDepsCol()) {
              <div class="tg-col-deps">Dependencies &amp; Links</div>
            }
            @if (showSprintCol()) {
              <div class="tg-col-sprint">Sprint</div>
            }
            @if (showStateCol()) {
              <div class="tg-col-state">State</div>
            }
            <div class="tg-col-timeline">
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

          <!-- Body Container with SVG Dependency Overlay -->
          <div class="tree-gantt-body">
            @for (row of visibleTreeRows(); track row.item.id) {
              <div
                class="tree-gantt-row"
                [class.root-row]="row.depth === 0"
                [style.gridTemplateColumns]="gridTemplateColumns()"
              >
                <!-- Tree-Grid Hierarchy Column -->
                <div class="tg-col-tree" [style.paddingLeft.px]="10 + row.depth * 22">
                  @if (row.depth > 0) {
                    <span class="tree-branch">{{ row.isLastChild ? '└─' : '├─' }}</span>
                  }
                  @if (row.hasChildren) {
                    <button
                      type="button"
                      class="tree-toggle-btn"
                      (click)="toggleExpand(row.item.id)"
                      [title]="row.expanded ? 'Collapse children' : 'Expand children'"
                    >
                      {{ row.expanded ? '▼' : '▶' }}
                    </button>
                  } @else {
                    <span class="tree-leaf-spacer">•</span>
                  }
                  <span class="type-badge" [attr.data-type]="row.item.type">
                    {{ row.item.type }}
                  </span>
                  <span class="cat-badge" [attr.data-cat]="row.item.category">
                    {{ row.item.category }}
                  </span>
                  <a [routerLink]="['/', row.item.key]" class="issue-key">
                    {{ row.item.key }}
                  </a>
                  <a
                    [routerLink]="['/', row.item.key]"
                    class="gantt-item-title"
                    [title]="row.item.title"
                  >
                    {{ row.item.title }}
                  </a>
                </div>

                <!-- Dependencies Column -->
                @if (showDepsCol()) {
                  <div class="tg-col-deps">
                    @if (row.dependencies.length > 0) {
                      <div class="dep-badges-wrap">
                        @for (dep of row.dependencies; track dep.id) {
                          <a
                            [routerLink]="['/', dep.otherKey]"
                            class="dep-chip"
                            [class.dep-blocker]="dep.isBlocker"
                            [title]="dep.relation + ' ' + dep.otherKey + ': ' + dep.otherTitle"
                          >
                            {{ dep.relation }} {{ dep.otherKey }}
                          </a>
                        }
                      </div>
                    } @else if (row.item.parent_key) {
                      <span class="dep-parent-ref" [title]="'Child of ' + row.item.parent_key">
                        ↳ {{ row.item.parent_key }}
                      </span>
                    } @else {
                      <span class="text-muted">—</span>
                    }
                  </div>
                }

                <!-- Sprint Column -->
                @if (showSprintCol()) {
                  <div class="tg-col-sprint">
                    <span class="sprint-cell-text" [title]="row.item.sprint_name || 'Backlog'">
                      {{ row.item.sprint_name || 'Backlog' }}
                    </span>
                  </div>
                }

                <!-- State Column -->
                @if (showStateCol()) {
                  <div class="tg-col-state">
                    <select
                      class="state-select"
                      [id]="'gantt-row-state-' + row.item.id"
                      [name]="'gantt_row_state_' + row.item.id"
                      [attr.name]="'gantt_row_state_' + row.item.id"
                      [attr.data-state]="row.item.status"
                      [ngModel]="row.item.status"
                      (ngModelChange)="onQuickStateChange(row.item, $event)"
                    >
                      @for (st of states; track st) {
                        <option [value]="st">{{ st }}</option>
                      }
                    </select>
                  </div>
                }

                <!-- Gantt Timeline Column -->
                <div class="tg-col-timeline">
                  <div class="timeline-grid-bg">
                    @for (d of timelineDays(); track d.iso) {
                      <div class="grid-cell" [class.weekend]="d.isWeekend"></div>
                    }
                  </div>

                  <div
                    class="gantt-bar"
                    [class.epic-bar]="row.item.type === 'Epic'"
                    [attr.data-state]="row.item.status"
                    [style.left.%]="barLeftPercent(row.item)"
                    [style.width.%]="barWidthPercent(row.item)"
                    [title]="
                      row.item.key +
                      ' (' +
                      row.item.type +
                      '): ' +
                      (row.item.start_date || 'now') +
                      ' → ' +
                      (row.item.due_date || 'ongoing') +
                      ' · ' +
                      row.item.status +
                      ' (' +
                      row.item.progress +
                      '%)'
                    "
                  >
                    <div class="gantt-bar-fill" [style.width.%]="row.item.progress"></div>
                    <span class="gantt-bar-label">
                      {{ row.item.key }} ({{ row.item.progress }}%)
                    </span>
                  </div>
                </div>
              </div>
            } @empty {
              <div class="gantt-empty-lane" style="padding: 24px; text-align: center;">
                No work items match the selected Sprint and Workflow State filters.
              </div>
            }

            <!-- SVG Overlay for Item Dependency Connector Lines on Timeline -->
            <svg
              class="gantt-dep-svg"
              [style.left.px]="timelineLeftOffsetPx()"
              [style.width]="'calc(100% - ' + timelineLeftOffsetPx() + 'px)'"
              [attr.viewBox]="'0 0 1000 ' + visibleTreeRows().length * 42"
              preserveAspectRatio="none"
            >
              <defs>
                <marker
                  id="dep-arrow"
                  viewBox="0 0 10 10"
                  refX="8"
                  refY="5"
                  markerWidth="6"
                  markerHeight="6"
                  orient="auto-start-reverse"
                >
                  <path d="M 0 1 L 10 5 L 0 9 z" fill="#de350b" />
                </marker>
                <marker
                  id="parent-arrow"
                  viewBox="0 0 10 10"
                  refX="8"
                  refY="5"
                  markerWidth="5"
                  markerHeight="5"
                  orient="auto-start-reverse"
                >
                  <path d="M 0 1 L 10 5 L 0 9 z" fill="#6554c0" />
                </marker>
              </defs>
              @for (line of dependencyLines(); track line.id) {
                <path
                  [attr.d]="line.path"
                  fill="none"
                  [attr.stroke]="line.color"
                  stroke-width="2"
                  stroke-dasharray="4 2"
                  [attr.marker-end]="line.color === '#de350b' ? 'url(#dep-arrow)' : 'url(#parent-arrow)'"
                />
              }
            </svg>
          </div>
        </div>
      </div>
    </div>
  `,
})
export class GanttComponent implements OnInit {
  readonly api = inject(ApiService);
  private readonly router = inject(Router);

  readonly states = WORKFLOW_STATES;
  readonly items = signal<WorkItem[]>([]);
  readonly links = signal<WorkItemLink[]>([]);

  // Default Sprint filter: All Sprints ("")
  readonly selectedSprint = signal<string>('');
  // Default States filter: All workflow states checked except 'Backlog'
  readonly checkedStates = signal<Set<WorkflowState>>(
    new Set(WORKFLOW_STATES.filter((s) => s !== 'Backlog'))
  );
  // Optional Tree-Grid columns (unchecked by default)
  readonly showDepsCol = signal<boolean>(false);
  readonly showSprintCol = signal<boolean>(false);
  readonly showStateCol = signal<boolean>(false);

  readonly gridTemplateColumns = computed(() => {
    const cols = ['380px'];
    if (this.showDepsCol()) cols.push('180px');
    if (this.showSprintCol()) cols.push('150px');
    if (this.showStateCol()) cols.push('140px');
    cols.push('1fr');
    return cols.join(' ');
  });

  readonly timelineLeftOffsetPx = computed(() => {
    let offset = 380;
    if (this.showDepsCol()) offset += 180;
    if (this.showSprintCol()) offset += 150;
    if (this.showStateCol()) offset += 140;
    return offset;
  });
  //Collapsed parent node IDs in the Tree-Grid
  readonly collapsedIds = signal<Set<number>>(new Set());
  readonly toastMsg = signal('');

  readonly windowStart = new Date('2026-10-14T00:00:00');
  readonly totalDays = 32;

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

  readonly filteredItems = computed<WorkItem[]>(() => {
    const sp = this.selectedSprint();
    const activeStates = this.checkedStates();
    return this.items().filter((it) => {
      if (!activeStates.has(it.status)) return false;
      if (sp && String(it.sprint_id || '') !== String(sp)) return false;
      return true;
    });
  });

  readonly visibleTreeRows = computed<TreeGridRow[]>(() => {
    const list = this.filteredItems();
    const allLinks = this.links();
    const collapsed = this.collapsedIds();
    const idSet = new Set(list.map((i) => i.id));

    const childrenMap = new Map<number, WorkItem[]>();
    const roots: WorkItem[] = [];

    for (const it of list) {
      const pid = it.parent_id;
      if (pid && idSet.has(pid)) {
        const arr = childrenMap.get(pid) || [];
        arr.push(it);
        childrenMap.set(pid, arr);
      } else {
        roots.push(it);
      }
    }

    const buildDependencies = (itemId: number) => {
      const out: TreeGridRow['dependencies'] = [];
      for (const lk of allLinks) {
        if (lk.source_item_id === itemId) {
          const rel =
            lk.link_type === 'blocks'
              ? '⛔ Blocks'
              : lk.link_type === 'is_blocked_by'
                ? '⛓ Dep on'
                : '⇄ Relates';
          out.push({
            id: lk.id,
            relation: rel,
            otherId: lk.target_item_id,
            otherKey: lk.target_key,
            otherTitle: lk.target_title,
            isBlocker: lk.link_type === 'blocks' || lk.link_type === 'is_blocked_by',
          });
        } else if (lk.target_item_id === itemId) {
          const rel =
            lk.link_type === 'blocks'
              ? '⛓ Blocked by'
              : lk.link_type === 'is_blocked_by'
                ? '⛔ Blocks'
                : '⇄ Relates';
          out.push({
            id: lk.id * 10000 + 1,
            relation: rel,
            otherId: lk.source_item_id,
            otherKey: lk.source_key,
            otherTitle: lk.source_title,
            isBlocker: lk.link_type === 'blocks' || lk.link_type === 'is_blocked_by',
          });
        }
      }
      return out;
    };

    const rows: TreeGridRow[] = [];
    const visit = (node: WorkItem, depth: number, isLast: boolean) => {
      const children = childrenMap.get(node.id) || [];
      const isExpanded = !collapsed.has(node.id);
      rows.push({
        item: node,
        depth,
        hasChildren: children.length > 0,
        expanded: isExpanded,
        isLastChild: isLast,
        dependencies: buildDependencies(node.id),
      });
      if (isExpanded) {
        children.forEach((child, idx) => {
          visit(child, depth + 1, idx === children.length - 1);
        });
      }
    };

    roots.forEach((root, idx) => {
      visit(root, 0, idx === roots.length - 1);
    });

    return rows;
  });

  readonly dependencyLines = computed<DependencyLine[]>(() => {
    const rows = this.visibleTreeRows();
    const rowIdxById = new Map<number, number>();
    rows.forEach((r, idx) => rowIdxById.set(r.item.id, idx));

    const lines: DependencyLine[] = [];
    const ROW_H = 42;

    // 1. Explicit issue dependency links (blocks / relates_to)
    for (const lk of this.links()) {
      const sIdx = rowIdxById.get(lk.source_item_id);
      const tIdx = rowIdxById.get(lk.target_item_id);
      if (sIdx === undefined || tIdx === undefined) continue;

      const sItem = rows[sIdx].item;
      const tItem = rows[tIdx].item;

      const x1 = (this.barLeftPercent(sItem) + this.barWidthPercent(sItem)) * 10;
      const y1 = sIdx * ROW_H + 21;
      const x2 = this.barLeftPercent(tItem) * 10;
      const y2 = tIdx * ROW_H + 21;
      const midX = (x1 + x2) / 2;

      lines.push({
        id: lk.id,
        path: `M ${x1} ${y1} C ${midX} ${y1}, ${midX} ${y2}, ${x2} ${y2}`,
        color: '#de350b',
        label: lk.link_type,
      });
    }

    return lines;
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
    const [itemList, linkList] = await Promise.all([
      this.api.getItems(),
      this.api.getAllLinks(),
    ]);
    this.items.set(itemList);
    this.links.set(linkList);
  }

  isStateChecked(st: WorkflowState): boolean {
    return this.checkedStates().has(st);
  }

  toggleState(st: WorkflowState): void {
    const next = new Set(this.checkedStates());
    if (next.has(st)) {
      next.delete(st);
    } else {
      next.add(st);
    }
    this.checkedStates.set(next);
  }

  checkAllStates(): void {
    this.checkedStates.set(new Set(WORKFLOW_STATES));
  }

  uncheckAllStates(): void {
    this.checkedStates.set(new Set());
  }

  toggleExpand(itemId: number): void {
    const next = new Set(this.collapsedIds());
    if (next.has(itemId)) {
      next.delete(itemId);
    } else {
      next.add(itemId);
    }
    this.collapsedIds.set(next);
  }

  expandAll(): void {
    this.collapsedIds.set(new Set());
  }

  collapseAll(): void {
    const allParentIds = new Set<number>();
    for (const it of this.items()) {
      if (it.subtask_count > 0 || it.type === 'Epic') {
        allParentIds.add(it.id);
      }
      if (it.parent_id) {
        allParentIds.add(it.parent_id);
      }
    }
    this.collapsedIds.set(allParentIds);
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

  async onQuickStateChange(item: WorkItem, newState: WorkflowState): Promise<void> {
    const oldState = item.status;
    if (oldState === newState) return;
    try {
      const updated = await this.api.transitionItemState(item.key, newState, {
        comment: `Changed state from '${oldState}' to '${newState}' on Gantt Tree-Grid`,
      });
      this.items.update((list) => list.map((i) => (i.id === item.id ? updated : i)));
      this.showToast(`${item.key} state updated: ${oldState} → ${newState}`);
    } catch (err) {
      this.showToast(`Error updating ${item.key}: ${(err as Error).message}`);
    }
  }

  private showToast(msg: string): void {
    this.toastMsg.set(msg);
    setTimeout(() => {
      if (this.toastMsg() === msg) this.toastMsg.set('');
    }, 3200);
  }
}
