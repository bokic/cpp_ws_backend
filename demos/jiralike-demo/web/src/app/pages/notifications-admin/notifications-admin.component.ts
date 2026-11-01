import { Component, OnInit, inject, signal } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { ApiService } from '../../services/api.service';
import {
  NotificationScheme,
  NotificationSchemeEntry,
  Webhook,
  WebhookDelivery,
  SmtpConfig,
  OutboundEmail,
  Project,
} from '../../models/jira.models';

@Component({
  selector: 'app-notifications-admin',
  standalone: true,
  imports: [CommonModule, FormsModule, RouterLink],
  templateUrl: './notifications-admin.component.html',
  styleUrl: './notifications-admin.component.css',
})
export class NotificationsAdminComponent implements OnInit {
  readonly api = inject(ApiService);

  readonly activeTab = signal<'webhooks' | 'schemes' | 'smtp'>('webhooks');
  readonly loading = signal<boolean>(false);
  readonly feedback = signal<{ type: 'success' | 'error'; message: string } | null>(null);

  // Webhooks Tab
  readonly webhooks = signal<Webhook[]>([]);
  readonly webhookDeliveries = signal<WebhookDelivery[]>([]);
  readonly selectedWebhook = signal<Webhook | null>(null);
  readonly showCreateWebhookModal = signal<boolean>(false);
  readonly webhookForm = signal<{
    name: string;
    url: string;
    secret: string;
    events: string[];
    is_active: boolean;
  }>({
    name: '',
    url: '',
    secret: '',
    events: ['issue_created', 'issue_updated', 'issue_commented'],
    is_active: true,
  });

  // Schemes Tab
  readonly schemes = signal<NotificationScheme[]>([]);
  readonly selectedScheme = signal<NotificationScheme | null>(null);
  readonly showCreateSchemeModal = signal<boolean>(false);
  readonly newSchemeName = signal<string>('');
  readonly newSchemeDesc = signal<string>('');
  readonly newSchemeDefault = signal<boolean>(false);

  readonly newEntryEventType = signal<string>('issue_created');
  readonly newEntryRecipientType = signal<string>('assignee');
  readonly newEntryParam = signal<string>('');

  readonly selectedProjectId = signal<number>(1);
  readonly assignSchemeId = signal<number>(1);

  // SMTP Tab
  readonly smtpConfig = signal<SmtpConfig | null>(null);
  readonly outboundEmails = signal<OutboundEmail[]>([]);
  readonly testEmailRecipient = signal<string>('');
  readonly testEmailSubject = signal<string>('[JIRALIKE] Test Email Notification');
  readonly testEmailMessage = signal<string>('This is a test notification from JiraLike SMTP Engine.');

  readonly selectedEmail = signal<OutboundEmail | null>(null);

  readonly availableEvents = [
    { key: 'issue_created', label: 'Issue Created' },
    { key: 'issue_updated', label: 'Issue Updated' },
    { key: 'issue_assigned', label: 'Issue Assigned' },
    { key: 'issue_resolved', label: 'Issue Resolved' },
    { key: 'issue_commented', label: 'Issue Commented' },
    { key: 'worklog_added', label: 'Worklog Added' },
    { key: 'generic_event', label: 'Generic / Custom Automation Event' },
  ];

  readonly recipientTypes = [
    { key: 'assignee', label: 'Current Assignee' },
    { key: 'reporter', label: 'Current Reporter' },
    { key: 'watchers', label: 'All Watchers' },
    { key: 'project_lead', label: 'Project Lead' },
    { key: 'user', label: 'Specific User (ID or Username)' },
    { key: 'role', label: 'Project Role' },
    { key: 'group', label: 'User Group' },
  ];

  async ngOnInit(): Promise<void> {
    await this.loadAll();
  }

  async loadAll(): Promise<void> {
    this.loading.set(true);
    try {
      await Promise.all([
        this.loadWebhooks(),
        this.loadSchemes(),
        this.loadSmtp(),
      ]);
    } finally {
      this.loading.set(false);
    }
  }

  setFeedback(type: 'success' | 'error', message: string): void {
    this.feedback.set({ type, message });
    setTimeout(() => {
      if (this.feedback()?.message === message) {
        this.feedback.set(null);
      }
    }, 5000);
  }

  // --- Webhooks ---
  async loadWebhooks(): Promise<void> {
    try {
      const whList = await this.api.getWebhooks();
      this.webhooks.set(whList);
      const delivs = await this.api.getWebhookDeliveries();
      this.webhookDeliveries.set(delivs);
    } catch (e: any) {
      this.setFeedback('error', 'Failed loading webhooks: ' + (e?.message || e));
    }
  }

  generateSecret(): void {
    const chars = '0123456789abcdef';
    let res = '';
    for (let i = 0; i < 32; i++) {
      res += chars[Math.floor(Math.random() * chars.length)];
    }
    this.webhookForm.update((f) => ({ ...f, secret: res }));
  }

  toggleWebhookEvent(eventKey: string): void {
    this.webhookForm.update((f) => {
      const exists = f.events.includes(eventKey);
      const next = exists ? f.events.filter((e) => e !== eventKey) : [...f.events, eventKey];
      return { ...f, events: next };
    });
  }

  async saveWebhook(): Promise<void> {
    const f = this.webhookForm();
    if (!f.name || !f.url) {
      this.setFeedback('error', 'Name and URL are required.');
      return;
    }
    try {
      await this.api.createWebhook({
        name: f.name,
        url: f.url,
        secret: f.secret,
        events: JSON.stringify(f.events),
        is_active: f.is_active,
      });
      this.setFeedback('success', `Webhook "${f.name}" registered successfully.`);
      this.showCreateWebhookModal.set(false);
      this.webhookForm.set({
        name: '',
        url: '',
        secret: '',
        events: ['issue_created', 'issue_updated', 'issue_commented'],
        is_active: true,
      });
      await this.loadWebhooks();
    } catch (e: any) {
      this.setFeedback('error', 'Error creating webhook: ' + (e?.message || e));
    }
  }

  async toggleWebhookActive(wh: Webhook): Promise<void> {
    try {
      await this.api.updateWebhook(wh.id, { is_active: !wh.is_active });
      await this.loadWebhooks();
    } catch (e: any) {
      this.setFeedback('error', 'Error updating webhook: ' + (e?.message || e));
    }
  }

  async testWebhook(wh: Webhook): Promise<void> {
    try {
      const res = await this.api.testWebhook(wh.id);
      if (res.ok && res.status === 'success') {
        this.setFeedback('success', `Test ping dispatched to ${wh.name}: HTTP ${res.response_status} in ${res.duration_ms.toFixed(1)}ms.`);
      } else {
        this.setFeedback('error', `Test ping failed for ${wh.name}: HTTP ${res.response_status} (${res.error || 'Connection failed'}). Delivery logged.`);
      }
      await this.loadWebhooks();
    } catch (e: any) {
      this.setFeedback('error', 'Error testing webhook: ' + (e?.message || e));
    }
  }

  async redeliver(delivery: WebhookDelivery): Promise<void> {
    try {
      const res = await this.api.redeliverWebhook(delivery.id);
      this.setFeedback('success', `Redelivery attempt completed: HTTP ${res.response_status} (${res.status}).`);
      await this.loadWebhooks();
    } catch (e: any) {
      this.setFeedback('error', 'Redelivery failed: ' + (e?.message || e));
    }
  }

  async deleteWebhook(wh: Webhook): Promise<void> {
    if (!confirm(`Delete webhook "${wh.name}"?`)) return;
    try {
      await this.api.deleteWebhook(wh.id);
      this.setFeedback('success', 'Webhook deleted.');
      await this.loadWebhooks();
    } catch (e: any) {
      this.setFeedback('error', 'Error deleting webhook: ' + (e?.message || e));
    }
  }

  // --- Notification Schemes ---
  async loadSchemes(): Promise<void> {
    try {
      const list = await this.api.getNotificationSchemes();
      this.schemes.set(list);
      if (list.length > 0 && !this.selectedScheme()) {
        await this.selectScheme(list[0]);
      }
    } catch (e: any) {
      this.setFeedback('error', 'Failed loading schemes: ' + (e?.message || e));
    }
  }

  async selectScheme(s: NotificationScheme): Promise<void> {
    try {
      const full = await this.api.getNotificationScheme(s.id);
      this.selectedScheme.set(full);
    } catch (e: any) {
      this.setFeedback('error', 'Failed loading scheme details: ' + (e?.message || e));
    }
  }

  async createScheme(): Promise<void> {
    const name = this.newSchemeName().trim();
    if (!name) {
      this.setFeedback('error', 'Scheme name is required.');
      return;
    }
    try {
      const s = await this.api.createNotificationScheme({
        name,
        description: this.newSchemeDesc().trim(),
        is_default: this.newSchemeDefault(),
      });
      this.setFeedback('success', `Scheme "${name}" created.`);
      this.showCreateSchemeModal.set(false);
      this.newSchemeName.set('');
      this.newSchemeDesc.set('');
      await this.loadSchemes();
      await this.selectScheme(s);
    } catch (e: any) {
      this.setFeedback('error', 'Error creating scheme: ' + (e?.message || e));
    }
  }

  async deleteScheme(s: NotificationScheme): Promise<void> {
    if (s.is_default) {
      alert('Cannot delete the default notification scheme.');
      return;
    }
    if (!confirm(`Delete scheme "${s.name}"?`)) return;
    try {
      await this.api.deleteNotificationScheme(s.id);
      this.setFeedback('success', 'Scheme deleted.');
      this.selectedScheme.set(null);
      await this.loadSchemes();
    } catch (e: any) {
      this.setFeedback('error', 'Error deleting scheme: ' + (e?.message || e));
    }
  }

  async addSchemeEntry(): Promise<void> {
    const scheme = this.selectedScheme();
    if (!scheme) return;
    try {
      await this.api.addNotificationSchemeEntry(scheme.id, {
        event_type: this.newEntryEventType(),
        recipient_type: this.newEntryRecipientType(),
        recipient_parameter: this.newEntryParam().trim(),
      });
      this.setFeedback('success', 'Recipient entry added to scheme.');
      this.newEntryParam.set('');
      await this.selectScheme(scheme);
    } catch (e: any) {
      this.setFeedback('error', 'Error adding entry: ' + (e?.message || e));
    }
  }

  async deleteSchemeEntry(entryId: number): Promise<void> {
    const scheme = this.selectedScheme();
    if (!scheme) return;
    try {
      await this.api.deleteNotificationSchemeEntry(scheme.id, entryId);
      this.setFeedback('success', 'Entry removed.');
      await this.selectScheme(scheme);
    } catch (e: any) {
      this.setFeedback('error', 'Error removing entry: ' + (e?.message || e));
    }
  }

  async assignSchemeToProject(): Promise<void> {
    try {
      await this.api.setProjectNotificationScheme(this.selectedProjectId(), this.assignSchemeId());
      this.setFeedback('success', 'Project notification scheme updated.');
      await this.loadSchemes();
    } catch (e: any) {
      this.setFeedback('error', 'Error updating project scheme: ' + (e?.message || e));
    }
  }

  // --- SMTP ---
  async loadSmtp(): Promise<void> {
    try {
      const cfg = await this.api.getSmtpConfig();
      this.smtpConfig.set(cfg);
      const queue = await this.api.getSmtpQueue();
      this.outboundEmails.set(queue);
    } catch (e: any) {
      this.setFeedback('error', 'Failed loading SMTP configuration: ' + (e?.message || e));
    }
  }

  async saveSmtpConfig(): Promise<void> {
    const cfg = this.smtpConfig();
    if (!cfg) return;
    try {
      await this.api.updateSmtpConfig(cfg);
      this.setFeedback('success', 'SMTP settings saved successfully.');
      await this.loadSmtp();
    } catch (e: any) {
      this.setFeedback('error', 'Error saving SMTP settings: ' + (e?.message || e));
    }
  }

  async sendTestEmail(): Promise<void> {
    const to = this.testEmailRecipient().trim() || this.api.currentUser()?.email || '';
    if (!to) {
      this.setFeedback('error', 'Recipient email is required.');
      return;
    }
    try {
      const res = await this.api.sendTestEmail(to, this.testEmailSubject(), this.testEmailMessage());
      this.setFeedback('success', `Test email queued (ID #${res.email_id}) to ${to}.`);
      await this.loadSmtp();
    } catch (e: any) {
      this.setFeedback('error', 'Error dispatching test email: ' + (e?.message || e));
    }
  }

  formatJson(str: string): string {
    try {
      return JSON.stringify(JSON.parse(str), null, 2);
    } catch {
      return str;
    }
  }
}
