import { Component, OnInit, inject, signal } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { RouterModule } from '@angular/router';
import { DomSanitizer, SafeHtml } from '@angular/platform-browser';
import { ApiService } from '../../services/api.service';
import { SsoConfig, TwoFactorSetup, TwoFactorStatus } from '../../models/jira.models';

@Component({
  selector: 'app-security-settings',
  standalone: true,
  imports: [CommonModule, FormsModule, RouterModule],
  template: `
    <div class="security-settings-container" style="max-width: 900px; margin: 24px auto; padding: 0 16px;">
      <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 24px;">
        <div>
          <h1 style="margin: 0 0 4px 0; font-size: 24px; font-weight: 700; color: var(--text-primary);">
            Account Security &amp; Enterprise SSO
          </h1>
          <p style="margin: 0; font-size: 14px; color: var(--text-secondary);">
            Manage two-factor authentication (TOTP), backup recovery codes, and enterprise directory settings
          </p>
        </div>
      </div>

      @if (feedback()) {
        <div
          [class]="'alert alert-' + feedback()!.type"
          style="margin-bottom: 20px; padding: 12px 16px; border-radius: 6px; display: flex; justify-content: space-between; align-items: center;"
        >
          <span>{{ feedback()!.message }}</span>
          <button type="button" class="btn-ghost" style="font-size: 16px;" (click)="feedback.set(null)">✕</button>
        </div>
      }

      <div style="display: grid; grid-template-columns: 1fr; gap: 24px;">
        <!-- Card 1: Two-Factor Authentication (RFC 6238 TOTP) -->
        <div class="card" style="background: var(--bg-surface); border: 1px solid var(--border-color); border-radius: 8px; padding: 24px; box-shadow: var(--shadow-sm);">
          <div style="display: flex; justify-content: space-between; align-items: flex-start; margin-bottom: 16px;">
            <div style="display: flex; gap: 12px; align-items: center;">
              <span style="font-size: 28px;">📱</span>
              <div>
                <h2 style="margin: 0; font-size: 18px; font-weight: 600; color: var(--text-primary);">
                  Two-Factor Authentication (2FA)
                </h2>
                <p style="margin: 2px 0 0 0; font-size: 13px; color: var(--text-secondary);">
                  RFC 6238 TOTP authenticator app support (Google Authenticator, Authy, 1Password)
                </p>
              </div>
            </div>
            <div>
              @if (twoFactorStatus()?.is_enabled) {
                <span class="badge" style="background: #E3FCEF; color: #006644; font-weight: 700; padding: 4px 10px; border-radius: 12px; font-size: 12px;">
                  ✓ 2FA ENABLED
                </span>
              } @else {
                <span class="badge" style="background: #FFEBE6; color: #DE350B; font-weight: 700; padding: 4px 10px; border-radius: 12px; font-size: 12px;">
                  DISABLED
                </span>
              }
            </div>
          </div>

          <!-- Active 2FA Info -->
          @if (twoFactorStatus()?.is_enabled && !setupStep()) {
            <div style="background: var(--bg-subtle); border: 1px solid var(--border-color); border-radius: 6px; padding: 16px; margin-bottom: 16px;">
              <div style="display: flex; justify-content: space-between; align-items: center;">
                <div>
                  <div style="font-weight: 600; font-size: 14px; color: var(--text-primary); margin-bottom: 4px;">
                    Your account is protected with Two-Factor Authentication
                  </div>
                  <div style="font-size: 13px; color: var(--text-secondary);">
                    One-time login codes are required when signing in with username and password.
                  </div>
                  <div style="font-size: 12px; margin-top: 8px; color: var(--text-muted);">
                    Remaining emergency backup recovery codes: <strong>{{ twoFactorStatus()?.remaining_recovery_codes || 0 }}</strong>
                  </div>
                </div>
                <div style="display: flex; gap: 8px;">
                  <button type="button" class="btn btn-secondary btn-sm" (click)="startSetup()" [disabled]="loading()">
                    Reconfigure 2FA
                  </button>
                  <button type="button" class="btn btn-danger btn-sm" (click)="disable2fa()" [disabled]="loading()">
                    Disable 2FA
                  </button>
                </div>
              </div>
            </div>
          }

          <!-- Setup Workflow Wizard -->
          @if (!twoFactorStatus()?.is_enabled && !setupStep()) {
            <div style="margin-top: 12px;">
              <p style="font-size: 14px; color: var(--text-secondary); line-height: 1.5; margin-bottom: 16px;">
                Two-factor authentication adds an extra layer of security to your JiraLike account.
                In addition to your password, you'll need to enter a 6-digit code from an authenticator app when signing in.
              </p>
              <button type="button" class="btn btn-primary" (click)="startSetup()" [disabled]="loading()">
                {{ loading() ? 'Generating Secret...' : 'Set Up Two-Factor Authentication →' }}
              </button>
            </div>
          }

          @if (setupStep() && setupData()) {
            <div class="setup-wizard" style="margin-top: 16px; border-top: 1px solid var(--border-color); padding-top: 16px;">
              <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 16px;">
                <h3 style="margin: 0; font-size: 16px; font-weight: 600;">Complete 2FA Configuration</h3>
                <button type="button" class="btn btn-ghost btn-sm" (click)="setupStep.set(false)">Cancel</button>
              </div>

              <div style="display: grid; grid-template-columns: 240px 1fr; gap: 24px; align-items: start;">
                <!-- Column 1: QR Code SVG -->
                <div style="text-align: center; background: white; padding: 16px; border-radius: 8px; border: 1px solid var(--border-color);">
                  <div [innerHTML]="sanitizedQrSvg()" style="width: 200px; height: 200px; margin: 0 auto;"></div>
                  <span style="font-size: 11px; color: #555; display: block; margin-top: 8px;">
                    Scan with Google Authenticator or Authy
                  </span>
                </div>

                <!-- Column 2: Secret & Verification -->
                <div>
                  <div style="margin-bottom: 16px;">
                    <label style="display: block; font-size: 12px; font-weight: 600; color: var(--text-secondary); margin-bottom: 4px;">
                      Manual Secret Key (Base32):
                    </label>
                    <div style="display: flex; gap: 8px; align-items: center;">
                      <code style="background: var(--bg-subtle); padding: 8px 12px; border-radius: 4px; font-size: 14px; font-family: monospace; letter-spacing: 1px; flex: 1;">
                        {{ setupData()!.secret }}
                      </code>
                      <button type="button" class="btn btn-ghost btn-sm" (click)="copyText(setupData()!.secret)" title="Copy Secret">
                        📋 Copy
                      </button>
                    </div>
                  </div>

                  <!-- 10 Recovery Codes -->
                  <div style="margin-bottom: 16px;">
                    <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 4px;">
                      <label style="font-size: 12px; font-weight: 600; color: var(--text-secondary);">
                        Emergency Backup Recovery Codes (One-Time Use):
                      </label>
                      <button type="button" class="btn btn-ghost btn-sm" (click)="copyRecoveryCodes()" style="font-size: 11px; padding: 2px 6px;">
                        📋 Copy All Codes
                      </button>
                    </div>
                    <div style="display: grid; grid-template-columns: repeat(2, 1fr); gap: 6px; background: var(--bg-subtle); padding: 10px; border-radius: 4px; border: 1px solid var(--border-color); font-family: monospace; font-size: 12px;">
                      @for (code of setupData()!.recovery_codes; track code) {
                        <div>• {{ code }}</div>
                      }
                    </div>
                    <small style="color: var(--text-muted); font-size: 11px; display: block; margin-top: 4px;">
                      ⚠️ Save these 10 recovery codes somewhere safe. If you lose your phone, you can use these to sign in.
                    </small>
                  </div>

                  <!-- Verification Form -->
                  <form (ngSubmit)="confirmAndActivate()" style="background: var(--bg-subtle); padding: 16px; border-radius: 6px; border: 1px solid var(--border-color);">
                    <label style="display: block; font-size: 13px; font-weight: 600; margin-bottom: 6px;">
                      Step 3: Enter the 6-digit code shown in your Authenticator App:
                    </label>
                    <div style="display: flex; gap: 8px;">
                      <input
                        type="text"
                        maxlength="6"
                        [(ngModel)]="verificationCode"
                        name="verificationCode"
                        placeholder="123456"
                        required
                        style="width: 140px; font-size: 18px; text-align: center; letter-spacing: 3px; font-family: monospace; font-weight: 700; border: 1px solid var(--border-color); border-radius: 4px; padding: 6px;"
                      />
                      <button type="submit" class="btn btn-primary" [disabled]="loading() || verificationCode.length < 6">
                        {{ loading() ? 'Verifying...' : 'Verify &amp; Activate 2FA' }}
                      </button>
                    </div>
                  </form>
                </div>
              </div>
            </div>
          }
        </div>

        <!-- Card 2: Enterprise SSO Configuration (Admin Only or Status) -->
        <div class="card" style="background: var(--bg-surface); border: 1px solid var(--border-color); border-radius: 8px; padding: 24px; box-shadow: var(--shadow-sm);">
          <div style="display: flex; justify-content: space-between; align-items: flex-start; margin-bottom: 16px;">
            <div style="display: flex; gap: 12px; align-items: center;">
              <span style="font-size: 28px;">🏢</span>
              <div>
                <h2 style="margin: 0; font-size: 18px; font-weight: 600; color: var(--text-primary);">
                  Enterprise Single Sign-On (SAML 2.0 / OIDC)
                </h2>
                <p style="margin: 2px 0 0 0; font-size: 13px; color: var(--text-secondary);">
                  Centralized identity provider integration with Okta, Azure AD / Microsoft Entra ID, Google Workspace, Keycloak
                </p>
              </div>
            </div>
            <div>
              @if (ssoConfig()?.is_enabled) {
                <span class="badge" style="background: #E3FCEF; color: #006644; font-weight: 700; padding: 4px 10px; border-radius: 12px; font-size: 12px;">
                  ✓ SSO ACTIVE ({{ ssoConfig()?.protocol }})
                </span>
              } @else {
                <span class="badge" style="background: #DFE1E6; color: #42526E; font-weight: 700; padding: 4px 10px; border-radius: 12px; font-size: 12px;">
                  SSO DISABLED
                </span>
              }
            </div>
          </div>

          @if (api.currentUser()?.role === 'Admin') {
            <!-- Admin SSO Configuration Form -->
            <form (ngSubmit)="saveSso()" style="display: grid; grid-template-columns: 1fr 1fr; gap: 16px; margin-top: 16px;">
              <div class="form-group" style="grid-column: span 2;">
                <label style="display: flex; align-items: center; gap: 8px; cursor: pointer; font-weight: 600;">
                  <input type="checkbox" [(ngModel)]="ssoForm.is_enabled" name="sso_enabled" />
                  <span>Enable Enterprise SSO Authentication</span>
                </label>
              </div>

              <div class="form-group">
                <label style="display: block; font-size: 12px; font-weight: 600; margin-bottom: 4px;">Protocol</label>
                <select class="form-control" [(ngModel)]="ssoForm.protocol" name="protocol">
                  <option value="SAML2">SAML 2.0</option>
                  <option value="OIDC">OpenID Connect (OIDC)</option>
                </select>
              </div>

              <div class="form-group">
                <label style="display: block; font-size: 12px; font-weight: 600; margin-bottom: 4px;">Identity Provider Name</label>
                <input type="text" class="form-control" [(ngModel)]="ssoForm.provider_name" name="provider_name" placeholder="Okta / Azure AD" />
              </div>

              <div class="form-group">
                <label style="display: block; font-size: 12px; font-weight: 600; margin-bottom: 4px;">IdP Single Sign-On URL</label>
                <input type="text" class="form-control" [(ngModel)]="ssoForm.sso_url" name="sso_url" placeholder="https://idp.example.com/sso/login" />
              </div>

              <div class="form-group">
                <label style="display: block; font-size: 12px; font-weight: 600; margin-bottom: 4px;">IdP Entity ID / Issuer</label>
                <input type="text" class="form-control" [(ngModel)]="ssoForm.issuer" name="issuer" placeholder="https://idp.example.com" />
              </div>

              <div class="form-group">
                <label style="display: block; font-size: 12px; font-weight: 600; margin-bottom: 4px;">Allowed Email Domain</label>
                <input type="text" class="form-control" [(ngModel)]="ssoForm.email_domain" name="email_domain" placeholder="company.com" />
              </div>

              <div class="form-group">
                <label style="display: block; font-size: 12px; font-weight: 600; margin-bottom: 4px;">Default New User Role</label>
                <select class="form-control" [(ngModel)]="ssoForm.default_role" name="default_role">
                  <option value="Developer">Developer</option>
                  <option value="Product Manager">Product Manager</option>
                  <option value="QA Engineer">QA Engineer</option>
                  <option value="Viewer">Viewer</option>
                </select>
              </div>

              <div class="form-group" style="grid-column: span 2;">
                <label style="display: flex; align-items: center; gap: 8px; cursor: pointer; font-size: 13px;">
                  <input type="checkbox" [(ngModel)]="ssoForm.auto_provision" name="auto_provision" />
                  <span>Just-In-Time (JIT) User Provisioning (automatically create user account on first successful SSO sign in)</span>
                </label>
              </div>

              <div style="grid-column: span 2; display: flex; justify-content: space-between; align-items: center; border-top: 1px solid var(--border-color); padding-top: 12px;">
                <a href="/api/auth/sso/metadata" target="_blank" class="btn btn-ghost btn-sm">
                  Download SP SAML Metadata XML ↗
                </a>
                <button type="submit" class="btn btn-primary" [disabled]="loading()">
                  {{ loading() ? 'Saving...' : 'Save SSO Configuration' }}
                </button>
              </div>
            </form>
          } @else {
            <p style="font-size: 14px; color: var(--text-secondary); margin: 0;">
              Enterprise SSO is currently <strong>{{ ssoConfig()?.is_enabled ? 'Active' : 'Disabled' }}</strong> for domain <code>{{ ssoConfig()?.email_domain }}</code>.
              Only administrators can modify SSO parameters.
            </p>
          }
        </div>
      </div>
    </div>
  `,
})
export class SecuritySettingsComponent implements OnInit {
  readonly api = inject(ApiService);
  private readonly sanitizer = inject(DomSanitizer);

  readonly loading = signal(false);
  readonly feedback = signal<{ type: 'success' | 'error'; message: string } | null>(null);

  // 2FA state
  readonly twoFactorStatus = signal<TwoFactorStatus | null>(null);
  readonly setupStep = signal(false);
  readonly setupData = signal<TwoFactorSetup | null>(null);
  verificationCode = '';

  // SSO state
  readonly ssoConfig = signal<SsoConfig | null>(null);
  ssoForm: Partial<SsoConfig> = {
    is_enabled: false,
    protocol: 'SAML2',
    provider_name: 'Enterprise IdP',
    entity_id: 'urn:jiralike:sp',
    sso_url: 'https://idp.example.com/sso/login',
    issuer: 'https://idp.example.com',
    client_id: 'jiralike-enterprise-client',
    email_domain: 'example.com',
    auto_provision: true,
    default_role: 'Developer',
  };

  async ngOnInit(): Promise<void> {
    await this.loadAll();
  }

  async loadAll(): Promise<void> {
    this.loading.set(true);
    try {
      const [st, sso] = await Promise.all([
        this.api.get2faStatus().catch(() => null),
        this.api.getSsoConfig().catch(() => null),
      ]);
      if (st) this.twoFactorStatus.set(st);
      if (sso) {
        this.ssoConfig.set(sso);
        this.ssoForm = { ...sso };
      }
    } finally {
      this.loading.set(false);
    }
  }

  sanitizedQrSvg(): SafeHtml {
    const raw = this.setupData()?.qr_svg || '';
    return this.sanitizer.bypassSecurityTrustHtml(raw);
  }

  async startSetup(): Promise<void> {
    this.loading.set(true);
    this.feedback.set(null);
    try {
      const setup = await this.api.setup2fa();
      this.setupData.set(setup);
      this.verificationCode = '';
      this.setupStep.set(true);
    } catch (err) {
      this.feedback.set({ type: 'error', message: (err as Error).message });
    } finally {
      this.loading.set(false);
    }
  }

  async confirmAndActivate(): Promise<void> {
    if (!this.verificationCode || this.verificationCode.length < 6) return;
    this.loading.set(true);
    this.feedback.set(null);
    try {
      await this.api.verifyAndEnable2fa(this.verificationCode.trim());
      this.feedback.set({ type: 'success', message: 'Two-Factor Authentication successfully activated!' });
      this.setupStep.set(false);
      await this.loadAll();
    } catch (err) {
      this.feedback.set({ type: 'error', message: (err as Error).message });
    } finally {
      this.loading.set(false);
    }
  }

  async disable2fa(): Promise<void> {
    if (!confirm('Are you sure you want to disable Two-Factor Authentication on your account?')) return;
    this.loading.set(true);
    this.feedback.set(null);
    try {
      await this.api.disable2fa();
      this.feedback.set({ type: 'success', message: 'Two-Factor Authentication disabled' });
      await this.loadAll();
    } catch (err) {
      this.feedback.set({ type: 'error', message: (err as Error).message });
    } finally {
      this.loading.set(false);
    }
  }

  copyText(text: string): void {
    navigator.clipboard.writeText(text);
    this.feedback.set({ type: 'success', message: 'Copied secret key to clipboard!' });
  }

  copyRecoveryCodes(): void {
    if (!this.setupData()?.recovery_codes) return;
    const all = this.setupData()!.recovery_codes.join('\n');
    navigator.clipboard.writeText(all);
    this.feedback.set({ type: 'success', message: 'Copied all 10 emergency backup recovery codes to clipboard!' });
  }

  async saveSso(): Promise<void> {
    this.loading.set(true);
    this.feedback.set(null);
    try {
      const updated = await this.api.updateSsoConfig(this.ssoForm);
      this.ssoConfig.set(updated);
      this.ssoForm = { ...updated };
      this.feedback.set({ type: 'success', message: 'Enterprise SSO configuration saved successfully!' });
    } catch (err) {
      this.feedback.set({ type: 'error', message: (err as Error).message });
    } finally {
      this.loading.set(false);
    }
  }
}
