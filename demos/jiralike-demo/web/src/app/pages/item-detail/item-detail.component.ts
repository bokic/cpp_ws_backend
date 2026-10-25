import { Component, OnInit, inject, signal } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { ActivatedRoute, Router, RouterLink } from '@angular/router';
import { ApiService } from '../../services/api.service';
import {
  PRIORITIES,
  WORKFLOW_STATES,
  WORK_ITEM_CATEGORIES,
  WORK_ITEM_TYPES,
  WorkItem,
  WorkItemDetail,
  WorkflowState,
} from '../../models/jira.models';

@Component({
  selector: 'app-item-detail',
  standalone: true,
  imports: [CommonModule, FormsModule, RouterLink],
  template: `
    @if (item(); as it) {
      <div class="page-container">
        <!-- Top Breadcrumb & Action Bar -->
        <div class="detail-top-bar">
          <div class="breadcrumb">
            <a routerLink="/board">Board</a>
            <span>/</span>
            @if (it.parent_key) {
              <a [routerLink]="['/', it.parent_key]" class="parent-link">
                {{ it.parent_key }}: {{ it.parent_title }}
              </a>
              <span>/</span>
            }
            <span class="type-badge" [attr.data-type]="it.type">{{ it.type }}</span>
            <span class="cat-badge" [attr.data-cat]="it.category">{{ it.category }}</span>
            <strong class="issue-key-lg">{{ it.key }}</strong>
          </div>
          <div class="toolbar-actions">
            <a
              routerLink="/create"
              [queryParams]="{ type: 'Subtask', parent_id: it.id }"
              class="btn btn-secondary btn-sm"
            >
              + Create Subtask
            </a>
            <button type="button" class="btn btn-danger btn-sm" (click)="deleteCurrentItem()">
              Delete
            </button>
          </div>
        </div>

        @if (toastMsg()) {
          <div class="toast-banner">{{ toastMsg() }}</div>
        }

        <!-- Interactive Jira Workflow State Transition Pipeline -->
        <div class="workflow-pipeline-bar">
          <span class="pipeline-label">Workflow State:</span>
          <div class="pipeline-steps">
            @for (st of states; track st) {
              <button
                type="button"
                class="pipeline-step"
                [attr.data-state]="st"
                [class.active]="it.status === st"
                (click)="transitionState(st)"
              >
                <span class="state-dot" [attr.data-state]="st"></span>
                {{ st }}
              </button>
            }
          </div>
        </div>

        <!-- Two Column Detail Layout -->
        <div class="detail-grid">
          <!-- Main Left Column -->
          <div class="detail-main">
            <div class="card-panel">
              <div class="form-group">
                <label for="detail-title">Title</label>
                <input
                  type="text"
                  id="detail-title"
                  name="detail_title"
                  autocomplete="off"
                  [(ngModel)]="it.title"
                  class="title-input"
                />
              </div>

              <div class="form-group">
                <label for="detail-description">Description</label>
                <textarea
                  id="detail-description"
                  name="detail_description"
                  [(ngModel)]="it.description"
                  rows="4"
                ></textarea>
              </div>

              @if (it.category === 'Bug') {
                <div class="bug-fields-box">
                  <div class="grid-2">
                    <div class="form-group">
                      <label for="detail-severity">Severity</label>
                      <select id="detail-severity" name="detail_severity" [(ngModel)]="it.severity">
                        <option value="">None</option>
                        <option value="Minor">Minor</option>
                        <option value="Major">Major</option>
                        <option value="Critical">Critical</option>
                        <option value="Blocker">Blocker</option>
                      </select>
                    </div>
                    <div class="form-group">
                      <label for="detail-steps">Steps to Reproduce</label>
                      <textarea
                        id="detail-steps"
                        name="detail_steps"
                        [(ngModel)]="it.steps_to_reproduce"
                        rows="3"
                      ></textarea>
                    </div>
                  </div>
                </div>
              }

              <div class="form-actions-inline">
                <button type="button" class="btn btn-primary" (click)="saveChanges()">
                  Save Details
                </button>
              </div>
            </div>

            <!-- Subtasks Section -->
            <div class="card-panel">
              <div class="section-header">
                <h3>Subtasks &amp; Child Work Items ({{ it.subtasks.length }})</h3>
              </div>
              @if (it.subtasks.length > 0) {
                <div class="subtask-list">
                  @for (sub of it.subtasks; track sub.id) {
                    <div class="subtask-row">
                      <span class="type-badge" [attr.data-type]="sub.type">{{ sub.type }}</span>
                      <a [routerLink]="['/', sub.key]" class="issue-key">{{ sub.key }}</a>
                      <a [routerLink]="['/', sub.key]" class="subtask-title">{{ sub.title }}</a>
                      <span class="state-pill" [attr.data-state]="sub.status">{{ sub.status }}</span>
                      <span class="text-muted">{{ sub.assignee_name || 'Unassigned' }}</span>
                    </div>
                  }
                </div>
              }
              <div class="quick-subtask-bar">
                <input
                  type="text"
                  id="detail-new-subtask"
                  name="new_subtask_title"
                  autocomplete="off"
                  [(ngModel)]="newSubtaskTitle"
                  placeholder="Quick add a Subtask under {{ it.key }}..."
                  (keyup.enter)="quickAddSubtask()"
                />
                <button type="button" class="btn btn-secondary" (click)="quickAddSubtask()">
                  + Add Subtask
                </button>
              </div>
            </div>

            <!-- Linked Work Items Section -->
            <div class="card-panel">
              <div class="section-header">
                <h3>Linked Issues ({{ it.links.length }})</h3>
              </div>
              @if (it.links.length > 0) {
                <div class="subtask-list">
                  @for (lk of it.links; track lk.id) {
                    <div class="subtask-row">
                      <span class="link-type-chip">{{ lk.link_type }}</span>
                      @if (lk.source_item_id === it.id) {
                        <a [routerLink]="['/', lk.target_key]" class="issue-key">
                          {{ lk.target_key }}
                        </a>
                        <span class="subtask-title">{{ lk.target_title }}</span>
                        <span class="state-pill" [attr.data-state]="lk.target_status">
                          {{ lk.target_status }}
                        </span>
                      } @else {
                        <a [routerLink]="['/', lk.source_key]" class="issue-key">
                          {{ lk.source_key }}
                        </a>
                        <span class="subtask-title">{{ lk.source_title }}</span>
                        <span class="state-pill" [attr.data-state]="lk.source_status">
                          {{ lk.source_status }}
                        </span>
                      }
                      <button
                        type="button"
                        class="btn btn-ghost btn-sm"
                        (click)="removeLink(lk.id)"
                      >
                        ×
                      </button>
                    </div>
                  }
                </div>
              }
              <div class="quick-subtask-bar">
                <select
                  id="detail-link-type"
                  name="new_link_type"
                  [(ngModel)]="newLinkType"
                  style="max-width: 160px;"
                >
                  <option value="blocks">blocks</option>
                  <option value="is_blocked_by">is blocked by</option>
                  <option value="relates_to">relates to</option>
                  <option value="duplicates">duplicates</option>
                </select>
                <select id="detail-link-target" name="new_link_target_id" [(ngModel)]="newLinkTargetId">
                  <option [ngValue]="0">Select work item to link...</option>
                  @for (other of allItems(); track other.id) {
                    @if (other.id !== it.id) {
                      <option [ngValue]="other.id">{{ other.key }} — {{ other.title }}</option>
                    }
                  }
                </select>
                <button type="button" class="btn btn-secondary" (click)="addIssueLink()">
                  Link Issue
                </button>
              </div>
            </div>

            <!-- File Attachments (blobs/) Section -->
            <div class="card-panel">
              <div class="section-header">
                <h3>File Attachments in <code>blobs/</code> ({{ it.attachments.length }})</h3>
                <label class="btn btn-secondary btn-sm upload-btn-label" for="detail-upload-file">
                  📎 Upload File to blobs/
                  <input
                    type="file"
                    id="detail-upload-file"
                    name="detail_attachment"
                    hidden
                    (change)="uploadFile($event)"
                  />
                </label>
              </div>

              @if (it.attachments.length > 0) {
                <div class="attachment-grid">
                  @for (att of it.attachments; track att.id) {
                    <div class="attachment-card">
                      <div class="att-icon">📄</div>
                      <div class="att-info">
                        <a [href]="att.url" target="_blank" class="att-name">
                          {{ att.original_filename }}
                        </a>
                        <div class="att-meta">
                          {{ (att.size_bytes / 1024) | number:'1.0-1' }} KB · {{ att.mime_type }} ·
                          Uploaded by {{ att.uploader_name }}
                        </div>
                        <div class="att-blob-path">
                          Blob: <code>blobs/{{ att.blob_filename }}</code>
                        </div>
                      </div>
                      <div class="att-actions">
                        <a [href]="att.url" [download]="att.original_filename" class="btn btn-ghost btn-sm">
                          ⬇
                        </a>
                        <button
                          type="button"
                          class="btn btn-ghost btn-sm"
                          (click)="deleteAttachment(att.id)"
                        >
                          🗑
                        </button>
                      </div>
                    </div>
                  }
                </div>
              } @else {
                <p class="text-muted">No files attached yet. Click "Upload File to blobs/" above.</p>
              }
            </div>

            <!-- Comments & Workflow Audit Log -->
            <div class="card-panel">
              <div class="tabs-bar">
                <button
                  type="button"
                  class="tab-btn"
                  [class.active]="activeTab() === 'comments'"
                  (click)="activeTab.set('comments')"
                >
                  Comments ({{ it.comments.length }})
                </button>
                <button
                  type="button"
                  class="tab-btn"
                  [class.active]="activeTab() === 'history'"
                  (click)="activeTab.set('history')"
                >
                  Workflow State History ({{ it.history.length }})
                </button>
              </div>

              @if (activeTab() === 'comments') {
                <div class="comments-list">
                  @for (c of it.comments; track c.id) {
                    <div class="comment-item">
                      <div class="avatar-circle" [style.background]="c.author_color">
                        {{ c.author_name.slice(0, 2).toUpperCase() }}
                      </div>
                      <div class="comment-content">
                        <div class="comment-header">
                          <strong>{{ c.author_name }}</strong>
                          <span class="text-muted">{{ c.created_at }}</span>
                        </div>
                        <p>{{ c.body }}</p>
                      </div>
                    </div>
                  }
                </div>
                <div class="quick-subtask-bar" style="margin-top: 12px;">
                  <input
                    type="text"
                    id="detail-new-comment"
                    name="new_comment_text"
                    autocomplete="off"
                    [(ngModel)]="newCommentText"
                    placeholder="Add a comment..."
                    (keyup.enter)="postComment()"
                  />
                  <button type="button" class="btn btn-primary" (click)="postComment()">
                    Post Comment
                  </button>
                </div>
              } @else {
                <div class="history-list">
                  @for (h of it.history; track h.id) {
                    <div class="history-row">
                      <strong>{{ h.changed_by_name }}</strong> transitioned state from
                      <span class="state-pill" [attr.data-state]="h.from_status">{{ h.from_status }}</span>
                      →
                      <span class="state-pill" [attr.data-state]="h.to_status">{{ h.to_status }}</span>
                      <span class="text-muted">({{ h.created_at }})</span>
                      @if (h.comment) {
                        <div class="history-note">{{ h.comment }}</div>
                      }
                    </div>
                  }
                </div>
              }
            </div>
          </div>

          <!-- Right Metadata Sidebar -->
          <div class="detail-sidebar">
            <div class="card-panel">
              <h3>Attributes &amp; Scheduling</h3>

              <div class="form-group">
                <label for="detail-type">Work Element Type (Hierarchy)</label>
                <select id="detail-type" name="detail_type" [(ngModel)]="it.type" (change)="saveChanges()">
                  @for (t of itemTypes; track t) {
                    <option [value]="t">{{ t }}</option>
                  }
                </select>
              </div>

              <div class="form-group">
                <label for="detail-category">Work Category</label>
                <select
                  id="detail-category"
                  name="detail_category"
                  [(ngModel)]="it.category"
                  (change)="saveChanges()"
                >
                  @for (c of itemCategories; track c) {
                    <option [value]="c">{{ c }}</option>
                  }
                </select>
              </div>

              <div class="form-group">
                <label for="detail-priority">Priority</label>
                <select
                  id="detail-priority"
                  name="detail_priority"
                  [(ngModel)]="it.priority"
                  (change)="saveChanges()"
                >
                  @for (p of priorities; track p) {
                    <option [value]="p">{{ p }}</option>
                  }
                </select>
              </div>

              <div class="form-group">
                <label for="detail-assignee">Assignee</label>
                <select
                  id="detail-assignee"
                  name="detail_assignee_id"
                  [(ngModel)]="it.assignee_id"
                  (change)="saveChanges()"
                >
                  <option [ngValue]="null">— Unassigned —</option>
                  @for (u of api.users(); track u.id) {
                    <option [ngValue]="u.id">{{ u.display_name }} ({{ u.role }})</option>
                  }
                </select>
              </div>

              <div class="form-group">
                <label for="detail-sprint">Sprint</label>
                <select
                  id="detail-sprint"
                  name="detail_sprint_id"
                  [(ngModel)]="it.sprint_id"
                  (change)="saveChanges()"
                >
                  <option [ngValue]="null">— Backlog —</option>
                  @for (s of api.sprints(); track s.id) {
                    <option [ngValue]="s.id">{{ s.name }}</option>
                  }
                </select>
              </div>

              <div class="form-group">
                <label for="detail-parent">Parent Item</label>
                <select
                  id="detail-parent"
                  name="detail_parent_id"
                  [(ngModel)]="it.parent_id"
                  (change)="saveChanges()"
                >
                  <option [ngValue]="null">— None —</option>
                  @for (other of allItems(); track other.id) {
                    @if (other.id !== it.id) {
                      <option [ngValue]="other.id">[{{ other.type }}] {{ other.key }}: {{ other.title }}</option>
                    }
                  }
                </select>
              </div>

              <div class="grid-2">
                <div class="form-group">
                  <label for="detail-story-points">Story Points</label>
                  <input
                    type="number"
                    id="detail-story-points"
                    name="detail_story_points"
                    [(ngModel)]="it.story_points"
                    (change)="saveChanges()"
                  />
                </div>
                <div class="form-group">
                  <label for="detail-progress">Progress ({{ it.progress }}%)</label>
                  <input
                    type="number"
                    id="detail-progress"
                    name="detail_progress"
                    min="0"
                    max="100"
                    [(ngModel)]="it.progress"
                    (change)="saveChanges()"
                  />
                </div>
              </div>

              <div class="grid-2">
                <div class="form-group">
                  <label for="detail-start-date">Start Date</label>
                  <input
                    type="date"
                    id="detail-start-date"
                    name="detail_start_date"
                    [(ngModel)]="it.start_date"
                    (change)="saveChanges()"
                  />
                </div>
                <div class="form-group">
                  <label for="detail-due-date">Due Date</label>
                  <input
                    type="date"
                    id="detail-due-date"
                    name="detail_due_date"
                    [(ngModel)]="it.due_date"
                    (change)="saveChanges()"
                  />
                </div>
              </div>

              <div class="form-group">
                <label for="detail-labels">Labels</label>
                <input
                  type="text"
                  id="detail-labels"
                  name="detail_labels"
                  autocomplete="off"
                  [(ngModel)]="it.labels"
                  (change)="saveChanges()"
                />
              </div>

              <div class="meta-timestamps">
                <div>Reporter: <strong>{{ it.reporter_name || 'System' }}</strong></div>
                <div>Created: {{ it.created_at }}</div>
                <div>Updated: {{ it.updated_at }}</div>
              </div>
            </div>
          </div>
        </div>
      </div>
    }
  `,
})
export class ItemDetailComponent implements OnInit {
  readonly api = inject(ApiService);
  private readonly route = inject(ActivatedRoute);
  private readonly router = inject(Router);

  readonly states = WORKFLOW_STATES;
  readonly itemTypes = WORK_ITEM_TYPES;
  readonly itemCategories = WORK_ITEM_CATEGORIES;
  readonly priorities = PRIORITIES;

  readonly item = signal<WorkItemDetail | null>(null);
  readonly allItems = signal<WorkItem[]>([]);
  readonly activeTab = signal<'comments' | 'history'>('comments');
  readonly toastMsg = signal('');

  newSubtaskTitle = '';
  newCommentText = '';
  newLinkType = 'relates_to';
  newLinkTargetId = 0;

  async ngOnInit(): Promise<void> {
    await this.api.loadReferenceData();
    const list = await this.api.getItems();
    this.allItems.set(list);

    this.route.paramMap.subscribe(async (params) => {
      const idOrKey = params.get('id')?.trim();
      if (idOrKey) {
        await this.reloadDetail(idOrKey);
      }
    });
  }

  async reloadDetail(idOrKey: string | number): Promise<void> {
    const d = await this.api.getItemDetail(idOrKey);
    this.item.set(d);
  }

  async transitionState(newState: WorkflowState): Promise<void> {
    const cur = this.item();
    if (!cur || cur.status === newState) return;
    await this.api.transitionItemState(cur.key, newState, {
      comment: `Transitioned to '${newState}' from Item Detail page`,
    });
    await this.reloadDetail(cur.key);
    this.showToast(`${cur.key} transitioned to ${newState}`);
  }

  async saveChanges(): Promise<void> {
    const cur = this.item();
    if (!cur) return;
    await this.api.updateItem(cur.key, {
      type: cur.type,
      category: cur.category,
      status: cur.status,
      priority: cur.priority,
      title: cur.title,
      description: cur.description,
      parent_id: cur.parent_id || 0,
      sprint_id: cur.sprint_id || 0,
      assignee_id: cur.assignee_id || 0,
      story_points: cur.story_points,
      progress: cur.progress,
      start_date: cur.start_date,
      due_date: cur.due_date,
      severity: cur.severity,
      steps_to_reproduce: cur.steps_to_reproduce,
      labels: cur.labels,
    });
    await this.reloadDetail(cur.key);
    this.showToast(`Saved changes to ${cur.key}`);
  }

  async quickAddSubtask(): Promise<void> {
    const cur = this.item();
    if (!cur || !this.newSubtaskTitle.trim()) return;
    await this.api.createItem({
      project_id: cur.project_id,
      type: 'Subtask',
      category: cur.category || 'New Feature',
      status: 'To Do',
      priority: 'Medium',
      title: this.newSubtaskTitle.trim(),
      parent_id: cur.id,
      sprint_id: cur.sprint_id || 0,
      assignee_id: cur.assignee_id || 0,
      start_date: cur.start_date,
      due_date: cur.due_date,
    });
    this.newSubtaskTitle = '';
    await this.reloadDetail(cur.key);
    this.showToast('Subtask created');
  }

  async addIssueLink(): Promise<void> {
    const cur = this.item();
    if (!cur || this.newLinkTargetId <= 0) return;
    await this.api.addLink(cur.key, this.newLinkTargetId, this.newLinkType);
    this.newLinkTargetId = 0;
    await this.reloadDetail(cur.key);
    this.showToast('Issue link added');
  }

  async removeLink(linkId: number): Promise<void> {
    const cur = this.item();
    if (!cur) return;
    await this.api.deleteLink(linkId);
    await this.reloadDetail(cur.key);
  }

  async uploadFile(event: Event): Promise<void> {
    const cur = this.item();
    const input = event.target as HTMLInputElement;
    if (!cur || !input.files || input.files.length === 0) return;
    const file = input.files[0];
    await this.api.uploadAttachment(cur.key, file);
    input.value = '';
    await this.reloadDetail(cur.key);
    this.showToast(`Uploaded '${file.name}' to blobs/`);
  }

  async deleteAttachment(attId: number): Promise<void> {
    const cur = this.item();
    if (!cur) return;
    await this.api.deleteAttachment(attId);
    await this.reloadDetail(cur.key);
    this.showToast('Attachment removed from blobs/');
  }

  async postComment(): Promise<void> {
    const cur = this.item();
    if (!cur || !this.newCommentText.trim()) return;
    await this.api.addComment(cur.key, this.newCommentText.trim());
    this.newCommentText = '';
    await this.reloadDetail(cur.key);
  }

  async deleteCurrentItem(): Promise<void> {
    const cur = this.item();
    if (!cur) return;
    await this.api.deleteItem(cur.key);
    await this.router.navigate(['/board']);
  }

  private showToast(msg: string): void {
    this.toastMsg.set(msg);
    setTimeout(() => {
      if (this.toastMsg() === msg) this.toastMsg.set('');
    }, 3000);
  }
}
