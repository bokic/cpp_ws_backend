import { Component, OnInit, inject, signal } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { RouterModule } from '@angular/router';
import { ApiService } from '../../services/api.service';
import { I18nService, SUPPORTED_LANGUAGES, I18nPipe, LocaleDatePipe } from '../../services/i18n.service';
import { UserPreferences } from '../../models/jira.models';

@Component({
  selector: 'app-user-preferences',
  standalone: true,
  imports: [CommonModule, FormsModule, RouterModule, I18nPipe],
  template: `
    <div class="preferences-container" style="max-width: 900px; margin: 24px auto; padding: 0 16px;">
      <!-- Header -->
      <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 24px;">
        <div>
          <h1 style="margin: 0 0 4px 0; font-size: 24px; font-weight: 700; color: var(--text-primary);">
            {{ 'preferences.title' | i18n }}
          </h1>
          <p style="margin: 0; font-size: 14px; color: var(--text-secondary);">
            {{ 'preferences.subtitle' | i18n }}
          </p>
        </div>

        <div style="display: flex; gap: 8px;">
          <a routerLink="/settings/security" class="btn btn-secondary btn-sm">
            🔐 {{ 'nav.security' | i18n }}
          </a>
          <a routerLink="/developer/docs" class="btn btn-secondary btn-sm">
            📖 {{ 'nav.developerDocs' | i18n }}
          </a>
        </div>
      </div>

      <!-- Feedback Banner -->
      @if (feedback()) {
        <div
          [class]="'alert alert-' + feedback()!.type"
          style="margin-bottom: 20px; padding: 12px 16px; border-radius: 6px; display: flex; justify-content: space-between; align-items: center;"
        >
          <span>{{ feedback()!.message }}</span>
          <button type="button" class="btn-ghost" style="font-size: 16px;" (click)="feedback.set(null)">✕</button>
        </div>
      }

      <div style="display: grid; grid-template-columns: 2fr 1fr; gap: 24px;">
        <!-- Left: Settings Form -->
        <div class="card" style="background: var(--bg-surface); border: 1px solid var(--border-color); border-radius: 8px; padding: 24px; box-shadow: var(--shadow-sm);">
          <form (ngSubmit)="savePreferences()">
            <!-- Language Selector -->
            <div style="margin-bottom: 20px;">
              <label style="display: block; font-size: 14px; font-weight: 600; color: var(--text-primary); margin-bottom: 6px;">
                🌐 {{ 'preferences.language' | i18n }}
              </label>
              <select
                class="form-control"
                [(ngModel)]="form.language"
                name="language"
                style="width: 100%; padding: 8px 12px; border: 1px solid var(--border-color); border-radius: 6px; background: var(--bg-surface); color: var(--text-primary); font-size: 14px;"
                (ngModelChange)="onLanguageChange($event)"
              >
                @for (lang of supportedLanguages; track lang.code) {
                  <option [value]="lang.code">
                    {{ lang.flag }} {{ lang.name }} ({{ lang.nativeName }})
                  </option>
                }
              </select>
              <small style="display: block; margin-top: 4px; color: var(--text-secondary); font-size: 12px;">
                Instant UI runtime translation across navigation, boards, and modals.
              </small>
            </div>

            <!-- Timezone Selector -->
            <div style="margin-bottom: 20px;">
              <label style="display: block; font-size: 14px; font-weight: 600; color: var(--text-primary); margin-bottom: 6px;">
                ⏰ {{ 'preferences.timezone' | i18n }}
              </label>
              <select
                class="form-control"
                [(ngModel)]="form.timezone"
                name="timezone"
                style="width: 100%; padding: 8px 12px; border: 1px solid var(--border-color); border-radius: 6px; background: var(--bg-surface); color: var(--text-primary); font-size: 14px;"
              >
                @for (tz of timezones; track tz) {
                  <option [value]="tz">{{ tz }}</option>
                }
              </select>
            </div>

            <!-- Date Format Selector -->
            <div style="margin-bottom: 20px;">
              <label style="display: block; font-size: 14px; font-weight: 600; color: var(--text-primary); margin-bottom: 6px;">
                📅 {{ 'preferences.dateFormat' | i18n }}
              </label>
              <div style="display: flex; gap: 12px; flex-wrap: wrap;">
                <label style="display: flex; align-items: center; gap: 6px; font-size: 14px; cursor: pointer;">
                  <input type="radio" name="date_format" [(ngModel)]="form.date_format" value="YYYY-MM-DD" />
                  <span>YYYY-MM-DD (ISO 8601)</span>
                </label>
                <label style="display: flex; align-items: center; gap: 6px; font-size: 14px; cursor: pointer;">
                  <input type="radio" name="date_format" [(ngModel)]="form.date_format" value="DD/MM/YYYY" />
                  <span>DD/MM/YYYY (Europe / UK)</span>
                </label>
                <label style="display: flex; align-items: center; gap: 6px; font-size: 14px; cursor: pointer;">
                  <input type="radio" name="date_format" [(ngModel)]="form.date_format" value="MM/DD/YYYY" />
                  <span>MM/DD/YYYY (US)</span>
                </label>
              </div>
            </div>

            <!-- Time Format Selector -->
            <div style="margin-bottom: 24px;">
              <label style="display: block; font-size: 14px; font-weight: 600; color: var(--text-primary); margin-bottom: 6px;">
                ⏱️ {{ 'preferences.timeFormat' | i18n }}
              </label>
              <div style="display: flex; gap: 16px;">
                <label style="display: flex; align-items: center; gap: 6px; font-size: 14px; cursor: pointer;">
                  <input type="radio" name="time_format" [(ngModel)]="form.time_format" value="24h" />
                  <span>24-Hour (14:30)</span>
                </label>
                <label style="display: flex; align-items: center; gap: 6px; font-size: 14px; cursor: pointer;">
                  <input type="radio" name="time_format" [(ngModel)]="form.time_format" value="12h" />
                  <span>12-Hour AM/PM (2:30 PM)</span>
                </label>
              </div>
            </div>

            <div style="display: flex; justify-content: flex-end; gap: 12px;">
              <button
                type="submit"
                class="btn btn-primary"
                [disabled]="saving()"
              >
                {{ saving() ? ('common.loading' | i18n) : ('preferences.saveBtn' | i18n) }}
              </button>
            </div>
          </form>
        </div>

        <!-- Right: Live Formatting Preview Card -->
        <div style="display: flex; flex-direction: column; gap: 16px;">
          <div class="card" style="background: var(--bg-surface); border: 1px solid var(--border-color); border-radius: 8px; padding: 20px; box-shadow: var(--shadow-sm);">
            <h3 style="margin: 0 0 8px 0; font-size: 16px; font-weight: 600; color: var(--text-primary);">
              ✨ {{ 'preferences.previewTitle' | i18n }}
            </h3>
            <p style="margin: 0 0 16px 0; font-size: 13px; color: var(--text-secondary);">
              {{ 'preferences.previewDesc' | i18n }}
            </p>

            <div style="background: var(--bg-subtle); padding: 14px; border-radius: 6px; border: 1px dashed var(--border-color); text-align: center;">
              <div style="font-size: 20px; font-weight: 700; color: var(--brand-primary); font-family: monospace;">
                {{ getFormattedPreview() }}
              </div>
              <div style="font-size: 12px; color: var(--text-secondary); margin-top: 6px;">
                Format: <code>{{ form.date_format }}</code>, <code>{{ form.time_format }}</code>
              </div>
            </div>

            <div style="margin-top: 16px; font-size: 12px; color: var(--text-secondary); line-height: 1.5;">
              <div>• Active Language: <strong>{{ form.language.toUpperCase() }}</strong></div>
              <div>• Selected Timezone: <strong>{{ form.timezone }}</strong></div>
            </div>
          </div>

          <!-- Quick Navigation Card -->
          <div class="card" style="background: var(--bg-surface); border: 1px solid var(--border-color); border-radius: 8px; padding: 20px; box-shadow: var(--shadow-sm);">
            <h4 style="margin: 0 0 12px 0; font-size: 14px; font-weight: 600; color: var(--text-primary);">
              Enterprise Security &amp; Docs
            </h4>
            <div style="display: flex; flex-direction: column; gap: 8px;">
              <a routerLink="/settings/security" style="color: var(--brand-primary); text-decoration: none; font-size: 13px; display: flex; align-items: center; gap: 6px;">
                <span>🛡️</span> Two-Factor Authentication &amp; SSO →
              </a>
              <a routerLink="/settings/mail-handlers" style="color: var(--brand-primary); text-decoration: none; font-size: 13px; display: flex; align-items: center; gap: 6px;">
                <span>✉️</span> Inbound Mail Handlers →
              </a>
              <a routerLink="/developer/docs" style="color: var(--brand-primary); text-decoration: none; font-size: 13px; display: flex; align-items: center; gap: 6px;">
                <span>🚀</span> Developer Portal &amp; Swagger Docs →
              </a>
            </div>
          </div>
        </div>
      </div>
    </div>
  `
})
export class UserPreferencesComponent implements OnInit {
  readonly api = inject(ApiService);
  readonly i18n = inject(I18nService);

  readonly supportedLanguages = SUPPORTED_LANGUAGES;
  readonly timezones = [
    'UTC',
    'America/New_York',
    'America/Chicago',
    'America/Denver',
    'America/Los_Angeles',
    'Europe/London',
    'Europe/Berlin',
    'Europe/Paris',
    'Asia/Tokyo',
    'Asia/Shanghai',
    'Asia/Singapore',
    'Australia/Sydney'
  ];

  form: UserPreferences = {
    language: 'en',
    timezone: 'UTC',
    date_format: 'YYYY-MM-DD',
    time_format: '24h'
  };

  readonly saving = signal(false);
  readonly feedback = signal<{ type: 'success' | 'danger'; message: string } | null>(null);

  async ngOnInit(): Promise<void> {
    const cur = this.i18n.preferences();
    this.form = { ...cur };

    try {
      const prefs = await this.api.getUserPreferences();
      this.form = { ...prefs };
    } catch {
      // Use fallback
    }
  }

  onLanguageChange(newLang: string): void {
    this.i18n.setLanguage(newLang);
  }

  getFormattedPreview(): string {
    const d = new Date();
    const df = this.form.date_format || 'YYYY-MM-DD';
    const tf = this.form.time_format || '24h';

    const year = d.getFullYear();
    const month = String(d.getMonth() + 1).padStart(2, '0');
    const day = String(d.getDate()).padStart(2, '0');

    let datePart = `${year}-${month}-${day}`;
    if (df === 'DD/MM/YYYY') {
      datePart = `${day}/${month}/${year}`;
    } else if (df === 'MM/DD/YYYY') {
      datePart = `${month}/${day}/${year}`;
    }

    let hours = d.getHours();
    const minutes = String(d.getMinutes()).padStart(2, '0');
    let timePart = '';

    if (tf === '12h') {
      const ampm = hours >= 12 ? 'PM' : 'AM';
      hours = hours % 12;
      hours = hours ? hours : 12;
      timePart = `${hours}:${minutes} ${ampm}`;
    } else {
      timePart = `${String(hours).padStart(2, '0')}:${minutes}`;
    }

    return `${datePart} ${timePart}`;
  }

  async savePreferences(): Promise<void> {
    this.saving.set(true);
    this.feedback.set(null);
    try {
      const updated = await this.api.updateUserPreferences(this.form);
      this.form = { ...updated };
      this.feedback.set({
        type: 'success',
        message: this.i18n.t('preferences.saved')
      });
    } catch (err: any) {
      this.feedback.set({
        type: 'danger',
        message: err.message || 'Failed to save preferences'
      });
    } finally {
      this.saving.set(false);
    }
  }
}
