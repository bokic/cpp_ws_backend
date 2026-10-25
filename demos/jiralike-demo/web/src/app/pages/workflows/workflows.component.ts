import { Component, OnInit, inject, signal } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { RouterLink } from '@angular/router';
import { ApiService } from '../../services/api.service';
import { WORKFLOW_STATES, WorkItem, WorkflowState } from '../../models/jira.models';

@Component({
  selector: 'app-workflows',
  standalone: true,
  imports: [CommonModule, FormsModule, RouterLink],
  template: `
    <div class="page-container">
      <div class="page-toolbar">
        <div>
          <h2>Jira Workflows, Sprints &amp; Backlog Management</h2>
          <p class="text-muted">
            Configure sprints, manage team members, inspect SQLite schema migrations, and transition backlog items.
          </p>
        </div>
        <div class="toolbar-actions">
          <a routerLink="/create" class="btn btn-primary">+ Create Work Item</a>
        </div>
      </div>

      @if (toastMsg()) {
        <div class="toast-banner">{{ toastMsg() }}</div>
      }

      <!-- Workflow State Machine Diagram -->
      <div class="card-panel">
        <h3>Jira Agile Workflow State Pipeline</h3>
        <p class="text-muted">
          Items can be transitioned across these states via drag-and-drop on the Kanban Board, Gantt State Matrix, or Detail view. Every transition is recorded in the SQLite <code>workflow_history</code> audit table.
        </p>
        <div class="workflow-diagram">
          <div class="wf-node" data-state="Backlog">
            <strong>Backlog</strong>
            <small>{{ countState('Backlog') }} items</small>
          </div>
          <span class="wf-arrow">→</span>
          <div class="wf-node" data-state="To Do">
            <strong>To Do</strong>
            <small>{{ countState('To Do') }} items</small>
          </div>
          <span class="wf-arrow">→</span>
          <div class="wf-node" data-state="In Progress">
            <strong>In Progress</strong>
            <small>{{ countState('In Progress') }} items</small>
          </div>
          <span class="wf-arrow">→</span>
          <div class="wf-node" data-state="In Review">
            <strong>In Review</strong>
            <small>{{ countState('In Review') }} items</small>
          </div>
          <span class="wf-arrow">→</span>
          <div class="wf-node" data-state="QA / Testing">
            <strong>QA / Testing</strong>
            <small>{{ countState('QA / Testing') }} items</small>
          </div>
          <span class="wf-arrow">→</span>
          <div class="wf-node" data-state="Done">
            <strong>Done</strong>
            <small>{{ countState('Done') }} items</small>
          </div>
          <span class="wf-arrow">⇄</span>
          <div class="wf-node" data-state="Blocked">
            <strong>Blocked</strong>
            <small>{{ countState('Blocked') }} items</small>
          </div>
        </div>
      </div>

      <!-- Sprints & Team Members Grid -->
      <div class="grid-2">
        <div class="card-panel">
          <h3>Sprints ({{ api.sprints().length }})</h3>
          <div class="subtask-list">
            @for (sp of api.sprints(); track sp.id) {
              <div class="subtask-row">
                <strong>{{ sp.name }}</strong>
                <span class="badge badge-info">{{ sp.status }}</span>
                <span class="text-muted">{{ sp.start_date }} → {{ sp.end_date }}</span>
              </div>
            }
          </div>
          <form (ngSubmit)="createNewSprint()" class="inline-create-box">
            <div class="grid-2">
              <input
                type="text"
                id="wf-sprint-name"
                name="sprint_title"
                autocomplete="off"
                [(ngModel)]="newSprint.name"
                placeholder="New Sprint Name (e.g., Sprint 3)"
                required
              />
              <input
                type="text"
                id="wf-sprint-goal"
                name="sprint_goal"
                autocomplete="off"
                [(ngModel)]="newSprint.goal"
                placeholder="Sprint Goal"
              />
            </div>
            <div class="grid-3" style="margin-top: 8px;">
              <input type="date" id="wf-sprint-start" name="spStart" [(ngModel)]="newSprint.start_date" />
              <input type="date" id="wf-sprint-end" name="spEnd" [(ngModel)]="newSprint.end_date" />
              <button type="submit" class="btn btn-secondary">+ Create Sprint</button>
            </div>
          </form>
        </div>

        <div class="card-panel">
          <h3>Team Members ({{ api.users().length }})</h3>
          <div class="subtask-list">
            @for (u of api.users(); track u.id) {
              <div class="subtask-row">
                <div class="avatar-circle" [style.background]="u.avatar_color">
                  {{ u.display_name.slice(0, 2).toUpperCase() }}
                </div>
                <strong>{{ u.display_name }}</strong>
                <span class="text-muted">&#64;{{ u.username }}</span>
                <span class="badge">{{ u.role }}</span>
              </div>
            }
          </div>
          <form (ngSubmit)="createNewUser()" class="inline-create-box">
            <div class="grid-3">
              <input
                type="text"
                id="wf-user-username"
                name="username"
                autocomplete="username"
                [(ngModel)]="newUser.username"
                placeholder="username"
                required
              />
              <input
                type="text"
                id="wf-user-display-name"
                name="display_name"
                autocomplete="name"
                [(ngModel)]="newUser.display_name"
                placeholder="Full Name"
                required
              />
              <select id="wf-user-role" name="uRole" [(ngModel)]="newUser.role">
                <option value="Developer">Developer</option>
                <option value="Project Manager">Project Manager</option>
                <option value="QA Engineer">QA Engineer</option>
                <option value="Reporter">Reporter</option>
                <option value="Admin">Admin</option>
              </select>
            </div>
            <div style="margin-top: 8px; display: flex; justify-content: flex-end;">
              <button type="submit" class="btn btn-secondary">+ Add Team Member</button>
            </div>
          </form>
        </div>
      </div>

      <!-- Full Backlog & Work Items Table -->
      <div class="card-panel">
        <h3>All Project Work Elements ({{ items().length }})</h3>
        <div class="table-responsive">
          <table class="data-table">
            <thead>
              <tr>
                <th>Type</th>
                <th>Category</th>
                <th>Key</th>
                <th>Title</th>
                <th>Parent</th>
                <th>Sprint</th>
                <th>Assignee</th>
                <th>Points</th>
                <th>Workflow State</th>
              </tr>
            </thead>
            <tbody>
              @for (it of items(); track it.id) {
                <tr>
                  <td><span class="type-badge" [attr.data-type]="it.type">{{ it.type }}</span></td>
                  <td><span class="cat-badge" [attr.data-cat]="it.category">{{ it.category }}</span></td>
                  <td><a [routerLink]="['/', it.key]" class="issue-key">{{ it.key }}</a></td>
                  <td>
                    <a [routerLink]="['/', it.key]" class="table-title-link">{{ it.title }}</a>
                  </td>
                  <td>{{ it.parent_key || '—' }}</td>
                  <td>{{ it.sprint_name || 'Backlog' }}</td>
                  <td>{{ it.assignee_name || 'Unassigned' }}</td>
                  <td>{{ it.story_points }} SP</td>
                  <td>
                    <select
                      class="state-select"
                      [id]="'wf-item-state-' + it.id"
                      [name]="'wf_item_state_' + it.id"
                      [attr.name]="'wf_item_state_' + it.id"
                      [attr.data-state]="it.status"
                      [ngModel]="it.status"
                      (ngModelChange)="changeItemState(it, $event)"
                    >
                      @for (st of states; track st) {
                        <option [value]="st">{{ st }}</option>
                      }
                    </select>
                  </td>
                </tr>
              }
            </tbody>
          </table>
        </div>
      </div>

      <!-- SQLite Schema Version & Migrations Status -->
      <div class="card-panel">
        <h3>Database Schema Versioning (<code>schema_version</code> table)</h3>
        <p class="text-muted">
          Current Schema Version: <strong>v{{ api.setupStatus()?.schema_version }}</strong>
          (Target Latest: <strong>v{{ api.setupStatus()?.latest_schema_version }}</strong>)
        </p>
        <div class="subtask-list">
          @for (m of api.setupStatus()?.migrations; track m.version) {
            <div class="subtask-row">
              <span class="ver-pill">v{{ m.version }}</span>
              <span style="flex: 1;">{{ m.description }}</span>
              <span class="text-muted">Applied: {{ m.applied_at }}</span>
            </div>
          }
        </div>
      </div>
    </div>
  `,
})
export class WorkflowsComponent implements OnInit {
  readonly api = inject(ApiService);
  readonly states = WORKFLOW_STATES;
  readonly items = signal<WorkItem[]>([]);
  readonly toastMsg = signal('');

  newSprint = {
    project_id: 1,
    name: '',
    goal: '',
    status: 'planned',
    start_date: '2026-11-21',
    end_date: '2026-12-05',
  };

  newUser = {
    username: '',
    display_name: '',
    email: '',
    role: 'Developer',
    password: 'demo123',
    avatar_color: '#00B8D9',
  };

  async ngOnInit(): Promise<void> {
    await this.api.checkSetupStatus();
    await this.api.loadReferenceData();
    this.items.set(await this.api.getItems());
  }

  countState(st: WorkflowState): number {
    return this.items().filter((i) => i.status === st).length;
  }

  async changeItemState(item: WorkItem, newState: WorkflowState): Promise<void> {
    if (item.status === newState) return;
    const updated = await this.api.transitionItemState(item.key, newState, {
      comment: `Transitioned to '${newState}' from Workflows & Backlog page`,
    });
    this.items.update((list) => list.map((i) => (i.id === item.id ? updated : i)));
    this.showToast(`${item.key} transitioned to ${newState}`);
  }

  async createNewSprint(): Promise<void> {
    if (!this.newSprint.name.trim()) return;
    await this.api.createSprint(this.newSprint);
    this.newSprint.name = '';
    this.newSprint.goal = '';
    this.showToast('Sprint created');
  }

  async createNewUser(): Promise<void> {
    if (!this.newUser.username.trim() || !this.newUser.display_name.trim()) return;
    await this.api.createUser({
      ...this.newUser,
      email: `${this.newUser.username}@jiralike.local`,
    });
    this.newUser.username = '';
    this.newUser.display_name = '';
    this.showToast('Team member added');
  }

  private showToast(msg: string): void {
    this.toastMsg.set(msg);
    setTimeout(() => {
      if (this.toastMsg() === msg) this.toastMsg.set('');
    }, 3000);
  }
}
