import { Component, OnInit, computed, inject, signal } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { Router, RouterLink } from '@angular/router';
import { ApiService } from '../../services/api.service';
import { Sprint } from '../../models/jira.models';

@Component({
  selector: 'app-sprints',
  standalone: true,
  imports: [CommonModule, FormsModule, RouterLink],
  template: `
    <div class="page-container narrow-container">
      <div class="page-toolbar">
        <div>
          <h2>Sprint Editor (Admin)</h2>
          <p class="text-muted">
            Create, edit, and schedule project sprints, and designate the <strong>Current Sprint</strong> used as the default across the workspace.
          </p>
        </div>
        <div class="toolbar-actions">
          <a routerLink="/board" class="btn btn-secondary">Back to Kanban Board</a>
        </div>
      </div>

      @if (!isAdmin()) {
        <div class="alert alert-error">
          <strong>Administrator Access Required:</strong> You are signed in as
          <code>{{ api.currentUser()?.display_name }} ({{ api.currentUser()?.role }})</code>.
          Only users with the <code>Admin</code> role can create, edit, or change the Current Sprint.
        </div>
      }

      @if (toastMsg()) {
        <div class="toast-banner">{{ toastMsg() }}</div>
      }
      @if (errorMsg()) {
        <div class="alert alert-error">{{ errorMsg() }}</div>
      }

      <!-- Current Sprint Highlight Banner -->
      @if (api.currentSprint(); as cur) {
        <div class="current-sprint-banner">
          <div class="cs-left">
            <span class="cs-star">★ CURRENT SPRINT</span>
            <h3>{{ cur.name }}</h3>
            <p>{{ cur.goal || 'No sprint goal specified' }}</p>
            <div class="cs-meta">
              <span>📅 {{ cur.start_date || 'TBD' }} → {{ cur.end_date || 'TBD' }}</span>
              <span>·</span>
              <span>📋 {{ cur.done_count || 0 }} / {{ cur.item_count || 0 }} items Done</span>
              <span>·</span>
              <span>⚡ {{ cur.total_points || 0 }} Story Points</span>
            </div>
          </div>
          <div class="cs-right">
            <a routerLink="/board" class="btn btn-primary btn-sm">Open on Board</a>
            <a routerLink="/gantt" class="btn btn-secondary btn-sm">Open on Gantt</a>
          </div>
        </div>
      }

      <!-- Create New Sprint Form (Admins Only) -->
      @if (isAdmin()) {
        <form (ngSubmit)="createSprint()" class="card-panel">
          <h3>Create New Sprint</h3>
          <div class="grid-2">
            <div class="form-group">
              <label for="new-sprint-name">Sprint Name *</label>
              <input
                type="text"
                id="new-sprint-name"
                name="sprint_name"
                autocomplete="off"
                [(ngModel)]="newSprint.name"
                required
                placeholder="e.g., Sprint 3 - Performance & Mobile"
              />
            </div>
            <div class="form-group">
              <label for="new-sprint-status">Sprint Status</label>
              <select id="new-sprint-status" name="status" [(ngModel)]="newSprint.status">
                <option value="planned">planned</option>
                <option value="active">active</option>
                <option value="closed">closed</option>
              </select>
            </div>
          </div>

          <div class="form-group">
            <label for="new-sprint-goal">Sprint Goal</label>
            <input
              type="text"
              id="new-sprint-goal"
              name="sprint_goal"
              autocomplete="off"
              [(ngModel)]="newSprint.goal"
              placeholder="Key deliverables and objective for this sprint..."
            />
          </div>

          <div class="grid-3">
            <div class="form-group">
              <label for="new-sprint-start">Start Date</label>
              <input
                type="date"
                id="new-sprint-start"
                name="start_date"
                [(ngModel)]="newSprint.start_date"
                (change)="autoFillTwoWeeks()"
              />
            </div>
            <div class="form-group">
              <label for="new-sprint-end">End Date (Default 2 Weeks)</label>
              <input
                type="date"
                id="new-sprint-end"
                name="end_date"
                [(ngModel)]="newSprint.end_date"
              />
            </div>
            <div class="form-group" style="display: flex; align-items: flex-end;">
              <label class="checkbox-label" style="margin-bottom: 8px;" for="new-sprint-current">
                <input
                  type="checkbox"
                  id="new-sprint-current"
                  name="is_current"
                  [(ngModel)]="newSprint.is_current"
                />
                <strong>Set as Current Sprint (★)</strong>
              </label>
            </div>
          </div>

          <div style="display: flex; justify-content: flex-end;">
            <button type="submit" class="btn btn-primary">+ Create Sprint</button>
          </div>
        </form>
      }

      <!-- Existing Sprints Editor List -->
      <div class="card-panel">
        <h3>All Project Sprints ({{ editableSprints().length }})</h3>
        <div class="sprint-editor-list">
          @for (sp of editableSprints(); track sp.id) {
            <div class="sprint-editor-card" [class.is-current-card]="sp.is_current">
              <div class="sprint-card-header">
                <div style="display: flex; align-items: center; gap: 10px;">
                  @if (sp.is_current) {
                    <span class="current-pill">★ CURRENT SPRINT</span>
                  } @else if (isAdmin()) {
                    <button
                      type="button"
                      class="btn btn-secondary btn-sm"
                      (click)="makeCurrent(sp)"
                    >
                      ☆ Set as Current Sprint
                    </button>
                  }
                  <span class="badge">{{ sp.status }}</span>
                  <span class="text-muted">
                    {{ sp.done_count || 0 }}/{{ sp.item_count || 0 }} items done ·
                    {{ sp.total_points || 0 }} SP
                  </span>
                </div>

                @if (isAdmin()) {
                  <div style="display: flex; gap: 8px;">
                    <button type="button" class="btn btn-primary btn-sm" (click)="saveSprint(sp)">
                      Save Sprint
                    </button>
                    <button
                      type="button"
                      class="btn btn-danger btn-sm"
                      (click)="removeSprint(sp)"
                      [disabled]="sp.is_current"
                      [title]="sp.is_current ? 'Assign another sprint as Current before deleting' : 'Delete Sprint'"
                    >
                      Delete
                    </button>
                  </div>
                }
              </div>

              <div class="grid-3" style="margin-top: 10px;">
                <div class="form-group">
                  <label [attr.for]="'sprint-name-' + sp.id">Sprint Name</label>
                  <input
                    type="text"
                    [id]="'sprint-name-' + sp.id"
                    [name]="'sprint_title_' + sp.id"
                    [attr.name]="'sprint_title_' + sp.id"
                    autocomplete="off"
                    [(ngModel)]="sp.name"
                    [disabled]="!isAdmin()"
                  />
                </div>
                <div class="form-group">
                  <label [attr.for]="'sprint-start-' + sp.id">Start Date</label>
                  <input
                    type="date"
                    [id]="'sprint-start-' + sp.id"
                    [name]="'sprint_start_' + sp.id"
                    [attr.name]="'sprint_start_' + sp.id"
                    [(ngModel)]="sp.start_date"
                    [disabled]="!isAdmin()"
                  />
                </div>
                <div class="form-group">
                  <label [attr.for]="'sprint-end-' + sp.id">End Date</label>
                  <input
                    type="date"
                    [id]="'sprint-end-' + sp.id"
                    [name]="'sprint_end_' + sp.id"
                    [attr.name]="'sprint_end_' + sp.id"
                    [(ngModel)]="sp.end_date"
                    [disabled]="!isAdmin()"
                  />
                </div>
              </div>

              <div class="grid-2">
                <div class="form-group">
                  <label [attr.for]="'sprint-goal-' + sp.id">Sprint Goal</label>
                  <input
                    type="text"
                    [id]="'sprint-goal-' + sp.id"
                    [name]="'sprint_goal_' + sp.id"
                    [attr.name]="'sprint_goal_' + sp.id"
                    autocomplete="off"
                    [(ngModel)]="sp.goal"
                    [disabled]="!isAdmin()"
                  />
                </div>
                <div class="form-group">
                  <label [attr.for]="'sprint-status-' + sp.id">Lifecycle Status</label>
                  <select
                    [id]="'sprint-status-' + sp.id"
                    [name]="'sprint_status_' + sp.id"
                    [attr.name]="'sprint_status_' + sp.id"
                    [(ngModel)]="sp.status"
                    [disabled]="!isAdmin()"
                  >
                    <option value="planned">planned</option>
                    <option value="active">active</option>
                    <option value="closed">closed</option>
                  </select>
                </div>
              </div>
            </div>
          }
        </div>
      </div>
    </div>
  `,
})
export class SprintsComponent implements OnInit {
  readonly api = inject(ApiService);
  private readonly router = inject(Router);

  readonly editableSprints = signal<Sprint[]>([]);
  readonly toastMsg = signal('');
  readonly errorMsg = signal('');

  readonly isAdmin = computed(() => this.api.currentUser()?.role === 'Admin');

  newSprint = {
    project_id: 1,
    name: '',
    goal: '',
    status: 'planned',
    start_date: '2026-11-21',
    end_date: '2026-12-05',
    is_current: false,
  };

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
    await this.reloadSprints();
  }

  async reloadSprints(): Promise<void> {
    await this.api.loadReferenceData();
    // Deep copy for inline editing
    this.editableSprints.set(this.api.sprints().map((s) => ({ ...s })));
  }

  autoFillTwoWeeks(): void {
    if (!this.newSprint.start_date) return;
    const dt = new Date(this.newSprint.start_date + 'T00:00:00');
    if (isNaN(dt.getTime())) return;
    const end = new Date(dt.getTime() + 14 * 86400000);
    this.newSprint.end_date = end.toISOString().slice(0, 10);
  }

  async createSprint(): Promise<void> {
    if (!this.newSprint.name.trim()) return;
    this.errorMsg.set('');
    try {
      await this.api.createSprint(this.newSprint);
      this.newSprint.name = '';
      this.newSprint.goal = '';
      this.newSprint.is_current = false;
      await this.reloadSprints();
      this.showToast('Sprint created successfully');
    } catch (err) {
      this.errorMsg.set((err as Error).message);
    }
  }

  async saveSprint(sp: Sprint): Promise<void> {
    this.errorMsg.set('');
    try {
      await this.api.updateSprint(sp.id, {
        project_id: sp.project_id,
        name: sp.name,
        goal: sp.goal,
        status: sp.status,
        start_date: sp.start_date,
        end_date: sp.end_date,
        is_current: sp.is_current,
      });
      await this.reloadSprints();
      this.showToast(`Saved "${sp.name}"`);
    } catch (err) {
      this.errorMsg.set((err as Error).message);
    }
  }

  async makeCurrent(sp: Sprint): Promise<void> {
    this.errorMsg.set('');
    try {
      await this.api.setCurrentSprint(sp.id);
      await this.reloadSprints();
      this.showToast(`"${sp.name}" is now set as the Current Sprint (★)`);
    } catch (err) {
      this.errorMsg.set((err as Error).message);
    }
  }

  async removeSprint(sp: Sprint): Promise<void> {
    this.errorMsg.set('');
    try {
      await this.api.deleteSprint(sp.id);
      await this.reloadSprints();
      this.showToast(`Deleted sprint "${sp.name}"`);
    } catch (err) {
      this.errorMsg.set((err as Error).message);
    }
  }

  private showToast(msg: string): void {
    this.toastMsg.set(msg);
    setTimeout(() => {
      if (this.toastMsg() === msg) this.toastMsg.set('');
    }, 3200);
  }
}
