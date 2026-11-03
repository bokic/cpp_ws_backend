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

        @if (mfaRequired()) {
          <!-- 2FA / TOTP Challenge View -->
          <div class="mfa-challenge-box" style="margin-top: 16px;">
            <div style="text-align: center; margin-bottom: 20px;">
              <div style="font-size: 36px; margin-bottom: 8px;">🔐</div>
              <h2 style="font-size: 18px; margin: 0 0 4px 0; color: var(--text-primary);">Two-Factor Authentication</h2>
              <p style="font-size: 13px; color: var(--text-secondary); margin: 0;">
                Account <strong style="color: var(--brand-primary);">&#64;{{ mfaUsername() }}</strong> is protected with 2FA
              </p>
            </div>

            @if (!useRecoveryCode()) {
              <form (ngSubmit)="submitMfaTotp()" class="setup-form">
                <div class="form-group">
                  <label for="totp-code" style="display: flex; justify-content: space-between;">
                    <span>Authenticator Code</span>
                    <span style="font-size: 11px; color: var(--text-muted);">6-digit code</span>
                  </label>
                  <input
                    type="text"
                    id="totp-code"
                    name="totpCode"
                    maxlength="7"
                    [(ngModel)]="totpCode"
                    required
                    autofocus
                    placeholder="123456"
                    style="font-size: 20px; letter-spacing: 4px; text-align: center; font-weight: 700; font-family: monospace;"
                  />
                </div>
                <button type="submit" class="btn btn-primary btn-block btn-lg" [disabled]="loading() || !totpCode">
                  {{ loading() ? 'Verifying...' : 'Verify &amp; Sign In' }}
                </button>
              </form>
              <div style="margin-top: 16px; text-align: center;">
                <button type="button" class="btn btn-ghost btn-sm" (click)="useRecoveryCode.set(true)">
                  Use an Emergency Recovery Code →
                </button>
              </div>
            } @else {
              <form (ngSubmit)="submitMfaRecovery()" class="setup-form">
                <div class="form-group">
                  <label for="rec-code">Emergency Backup Recovery Code</label>
                  <input
                    type="text"
                    id="rec-code"
                    name="recoveryCode"
                    [(ngModel)]="recoveryCode"
                    required
                    autofocus
                    placeholder="ABCD-1234"
                    style="font-size: 16px; letter-spacing: 2px; text-align: center; font-family: monospace;"
                  />
                  <small style="color: var(--text-secondary); font-size: 11px; margin-top: 4px; display: block;">
                    Entering a valid backup code will burn it for one-time emergency access.
                  </small>
                </div>
                <button type="submit" class="btn btn-primary btn-block btn-lg" [disabled]="loading() || !recoveryCode">
                  {{ loading() ? 'Verifying...' : 'Verify Backup Code &amp; Sign In' }}
                </button>
              </form>
              <div style="margin-top: 16px; text-align: center;">
                <button type="button" class="btn btn-ghost btn-sm" (click)="useRecoveryCode.set(false)">
                  ← Return to 6-digit Authenticator Code
                </button>
              </div>
            }

            <div style="margin-top: 12px; text-align: center; border-top: 1px solid var(--border-color); padding-top: 12px;">
              <button type="button" class="btn btn-ghost btn-sm" (click)="cancelMfa()">
                Cancel &amp; Sign In with Another Account
              </button>
            </div>
          </div>
        } @else {
          <!-- Standard Username / Password Login -->
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

          <!-- Enterprise SSO Login Section -->
          <div style="margin: 20px 0 16px 0; position: relative; text-align: center;">
            <div style="position: absolute; top: 50%; left: 0; right: 0; height: 1px; background: var(--border-color); z-index: 1;"></div>
            <span style="position: relative; z-index: 2; background: var(--bg-surface); padding: 0 12px; font-size: 12px; color: var(--text-muted); text-transform: uppercase;">
              Or sign in with
            </span>
          </div>

          <div class="sso-login-box">
            @if (!showSsoDomainInput()) {
              <button
                type="button"
                class="btn btn-secondary btn-block"
                style="display: flex; align-items: center; justify-content: center; gap: 8px; font-weight: 600;"
                (click)="toggleSsoDomain()"
              >
                <span>🏢</span>
                <span>Log in with Enterprise SSO (SAML 2.0 / OIDC)</span>
              </button>
            } @else {
              <div style="background: var(--bg-subtle); padding: 12px; border-radius: 6px; border: 1px solid var(--border-color);">
                <label style="display: block; font-size: 12px; font-weight: 600; margin-bottom: 4px;">Company Email or Domain</label>
                <div style="display: flex; gap: 6px; margin-bottom: 8px;">
                  <input
                    type="text"
                    [(ngModel)]="ssoDomain"
                    placeholder="example.com or user@example.com"
                    style="flex: 1; padding: 6px 10px; font-size: 13px; border-radius: 4px; border: 1px solid var(--border-color);"
                  />
                  <button type="button" class="btn btn-primary btn-sm" (click)="submitSsoHandshake()" [disabled]="loading()">
                    Continue →
                  </button>
                </div>
                <div style="display: flex; justify-content: space-between; font-size: 11px;">
                  <button type="button" class="btn btn-ghost btn-sm" (click)="simulateSsoDemo()" style="padding: 2px 4px; font-size: 11px; color: var(--brand-primary);">
                    ⚡ Test Mock IdP Assertion
                  </button>
                  <button type="button" class="btn btn-ghost btn-sm" (click)="showSsoDomainInput.set(false)" style="padding: 2px 4px; font-size: 11px;">
                    Cancel
                  </button>
                </div>
              </div>
            }
          </div>
        }
      </div>
    </div>
  `,
})
export class LoginComponent implements OnInit {
  readonly api = inject(ApiService);
  private readonly router = inject(Router);

  username = '';
  password = '';
  readonly loading = signal(false);
  readonly errorMsg = signal('');

  // 2FA Challenge Signals
  readonly mfaRequired = signal(false);
  readonly mfaToken = signal('');
  readonly mfaUsername = signal('');
  readonly useRecoveryCode = signal(false);
  totpCode = '';
  recoveryCode = '';

  // SSO Signals
  readonly showSsoDomainInput = signal(false);
  ssoDomain = 'example.com';

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

  toggleSsoDomain(): void {
    this.showSsoDomainInput.set(true);
    if (this.username.includes('@')) {
      this.ssoDomain = this.username.split('@')[1];
    }
  }

  async submitLogin(): Promise<void> {
    this.loading.set(true);
    this.errorMsg.set('');
    try {
      const res = await this.api.login(this.username, this.password);
      if (res.mfa_required && res.mfa_token) {
        this.mfaRequired.set(true);
        this.mfaToken.set(res.mfa_token);
        this.mfaUsername.set(res.username || this.username);
        this.totpCode = '';
        this.recoveryCode = '';
      } else if (res.user) {
        await this.router.navigate(['/board']);
      }
    } catch (err) {
      this.errorMsg.set((err as Error).message);
    } finally {
      this.loading.set(false);
    }
  }

  async submitMfaTotp(): Promise<void> {
    if (!this.totpCode || !this.mfaToken()) return;
    this.loading.set(true);
    this.errorMsg.set('');
    try {
      await this.api.verify2faLogin(this.mfaToken(), this.totpCode.trim());
      await this.router.navigate(['/board']);
    } catch (err) {
      this.errorMsg.set((err as Error).message);
    } finally {
      this.loading.set(false);
    }
  }

  async submitMfaRecovery(): Promise<void> {
    if (!this.recoveryCode || !this.mfaToken()) return;
    this.loading.set(true);
    this.errorMsg.set('');
    try {
      await this.api.recovery2faLogin(this.mfaToken(), this.recoveryCode.trim());
      await this.router.navigate(['/board']);
    } catch (err) {
      this.errorMsg.set((err as Error).message);
    } finally {
      this.loading.set(false);
    }
  }

  cancelMfa(): void {
    this.mfaRequired.set(false);
    this.mfaToken.set('');
    this.totpCode = '';
    this.recoveryCode = '';
    this.useRecoveryCode.set(false);
  }

  async submitSsoHandshake(): Promise<void> {
    this.loading.set(true);
    this.errorMsg.set('');
    try {
      const resp = await this.api.initiateSso(this.ssoDomain);
      if (!resp.is_enabled) {
        this.errorMsg.set('Enterprise SSO is not enabled on this instance. Please sign in with username/password or contact your administrator.');
        return;
      }
      if (resp.redirect_url) {
        window.location.href = resp.redirect_url;
      }
    } catch (err) {
      this.errorMsg.set((err as Error).message);
    } finally {
      this.loading.set(false);
    }
  }

  async simulateSsoDemo(): Promise<void> {
    this.loading.set(true);
    this.errorMsg.set('');
    try {
      // Simulate SAML / OIDC JIT claim exchange
      const mockClaims = {
        email: 'alex.sso@example.com',
        username: 'alex.sso',
        display_name: 'Alex Rivera (SSO)',
        role: 'Developer',
      };
      await this.api.ssoCallback(mockClaims);
      await this.router.navigate(['/board']);
    } catch (err) {
      this.errorMsg.set((err as Error).message);
    } finally {
      this.loading.set(false);
    }
  }
}
