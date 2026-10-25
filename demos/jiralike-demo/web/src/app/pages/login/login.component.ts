import { Component, OnInit, inject, signal } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { Router } from '@angular/router';
import { ApiService } from '../../services/api.service';

@Component({
  selector: 'app-login',
  standalone: true,
  imports: [CommonModule, FormsModule],
  template: `
    <div class="auth-shell">
      <div class="login-card">
        <div class="setup-header">
          <div class="brand-badge">JL</div>
          <div>
            <h1>{{ api.setupStatus()?.instance_name || 'JiraLike Workspace' }}</h1>
            <p class="subtitle">
              Sign in to manage Epics, Stories, Tasks, Subtasks, Bugs, and Workflows
            </p>
          </div>
        </div>

        @if (errorMsg()) {
          <div class="alert alert-error">{{ errorMsg() }}</div>
        }

        <form (ngSubmit)="submitLogin()" class="setup-form">
          <div class="form-group">
            <label for="login-username">Username or Email</label>
            <input
              type="text"
              id="login-username"
              name="username"
              autocomplete="username"
              [(ngModel)]="username"
              required
              placeholder="admin"
            />
          </div>
          <div class="form-group">
            <label for="login-password">Password</label>
            <input
              type="password"
              id="login-password"
              name="password"
              autocomplete="current-password"
              [(ngModel)]="password"
              required
              placeholder="••••••••"
            />
          </div>
          <button type="submit" class="btn btn-primary btn-block btn-lg" [disabled]="loading()">
            {{ loading() ? 'Signing in...' : 'Sign In' }}
          </button>
        </form>

        <div class="demo-accounts">
          <div class="demo-title">Quick Fill Demo Accounts:</div>
          <div class="demo-pills">
            <button type="button" class="btn btn-ghost btn-sm" (click)="fillDemo('admin', 'admin123')">
              Admin (admin / admin123)
            </button>
            <button type="button" class="btn btn-ghost btn-sm" (click)="fillDemo('sarah', 'demo123')">
              PM (sarah / demo123)
            </button>
            <button type="button" class="btn btn-ghost btn-sm" (click)="fillDemo('marcus', 'demo123')">
              Dev (marcus / demo123)
            </button>
            <button type="button" class="btn btn-ghost btn-sm" (click)="fillDemo('elena', 'demo123')">
              QA (elena / demo123)
            </button>
          </div>
        </div>
      </div>
    </div>
  `,
})
export class LoginComponent implements OnInit {
  readonly api = inject(ApiService);
  private readonly router = inject(Router);

  username = 'admin';
  password = 'admin123';
  readonly loading = signal(false);
  readonly errorMsg = signal('');

  async ngOnInit(): Promise<void> {
    try {
      const st = await this.api.checkSetupStatus();
      if (!st.setup_complete) {
        await this.router.navigate(['/setup']);
        return;
      }
      if (this.api.currentUser()) {
        await this.router.navigate(['/board']);
      }
    } catch {
      // ignore
    }
  }

  fillDemo(u: string, p: string): void {
    this.username = u;
    this.password = p;
  }

  async submitLogin(): Promise<void> {
    this.loading.set(true);
    this.errorMsg.set('');
    try {
      await this.api.login(this.username, this.password);
      await this.router.navigate(['/board']);
    } catch (err) {
      this.errorMsg.set((err as Error).message);
    } finally {
      this.loading.set(false);
    }
  }
}
