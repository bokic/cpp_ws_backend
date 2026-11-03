import { Component, OnInit, computed, inject, signal } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { RouterLink } from '@angular/router';
import { ApiService } from '../../services/api.service';
import {
  ProjectComponent,
  ProjectVersion,
  ReleaseNotesResponse,
} from '../../models/jira.models';

@Component({
  selector: 'app-project-settings',
  standalone: true,
  imports: [CommonModule, FormsModule, RouterLink],
  template: `
    <div class="page-container">
      <div class="page-toolbar">
        <div>
          <h2>Project Domain Settings &amp; Release Hub</h2>
          <p class="text-muted">
            Configure Jira Components, Versions &amp; Releases (with Release Notes generator), Issue Types &amp; Schemes, and Resolutions.
          </p>
        </div>
        <div class="toolbar-actions">
          <select
            id="settings-project-select"
            name="selected_project_id"
            [ngModel]="selectedProjectId()"
            (ngModelChange)="onProjectChange($event)"
            style="min-width: 220px;"
          >
            @for (p of api.projects(); track p.id) {
              <option [ngValue]="p.id">{{ p.key }} — {{ p.name }}</option>
            }
          </select>
          <a routerLink="/board" class="btn btn-secondary">Back to Board</a>
        </div>
      </div>

      @if (toastMsg()) {
        <div class="toast-banner">{{ toastMsg() }}</div>
      }
      @if (errorMsg()) {
        <div class="alert alert-error">{{ errorMsg() }}</div>
      }

      <!-- Section Tabs -->
      <div class="tabs-bar" style="margin-bottom: 18px;">
        <button
          type="button"
          class="tab-btn"
          [class.active]="activeTab() === 'components'"
          (click)="activeTab.set('components')"
        >
          🧩 Components ({{ projectComponents().length }})
        </button>
        <button
          type="button"
          class="tab-btn"
          [class.active]="activeTab() === 'versions'"
          (click)="activeTab.set('versions')"
        >
          🚀 Versions &amp; Releases ({{ projectVersions().length }})
        </button>
        <button
          type="button"
          class="tab-btn"
          [class.active]="activeTab() === 'issue-types'"
          (click)="activeTab.set('issue-types')"
        >
          🏷 Issue Types &amp; Schemes ({{ api.issueTypes().length }})
        </button>
        <button
          type="button"
          class="tab-btn"
          [class.active]="activeTab() === 'resolutions'"
          (click)="activeTab.set('resolutions')"
        >
          ✔ Resolutions ({{ api.resolutions().length }})
        </button>
        <button
          type="button"
          class="tab-btn"
          [class.active]="activeTab() === 'project-details'"
          (click)="activeTab.set('project-details'); loadProjectEditForm()"
        >
          ⚙️ Project Details &amp; Danger Zone
        </button>
      </div>

      <!-- TAB 1: COMPONENTS -->
      @if (activeTab() === 'components') {
        <div class="detail-grid">
          <div class="detail-main">
            <div class="card-panel">
              <h3>Project Components ({{ projectComponents().length }})</h3>
              <p class="text-muted" style="margin-bottom: 12px;">
                Components group issues within a project and support automatic default-assignee routing when issues are created.
              </p>

              @if (projectComponents().length === 0) {
                <p class="text-muted">No components configured for this project yet.</p>
              } @else {
                <div class="subtask-list">
                  @for (c of projectComponents(); track c.id) {
                    <div class="subtask-row" style="justify-content: space-between; gap: 12px; flex-wrap: wrap;">
                      <div style="flex: 1; min-width: 200px;">
                        <strong style="font-size: 0.98rem;">🧩 {{ c.name }}</strong>
                        <div class="text-muted" style="font-size: 0.82rem;">
                          {{ c.description || 'No description' }}
                        </div>
                      </div>
                      <div style="display: flex; align-items: center; gap: 10px; flex-wrap: wrap;">
                        <span class="state-pill">Lead: {{ c.lead_name || 'Unassigned' }}</span>
                        <span class="link-type-chip">Auto-Assign: {{ c.default_assignee_type }}</span>
                        <span class="type-badge" data-type="Task">{{ c.item_count || 0 }} items</span>
                        <button
                          type="button"
                          class="btn btn-ghost btn-sm"
                          (click)="deleteComponent(c)"
                        >
                          🗑
                        </button>
                      </div>
                    </div>
                  }
                </div>
              }
            </div>
          </div>

          <div class="detail-sidebar">
            <form (ngSubmit)="createComponent()" class="card-panel">
              <h3>Add Component</h3>
              <div class="form-group">
                <label for="comp-name">Component Name *</label>
                <input
                  type="text"
                  id="comp-name"
                  name="comp_name"
                  [(ngModel)]="newComponent.name"
                  required
                  placeholder="e.g., Backend Core, Web UI, Auth"
                />
              </div>
              <div class="form-group">
                <label for="comp-desc">Description</label>
                <textarea
                  id="comp-desc"
                  name="comp_desc"
                  [(ngModel)]="newComponent.description"
                  rows="2"
                  placeholder="Subsystem responsibility..."
                ></textarea>
              </div>
              <div class="form-group">
                <label for="comp-lead">Component Lead</label>
                <select id="comp-lead" name="comp_lead" [(ngModel)]="newComponent.lead_user_id">
                  <option [ngValue]="null">— None —</option>
                  @for (u of api.users(); track u.id) {
                    <option [ngValue]="u.id">{{ u.display_name }} ({{ u.role }})</option>
                  }
                </select>
              </div>
              <div class="form-group">
                <label for="comp-routing">Default Assignee Routing</label>
                <select
                  id="comp-routing"
                  name="comp_routing"
                  [(ngModel)]="newComponent.default_assignee_type"
                >
                  <option value="COMPONENT_LEAD">Component Lead</option>
                  <option value="PROJECT_LEAD">Project Lead</option>
                  <option value="PROJECT_DEFAULT">Project Default</option>
                  <option value="UNASSIGNED">Unassigned</option>
                </select>
              </div>
              <button type="submit" class="btn btn-primary" style="width: 100%;">
                + Create Component
              </button>
            </form>
          </div>
        </div>
      }

      <!-- TAB 2: VERSIONS & RELEASES -->
      @if (activeTab() === 'versions') {
        <div class="detail-grid">
          <div class="detail-main">
            <div class="card-panel">
              <h3>Versions &amp; Releases ({{ projectVersions().length }})</h3>
              <p class="text-muted" style="margin-bottom: 12px;">
                Track Fix Versions and Affects Versions across work items, manage release lifecycles, and generate Markdown Release Notes.
              </p>

              @if (projectVersions().length === 0) {
                <p class="text-muted">No versions created for this project yet.</p>
              } @else {
                <div class="subtask-list">
                  @for (v of projectVersions(); track v.id) {
                    <div class="subtask-row" style="justify-content: space-between; gap: 12px; flex-wrap: wrap;">
                      <div style="flex: 1; min-width: 220px;">
                        <div style="display: flex; align-items: center; gap: 8px;">
                          <strong style="font-size: 1rem;">🏷 {{ v.name }}</strong>
                          <span
                            class="state-pill"
                            [attr.data-state]="v.status === 'released' ? 'Done' : v.status === 'unreleased' ? 'In Progress' : 'Backlog'"
                          >
                            {{ v.status.toUpperCase() }}
                          </span>
                        </div>
                        <div class="text-muted" style="font-size: 0.82rem; margin-top: 2px;">
                          {{ v.description || 'No release description' }}
                        </div>
                        <div class="text-muted" style="font-size: 0.78rem; margin-top: 4px;">
                          📅 Start: {{ v.start_date || 'TBD' }} · Release: {{ v.release_date || 'TBD' }}
                        </div>
                      </div>

                      <div style="display: flex; align-items: center; gap: 8px; flex-wrap: wrap;">
                        <span class="type-badge" data-type="Story" title="Done / Total Fix Version Items">
                          Fix: {{ v.fix_done_count || 0 }}/{{ v.fix_count || 0 }} Done
                        </span>
                        <span class="cat-badge" data-cat="Bug" title="Affects Version Items">
                          Affects: {{ v.affects_count || 0 }}
                        </span>

                        @if (v.status === 'unreleased') {
                          <button
                            type="button"
                            class="btn btn-secondary btn-sm"
                            (click)="releaseVersion(v)"
                          >
                            🚀 Mark Released
                          </button>
                        }
                        <button
                          type="button"
                          class="btn btn-primary btn-sm"
                          (click)="openReleaseNotes(v.id)"
                        >
                          📝 Release Notes
                        </button>
                        <button
                          type="button"
                          class="btn btn-ghost btn-sm"
                          (click)="deleteVersion(v)"
                        >
                          🗑
                        </button>
                      </div>
                    </div>
                  }
                </div>
              }
            </div>

            @if (releaseNotes(); as rn) {
              <div class="card-panel" style="margin-top: 16px;">
                <div class="section-header">
                  <h3>📝 Generated Release Notes — {{ rn.version_name }} ({{ rn.total_items }} items)</h3>
                  <div style="display: flex; gap: 8px;">
                    <button type="button" class="btn btn-secondary btn-sm" (click)="copyReleaseNotes()">
                      📋 Copy Markdown
                    </button>
                    <button type="button" class="btn btn-ghost btn-sm" (click)="releaseNotes.set(null)">
                      × Close
                    </button>
                  </div>
                </div>
                <pre
                  style="background: #0f172a; color: #e2e8f0; padding: 14px; border-radius: 8px; overflow-x: auto; font-size: 0.85rem; line-height: 1.5; white-space: pre-wrap;"
                >{{ rn.markdown }}</pre>
              </div>
            }
          </div>

          <div class="detail-sidebar">
            <form (ngSubmit)="createVersion()" class="card-panel">
              <h3>Create Version / Release</h3>
              <div class="form-group">
                <label for="ver-name">Version Name *</label>
                <input
                  type="text"
                  id="ver-name"
                  name="ver_name"
                  [(ngModel)]="newVersion.name"
                  required
                  placeholder="e.g., v1.2.0, 2026.Q4"
                />
              </div>
              <div class="form-group">
                <label for="ver-status">Release Status</label>
                <select id="ver-status" name="ver_status" [(ngModel)]="newVersion.status">
                  <option value="unreleased">unreleased</option>
                  <option value="released">released</option>
                  <option value="archived">archived</option>
                </select>
              </div>
              <div class="grid-2">
                <div class="form-group">
                  <label for="ver-start">Start Date</label>
                  <input type="date" id="ver-start" name="ver_start" [(ngModel)]="newVersion.start_date" />
                </div>
                <div class="form-group">
                  <label for="ver-rel">Release Date</label>
                  <input type="date" id="ver-rel" name="ver_rel" [(ngModel)]="newVersion.release_date" />
                </div>
              </div>
              <div class="form-group">
                <label for="ver-desc">Description</label>
                <textarea
                  id="ver-desc"
                  name="ver_desc"
                  [(ngModel)]="newVersion.description"
                  rows="2"
                  placeholder="Scope and highlights of this release..."
                ></textarea>
              </div>
              <button type="submit" class="btn btn-primary" style="width: 100%;">
                + Create Version
              </button>
            </form>
          </div>
        </div>
      }

      <!-- TAB 3: ISSUE TYPES & SCHEMES -->
      @if (activeTab() === 'issue-types') {
        <div class="detail-grid">
          <div class="detail-main">
            <div class="card-panel">
              <h3>Issue Types Hierarchy ({{ api.issueTypes().length }})</h3>
              <p class="text-muted" style="margin-bottom: 12px;">
                Configurable Jira hierarchy levels: Level 3 (Initiative), Level 2 (Epic), Level 1 (Standard Story/Task), Level 0 (Subtask).
              </p>
              <div class="subtask-list">
                @for (it of api.issueTypes(); track it.id) {
                  <div class="subtask-row" style="justify-content: space-between;">
                    <div style="display: flex; align-items: center; gap: 10px;">
                      <span
                        style="display: inline-block; width: 12px; height: 12px; border-radius: 50%;"
                        [style.background]="it.color"
                      ></span>
                      <strong>{{ it.name }}</strong>
                      <span class="text-muted">{{ it.description }}</span>
                    </div>
                    <div style="display: flex; align-items: center; gap: 8px;">
                      <span class="link-type-chip">Level {{ it.hierarchy_level }}</span>
                      @if (it.is_subtask) {
                        <span class="state-pill">Subtask</span>
                      }
                      @if (it.is_system) {
                        <span class="text-muted" style="font-size: 0.78rem;">System</span>
                      }
                      <button
                        type="button"
                        class="btn btn-ghost btn-sm"
                        (click)="deleteIssueType(it)"
                        title="Delete Issue Type"
                      >
                        🗑
                      </button>
                    </div>
                  </div>
                }
              </div>
            </div>

            <div class="card-panel" style="margin-top: 16px;">
              <h3>Issue Type Schemes ({{ api.issueTypeSchemes().length }})</h3>
              @for (sch of api.issueTypeSchemes(); track sch.id) {
                <div style="padding: 10px 0; border-bottom: 1px solid #e2e8f0;">
                  <div style="display: flex; align-items: center; gap: 8px;">
                    <strong>{{ sch.name }}</strong>
                    @if (sch.is_default) {
                      <span class="current-pill">Default Scheme</span>
                    }
                  </div>
                  <div class="text-muted" style="font-size: 0.84rem; margin: 4px 0;">
                    {{ sch.description }}
                  </div>
                  <div style="display: flex; gap: 6px; flex-wrap: wrap; margin-top: 6px;">
                    @for (t of sch.issue_types; track t.id) {
                      <span class="type-badge" [attr.data-type]="t.name">
                        {{ t.name }} (L{{ t.hierarchy_level }})
                      </span>
                    }
                  </div>
                </div>
              }
            </div>
          </div>

          <div class="detail-sidebar">
            <form (ngSubmit)="createIssueType()" class="card-panel">
              <h3>Add Custom Issue Type</h3>
              <div class="form-group">
                <label for="it-name">Name *</label>
                <input
                  type="text"
                  id="it-name"
                  name="it_name"
                  [(ngModel)]="newIssueType.name"
                  required
                  placeholder="e.g., Spike, Incident, Change Request"
                />
              </div>
              <div class="form-group">
                <label for="it-level">Hierarchy Level</label>
                <select id="it-level" name="it_level" [(ngModel)]="newIssueType.hierarchy_level">
                  <option [ngValue]="3">Level 3 — Initiative</option>
                  <option [ngValue]="2">Level 2 — Epic</option>
                  <option [ngValue]="1">Level 1 — Standard (Story / Task)</option>
                  <option [ngValue]="0">Level 0 — Subtask</option>
                </select>
              </div>
              <div class="form-group">
                <label for="it-color">Badge Color</label>
                <input type="color" id="it-color" name="it_color" [(ngModel)]="newIssueType.color" />
              </div>
              <div class="form-group">
                <label for="it-desc">Description</label>
                <textarea
                  id="it-desc"
                  name="it_desc"
                  [(ngModel)]="newIssueType.description"
                  rows="2"
                ></textarea>
              </div>
              <button type="submit" class="btn btn-primary" style="width: 100%;">
                + Create Issue Type
              </button>
            </form>
          </div>
        </div>
      }

      <!-- TAB 4: RESOLUTIONS -->
      @if (activeTab() === 'resolutions') {
        <div class="detail-grid">
          <div class="detail-main">
            <div class="card-panel">
              <h3>Issue Resolutions ({{ api.resolutions().length }})</h3>
              <p class="text-muted" style="margin-bottom: 12px;">
                Resolutions record how an issue was closed when transitioning into the <code>Done</code> workflow category, and are automatically cleared if reopened.
              </p>
              <div class="subtask-list">
                @for (r of api.resolutions(); track r.id) {
                  <div class="subtask-row" style="justify-content: space-between;">
                    <div>
                      <strong>✔ {{ r.name }}</strong>
                      <span class="text-muted" style="margin-left: 10px;">{{ r.description }}</span>
                    </div>
                    <div style="display: flex; align-items: center; gap: 8px;">
                      @if (r.is_default) {
                        <span class="current-pill">Default Resolution</span>
                      }
                      <button
                        type="button"
                        class="btn btn-ghost btn-sm"
                        (click)="deleteResolution(r)"
                        title="Delete Resolution"
                      >
                        🗑
                      </button>
                    </div>
                  </div>
                }
              </div>
            </div>
          </div>

          <div class="detail-sidebar">
            <form (ngSubmit)="createResolution()" class="card-panel">
              <h3>Add Resolution</h3>
              <div class="form-group">
                <label for="res-name">Resolution Name *</label>
                <input
                  type="text"
                  id="res-name"
                  name="res_name"
                  [(ngModel)]="newResolution.name"
                  required
                  placeholder="e.g., Deferred, Workaround"
                />
              </div>
              <div class="form-group">
                <label for="res-desc">Description</label>
                <textarea
                  id="res-desc"
                  name="res_desc"
                  [(ngModel)]="newResolution.description"
                  rows="2"
                ></textarea>
              </div>
              <button type="submit" class="btn btn-primary" style="width: 100%;">
                + Create Resolution
              </button>
            </form>
          </div>
        </div>
      }

      <!-- TAB 5: PROJECT DETAILS & DANGER ZONE -->
      @if (activeTab() === 'project-details') {
        <div class="detail-grid">
          <div class="detail-main">
            <div class="card-panel">
              <h3>Project Information</h3>
              <p class="text-muted" style="margin-bottom: 16px;">
                Edit core attributes of the current project (name, key, description, project lead, category).
              </p>
              <form (ngSubmit)="saveProjectDetails()">
                <div class="grid-2">
                  <div class="form-group">
                    <label>Project Name *</label>
                    <input type="text" [(ngModel)]="editProjectForm.name" name="p_name" required />
                  </div>
                  <div class="form-group">
                    <label>Project Key *</label>
                    <input type="text" [(ngModel)]="editProjectForm.key" name="p_key" required />
                  </div>
                </div>
                <div class="grid-2">
                  <div class="form-group">
                    <label>Project Lead</label>
                    <select [(ngModel)]="editProjectForm.lead_user_id" name="p_lead">
                      <option [ngValue]="null">Unassigned</option>
                      @for (u of api.users(); track u.id) {
                        <option [ngValue]="u.id">{{ u.display_name }} (&#64;{{ u.username }})</option>
                      }
                    </select>
                  </div>
                  <div class="form-group">
                    <label>Category</label>
                    <input type="text" [(ngModel)]="editProjectForm.project_category" name="p_cat" placeholder="e.g. Software, Core" />
                  </div>
                </div>
                <div class="form-group">
                  <label>URL</label>
                  <input type="url" [(ngModel)]="editProjectForm.url" name="p_url" placeholder="https://..." />
                </div>
                <div class="form-group">
                  <label>Description</label>
                  <textarea [(ngModel)]="editProjectForm.description" name="p_desc" rows="3"></textarea>
                </div>
                <button type="submit" class="btn btn-primary" style="margin-top: 8px;">
                  Save Project Details
                </button>
              </form>
            </div>

            <div class="card-panel" style="margin-top: 20px; border-color: #ffbdad; background-color: #fffaf9;">
              <h3 style="color: #bf2600;">⚠️ Danger Zone: Delete Project</h3>
              <p class="text-muted" style="margin-bottom: 14px;">
                Deleting a project will permanently remove its components, versions, sprints, boards, and work items. This action cannot be undone.
              </p>
              <button type="button" class="btn btn-danger" (click)="deleteCurrentProject()">
                Delete This Project Permanently
              </button>
            </div>
          </div>
        </div>
      }
    </div>
  `,
})
export class ProjectSettingsComponent implements OnInit {
  readonly api = inject(ApiService);

  readonly activeTab = signal<'components' | 'versions' | 'issue-types' | 'resolutions' | 'project-details'>('components');
  readonly selectedProjectId = signal<number>(1);
  readonly releaseNotes = signal<ReleaseNotesResponse | null>(null);
  readonly toastMsg = signal('');
  readonly errorMsg = signal('');

  readonly projectComponents = computed<ProjectComponent[]>(() => {
    const pid = this.selectedProjectId();
    return this.api.components().filter((c) => c.project_id === pid);
  });

  readonly projectVersions = computed<ProjectVersion[]>(() => {
    const pid = this.selectedProjectId();
    return this.api.versions().filter((v) => v.project_id === pid);
  });

  newComponent = {
    name: '',
    description: '',
    lead_user_id: null as number | null,
    default_assignee_type: 'COMPONENT_LEAD',
  };

  newVersion = {
    name: '',
    description: '',
    status: 'unreleased',
    start_date: '2026-10-15',
    release_date: '2026-11-15',
  };

  newIssueType = {
    name: '',
    description: '',
    color: '#0ea5e9',
    hierarchy_level: 1,
  };

  newResolution = {
    name: '',
    description: '',
  };

  async ngOnInit(): Promise<void> {
    await this.api.loadReferenceData();
    const projs = this.api.projects();
    if (projs.length > 0) {
      this.selectedProjectId.set(projs[0].id);
    }
  }

  onProjectChange(pid: number): void {
    this.selectedProjectId.set(Number(pid));
    this.releaseNotes.set(null);
  }

  async createComponent(): Promise<void> {
    if (!this.newComponent.name.trim()) return;
    this.errorMsg.set('');
    try {
      await this.api.createComponent({
        project_id: this.selectedProjectId(),
        name: this.newComponent.name.trim(),
        description: this.newComponent.description.trim(),
        lead_user_id: this.newComponent.lead_user_id,
        default_assignee_type: this.newComponent.default_assignee_type,
      });
      this.newComponent.name = '';
      this.newComponent.description = '';
      this.showToast('Component created');
    } catch (e) {
      this.errorMsg.set((e as Error).message);
    }
  }

  async deleteComponent(c: ProjectComponent): Promise<void> {
    try {
      await this.api.deleteComponent(c.id);
      this.showToast(`Deleted component '${c.name}'`);
    } catch (e) {
      this.errorMsg.set((e as Error).message);
    }
  }

  async createVersion(): Promise<void> {
    if (!this.newVersion.name.trim()) return;
    this.errorMsg.set('');
    try {
      await this.api.createVersion({
        project_id: this.selectedProjectId(),
        name: this.newVersion.name.trim(),
        description: this.newVersion.description.trim(),
        status: this.newVersion.status,
        start_date: this.newVersion.start_date,
        release_date: this.newVersion.release_date,
      });
      this.newVersion.name = '';
      this.newVersion.description = '';
      this.showToast('Version created');
    } catch (e) {
      this.errorMsg.set((e as Error).message);
    }
  }

  async releaseVersion(v: ProjectVersion): Promise<void> {
    try {
      const today = new Date().toISOString().slice(0, 10);
      await this.api.updateVersion(v.id, {
        name: v.name,
        description: v.description,
        status: 'released',
        start_date: v.start_date,
        release_date: v.release_date || today,
      });
      this.showToast(`Version ${v.name} marked as Released!`);
    } catch (e) {
      this.errorMsg.set((e as Error).message);
    }
  }

  async deleteVersion(v: ProjectVersion): Promise<void> {
    try {
      await this.api.deleteVersion(v.id);
      if (this.releaseNotes()?.version_id === v.id) {
        this.releaseNotes.set(null);
      }
      this.showToast(`Deleted version '${v.name}'`);
    } catch (e) {
      this.errorMsg.set((e as Error).message);
    }
  }

  async openReleaseNotes(versionId: number): Promise<void> {
    try {
      const rn = await this.api.getReleaseNotes(versionId);
      this.releaseNotes.set(rn);
    } catch (e) {
      this.errorMsg.set((e as Error).message);
    }
  }

  async copyReleaseNotes(): Promise<void> {
    const rn = this.releaseNotes();
    if (!rn) return;
    try {
      await navigator.clipboard.writeText(rn.markdown);
      this.showToast('Release notes Markdown copied to clipboard');
    } catch {
      this.showToast('Release notes ready below');
    }
  }

  async createIssueType(): Promise<void> {
    if (!this.newIssueType.name.trim()) return;
    this.errorMsg.set('');
    try {
      await this.api.createIssueType({
        name: this.newIssueType.name.trim(),
        description: this.newIssueType.description.trim(),
        color: this.newIssueType.color,
        hierarchy_level: this.newIssueType.hierarchy_level,
        is_subtask: this.newIssueType.hierarchy_level === 0,
      });
      this.newIssueType.name = '';
      this.newIssueType.description = '';
      this.showToast('Custom Issue Type created');
    } catch (e) {
      this.errorMsg.set((e as Error).message);
    }
  }

  async createResolution(): Promise<void> {
    if (!this.newResolution.name.trim()) return;
    this.errorMsg.set('');
    try {
      await this.api.createResolution({
        name: this.newResolution.name.trim(),
        description: this.newResolution.description.trim(),
      });
      this.newResolution.name = '';
      this.newResolution.description = '';
      this.showToast('Resolution created');
    } catch (e) {
      this.errorMsg.set((e as Error).message);
    }
  }

  editProjectForm = {
    name: '',
    key: '',
    description: '',
    lead_user_id: null as number | null,
    url: '',
    project_category: '',
  };

  loadProjectEditForm(): void {
    const cur = this.api.projects().find(p => p.id === this.selectedProjectId());
    if (cur) {
      this.editProjectForm = {
        name: cur.name,
        key: cur.key,
        description: cur.description || '',
        lead_user_id: cur.lead_user_id || null,
        url: '',
        project_category: '',
      };
    }
  }

  async saveProjectDetails(): Promise<void> {
    const pid = this.selectedProjectId();
    if (!pid || !this.editProjectForm.name.trim() || !this.editProjectForm.key.trim()) return;
    this.errorMsg.set('');
    try {
      await this.api.updateProject(pid, {
        name: this.editProjectForm.name.trim(),
        key: this.editProjectForm.key.trim().toUpperCase(),
        description: this.editProjectForm.description.trim(),
        lead_user_id: this.editProjectForm.lead_user_id || undefined,
        url: this.editProjectForm.url.trim(),
        project_category: this.editProjectForm.project_category.trim(),
      });
      this.showToast('Project details successfully saved');
    } catch (e) {
      this.errorMsg.set((e as Error).message);
    }
  }

  async deleteCurrentProject(): Promise<void> {
    const pid = this.selectedProjectId();
    const cur = this.api.projects().find(p => p.id === pid);
    if (!pid || !cur) return;
    if (!confirm(`Are you absolutely sure you want to permanently delete project '${cur.name}' (${cur.key}) and ALL its items?`)) {
      return;
    }
    this.errorMsg.set('');
    try {
      await this.api.deleteProject(pid);
      this.showToast(`Project '${cur.name}' deleted`);
      const remaining = this.api.projects();
      if (remaining.length > 0) {
        this.selectedProjectId.set(remaining[0].id);
      }
    } catch (e) {
      this.errorMsg.set((e as Error).message);
    }
  }

  async deleteIssueType(it: any): Promise<void> {
    if (!confirm(`Delete issue type '${it.name}'?`)) return;
    this.errorMsg.set('');
    try {
      await this.api.deleteIssueType(it.id);
      this.showToast(`Issue type '${it.name}' deleted`);
    } catch (e) {
      this.errorMsg.set((e as Error).message);
    }
  }

  async deleteResolution(r: any): Promise<void> {
    if (!confirm(`Delete resolution '${r.name}'?`)) return;
    this.errorMsg.set('');
    try {
      await this.api.deleteResolution(r.id);
      this.showToast(`Resolution '${r.name}' deleted`);
    } catch (e) {
      this.errorMsg.set((e as Error).message);
    }
  }

  private showToast(msg: string): void {
    this.toastMsg.set(msg);
    setTimeout(() => {
      if (this.toastMsg() === msg) this.toastMsg.set('');
    }, 3000);
  }
}
