import { Component, OnInit, computed, inject, signal } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { Router, RouterLink } from '@angular/router';
import { ApiService } from '../../services/api.service';
import {
  AvailableTransition,
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

                  @if (item.labels || (item.components && item.components.length > 0) || (item.fix_versions && item.fix_versions.length > 0) || item.resolution_name) {
                    <div class="card-labels">
                      @if (item.resolution_name) {
                        <span class="label-chip" style="background: #dcfce7; color: #166534;">✔ {{ item.resolution_name }}</span>
                      }
                      @for (comp of item.components || []; track comp.id) {
                        <span class="label-chip" style="background: #e0f2fe; color: #0369a1;">🧩 {{ comp.name }}</span>
                      }
                      @for (fv of item.fix_versions || []; track fv.id) {
                        <span class="label-chip" style="background: #f3e8ff; color: #6b21a8;">🚀 {{ fv.name }}</span>
                      }
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

      <!-- Kanban Board Transition Screen Modal -->
      @if (boardTransitionModal(); as modal) {
        <div
          style="position: fixed; inset: 0; background: rgba(15, 23, 42, 0.55); z-index: 1200; display: flex; align-items: center; justify-content: center; padding: 16px;"
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
  readonly boardTransitionModal = signal<{ item: WorkItem; transition: AvailableTransition } | null>(null);
  readonly boardModalError = signal('');

  boardTransitionDraft = {
    resolution_id: 1,
    comment: '',
  };

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

  hasBoardValidator(tr: AvailableTransition, fieldName: string): boolean {
    return tr.validators.some((v) => v.rule_type === 'field_required' && v.field_name === fieldName);
  }

  hasBoardCommentValidator(tr: AvailableTransition): boolean {
    return tr.validators.some((v) => v.rule_type === 'comment_required');
  }

  closeBoardTransitionModal(): void {
    this.boardTransitionModal.set(null);
    this.boardModalError.set('');
  }

  async submitBoardTransitionModal(): Promise<void> {
    const modal = this.boardTransitionModal();
    if (!modal) return;
    try {
      const updated = await this.api.transitionItemState(modal.item.key, modal.transition.to_status, {
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

  async onDrop(event: DragEvent, targetState: WorkflowState): Promise<void> {
    event.preventDefault();
    this.dragOverState.set(null);
    const item = this.draggedItem();
    this.draggedItem.set(null);

    if (!item || item.status === targetState) {
      return;
    }

    try {
      const wfPayload = await this.api.getItemTransitions(item.key);
      const tr = wfPayload.transitions.find((t) => t.to_status === targetState);
      if (!tr) {
        this.showToast(
          `No directed transition from "${item.status}" to "${targetState}" in '${wfPayload.workflow_name}'`
        );
        return;
      }
      if (!tr.allowed) {
        this.showToast(tr.blocked_reasons[0] || `Transition to "${targetState}" blocked by workflow condition`);
        return;
      }
      if (tr.requires_screen) {
        const defRes = item.resolution_id || this.api.resolutions().find((r) => r.is_default)?.id || 1;
        this.boardTransitionDraft = {
          resolution_id: targetState === 'Done' ? defRes : item.resolution_id || 0,
          comment: '',
        };
        this.boardModalError.set('');
        this.boardTransitionModal.set({ item, transition: tr });
        return;
      }

      const prevStatus = item.status;
      const updated = await this.api.transitionItemState(item.key, targetState, {
        transition_id: tr.id,
        comment: `Moved from '${prevStatus}' to '${targetState}' on Kanban Board`,
      });
      this.items.update((list) => list.map((i) => (i.id === item.id ? updated : i)));
      this.showToast(`${item.key} moved from "${prevStatus}" → "${targetState}"`);
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
