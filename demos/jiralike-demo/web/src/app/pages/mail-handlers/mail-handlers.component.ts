import { Component, OnInit, inject, signal } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { RouterModule } from '@angular/router';
import { ApiService } from '../../services/api.service';
import { InboundMailHandler, InboundMailLog, Project, IssueTypeDef, User } from '../../models/jira.models';

@Component({
  selector: 'app-mail-handlers',
  standalone: true,
  imports: [CommonModule, FormsModule, RouterModule],
  template: `
    <div class="mail-handlers-container" style="max-width: 1100px; margin: 24px auto; padding: 0 16px;">
      <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 24px;">
        <div>
          <h1 style="margin: 0 0 4px 0; font-size: 24px; font-weight: 700; color: var(--text-primary);">
            Inbound Email-to-Issue Handlers
          </h1>
          <p style="margin: 0; font-size: 14px; color: var(--text-secondary);">
            Convert incoming customer and team emails into new work items or threaded comments automatically
          </p>
        </div>
        <div style="display: flex; gap: 8px;">
          <button type="button" class="btn btn-primary" (click)="openCreateModal()">
            + Add Mail Handler
          </button>
        </div>
      </div>

      <!-- Navigation Tabs -->
      <div style="display: flex; gap: 8px; border-bottom: 2px solid var(--border-color); margin-bottom: 20px;">
        <button
          type="button"
          class="btn btn-ghost"
          [style.border-bottom]="activeTab() === 'handlers' ? '2px solid var(--brand-primary)' : 'none'"
          [style.color]="activeTab() === 'handlers' ? 'var(--brand-primary)' : 'var(--text-secondary)'"
          [style.font-weight]="activeTab() === 'handlers' ? '600' : 'normal'"
          (click)="activeTab.set('handlers')"
        >
          📫 Mail Handlers ({{ handlers().length }})
        </button>
        <button
          type="button"
          class="btn btn-ghost"
          [style.border-bottom]="activeTab() === 'logs' ? '2px solid var(--brand-primary)' : 'none'"
          [style.color]="activeTab() === 'logs' ? 'var(--brand-primary)' : 'var(--text-secondary)'"
          [style.font-weight]="activeTab() === 'logs' ? '600' : 'normal'"
          (click)="switchTab('logs')"
        >
          📜 Processing Logs &amp; Audit Trail
        </button>
        <button
          type="button"
          class="btn btn-ghost"
          [style.border-bottom]="activeTab() === 'simulator' ? '2px solid var(--brand-primary)' : 'none'"
          [style.color]="activeTab() === 'simulator' ? 'var(--brand-primary)' : 'var(--text-secondary)'"
          [style.font-weight]="activeTab() === 'simulator' ? '600' : 'normal'"
          (click)="activeTab.set('simulator')"
        >
          🧪 Email Hook Simulator
        </button>
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

      <!-- TAB 1: Handlers Table -->
      @if (activeTab() === 'handlers') {
        <div class="card" style="background: var(--bg-surface); border: 1px solid var(--border-color); border-radius: 8px; overflow: hidden; box-shadow: var(--shadow-sm);">
          <table class="table" style="width: 100%; border-collapse: collapse; text-align: left; font-size: 13px;">
            <thead>
              <tr style="background: var(--bg-subtle); border-bottom: 1px solid var(--border-color); color: var(--text-secondary);">
                <th style="padding: 12px 16px;">Name &amp; Address</th>
                <th style="padding: 12px 16px;">Routing Rule</th>
                <th style="padding: 12px 16px;">Issue Type</th>
                <th style="padding: 12px 16px;">Fallback Reporter</th>
                <th style="padding: 12px 16px;">Status</th>
                <th style="padding: 12px 16px; text-align: right;">Actions</th>
              </tr>
            </thead>
            <tbody>
              @for (h of handlers(); track h.id) {
                <tr style="border-bottom: 1px solid var(--border-color);">
                  <td style="padding: 12px 16px;">
                    <div style="font-weight: 600; color: var(--text-primary);">{{ h.name }}</div>
                    <code style="color: var(--brand-primary); font-size: 12px;">{{ h.incoming_address }}</code>
                  </td>
                  <td style="padding: 12px 16px;">
                    <span class="badge" style="background: var(--bg-subtle); border: 1px solid var(--border-color); padding: 2px 6px; border-radius: 4px; font-weight: 600;">
                      {{ h.target_project_key || 'Project #' + h.target_project_id }}
                    </span>
                    <div style="font-size: 11px; color: var(--text-muted); margin-top: 2px;">
                      {{ h.handler_type }}
                    </div>
                  </td>
                  <td style="padding: 12px 16px;">
                    {{ h.issue_type_name || 'Project Default' }}
                  </td>
                  <td style="padding: 12px 16px;">
                    {{ h.default_reporter_name || 'Reporter #' + h.default_reporter_id }}
                  </td>
                  <td style="padding: 12px 16px;">
                    @if (h.is_active) {
                      <span style="color: #006644; font-weight: 600; font-size: 12px;">● Active</span>
                    } @else {
                      <span style="color: #6B778C; font-size: 12px;">○ Inactive</span>
                    }
                  </td>
                  <td style="padding: 12px 16px; text-align: right;">
                    <button type="button" class="btn btn-ghost btn-sm" (click)="editHandler(h)" style="margin-right: 4px;">Edit</button>
                    <button type="button" class="btn btn-danger btn-sm" (click)="deleteHandler(h.id)">Delete</button>
                  </td>
                </tr>
              } @empty {
                <tr>
                  <td colspan="6" style="padding: 32px; text-align: center; color: var(--text-secondary);">
                    No inbound mail handlers configured. Click "+ Add Mail Handler" to connect your incoming mailhook.
                  </td>
                </tr>
              }
            </tbody>
          </table>
        </div>
      }

      <!-- TAB 2: Logs Table -->
      @if (activeTab() === 'logs') {
        <div class="card" style="background: var(--bg-surface); border: 1px solid var(--border-color); border-radius: 8px; overflow: hidden; box-shadow: var(--shadow-sm);">
          <div style="padding: 12px 16px; background: var(--bg-subtle); border-bottom: 1px solid var(--border-color); display: flex; justify-content: space-between; align-items: center;">
            <span style="font-weight: 600; font-size: 13px;">Recent Inbound Email Processing History</span>
            <button type="button" class="btn btn-ghost btn-sm" (click)="loadLogs()" [disabled]="loading()">🔄 Refresh Logs</button>
          </div>
          <table class="table" style="width: 100%; border-collapse: collapse; text-align: left; font-size: 13px;">
            <thead>
              <tr style="border-bottom: 1px solid var(--border-color); color: var(--text-secondary);">
                <th style="padding: 10px 14px;">Timestamp</th>
                <th style="padding: 10px 14px;">Status</th>
                <th style="padding: 10px 14px;">From → To</th>
                <th style="padding: 10px 14px;">Subject</th>
                <th style="padding: 10px 14px;">Result Ticket</th>
              </tr>
            </thead>
            <tbody>
              @for (l of logs(); track l.id) {
                <tr style="border-bottom: 1px solid var(--border-color);">
                  <td style="padding: 10px 14px; color: var(--text-muted); font-size: 12px; white-space: nowrap;">
                    {{ l.created_at }}
                  </td>
                  <td style="padding: 10px 14px;">
                    @if (l.status === 'PROCESSED') {
                      <span class="badge" style="background: #E3FCEF; color: #006644; font-weight: 700; padding: 2px 8px; border-radius: 10px; font-size: 11px;">
                        ✓ PROCESSED
                      </span>
                    } @else if (l.status === 'REJECTED_SPAM') {
                      <span class="badge" style="background: #FFF0B3; color: #172B4D; font-weight: 700; padding: 2px 8px; border-radius: 10px; font-size: 11px;">
                        ⚠️ SPAM BLOCKED
                      </span>
                    } @else {
                      <span class="badge" style="background: #FFEBE6; color: #DE350B; font-weight: 700; padding: 2px 8px; border-radius: 10px; font-size: 11px;">
                        ✕ {{ l.status }}
                      </span>
                    }
                    @if (l.error_message) {
                      <div style="font-size: 11px; color: var(--text-muted); margin-top: 2px;">{{ l.error_message }}</div>
                    }
                  </td>
                  <td style="padding: 10px 14px; font-size: 12px;">
                    <div>{{ l.from_address }}</div>
                    <div style="color: var(--text-muted);">↳ {{ l.to_address }}</div>
                  </td>
                  <td style="padding: 10px 14px; font-weight: 500;">
                    {{ l.subject }}
                  </td>
                  <td style="padding: 10px 14px;">
                    @if (l.work_item_key) {
                      <a [routerLink]="['/items', l.work_item_key]" style="font-weight: 700; color: var(--brand-primary); text-decoration: none;">
                        {{ l.work_item_key }}
                      </a>
                      @if (l.comment_id) {
                        <span style="font-size: 11px; color: var(--text-muted);"> (Comment #{{ l.comment_id }})</span>
                      }
                    } @else {
                      <span style="color: var(--text-muted); font-size: 12px;">—</span>
                    }
                  </td>
                </tr>
              } @empty {
                <tr>
                  <td colspan="5" style="padding: 32px; text-align: center; color: var(--text-secondary);">
                    No email logs yet. Use the simulator tab to test inbound email hooks.
                  </td>
                </tr>
              }
            </tbody>
          </table>
        </div>
      }

      <!-- TAB 3: Email Simulator -->
      @if (activeTab() === 'simulator') {
        <div class="card" style="background: var(--bg-surface); border: 1px solid var(--border-color); border-radius: 8px; padding: 24px; box-shadow: var(--shadow-sm);">
          <h2 style="margin: 0 0 6px 0; font-size: 18px; font-weight: 600;">Incoming Email Hook Simulator (POST /api/mail/inbound)</h2>
          <p style="margin: 0 0 16px 0; font-size: 13px; color: var(--text-secondary);">
            Simulate mail servers (Postfix, Sendmail, AWS SES, SendGrid) delivering emails to JiraLike.
          </p>

          <form (ngSubmit)="sendSimulatedEmail()" style="display: grid; grid-template-columns: 1fr 1fr; gap: 16px;">
            <div class="form-group">
              <label style="display: block; font-size: 12px; font-weight: 600; margin-bottom: 4px;">From Email</label>
              <input type="email" class="form-control" [(ngModel)]="simFrom" name="simFrom" required placeholder="user@example.com" />
            </div>

            <div class="form-group">
              <label style="display: block; font-size: 12px; font-weight: 600; margin-bottom: 4px;">To Email (Mailhook Address)</label>
              <input type="email" class="form-control" [(ngModel)]="simTo" name="simTo" required placeholder="support@example.com" />
            </div>

            <div class="form-group" style="grid-column: span 2;">
              <label style="display: block; font-size: 12px; font-weight: 600; margin-bottom: 4px;">
                Subject Line (Tip: Include an issue key like <code>[PROJ-1]</code> to thread as a comment, or leave plain to create a new ticket)
              </label>
              <input type="text" class="form-control" [(ngModel)]="simSubject" name="simSubject" required placeholder="[PROJ-1] Follow up on database error" />
            </div>

            <div class="form-group" style="grid-column: span 2;">
              <label style="display: block; font-size: 12px; font-weight: 600; margin-bottom: 4px;">Email Body (Plain Text)</label>
              <textarea class="form-control" rows="4" [(ngModel)]="simBody" name="simBody" placeholder="Hello support team, here is the error log from production..."></textarea>
            </div>

            <div class="form-group" style="grid-column: span 2;">
              <label style="display: flex; align-items: center; gap: 8px; font-size: 13px;">
                <input type="checkbox" [(ngModel)]="simAttachFile" name="simAttachFile" />
                <span>Simulate attachment (e.g. <code>server_crash.log</code>)</span>
              </label>
            </div>

            <div style="grid-column: span 2; display: flex; justify-content: flex-end;">
              <button type="submit" class="btn btn-primary" [disabled]="loading()">
                {{ loading() ? 'Sending...' : '⚡ Send Simulated Email (POST /api/mail/inbound)' }}
              </button>
            </div>
          </form>

          @if (simResult()) {
            <div style="margin-top: 20px; background: var(--bg-subtle); padding: 16px; border-radius: 6px; border: 1px solid var(--border-color);">
              <h4 style="margin: 0 0 8px 0; font-size: 14px; font-weight: 600; color: #006644;">
                Server Response:
              </h4>
              <pre style="background: var(--bg-surface); padding: 12px; border-radius: 4px; border: 1px solid var(--border-color); margin: 0; font-size: 12px; overflow-x: auto;">{{ simResult() }}</pre>
            </div>
          }
        </div>
      }

      <!-- MODAL: Create / Edit Mail Handler -->
      @if (showModal()) {
        <div class="modal-backdrop" (click)="showModal.set(false)">
          <div class="modal-card" style="max-width: 600px; width: 90vw;" (click)="$event.stopPropagation()">
            <div class="modal-header">
              <h3 style="margin: 0; font-size: 18px; font-weight: 600;">
                {{ editingId() ? 'Edit Inbound Mail Handler' : 'Add Inbound Mail Handler' }}
              </h3>
              <button type="button" class="btn-ghost" style="font-size: 18px;" (click)="showModal.set(false)">✕</button>
            </div>

            <form (ngSubmit)="saveHandler()" style="padding: 20px; display: grid; grid-template-columns: 1fr 1fr; gap: 16px;">
              <div class="form-group" style="grid-column: span 2;">
                <label style="display: block; font-size: 12px; font-weight: 600; margin-bottom: 4px;">Handler Name</label>
                <input type="text" class="form-control" [(ngModel)]="handlerForm.name" name="name" required placeholder="Support Desk Inbox" />
              </div>

              <div class="form-group" style="grid-column: span 2;">
                <label style="display: block; font-size: 12px; font-weight: 600; margin-bottom: 4px;">
                  Incoming Email Address (or <code>*</code> for catch-all)
                </label>
                <input type="text" class="form-control" [(ngModel)]="handlerForm.incoming_address" name="incoming_address" required placeholder="support@example.com" />
              </div>

              <div class="form-group">
                <label style="display: block; font-size: 12px; font-weight: 600; margin-bottom: 4px;">Target Project</label>
                <select class="form-control" [(ngModel)]="handlerForm.target_project_id" name="target_project_id">
                  @for (p of api.projects(); track p.id) {
                    <option [value]="p.id">{{ p.name }} ({{ p.key }})</option>
                  }
                </select>
              </div>

              <div class="form-group">
                <label style="display: block; font-size: 12px; font-weight: 600; margin-bottom: 4px;">Created Issue Type</label>
                <select class="form-control" [(ngModel)]="handlerForm.issue_type_id" name="issue_type_id">
                  <option [value]="0">Project Default</option>
                  @for (t of api.issueTypes(); track t.id) {
                    <option [value]="t.id">{{ t.name }}</option>
                  }
                </select>
              </div>

              <div class="form-group">
                <label style="display: block; font-size: 12px; font-weight: 600; margin-bottom: 4px;">Fallback Default Reporter</label>
                <select class="form-control" [(ngModel)]="handlerForm.default_reporter_id" name="default_reporter_id">
                  @for (u of api.users(); track u.id) {
                    <option [value]="u.id">{{ u.display_name }} ({{ u.email }})</option>
                  }
                </select>
              </div>

              <div class="form-group">
                <label style="display: block; font-size: 12px; font-weight: 600; margin-bottom: 4px;">Processing Rule</label>
                <select class="form-control" [(ngModel)]="handlerForm.handler_type" name="handler_type">
                  <option value="CREATE_OR_COMMENT">Create new issue or append comment</option>
                  <option value="CREATE_ONLY">Create new issues only</option>
                  <option value="COMMENT_ONLY">Comment on existing issues only</option>
                </select>
              </div>

              <div class="form-group" style="grid-column: span 2;">
                <label style="display: block; font-size: 12px; font-weight: 600; margin-bottom: 4px;">
                  Spam Filter Keywords (comma-separated, auto-rejects bounces &amp; OOO)
                </label>
                <input
                  type="text"
                  class="form-control"
                  [(ngModel)]="handlerForm.spam_keywords"
                  name="spam_keywords"
                  placeholder="out of office,auto-reply,unsubscribe,mailer-daemon"
                />
              </div>

              <div class="form-group" style="grid-column: span 2;">
                <label style="display: flex; align-items: center; gap: 8px; font-size: 13px;">
                  <input type="checkbox" [(ngModel)]="handlerForm.is_active" name="is_active" />
                  <span>Handler is active and listening for incoming messages</span>
                </label>
              </div>

              <div style="grid-column: span 2; display: flex; justify-content: flex-end; gap: 8px; border-top: 1px solid var(--border-color); padding-top: 16px;">
                <button type="button" class="btn btn-ghost" (click)="showModal.set(false)">Cancel</button>
                <button type="submit" class="btn btn-primary" [disabled]="loading()">
                  {{ loading() ? 'Saving...' : 'Save Handler' }}
                </button>
              </div>
            </form>
          </div>
        </div>
      }
    </div>
  `,
})
export class MailHandlersComponent implements OnInit {
  readonly api = inject(ApiService);

  readonly activeTab = signal<'handlers' | 'logs' | 'simulator'>('handlers');
  readonly loading = signal(false);
  readonly feedback = signal<{ type: 'success' | 'error'; message: string } | null>(null);

  readonly handlers = signal<InboundMailHandler[]>([]);
  readonly logs = signal<InboundMailLog[]>([]);

  // Modal
  readonly showModal = signal(false);
  readonly editingId = signal<number | null>(null);
  handlerForm: Partial<InboundMailHandler> = {
    name: '',
    incoming_address: '',
    target_project_id: 1,
    issue_type_id: 0,
    default_reporter_id: 1,
    handler_type: 'CREATE_OR_COMMENT',
    spam_keywords: 'out of office,auto-reply,unsubscribe,mailer-daemon,delivery status notification',
    is_active: true,
  };

  // Simulator
  simFrom = 'customer@acme.corp';
  simTo = 'support@example.com';
  simSubject = 'Server outage in EU-West cluster';
  simBody = 'Our production database crashed with out-of-memory. Log attached below.';
  simAttachFile = true;
  readonly simResult = signal<string | null>(null);

  async ngOnInit(): Promise<void> {
    await this.loadHandlers();
  }

  async loadHandlers(): Promise<void> {
    this.loading.set(true);
    try {
      const list = await this.api.getMailHandlers();
      this.handlers.set(list);
      if (list.length > 0 && !this.simTo) {
        this.simTo = list[0].incoming_address;
      }
    } catch (err) {
      this.feedback.set({ type: 'error', message: (err as Error).message });
    } finally {
      this.loading.set(false);
    }
  }

  async switchTab(tab: 'handlers' | 'logs' | 'simulator'): Promise<void> {
    this.activeTab.set(tab);
    if (tab === 'logs') {
      await this.loadLogs();
    }
  }

  async loadLogs(): Promise<void> {
    this.loading.set(true);
    try {
      const l = await this.api.getMailLogs(50);
      this.logs.set(l);
    } catch (err) {
      this.feedback.set({ type: 'error', message: (err as Error).message });
    } finally {
      this.loading.set(false);
    }
  }

  openCreateModal(): void {
    const curUser = this.api.currentUser();
    const curProj = this.api.projects()[0];
    this.editingId.set(null);
    this.handlerForm = {
      name: '',
      incoming_address: '',
      target_project_id: curProj ? curProj.id : 1,
      issue_type_id: 0,
      default_reporter_id: curUser ? curUser.id : 1,
      handler_type: 'CREATE_OR_COMMENT',
      spam_keywords: 'out of office,auto-reply,unsubscribe,mailer-daemon,delivery status notification',
      is_active: true,
    };
    this.showModal.set(true);
  }

  editHandler(h: InboundMailHandler): void {
    this.editingId.set(h.id);
    this.handlerForm = { ...h };
    this.showModal.set(true);
  }

  async saveHandler(): Promise<void> {
    this.loading.set(true);
    this.feedback.set(null);
    try {
      if (this.editingId()) {
        await this.api.updateMailHandler(this.editingId()!, this.handlerForm);
        this.feedback.set({ type: 'success', message: 'Mail handler updated successfully!' });
      } else {
        await this.api.createMailHandler(this.handlerForm);
        this.feedback.set({ type: 'success', message: 'Mail handler created successfully!' });
      }
      this.showModal.set(false);
      await this.loadHandlers();
    } catch (err) {
      this.feedback.set({ type: 'error', message: (err as Error).message });
    } finally {
      this.loading.set(false);
    }
  }

  async deleteHandler(id: number): Promise<void> {
    if (!confirm('Are you sure you want to delete this inbound mail handler?')) return;
    this.loading.set(true);
    try {
      await this.api.deleteMailHandler(id);
      this.feedback.set({ type: 'success', message: 'Mail handler deleted' });
      await this.loadHandlers();
    } catch (err) {
      this.feedback.set({ type: 'error', message: (err as Error).message });
    } finally {
      this.loading.set(false);
    }
  }

  async sendSimulatedEmail(): Promise<void> {
    this.loading.set(true);
    this.feedback.set(null);
    this.simResult.set(null);
    try {
      const payload: any = {
        from: this.simFrom,
        to: this.simTo,
        subject: this.simSubject,
        body_plain: this.simBody,
      };

      if (this.simAttachFile) {
        payload.attachments = [
          {
            filename: 'crash_dump.log',
            content_base64: btoa('FATAL ERROR: Memory allocation failed in worker thread\nStack trace: 0x400234 0x400981'),
            mime_type: 'text/plain',
          },
        ];
      }

      const res = await this.api.simulateInboundMail(payload);
      this.simResult.set(JSON.stringify(res, null, 2));
      this.feedback.set({ type: 'success', message: `Email hook processed! Action: ${res.action} for ${res.work_item_key}` });
      await this.loadLogs();
    } catch (err) {
      this.feedback.set({ type: 'error', message: (err as Error).message });
    } finally {
      this.loading.set(false);
    }
  }
}
