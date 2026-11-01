import { Component, OnInit, computed, inject, signal } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { RouterLink } from '@angular/router';
import { ApiService } from '../../services/api.service';
import {
  AutomationAction,
  AutomationCondition,
  AutomationLog,
  AutomationRule,
  AutomationTriggerConfig,
  Project,
  WorkItem,
} from '../../models/jira.models';

interface TriggerOption {
  type: string;
  label: string;
  icon: string;
  description: string;
}

const TRIGGER_OPTIONS: TriggerOption[] = [
  { type: 'status_changed', label: 'Issue Transitioned / Status Changed', icon: '🔄', description: 'When an issue moves from one workflow status to another' },
  { type: 'issue_created', label: 'Issue Created', icon: '✨', description: 'When a new task, story, bug, or epic is submitted' },
  { type: 'field_value_changed', label: 'Field Value Changed', icon: '📝', description: 'When priority, assignee, labels, or attributes are edited' },
  { type: 'comment_added', label: 'Comment Added', icon: '💬', description: 'When a user posts a comment on an issue' },
  { type: 'scheduled_cron', label: 'Scheduled Cron Timer', icon: '⏱️', description: 'Periodic rule execution (e.g. daily, hourly maintenance)' },
  { type: 'incoming_webhook', label: 'Incoming Webhook', icon: '🌐', description: 'When an external service sends a webhook to this instance' },
  { type: 'manual', label: 'Manual Trigger', icon: '▶️', description: 'Triggered manually by users or via REST API' },
];

@Component({
  selector: 'app-automation-admin',
  standalone: true,
  imports: [CommonModule, FormsModule, RouterLink],
  templateUrl: './automation-admin.component.html',
  styleUrls: ['./automation-admin.component.css'],
})
export class AutomationAdminComponent implements OnInit {
  api = inject(ApiService);

  activeTab = signal<'rules' | 'logs'>('rules');
  searchQuery = signal('');
  selectedProjectFilter = signal<number | null>(null);

  rules = signal<AutomationRule[]>([]);
  logs = signal<AutomationLog[]>([]);
  projects = signal<Project[]>([]);
  recentItems = signal<WorkItem[]>([]);

  isLoading = signal(false);
  errorMessage = signal('');
  successMessage = signal('');

  // Builder modal
  showBuilderModal = signal(false);
  isEditingRule = signal(false);
  builderRuleId = signal<number | null>(null);
  builderName = signal('');
  builderDescription = signal('');
  builderProjectId = signal<number | null>(null);
  builderIsEnabled = signal(true);
  builderTriggerType = signal('status_changed');
  builderToStatus = signal('Done');
  builderFromStatus = signal('*');
  builderFieldName = signal('priority');
  builderConditions = signal<AutomationCondition[]>([]);
  builderActions = signal<AutomationAction[]>([]);

  // Logs filter
  logRuleFilter = signal<number | null>(null);
  logStatusFilter = signal<'ALL' | 'SUCCESS' | 'NO_ACTIONS' | 'FAILED' | 'ERROR'>('ALL');
  expandedLogId = signal<number | null>(null);

  // Run Rule modal
  showRunModal = signal(false);
  runningRule = signal<AutomationRule | null>(null);
  runIssueKey = signal('');
  runResult = signal<{ status: string; duration_ms: number; trace: string } | null>(null);
  isExecuting = signal(false);

  triggerOptions = TRIGGER_OPTIONS;

  filteredRules = computed(() => {
    let list = this.rules();
    const query = this.searchQuery().trim().toLowerCase();
    const pFilter = this.selectedProjectFilter();

    if (pFilter !== null) {
      list = list.filter((r) => r.project_id === pFilter || r.project_id === null);
    }
    if (query) {
      list = list.filter(
        (r) =>
          r.name.toLowerCase().includes(query) ||
          r.description.toLowerCase().includes(query) ||
          r.trigger_type.toLowerCase().includes(query)
      );
    }
    return list;
  });

  filteredLogs = computed(() => {
    let list = this.logs();
    const rId = this.logRuleFilter();
    const sFilter = this.logStatusFilter();

    if (rId !== null) {
      list = list.filter((l) => l.rule_id === rId);
    }
    if (sFilter !== 'ALL') {
      list = list.filter((l) => l.status === sFilter);
    }
    return list;
  });

  totalExecutions = computed(() => {
    return this.rules().reduce((acc, r) => acc + (r.execution_count || 0), 0);
  });

  activeRulesCount = computed(() => {
    return this.rules().filter((r) => r.is_enabled).length;
  });

  ngOnInit() {
    this.loadAll();
  }

  async loadAll() {
    this.isLoading.set(true);
    this.errorMessage.set('');
    try {
      const [rules, logs, projects, items] = await Promise.all([
        this.api.getAutomationRules(),
        this.api.getAutomationLogs(),
        this.api.getProjects(),
        this.api.getItems({ limit: 15 }),
      ]);
      this.rules.set(rules);
      this.logs.set(logs);
      this.projects.set(projects);
      this.recentItems.set(items);
    } catch (err: any) {
      this.errorMessage.set(err.message || 'Failed to load automation engine state');
    } finally {
      this.isLoading.set(false);
    }
  }

  async refreshLogs() {
    try {
      const logs = await this.api.getAutomationLogs(this.logRuleFilter() || undefined);
      this.logs.set(logs);
    } catch (err: any) {
      this.errorMessage.set(err.message || 'Failed to refresh logs');
    }
  }

  async toggleRule(rule: AutomationRule) {
    try {
      const res = await this.api.toggleAutomationRule(rule.id);
      this.rules.update((prev) =>
        prev.map((r) => (r.id === rule.id ? { ...r, is_enabled: res.is_enabled } : r))
      );
      this.showFlash(
        `Rule '${rule.name}' is now ${res.is_enabled ? 'ENABLED' : 'DISABLED'}`
      );
    } catch (err: any) {
      this.errorMessage.set(err.message || 'Failed to toggle rule');
    }
  }

  openCreateRuleModal() {
    this.isEditingRule.set(false);
    this.builderRuleId.set(null);
    this.builderName.set('');
    this.builderDescription.set('');
    this.builderProjectId.set(null);
    this.builderIsEnabled.set(true);
    this.builderTriggerType.set('status_changed');
    this.builderToStatus.set('Done');
    this.builderFromStatus.set('*');
    this.builderFieldName.set('priority');
    this.builderConditions.set([]);
    this.builderActions.set([]);
    this.showBuilderModal.set(true);
  }

  openEditRuleModal(rule: AutomationRule) {
    this.isEditingRule.set(true);
    this.builderRuleId.set(rule.id);
    this.builderName.set(rule.name);
    this.builderDescription.set(rule.description);
    this.builderProjectId.set(rule.project_id || null);
    this.builderIsEnabled.set(rule.is_enabled);
    this.builderTriggerType.set(rule.trigger_type);
    this.builderToStatus.set(rule.trigger_config?.to_status || 'Done');
    this.builderFromStatus.set(rule.trigger_config?.from_status || '*');
    this.builderFieldName.set(rule.trigger_config?.field_name || 'priority');
    this.builderConditions.set(JSON.parse(JSON.stringify(rule.conditions || [])));
    this.builderActions.set(JSON.parse(JSON.stringify(rule.actions || [])));
    this.showBuilderModal.set(true);
  }

  closeBuilderModal() {
    this.showBuilderModal.set(false);
  }

  loadTemplate(templateKey: string) {
    if (templateKey === 'auto_close_subtasks') {
      this.builderName.set('Auto-close Subtasks when Story is Done');
      this.builderDescription.set(
        'When an issue transitions to Done, automatically find all child subtasks and transition them to Done.'
      );
      this.builderTriggerType.set('status_changed');
      this.builderToStatus.set('Done');
      this.builderFromStatus.set('*');
      this.builderConditions.set([{ type: 'has_subtasks' }]);
      this.builderActions.set([
        {
          type: 'branch',
          target: 'subtasks',
          actions: [{ type: 'transition_issue', status: 'Done' }],
        },
      ]);
    } else if (templateKey === 'critical_bug') {
      this.builderName.set('Critical Bug Escalation Triage');
      this.builderDescription.set(
        'When a critical or high-priority bug is submitted, auto-alert team and apply escalation label.'
      );
      this.builderTriggerType.set('issue_created');
      this.builderConditions.set([
        { type: 'jql', jql: "type = 'Bug' AND priority IN ('High', 'Highest')" },
      ]);
      this.builderActions.set([
        {
          type: 'add_comment',
          body: '⚠️ Automated Escalation: Bug {{issue.key}} submitted by {{issue.reporter}} with priority {{issue.priority}}. Please triage immediately!',
        },
        {
          type: 'edit_field',
          field: 'labels',
          value: 'critical-escalation, auto-triaged',
        },
      ]);
    } else if (templateKey === 'welcome_comment') {
      this.builderName.set('Backlog Receipt & Due Date Setting');
      this.builderDescription.set(
        'Adds an automated confirmation comment with calculated due date.'
      );
      this.builderTriggerType.set('issue_created');
      this.builderConditions.set([]);
      this.builderActions.set([
        {
          type: 'add_comment',
          body: 'Issue {{issue.key}} created by {{issue.reporter}}. Target delivery deadline: {{now.plusDays(7)}}.',
        },
      ]);
    }
  }

  addCondition(type: 'jql' | 'issue_field' | 'has_subtasks' | 'user_condition') {
    const list = [...this.builderConditions()];
    if (type === 'jql') {
      list.push({ type: 'jql', jql: "priority = 'High' AND status != 'Done'" });
    } else if (type === 'issue_field') {
      list.push({ type: 'issue_field', field: 'priority', operator: 'equals', value: 'High' });
    } else if (type === 'has_subtasks') {
      list.push({ type: 'has_subtasks' });
    } else if (type === 'user_condition') {
      list.push({ type: 'user_condition', check: 'is_admin' });
    }
    this.builderConditions.set(list);
  }

  removeCondition(index: number) {
    const list = [...this.builderConditions()];
    list.splice(index, 1);
    this.builderConditions.set(list);
  }

  addAction(type: 'transition_issue' | 'add_comment' | 'edit_field' | 'create_subtask' | 'branch') {
    const list = [...this.builderActions()];
    if (type === 'transition_issue') {
      list.push({ type: 'transition_issue', status: 'Done' });
    } else if (type === 'add_comment') {
      list.push({
        type: 'add_comment',
        body: 'Automated note on {{issue.key}} ({{issue.summary}}). Status is {{issue.status}}.',
      });
    } else if (type === 'edit_field') {
      list.push({ type: 'edit_field', field: 'priority', value: 'High' });
    } else if (type === 'create_subtask') {
      list.push({
        type: 'create_subtask',
        title: 'Review checklist for issue',
        issue_type: 'Sub-task',
        priority: 'Medium',
      });
    } else if (type === 'branch') {
      list.push({
        type: 'branch',
        target: 'subtasks',
        actions: [{ type: 'transition_issue', status: 'Done' }],
      });
    }
    this.builderActions.set(list);
  }

  removeAction(index: number) {
    const list = [...this.builderActions()];
    list.splice(index, 1);
    this.builderActions.set(list);
  }

  insertSmartToken(action: AutomationAction, tokenName: string) {
    const token = '{{' + tokenName + '}}';
    if (action.type === 'add_comment') {
      action.body = (action.body || '') + token;
    } else if (action.type === 'edit_field') {
      action.value = (action.value || '') + token;
    } else if (action.type === 'create_subtask') {
      action.title = (action.title || '') + token;
    }
  }

  async saveRule() {
    const name = this.builderName().trim();
    if (!name) {
      this.errorMessage.set('Rule name cannot be empty');
      return;
    }

    const trigger_config: AutomationTriggerConfig = {};
    if (this.builderTriggerType() === 'status_changed') {
      trigger_config.to_status = this.builderToStatus();
      trigger_config.from_status = this.builderFromStatus();
    } else if (this.builderTriggerType() === 'field_value_changed') {
      trigger_config.field_name = this.builderFieldName();
    }

    const payload: Partial<AutomationRule> = {
      name,
      description: this.builderDescription().trim(),
      project_id: this.builderProjectId() || undefined,
      is_enabled: this.builderIsEnabled(),
      trigger_type: this.builderTriggerType(),
      trigger_config,
      conditions: this.builderConditions(),
      actions: this.builderActions(),
    };

    this.isLoading.set(true);
    try {
      if (this.isEditingRule() && this.builderRuleId()) {
        const updated = await this.api.updateAutomationRule(this.builderRuleId()!, payload);
        this.rules.update((prev) =>
          prev.map((r) => (r.id === updated.id ? updated : r))
        );
        this.showFlash(`Rule '${updated.name}' updated successfully`);
      } else {
        const created = await this.api.createAutomationRule(payload);
        this.rules.update((prev) => [...prev, created]);
        this.showFlash(`Rule '${created.name}' created successfully`);
      }
      this.closeBuilderModal();
    } catch (err: any) {
      this.errorMessage.set(err.message || 'Failed to save automation rule');
    } finally {
      this.isLoading.set(false);
    }
  }

  async deleteRule(rule: AutomationRule) {
    if (!confirm(`Are you sure you want to delete rule '${rule.name}'?`)) {
      return;
    }
    try {
      await this.api.deleteAutomationRule(rule.id);
      this.rules.update((prev) => prev.filter((r) => r.id !== rule.id));
      this.showFlash(`Rule '${rule.name}' deleted`);
    } catch (err: any) {
      this.errorMessage.set(err.message || 'Failed to delete rule');
    }
  }

  viewRuleLogs(rule: AutomationRule) {
    this.logRuleFilter.set(rule.id);
    this.activeTab.set('logs');
    this.refreshLogs();
  }

  openRunModal(rule: AutomationRule) {
    this.runningRule.set(rule);
    this.runResult.set(null);
    this.runIssueKey.set(this.recentItems()[0]?.key || '');
    this.showRunModal.set(true);
  }

  closeRunModal() {
    this.showRunModal.set(false);
    this.runningRule.set(null);
    this.runResult.set(null);
  }

  async executeRule() {
    const rule = this.runningRule();
    if (!rule) return;

    this.isExecuting.set(true);
    try {
      const res = await this.api.executeAutomationRule(rule.id, {
        issue_key: this.runIssueKey().trim(),
      });
      this.runResult.set(res);
      // Refresh rules execution count & logs
      const [updatedRules, logs] = await Promise.all([
        this.api.getAutomationRules(),
        this.api.getAutomationLogs(),
      ]);
      this.rules.set(updatedRules);
      this.logs.set(logs);
    } catch (err: any) {
      this.runResult.set({
        status: 'ERROR',
        duration_ms: 0,
        trace: err.message || 'Execution failed',
      });
    } finally {
      this.isExecuting.set(false);
    }
  }

  async triggerCron() {
    this.isLoading.set(true);
    try {
      const res = await this.api.triggerAutomationCron();
      this.showFlash(res.message);
      await this.refreshLogs();
    } catch (err: any) {
      this.errorMessage.set(err.message || 'Failed to trigger cron');
    } finally {
      this.isLoading.set(false);
    }
  }

  toggleExpandLog(logId: number) {
    this.expandedLogId.update((cur) => (cur === logId ? null : logId));
  }

  getTriggerLabel(type: string): string {
    const opt = TRIGGER_OPTIONS.find((t) => t.type === type);
    return opt ? opt.label : type;
  }

  getTriggerIcon(type: string): string {
    const opt = TRIGGER_OPTIONS.find((t) => t.type === type);
    return opt ? opt.icon : '⚡';
  }

  showFlash(msg: string) {
    this.successMessage.set(msg);
    setTimeout(() => {
      this.successMessage.set('');
    }, 4000);
  }
}
