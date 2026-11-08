import { Component, OnInit, inject, signal } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { ActivatedRoute, Router, RouterLink } from '@angular/router';
import { ApiService } from '../../services/api.service';
import { RichEditorComponent } from '../../components/rich-editor/rich-editor.component';
import {
  IssueSecurityLevel,
  PRIORITIES,
  PriorityType,
  ProjectComponent,
  ScreenDef,
  ScreenTab,
  ScreenTabField,
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
  imports: [CommonModule, FormsModule, RouterLink, RichEditorComponent],
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
            <select id="create-project-id" name="project_id" [(ngModel)]="form.project_id" (change)="onProjectChanged()">
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
          <label for="create-description" style="margin-bottom: 6px; font-weight: 600;">Description</label>
          <app-rich-editor
            [(value)]="form.description"
            placeholder="Detailed acceptance criteria, context, checklists, or paste images directly (Ctrl+V)..."
          ></app-rich-editor>
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
            <label for="create-assignee-id">
              Assignee
              @if (autoAssignedReason()) {
                <span style="margin-left: 6px; font-size: 0.75rem; background: #0052cc; color: white; padding: 2px 6px; border-radius: 4px; font-weight: normal;">
                  {{ autoAssignedReason() }}
                </span>
              }
            </label>
            <select
              id="create-assignee-id"
              name="assignee_id"
              [(ngModel)]="form.assignee_id"
              (change)="onAssigneeManuallyChanged()"
            >
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

          <div class="form-group">
            <label for="create-security-level">Security Level</label>
            <select id="create-security-level" name="security_level_id" [(ngModel)]="form.security_level_id">
              <option [ngValue]="null">— None (Public) —</option>
              @for (sec of availableSecurityLevels(); track sec.id) {
                <option [ngValue]="sec.id">{{ sec.name }}</option>
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

        <!-- Time Tracking & Estimation Fields -->
        <div class="grid-3" style="margin-top: 10px;">
          <div class="form-group">
            <label for="create-orig-est">Original Estimate (e.g. 2d 4h, 1w, 45m)</label>
            <input
              type="text"
              id="create-orig-est"
              name="original_estimate"
              autocomplete="off"
              [(ngModel)]="form.original_estimate"
              placeholder="e.g. 2d 4h"
            />
          </div>

          <div class="form-group">
            <label for="create-rem-est">Remaining Estimate (Defaults to Original)</label>
            <input
              type="text"
              id="create-rem-est"
              name="remaining_estimate"
              autocomplete="off"
              [(ngModel)]="form.remaining_estimate"
              placeholder="e.g. 2d 4h"
            />
          </div>

          <div class="form-group">
            <label for="create-tshirt-size">T-Shirt Size Estimation</label>
            <select
              id="create-tshirt-size"
              name="tshirt_size"
              [(ngModel)]="form.tshirt_size"
              (change)="onTshirtChange()"
            >
              <option value="">— None —</option>
              <option value="XS">XS (~2h)</option>
              <option value="S">S (~4h)</option>
              <option value="M">M (~1d)</option>
              <option value="L">L (~2d)</option>
              <option value="XL">XL (~1w)</option>
              <option value="XXL">XXL (~2w)</option>
            </select>
          </div>
        </div>

        <!-- Components, Fix Versions, and Affects Versions -->
        <div class="grid-3">
          <div class="form-group">
            <label>Components (Auto-routes Assignee if Unassigned)</label>
            <div style="display: flex; gap: 6px; flex-wrap: wrap; margin-top: 4px;">
              @for (c of projectComponents(); track c.id) {
                <button
                  type="button"
                  class="pill"
                  [class.active]="form.component_ids.includes(c.id)"
                  (click)="toggleComponent(c)"
                >
                  🧩 {{ c.name }}
                </button>
              }
              @if (projectComponents().length === 0) {
                <span class="text-muted" style="font-size: 0.82rem;">No components in project</span>
              }
            </div>
          </div>

          <div class="form-group">
            <label>Fix Version/s (Target Release)</label>
            <div style="display: flex; gap: 6px; flex-wrap: wrap; margin-top: 4px;">
              @for (v of projectVersions(); track v.id) {
                <button
                  type="button"
                  class="pill"
                  [class.active]="form.fix_version_ids.includes(v.id)"
                  (click)="toggleId(form.fix_version_ids, v.id)"
                >
                  🚀 {{ v.name }} ({{ v.status }})
                </button>
              }
              @if (projectVersions().length === 0) {
                <span class="text-muted" style="font-size: 0.82rem;">No versions in project</span>
              }
            </div>
          </div>

          <div class="form-group">
            <label>Affects Version/s</label>
            <div style="display: flex; gap: 6px; flex-wrap: wrap; margin-top: 4px;">
              @for (v of projectVersions(); track v.id) {
                <button
                  type="button"
                  class="pill"
                  [class.active]="form.affects_version_ids.includes(v.id)"
                  (click)="toggleId(form.affects_version_ids, v.id)"
                >
                  🏷 {{ v.name }}
                </button>
              }
            </div>
          </div>
        </div>

        <!-- Dynamic Custom Fields & Screen Tabs Renderer -->
        @if (resolvedScreen(); as screen) {
          <div class="card-panel" style="background: #f8fafc; border: 1px solid #e2e8f0; margin-top: 8px;">
            <div style="display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 8px; margin-bottom: 10px;">
              <div>
                <h4 style="margin: 0;">🧬 Custom Fields — {{ screen.name }}</h4>
                <small class="text-muted">Rendered dynamically from active Screen Scheme &amp; Field Configuration</small>
              </div>
              <a routerLink="/custom-fields" class="btn btn-ghost btn-sm">Configure Fields &amp; Screens →</a>
            </div>

            @if (screen.tabs.length > 0) {
              <div class="tabs-bar" style="margin-bottom: 14px;">
                @for (tab of screen.tabs; track tab.id) {
                  <button
                    type="button"
                    class="tab-btn"
                    [class.active]="activeScreenTabId() === tab.id"
                    (click)="activeScreenTabId.set(tab.id)"
                  >
                    {{ tab.name }} ({{ tab.fields.length }})
                  </button>
                }
              </div>

              @if (activeScreenTab(); as curTab) {
                <div class="grid-2">
                  @for (f of curTab.fields; track f.field_key) {
                    @if (!f.is_hidden && f.field_type !== 'formula') {
                      <div class="form-group" [style.gridColumn]="f.field_type === 'long_text' ? '1 / -1' : 'auto'">
                        <label>
                          {{ f.name }}
                          @if (f.is_required) {
                            <span style="color: #de350b; font-weight: bold;">*</span>
                          }
                          <small class="text-muted" style="font-weight: normal; margin-left: 6px;">({{ f.field_type }})</small>
                        </label>

                        @switch (f.field_type) {
                          @case ('long_text') {
                            <textarea
                              rows="3"
                              [name]="'cf_' + f.field_key"
                              [(ngModel)]="form.custom_fields[f.field_key]"
                              [placeholder]="f.description || 'Enter ' + f.name + '...'"
                            ></textarea>
                          }
                          @case ('number') {
                            <input
                              type="number"
                              [name]="'cf_' + f.field_key"
                              [attr.min]="f.min_value"
                              [attr.max]="f.max_value"
                              [(ngModel)]="form.custom_fields[f.field_key]"
                              [placeholder]="f.description || 'Numeric value'"
                            />
                          }
                          @case ('date') {
                            <input
                              type="date"
                              [name]="'cf_' + f.field_key"
                              [(ngModel)]="form.custom_fields[f.field_key]"
                            />
                          }
                          @case ('datetime') {
                            <input
                              type="datetime-local"
                              [name]="'cf_' + f.field_key"
                              [(ngModel)]="form.custom_fields[f.field_key]"
                            />
                          }
                          @case ('select_single') {
                            <select
                              [name]="'cf_' + f.field_key"
                              [(ngModel)]="form.custom_fields[f.field_key]"
                            >
                              <option value="">— Select {{ f.name }} —</option>
                              @for (opt of f.options; track opt.id) {
                                <option [value]="opt.value">{{ opt.value }}</option>
                              }
                            </select>
                          }
                          @case ('select_multi') {
                            <div style="display: flex; gap: 6px; flex-wrap: wrap; margin-top: 4px;">
                              @for (opt of f.options; track opt.id) {
                                <button
                                  type="button"
                                  class="pill"
                                  [class.active]="isMultiOptionSelected(f.field_key, opt.value)"
                                  (click)="toggleMultiOption(f.field_key, opt.value)"
                                >
                                  {{ opt.value }}
                                </button>
                              }
                            </div>
                          }
                          @case ('user_picker_single') {
                            <select
                              [name]="'cf_' + f.field_key"
                              [(ngModel)]="form.custom_fields[f.field_key]"
                            >
                              <option [ngValue]="null">— None —</option>
                              @for (u of api.users(); track u.id) {
                                <option [ngValue]="u.id">{{ u.display_name }} ({{ u.role }})</option>
                              }
                            </select>
                          }
                          @case ('checkbox') {
                            <label style="display: inline-flex; align-items: center; gap: 8px; margin-top: 6px; cursor: pointer;">
                              <input
                                type="checkbox"
                                [name]="'cf_' + f.field_key"
                                [(ngModel)]="form.custom_fields[f.field_key]"
                              />
                              <span>{{ f.description || f.name }}</span>
                            </label>
                          }
                          @case ('url') {
                            <input
                              type="url"
                              [name]="'cf_' + f.field_key"
                              [(ngModel)]="form.custom_fields[f.field_key]"
                              placeholder="https://..."
                            />
                          }
                          @default {
                            <input
                              type="text"
                              [name]="'cf_' + f.field_key"
                              [(ngModel)]="form.custom_fields[f.field_key]"
                              [placeholder]="f.description || ''"
                            />
                          }
                        }

                        @if (f.description && f.field_type !== 'checkbox') {
                          <small class="text-muted" style="display: block; margin-top: 3px; font-size: 0.76rem;">
                            {{ f.description }}
                          </small>
                        }
                      </div>
                    }
                  }
                </div>
              }
            }
          </div>
        }

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
  readonly resolvedScreen = signal<ScreenDef | null>(null);
  readonly activeScreenTabId = signal<number>(0);
  readonly availableSecurityLevels = signal<IssueSecurityLevel[]>([]);

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
    original_estimate: string;
    remaining_estimate: string;
    tshirt_size: string;
    component_ids: number[];
    fix_version_ids: number[];
    affects_version_ids: number[];
    custom_fields: Record<string, any>;
    security_level_id: number | null;
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
    original_estimate: '1d',
    remaining_estimate: '1d',
    tshirt_size: 'M',
    component_ids: [],
    fix_version_ids: [],
    affects_version_ids: [],
    custom_fields: {},
    security_level_id: null,
  };

  onTshirtChange(): void {
    const sizeToDuration: Record<string, string> = {
      XS: '2h',
      S: '4h',
      M: '1d',
      L: '2d',
      XL: '1w',
      XXL: '2w',
    };
    const sz = this.form.tshirt_size;
    if (sz && sizeToDuration[sz]) {
      this.form.original_estimate = sizeToDuration[sz];
      this.form.remaining_estimate = sizeToDuration[sz];
    }
  }

  activeScreenTab(): ScreenTab | null {
    const sc = this.resolvedScreen();
    if (!sc || sc.tabs.length === 0) return null;
    return sc.tabs.find((t) => t.id === this.activeScreenTabId()) || sc.tabs[0];
  }

  isMultiOptionSelected(fieldKey: string, val: string): boolean {
    const cur = this.form.custom_fields[fieldKey];
    return Array.isArray(cur) && cur.includes(val);
  }

  toggleMultiOption(fieldKey: string, val: string): void {
    const cur = Array.isArray(this.form.custom_fields[fieldKey])
      ? [...this.form.custom_fields[fieldKey]]
      : [];
    const idx = cur.indexOf(val);
    if (idx >= 0) {
      cur.splice(idx, 1);
    } else {
      cur.push(val);
    }
    this.form.custom_fields[fieldKey] = cur;
  }

  async loadCreateScreen(): Promise<void> {
    try {
      const sc = await this.api.resolveScreen(this.form.project_id, 'create', this.form.type);
      this.resolvedScreen.set(sc);
      if (sc.tabs.length > 0) {
        this.activeScreenTabId.set(sc.tabs[0].id);
      }
      for (const t of sc.tabs) {
        for (const f of t.fields) {
          if (this.form.custom_fields[f.field_key] === undefined) {
            if (f.field_type === 'checkbox') {
              this.form.custom_fields[f.field_key] = f.default_value === 'true';
            } else if (f.field_type === 'select_multi') {
              try {
                this.form.custom_fields[f.field_key] = f.default_value
                  ? JSON.parse(f.default_value)
                  : [];
              } catch {
                this.form.custom_fields[f.field_key] = [];
              }
            } else if (f.field_type === 'number') {
              this.form.custom_fields[f.field_key] = f.default_value
                ? Number(f.default_value)
                : null;
            } else if (f.field_type === 'user_picker_single') {
              this.form.custom_fields[f.field_key] = f.default_value
                ? Number(f.default_value)
                : null;
            } else {
              this.form.custom_fields[f.field_key] = f.default_value || '';
            }
          }
        }
      }
    } catch {
      // ignore
    }
  }

  readonly autoAssignedReason = signal<string>('');
  readonly userManuallyChangedAssignee = signal<boolean>(false);

  toggleComponent(c: ProjectComponent): void {
    this.toggleId(this.form.component_ids, c.id);
    this.evaluateComponentAutoAssignment();
  }

  onAssigneeManuallyChanged(): void {
    this.userManuallyChangedAssignee.set(true);
    this.autoAssignedReason.set('');
  }

  onProjectChanged(): void {
    this.form.component_ids = [];
    this.form.fix_version_ids = [];
    this.form.affects_version_ids = [];
    if (!this.userManuallyChangedAssignee()) {
      this.evaluateComponentAutoAssignment();
    }
  }

  evaluateComponentAutoAssignment(): void {
    if (this.userManuallyChangedAssignee()) {
      return;
    }

    const comps = this.projectComponents();
    const activeComps = this.form.component_ids
      .map(id => comps.find(c => c.id === id))
      .filter((c): c is ProjectComponent => !!c);

    if (activeComps.length > 0) {
      for (const comp of activeComps) {
        const routeType = (comp.default_assignee_type || 'PROJECT_DEFAULT').toUpperCase();
        if (routeType === 'COMPONENT_LEAD' && comp.lead_user_id) {
          this.form.assignee_id = comp.lead_user_id;
          this.autoAssignedReason.set('(Auto: Component Lead)');
          return;
        } else if (routeType === 'UNASSIGNED') {
          this.form.assignee_id = 0;
          this.autoAssignedReason.set('(Auto: Unassigned)');
          return;
        } else if (routeType === 'PROJECT_LEAD') {
          const proj = this.api.projects().find(p => p.id === this.form.project_id);
          if (proj?.lead_user_id) {
            this.form.assignee_id = proj.lead_user_id;
            this.autoAssignedReason.set('(Auto: Project Lead)');
            return;
          }
        } else if (routeType === 'PROJECT_DEFAULT') {
          break;
        }
      }
    }

    // Fall back to project default policy
    const proj = this.api.projects().find(p => p.id === this.form.project_id);
    if (proj) {
      const projDef = (proj.default_assignee_type || 'PROJECT_LEAD').toUpperCase();
      if (projDef === 'PROJECT_LEAD' && proj.lead_user_id) {
        this.form.assignee_id = proj.lead_user_id;
        this.autoAssignedReason.set('(Auto: Project Lead)');
        return;
      }
    }

    this.form.assignee_id = 0;
    this.autoAssignedReason.set('');
  }

  projectComponents() {
    return this.api.components().filter((c) => c.project_id === this.form.project_id);
  }

  projectVersions() {
    return this.api.versions().filter((v) => v.project_id === this.form.project_id);
  }

  toggleId(arr: number[], id: number): void {
    const idx = arr.indexOf(id);
    if (idx >= 0) {
      arr.splice(idx, 1);
    } else {
      arr.push(id);
    }
  }

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

    try {
      const secLevels = await this.api.getSecurityLevels(this.form.project_id);
      this.availableSecurityLevels.set(secLevels);
      const defSec = secLevels.find((s) => s.is_default);
      if (defSec) {
        this.form.security_level_id = defSec.id;
      }
    } catch {}

    await this.loadCreateScreen();
    this.evaluateComponentAutoAssignment();
  }

  typeDescription(t: WorkItemType): string {
    switch (t) {
      case 'Initiative':
        return 'Strategic cross-epic portfolio initiative';
      case 'Epic':
        return 'Large initiative spanning multiple sprints';
      case 'Story':
        return 'User-facing story or deliverable';
      case 'Task':
        return 'Engineering or operational task';
      case 'Subtask':
        return 'Granular child step under a parent item';
      default:
        return 'Custom project work item type';
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
      const payload = {
        ...this.form,
        manual_assignee: this.userManuallyChangedAssignee(),
        auto_assigned: !this.userManuallyChangedAssignee() && !!this.autoAssignedReason(),
      };
      const created = await this.api.createItem(payload);
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
