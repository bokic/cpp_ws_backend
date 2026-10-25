import { Component, OnInit, computed, inject, signal } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { Router, RouterLink } from '@angular/router';
import { ApiService } from '../../services/api.service';
import {
  WORKFLOW_STATES,
  WORK_ITEM_CATEGORIES,
  WORK_ITEM_TYPES,
  WorkItem,
  WorkItemCategory,
  WorkItemType,
  WorkflowState,
} from '../../models/jira.models';

@Component({
  selector: 'app-board',
  standalone: true,
  imports: [CommonModule, FormsModule, RouterLink],
  template: `
    <div class="page-container">
      <div class="page-toolbar">
        <div>
          <div style="display: flex; align-items: center; gap: 10px; flex-wrap: wrap;">
            <h2>Kanban Workflow State Board</h2>
            @if (api.currentSprint(); as cur) {
              <span class="current-pill">
                ★ Current Sprint: {{ cur.name }} ({{ cur.start_date }} → {{ cur.end_date }})
              </span>
            }
          </div>
          <p class="text-muted">
            Drag and drop work items across state columns to transition their workflow status.
          </p>
        </div>
        <div class="toolbar-actions">
          @if (api.currentUser()?.role === 'Admin') {
            <a routerLink="/sprints" class="btn btn-secondary">⚙ Sprint Editor</a>
          }
          <a routerLink="/gantt" class="btn btn-secondary">Open Gantt</a>
          <a routerLink="/states" class="btn btn-secondary">Open States</a>
          <a routerLink="/create" class="btn btn-primary">+ Create Work Item</a>
        </div>
      </div>

      <!-- Filter Bar -->
      <div class="filter-bar">
        <div class="search-box">
          <input
            type="text"
            id="board-search"
            name="board_search"
            autocomplete="off"
            placeholder="Search key, title, description, labels..."
            [ngModel]="searchQuery()"
            (ngModelChange)="searchQuery.set($event)"
          />
        </div>

        <div class="type-pills">
          <button
            type="button"
            class="pill"
            [class.active]="selectedType() === ''"
            (click)="selectedType.set('')"
          >
            All Types ({{ itemsMatchingOtherFilters().length }})
          </button>
          @for (t of itemTypes; track t) {
            <button
              type="button"
              class="pill"
              [class.active]="selectedType() === t"
              (click)="selectedType.set(t)"
            >
              <span class="type-dot" [attr.data-type]="t"></span>
              {{ t }} ({{ countByType(t) }})
            </button>
          }
        </div>

        <div class="filter-selects">
          <select
            id="board-category-filter"
            name="board_category_filter"
            [ngModel]="selectedCategory()"
            (ngModelChange)="selectedCategory.set($event)"
          >
            <option value="">All Categories</option>
            @for (cat of itemCategories; track cat) {
              <option [value]="cat">{{ cat }}</option>
            }
          </select>

          <select
            id="board-assignee-filter"
            name="board_assignee_filter"
            [ngModel]="selectedAssignee()"
            (ngModelChange)="selectedAssignee.set($event)"
          >
            <option value="">All Assignees</option>
            @for (u of api.users(); track u.id) {
              <option [value]="u.id">{{ u.display_name }}</option>
            }
          </select>

          <select
            id="board-sprint-filter"
            name="board_sprint_filter"
            [ngModel]="selectedSprint()"
            (ngModelChange)="selectedSprint.set($event)"
          >
            <option value="">All Sprints (Including Backlog)</option>
            @for (s of api.sprints(); track s.id) {
              <option [value]="s.id">
                {{ s.is_current ? '★ Current: ' + s.name : s.name }}
              </option>
            }
          </select>
        </div>
      </div>

      @if (toastMsg()) {
        <div class="toast-banner">{{ toastMsg() }}</div>
      }

      <!-- Kanban Columns -->
      <div class="kanban-board">
        @for (state of states; track state) {
          <div
            class="kanban-column"
            [class.drag-over]="dragOverState() === state"
            (dragover)="onDragOver($event, state)"
            (dragleave)="onDragLeave(state)"
            (drop)="onDrop($event, state)"
          >
            <div class="column-header" [attr.data-state]="state">
              <div class="column-title">
                <span class="state-dot" [attr.data-state]="state"></span>
                <span>{{ state }}</span>
              </div>
              <span class="column-count">{{ itemsByState(state).length }}</span>
            </div>

            <div class="column-body">
              @for (item of itemsByState(state); track item.id) {
                <div
                  class="issue-card"
                  draggable="true"
                  [class.dragging]="draggedItem()?.id === item.id"
                  (dragstart)="onDragStart($event, item)"
                  (dragend)="onDragEnd()"
                >
                  <div class="card-top">
                    <a [routerLink]="['/', item.key]" class="issue-key">{{ item.key }}</a>
                    <span class="priority-badge" [attr.data-priority]="item.priority">
                      {{ item.priority }}
                    </span>
                  </div>

                  <div class="card-top">
                    <span class="type-badge" [attr.data-type]="item.type">{{ item.type }}</span>
                    <span class="cat-badge" [attr.data-cat]="item.category">{{ item.category }}</span>
                  </div>

                  @if (item.parent_key) {
                    <div class="parent-chip">
                      ↳ {{ item.parent_key }}: {{ item.parent_title }}
                    </div>
                  }

                  <a [routerLink]="['/', item.key]" class="card-title">
                    {{ item.title }}
                  </a>

                  @if (item.labels) {
                    <div class="card-labels">
                      @for (lbl of splitLabels(item.labels); track lbl) {
                        <span class="label-chip">{{ lbl }}</span>
                      }
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
                      @if (item.story_points > 0) {
                        <span class="points-badge" title="Story Points">{{ item.story_points }} SP</span>
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
              } @empty {
                <div class="empty-column-drop">
                  Drop item here to move to <strong>{{ state }}</strong>
                </div>
              }
            </div>
          </div>
        }
      </div>
    </div>
  `,
})
export class BoardComponent implements OnInit {
  readonly api = inject(ApiService);
  private readonly router = inject(Router);

  readonly states = WORKFLOW_STATES;
  readonly itemTypes = WORK_ITEM_TYPES;
  readonly itemCategories = WORK_ITEM_CATEGORIES;

  readonly items = signal<WorkItem[]>([]);
  readonly searchQuery = signal('');
  readonly selectedType = signal<WorkItemType | ''>('');
  readonly selectedCategory = signal<WorkItemCategory | ''>('');
  readonly selectedAssignee = signal<string>('');
  readonly selectedSprint = signal<string>('');

  readonly draggedItem = signal<WorkItem | null>(null);
  readonly dragOverState = signal<WorkflowState | null>(null);
  readonly toastMsg = signal('');

  readonly itemsMatchingOtherFilters = computed(() => {
    const q = this.searchQuery().trim().toLowerCase();
    const c = this.selectedCategory();
    const a = this.selectedAssignee();
    const s = this.selectedSprint();

    return this.items().filter((item) => {
      if (c && item.category !== c) return false;
      if (a && String(item.assignee_id || '') !== String(a)) return false;
      if (s && String(item.sprint_id || '') !== String(s)) return false;
      if (q) {
        const hay = `${item.key} ${item.title} ${item.description} ${item.labels} ${item.category}`.toLowerCase();
        if (!hay.includes(q)) return false;
      }
      return true;
    });
  });

  readonly filteredItems = computed(() => {
    const t = this.selectedType();
    const base = this.itemsMatchingOtherFilters();
    return t ? base.filter((item) => item.type === t) : base;
  });

  countByType(type: WorkItemType): number {
    return this.itemsMatchingOtherFilters().filter((item) => item.type === type).length;
  }

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
      const cur = this.api.currentSprint();
      if (cur && !this.selectedSprint()) {
        this.selectedSprint.set(String(cur.id));
      }
      await this.loadItems();
    } catch {
      // ignore
    }
  }

  async loadItems(): Promise<void> {
    const list = await this.api.getItems();
    this.items.set(list);
  }

  itemsByState(state: WorkflowState): WorkItem[] {
    return this.filteredItems().filter((i) => i.status === state);
  }

  splitLabels(labels: string): string[] {
    return labels
      .split(',')
      .map((s) => s.trim())
      .filter(Boolean);
  }

  initials(name: string): string {
    return name
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

  onDragOver(event: DragEvent, state: WorkflowState): void {
    event.preventDefault();
    if (event.dataTransfer) {
      event.dataTransfer.dropEffect = 'move';
    }
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
  }

  async onDrop(event: DragEvent, targetState: WorkflowState): Promise<void> {
    event.preventDefault();
    this.dragOverState.set(null);
    const item = this.draggedItem();
    this.draggedItem.set(null);

    if (!item || item.status === targetState) {
      return;
    }

    const prevStatus = item.status;
    // Optimistic UI update
    this.items.update((list) =>
      list.map((i) => (i.id === item.id ? { ...i, status: targetState } : i))
    );

    try {
      const updated = await this.api.transitionItemState(item.key, targetState, {
        comment: `Moved from '${prevStatus}' to '${targetState}' on Kanban Board`,
      });
      this.items.update((list) => list.map((i) => (i.id === item.id ? updated : i)));
      this.showToast(`${item.key} moved from "${prevStatus}" → "${targetState}"`);
    } catch (err) {
      this.items.update((list) =>
        list.map((i) => (i.id === item.id ? { ...i, status: prevStatus } : i))
      );
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
