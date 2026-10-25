import { Component, OnInit, inject, signal } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { Router } from '@angular/router';
import { ApiService } from '../../services/api.service';

@Component({
  selector: 'app-setup',
  standalone: true,
  imports: [CommonModule, FormsModule],
  template: `
    <div class="auth-shell">
      <div class="setup-card">
        <div class="setup-header">
          <div class="brand-badge">JL</div>
          <div>
            <h1>First-Time Workspace Setup</h1>
            <p class="subtitle">
              Blank SQLite database initialized &amp; migrated automatically to
              <strong>Schema v{{ api.setupStatus()?.schema_version || 3 }}</strong>.
              Complete your initial configuration below.
            </p>
          </div>
        </div>

        @if (api.setupStatus()?.migrations?.length) {
          <div class="migration-banner">
            <div class="migration-title">
              <span>SQLite Startup Schema Migrations Applied</span>
              <span class="badge badge-success">
                v{{ api.setupStatus()?.schema_version }} / v{{ api.setupStatus()?.latest_schema_version }}
              </span>
            </div>
            <div class="migration-list">
              @for (m of api.setupStatus()?.migrations; track m.version) {
                <div class="migration-item">
                  <span class="ver-pill">v{{ m.version }}</span>
                  <span class="ver-desc">{{ m.description }}</span>
                  <span class="ver-time">{{ m.applied_at }}</span>
                </div>
              }
            </div>
          </div>
        }

        @if (errorMsg()) {
          <div class="alert alert-error">{{ errorMsg() }}</div>
        }

        <form (ngSubmit)="submitSetup()" class="setup-form">
          <div class="form-section">
            <h3>1. Workspace &amp; Initial Project</h3>
            <div class="grid-2">
              <div class="form-group">
                <label for="setup-instance-name">Workspace Instance Name</label>
                <input
                  type="text"
                  id="setup-instance-name"
                  name="instance_name"
                  autocomplete="organization"
                  [(ngModel)]="form.instance_name"
                  required
                  placeholder="JiraLike Engineering Hub"
                />
              </div>
              <div class="form-group">
                <label for="setup-project-key">Project Key Prefix</label>
                <input
                  type="text"
                  id="setup-project-key"
                  name="project_key"
                  autocomplete="off"
                  [(ngModel)]="form.project_key"
                  required
                  maxlength="8"
                  placeholder="JIRA"
                />
              </div>
            </div>
            <div class="grid-2">
              <div class="form-group">
                <label for="setup-project-name">Initial Project Name</label>
                <input
                  type="text"
                  id="setup-project-name"
                  name="project_name"
                  autocomplete="off"
                  [(ngModel)]="form.project_name"
                  required
                  placeholder="Core Platform Development"
                />
              </div>
              <div class="form-group">
                <label for="setup-project-desc">Project Description</label>
                <input
                  type="text"
                  id="setup-project-desc"
                  name="project_description"
                  autocomplete="off"
                  [(ngModel)]="form.project_description"
                  placeholder="Agile issue tracking, Kanban board, and Gantt timeline"
                />
              </div>
            </div>
          </div>

          <div class="form-section">
            <h3>2. Administrator Account</h3>
            <div class="grid-2">
              <div class="form-group">
                <label for="setup-admin-username">Admin Username</label>
                <input
                  type="text"
                  id="setup-admin-username"
                  name="admin_username"
                  autocomplete="username"
                  [(ngModel)]="form.admin_username"
                  required
                />
              </div>
              <div class="form-group">
                <label for="setup-admin-display-name">Display Name</label>
                <input
                  type="text"
                  id="setup-admin-display-name"
                  name="admin_display_name"
                  autocomplete="name"
                  [(ngModel)]="form.admin_display_name"
                  required
                />
              </div>
            </div>
            <div class="grid-2">
              <div class="form-group">
                <label for="setup-admin-email">Admin Email</label>
                <input
                  type="email"
                  id="setup-admin-email"
                  name="admin_email"
                  autocomplete="email"
                  [(ngModel)]="form.admin_email"
                  required
                />
              </div>
              <div class="form-group">
                <label for="setup-admin-password">Admin Password</label>
                <input
                  type="password"
                  id="setup-admin-password"
                  name="admin_password"
                  autocomplete="new-password"
                  [(ngModel)]="form.admin_password"
                  required
                />
              </div>
            </div>
          </div>

          <div class="form-section seed-box">
            <label class="checkbox-label" for="setup-seed-demo">
              <input
                type="checkbox"
                id="setup-seed-demo"
                name="seed_demo_data"
                [(ngModel)]="form.seed_demo_data"
              />
              <div>
                <strong>Populate Sample Jira Project Data</strong>
                <p>
                  Creates sample Epics, Stories, Tasks, Subtasks, Bugs, and Issues across workflow
                  states, active Sprints, issue links, and a sample file in <code>blobs/</code>.
                </p>
              </div>
            </label>
          </div>

          <div class="form-actions">
            <button type="submit" class="btn btn-primary btn-lg" [disabled]="loading()">
              {{ loading() ? 'Initializing Workspace...' : 'Initialize Workspace & Launch App' }}
            </button>
          </div>
        </form>
      </div>
    </div>
  `,
})
export class SetupComponent implements OnInit {
  readonly api = inject(ApiService);
  private readonly router = inject(Router);

  readonly loading = signal(false);
  readonly errorMsg = signal('');

  form = {
    instance_name: 'JiraLike',
    project_key: 'JIRA',
    project_name: 'Core Platform Development',
    project_description: 'Agile project tracking with SQLite, FastCGI, and Angular SPA',
    admin_username: 'admin',
    admin_display_name: 'Boris Admin',
    admin_email: 'admin@jiralike.local',
    admin_password: 'admin123',
    seed_demo_data: true,
  };

  async ngOnInit(): Promise<void> {
    try {
      const st = await this.api.checkSetupStatus();
      if (st.setup_complete) {
        await this.router.navigate(['/board']);
      }
    } catch (err) {
      this.errorMsg.set((err as Error).message);
    }
  }

  async submitSetup(): Promise<void> {
    this.loading.set(true);
    this.errorMsg.set('');
    try {
      await this.api.initSetup(this.form);
      await this.router.navigate(['/board']);
    } catch (err) {
      this.errorMsg.set((err as Error).message);
    } finally {
      this.loading.set(false);
    }
  }
}
