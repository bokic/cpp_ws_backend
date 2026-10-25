import { Component, OnInit, inject, signal } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { ActivatedRoute, Router, RouterLink } from '@angular/router';
import { ApiService } from '../../services/api.service';
import {
  PRIORITIES,
  PriorityType,
  WORKFLOW_STATES,
  WORK_ITEM_CATEGORIES,
  WORK_ITEM_TYPES,
  WorkItem,
  WorkItemCategory,
  WorkItemType,
  WorkflowState,
} from '../../models/jira.models';

@Component({
  selector: 'app-create-item',
  standalone: true,
  imports: [CommonModule, FormsModule, RouterLink],
  template: `
    <div class="page-container narrow-container">
      <div class="page-toolbar">
        <div>
          <h2>Create Work Item</h2>
          <p class="text-muted">
            Create a Story, Task, Subtask, Issue, Bug, or Epic and attach files to <code>blobs/</code>.
          </p>
        </div>
        <div class="toolbar-actions">
          <a routerLink="/board" class="btn btn-secondary">Cancel &amp; Back to Board</a>
        </div>
      </div>

      @if (errorMsg()) {
        <div class="alert alert-error">{{ errorMsg() }}</div>
      }

      <form (ngSubmit)="submitCreate()" class="card-panel form-panel">
        <!-- 1. Work Element Type (Hierarchy) Selector Pills -->
        <div class="form-group">
          <label>1. Work Element Type (Hierarchy Level)</label>
          <div class="type-selector-grid">
            @for (t of itemTypes; track t) {
              <button
                type="button"
                class="type-choice-btn"
                [class.selected]="form.type === t"
                [attr.data-type]="t"
                (click)="form.type = t"
              >
                <span class="type-dot" [attr.data-type]="t"></span>
                <div>
                  <strong>{{ t }}</strong>
                  <small>{{ typeDescription(t) }}</small>
                </div>
              </button>
            }
          </div>
        </div>

        <!-- 2. Work Category Selector Pills -->
        <div class="form-group">
          <label>2. Work Category (Nature of Work)</label>
          <div class="type-selector-grid">
            @for (cat of itemCategories; track cat) {
              <button
                type="button"
                class="type-choice-btn"
                [class.selected]="form.category === cat"
                [attr.data-cat]="cat"
                (click)="form.category = cat"
              >
                <span class="cat-dot" [attr.data-cat]="cat"></span>
                <div>
                  <strong>{{ cat }}</strong>
                  <small>{{ categoryDescription(cat) }}</small>
                </div>
              </button>
            }
          </div>
        </div>

        <div class="grid-3">
          <div class="form-group">
            <label for="create-project-id">Project</label>
            <select id="create-project-id" name="project_id" [(ngModel)]="form.project_id">
              @for (p of api.projects(); track p.id) {
                <option [ngValue]="p.id">{{ p.key }} — {{ p.name }}</option>
              }
            </select>
          </div>

          <div class="form-group">
            <label for="create-status">Initial Workflow State</label>
            <select id="create-status" name="status" [(ngModel)]="form.status">
              @for (s of states; track s) {
                <option [value]="s">{{ s }}</option>
              }
            </select>
          </div>

          <div class="form-group">
            <label for="create-priority">Priority</label>
            <select id="create-priority" name="priority" [(ngModel)]="form.priority">
              @for (pr of priorities; track pr) {
                <option [value]="pr">{{ pr }}</option>
              }
            </select>
          </div>
        </div>

        <div class="form-group">
          <label for="create-title">Title / Summary *</label>
          <input
            type="text"
            id="create-title"
            name="title"
            autocomplete="off"
            [(ngModel)]="form.title"
            required
            placeholder="e.g., Implement OAuth2 token refresh or Fix Kanban card overflow"
          />
        </div>

        <div class="form-group">
          <label for="create-description">Description</label>
          <textarea
            id="create-description"
            name="description"
            [(ngModel)]="form.description"
            rows="4"
            placeholder="Detailed acceptance criteria, context, or technical notes..."
          ></textarea>
        </div>

        @if (form.category === 'Bug') {
          <div class="bug-fields-box">
            <h4>Bug Report Details</h4>
            <div class="grid-2">
              <div class="form-group">
                <label for="create-severity">Defect Severity</label>
                <select id="create-severity" name="severity" [(ngModel)]="form.severity">
                  <option value="Minor">Minor</option>
                  <option value="Major">Major</option>
                  <option value="Critical">Critical</option>
                  <option value="Blocker">Blocker</option>
                </select>
              </div>
              <div class="form-group">
                <label for="create-steps">Steps to Reproduce</label>
                <textarea
                  id="create-steps"
                  name="steps_to_reproduce"
                  [(ngModel)]="form.steps_to_reproduce"
                  rows="3"
                  placeholder="1. Navigate to...\n2. Click...\n3. Observe error..."
                ></textarea>
              </div>
            </div>
          </div>
        }

        <div class="grid-3">
          <div class="form-group">
            <label for="create-parent-id">
              {{ form.type === 'Subtask' ? 'Parent Story / Task / Issue *' : 'Parent Epic / Work Item (Optional)' }}
            </label>
            <select id="create-parent-id" name="parent_id" [(ngModel)]="form.parent_id">
              <option [ngValue]="0">— None —</option>
              @for (it of existingItems(); track it.id) {
                <option [ngValue]="it.id">[{{ it.type }}] {{ it.key }}: {{ it.title }}</option>
              }
            </select>
          </div>

          <div class="form-group">
            <label for="create-assignee-id">Assignee</label>
            <select id="create-assignee-id" name="assignee_id" [(ngModel)]="form.assignee_id">
              <option [ngValue]="0">— Unassigned —</option>
              @for (u of api.users(); track u.id) {
                <option [ngValue]="u.id">{{ u.display_name }} ({{ u.role }})</option>
              }
            </select>
          </div>

          <div class="form-group">
            <label for="create-sprint-id">Sprint (Defaults to Current Sprint)</label>
            <select id="create-sprint-id" name="sprint_id" [(ngModel)]="form.sprint_id">
              <option [ngValue]="0">— Backlog (No Sprint) —</option>
              @for (sp of api.sprints(); track sp.id) {
                <option [ngValue]="sp.id">
                  {{ sp.is_current ? '★ Current Sprint: ' + sp.name : sp.name + ' (' + sp.status + ')' }}
                </option>
              }
            </select>
          </div>
        </div>

        <div class="grid-4">
          <div class="form-group">
            <label for="create-story-points">Story Points</label>
            <input
              type="number"
              id="create-story-points"
              name="story_points"
              min="0"
              max="100"
              [(ngModel)]="form.story_points"
            />
          </div>

          <div class="form-group">
            <label for="create-start-date">Start Date (Gantt)</label>
            <input type="date" id="create-start-date" name="start_date" [(ngModel)]="form.start_date" />
          </div>

          <div class="form-group">
            <label for="create-due-date">Due Date (Gantt)</label>
            <input type="date" id="create-due-date" name="due_date" [(ngModel)]="form.due_date" />
          </div>

          <div class="form-group">
            <label for="create-labels">Labels (comma-separated)</label>
            <input
              type="text"
              id="create-labels"
              name="labels"
              autocomplete="off"
              [(ngModel)]="form.labels"
              placeholder="backend, sqlite, urgent"
            />
          </div>
        </div>

        <!-- File Attachments Upload to blobs/ -->
        <div class="form-group">
          <label for="create-file-input">Attachments (Uploaded to <code>blobs/</code> directory)</label>
          <div class="upload-dropzone">
            <input
              type="file"
              id="create-file-input"
              name="attachments"
              multiple
              (change)="onFilesSelected($event)"
            />
            @if (selectedFiles().length > 0) {
              <div class="selected-files-list">
                @for (f of selectedFiles(); track f.name; let idx = $index) {
                  <span class="file-chip">
                    📎 {{ f.name }} ({{ (f.size / 1024) | number:'1.0-1' }} KB)
                    <button type="button" class="chip-remove" (click)="removeFile(idx)">×</button>
                  </span>
                }
              </div>
            }
          </div>
        </div>

        <div class="form-actions">
          <a routerLink="/board" class="btn btn-secondary">Cancel</a>
          <button type="submit" class="btn btn-primary btn-lg" [disabled]="submitting()">
            {{ submitting() ? 'Creating Work Item...' : 'Create ' + form.type }}
          </button>
        </div>
      </form>
    </div>
  `,
})
export class CreateItemComponent implements OnInit {
  readonly api = inject(ApiService);
  private readonly router = inject(Router);
  private readonly route = inject(ActivatedRoute);

  readonly itemTypes = WORK_ITEM_TYPES;
  readonly itemCategories = WORK_ITEM_CATEGORIES;
  readonly states = WORKFLOW_STATES;
  readonly priorities = PRIORITIES;

  readonly existingItems = signal<WorkItem[]>([]);
  readonly selectedFiles = signal<File[]>([]);
  readonly submitting = signal(false);
  readonly errorMsg = signal('');

  form: {
    project_id: number;
    type: WorkItemType;
    category: WorkItemCategory;
    status: WorkflowState;
    priority: PriorityType;
    title: string;
    description: string;
    parent_id: number;
    sprint_id: number;
    assignee_id: number;
    story_points: number;
    start_date: string;
    due_date: string;
    severity: string;
    steps_to_reproduce: string;
    labels: string;
  } = {
    project_id: 1,
    type: 'Task',
    category: 'New Feature',
    status: 'To Do',
    priority: 'Medium',
    title: '',
    description: '',
    parent_id: 0,
    sprint_id: 1,
    assignee_id: 0,
    story_points: 3,
    start_date: '2026-10-24',
    due_date: '2026-10-31',
    severity: 'Major',
    steps_to_reproduce: '',
    labels: '',
  };

  async ngOnInit(): Promise<void> {
    await this.api.loadReferenceData();
    const projs = this.api.projects();
    if (projs.length > 0) {
      this.form.project_id = projs[0].id;
    }
    const cur = this.api.currentSprint();
    if (cur) {
      this.form.sprint_id = cur.id;
    }
    const items = await this.api.getItems();
    this.existingItems.set(items);

    const qType = this.route.snapshot.queryParamMap.get('type') as WorkItemType | null;
    if (qType && WORK_ITEM_TYPES.includes(qType)) {
      this.form.type = qType;
    }
    const qParent = Number(this.route.snapshot.queryParamMap.get('parent_id') || 0);
    if (qParent > 0) {
      this.form.parent_id = qParent;
    }
  }

  typeDescription(t: WorkItemType): string {
    switch (t) {
      case 'Epic':
        return 'Large initiative spanning multiple sprints';
      case 'Story':
        return 'User-facing story or deliverable';
      case 'Task':
        return 'Engineering or operational task';
      case 'Subtask':
        return 'Granular child step under a parent item';
    }
  }

  categoryDescription(c: WorkItemCategory): string {
    switch (c) {
      case 'New Feature':
        return 'Brand-new capability or functionality';
      case 'Improvement':
        return 'Enhancement or optimization to existing work';
      case 'Bug':
        return 'Software defect or regression';
      case 'Issue':
        return 'General project issue, risk, or blocker';
    }
  }

  onFilesSelected(event: Event): void {
    const input = event.target as HTMLInputElement;
    if (!input.files) return;
    const files = Array.from(input.files);
    this.selectedFiles.update((prev) => [...prev, ...files]);
  }

  removeFile(idx: number): void {
    this.selectedFiles.update((prev) => prev.filter((_, i) => i !== idx));
  }

  async submitCreate(): Promise<void> {
    if (!this.form.title.trim()) {
      this.errorMsg.set('Title is required.');
      return;
    }
    this.submitting.set(true);
    this.errorMsg.set('');

    try {
      const created = await this.api.createItem(this.form);
      for (const file of this.selectedFiles()) {
        await this.api.uploadAttachment(created.key, file);
      }
      await this.router.navigate(['/', created.key]);
    } catch (err) {
      this.errorMsg.set((err as Error).message);
    } finally {
      this.submitting.set(false);
    }
  }
}
