import { Component, OnInit, OnDestroy, inject, signal, computed } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { Router, RouterLink, RouterLinkActive, RouterOutlet } from '@angular/router';
import { ApiService } from './services/api.service';
import {
  InAppNotification,
  UserMention,
  WorkItemType,
  PriorityType,
  PRIORITIES,
  WORK_ITEM_TYPES,
} from './models/jira.models';

interface CommandItem {
  id: string;
  title: string;
  category: string;
  icon: string;
  shortcut?: string;
  action: () => void | Promise<void>;
}

@Component({
  selector: 'app-root',
  standalone: true,
  imports: [CommonModule, FormsModule, RouterOutlet, RouterLink, RouterLinkActive],
  templateUrl: './app.html',
  styleUrl: './app.css',
})
export class App implements OnInit, OnDestroy {
  readonly api = inject(ApiService);
  private readonly router = inject(Router);

  readonly priorities = PRIORITIES;
  readonly workItemTypes = WORK_ITEM_TYPES;

  readonly showNotificationsDropdown = signal(false);
  readonly activeActivityTab = signal<'notifications' | 'mentions'>('notifications');
  readonly notificationFilter = signal<'all' | 'unread'>('all');

  // Modal Signals for Step 16 / Epic 14
  readonly showCreateModal = signal(false);
  readonly showCheatSheet = signal(false);
  readonly showCommandPalette = signal(false);
  readonly showQuickAssignModal = signal(false);

  // Command Palette
  readonly commandQuery = signal('');
  readonly selectedCommandIdx = signal(0);

  // Keyboard navigation on cards/lists
  readonly selectedCardIndex = signal(-1);
  readonly selectedItemKey = signal('');
  readonly selectedItemId = signal(0);

  // Quick Create Issue Form State
  createDraft = {
    project_id: 1,
    type: 'Task' as WorkItemType,
    title: '',
    description: '',
    priority: 'Medium' as PriorityType,
    assignee_id: null as number | null,
  };
  createModalSubmitting = signal(false);
  createModalError = signal('');

  // Quick Assign Form State
  assignTargetUser = signal<number | null>(null);

  private pollInterval: any = null;
  private keydownHandler = (e: KeyboardEvent) => this.handleGlobalKeydown(e);

  async ngOnInit(): Promise<void> {
    try {
      this.api.initThemeAndDensity();
      this.api.initRealtimeSync();

      const status = await this.api.checkSetupStatus();
      if (!status.setup_complete) {
        await this.router.navigate(['/setup']);
      } else if (!this.api.currentUser()) {
        await this.router.navigate(['/login']);
      } else {
        await this.api.loadReferenceData();
        await this.api.refreshMentions();
        await this.api.refreshNotifications();

        this.pollInterval = setInterval(async () => {
          if (this.api.currentUser()) {
            await this.api.refreshMentions();
            await this.api.refreshNotifications();
          }
        }, 15000);
      }
    } catch {
      // backend might not be reachable yet
    }

    if (typeof window !== 'undefined') {
      window.addEventListener('keydown', this.keydownHandler);
    }
  }

  ngOnDestroy(): void {
    if (this.pollInterval) {
      clearInterval(this.pollInterval);
    }
    if (typeof window !== 'undefined') {
      window.removeEventListener('keydown', this.keydownHandler);
    }
    this.api.closeRealtimeSync();
  }

  // =========================================================================
  // Keyboard Shortcuts Listener
  // =========================================================================
  handleGlobalKeydown(e: KeyboardEvent): void {
    // If Command Palette is open, handle navigation within palette
    if (this.showCommandPalette()) {
      if (e.key === 'Escape') {
        this.showCommandPalette.set(false);
        e.preventDefault();
        return;
      }
      if (e.key === 'ArrowDown') {
        e.preventDefault();
        const list = this.filteredCommands;
        this.selectedCommandIdx.update((i) => (i + 1) % (list.length || 1));
        return;
      }
      if (e.key === 'ArrowUp') {
        e.preventDefault();
        const list = this.filteredCommands;
        this.selectedCommandIdx.update((i) => (i - 1 + list.length) % (list.length || 1));
        return;
      }
      if (e.key === 'Enter') {
        e.preventDefault();
        const list = this.filteredCommands;
        const target = list[this.selectedCommandIdx()];
        if (target) {
          this.showCommandPalette.set(false);
          target.action();
        }
        return;
      }
      return;
    }

    if (e.key === 'Escape') {
      if (this.showCreateModal()) {
        this.showCreateModal.set(false);
        e.preventDefault();
        return;
      }
      if (this.showCheatSheet()) {
        this.showCheatSheet.set(false);
        e.preventDefault();
        return;
      }
      if (this.showQuickAssignModal()) {
        this.showQuickAssignModal.set(false);
        e.preventDefault();
        return;
      }
      if (this.showNotificationsDropdown()) {
        this.showNotificationsDropdown.set(false);
        e.preventDefault();
        return;
      }
      this.clearCardSelection();
      return;
    }

    // Ignore single-key shortcuts when typing in inputs/textareas/selects/contenteditable
    const active = document.activeElement;
    const isTyping =
      active &&
      (active.tagName === 'INPUT' ||
        active.tagName === 'TEXTAREA' ||
        active.tagName === 'SELECT' ||
        active.getAttribute('contenteditable') === 'true' ||
        (active as HTMLElement).isContentEditable);

    if (isTyping) {
      return;
    }

    // Global Shortcuts
    if (e.key === 'c' || e.key === 'C') {
      e.preventDefault();
      this.openCreateModal();
    } else if (e.key === '/') {
      e.preventDefault();
      this.focusSearchOrNavigate();
    } else if (e.key === '.') {
      e.preventDefault();
      this.openCommandPalette();
    } else if (e.key === '?') {
      e.preventDefault();
      this.showCheatSheet.set(true);
    } else if (e.key === 'j' || e.key === 'J') {
      e.preventDefault();
      this.navigateCards(1);
    } else if (e.key === 'k' || e.key === 'K') {
      e.preventDefault();
      this.navigateCards(-1);
    } else if (e.key === 'e' || e.key === 'E') {
      if (this.selectedItemKey()) {
        e.preventDefault();
        this.router.navigate(['/' + this.selectedItemKey()]);
      }
    } else if (e.key === 'a' || e.key === 'A') {
      if (this.selectedItemKey()) {
        e.preventDefault();
        this.openQuickAssign();
      }
    } else if (e.key === 'm' || e.key === 'M') {
      if (this.selectedItemKey()) {
        e.preventDefault();
        this.router.navigate(['/' + this.selectedItemKey()]);
      }
    }
  }

  // =========================================================================
  // Command Palette
  // =========================================================================
  openCommandPalette(): void {
    this.commandQuery.set('');
    this.selectedCommandIdx.set(0);
    this.showCommandPalette.set(true);
    setTimeout(() => {
      const input = document.getElementById('cmd-palette-input') as HTMLInputElement | null;
      input?.focus();
    }, 50);
  }

  get filteredCommands(): CommandItem[] {
    const q = this.commandQuery().trim().toLowerCase();
    const all: CommandItem[] = [
      {
        id: 'create_issue',
        title: 'Create Issue',
        category: 'Issues',
        icon: '➕',
        shortcut: 'c',
        action: () => this.openCreateModal(),
      },
      {
        id: 'search_issues',
        title: 'Search & JQL Engine',
        category: 'Navigation',
        icon: '🔍',
        shortcut: '/',
        action: () => this.focusSearchOrNavigate(),
      },
      {
        id: 'toggle_dark_mode',
        title: 'Toggle Dark Mode (' + (this.api.isDarkMode() ? 'Switch to Light' : 'Switch to Dark') + ')',
        category: 'Appearance',
        icon: this.api.isDarkMode() ? '☀️' : '🌙',
        action: () => this.api.toggleTheme(),
      },
      {
        id: 'toggle_density',
        title:
          'Toggle Density (' +
          (this.api.densityMode() === 'compact' ? 'Switch to Comfortable' : 'Switch to Compact') +
          ')',
        category: 'Appearance',
        icon: '⇥⇤',
        action: () => this.api.toggleDensity(),
      },
      {
        id: 'go_board',
        title: 'Go to Agile Boards',
        category: 'Navigation',
        icon: '📋',
        action: () => this.router.navigate(['/board']),
      },
      {
        id: 'go_backlog',
        title: 'Go to Agile Backlog',
        category: 'Navigation',
        icon: '📦',
        action: () => this.router.navigate(['/backlog']),
      },
      {
        id: 'go_dashboards',
        title: 'Go to Dashboards',
        category: 'Navigation',
        icon: '📊',
        action: () => this.router.navigate(['/dashboards']),
      },
      {
        id: 'go_reports',
        title: 'Go to Agile Reports & Analytics',
        category: 'Navigation',
        icon: '📈',
        action: () => this.router.navigate(['/reports']),
      },
      {
        id: 'go_gantt',
        title: 'Go to Gantt Roadmap',
        category: 'Navigation',
        icon: '📅',
        action: () => this.router.navigate(['/gantt']),
      },
      {
        id: 'go_servicedesk_agent',
        title: 'Go to Service Desk Queues (Agent)',
        category: 'ITSM',
        icon: '🎫',
        action: () => this.router.navigate(['/servicedesk/agent']),
      },
      {
        id: 'go_servicedesk_portal',
        title: 'Open Customer Help Portal',
        category: 'ITSM',
        icon: '↗️',
        action: () => window.open('/servicedesk', '_blank'),
      },
      {
        id: 'go_automation',
        title: 'Go to Automation Rules',
        category: 'Settings',
        icon: '⚡',
        action: () => this.router.navigate(['/automation']),
      },
      {
        id: 'go_notifications',
        title: 'Go to Notification Schemes & Webhooks',
        category: 'Settings',
        icon: '🔔',
        action: () => this.router.navigate(['/notifications']),
      },
      {
        id: 'go_permissions',
        title: 'Go to Permissions & Security Schemes',
        category: 'Settings',
        icon: '🛡️',
        action: () => this.router.navigate(['/permissions']),
      },
      {
        id: 'open_shortcuts',
        title: 'Keyboard Shortcuts Cheat Sheet',
        category: 'Help',
        icon: '⌨️',
        shortcut: '?',
        action: () => this.showCheatSheet.set(true),
      },
    ];

    if (!q) return all;
    return all.filter(
      (c) =>
        c.title.toLowerCase().includes(q) ||
        c.category.toLowerCase().includes(q) ||
        (c.shortcut && c.shortcut.toLowerCase().includes(q))
    );
  }

  executeCommand(cmd: CommandItem): void {
    this.showCommandPalette.set(false);
    cmd.action();
  }

  focusSearchOrNavigate(): void {
    if (this.router.url.startsWith('/search')) {
      const input = document.querySelector<HTMLInputElement>(
        'input.search-input, input[type="search"], textarea.jql-textarea'
      );
      if (input) {
        input.focus();
        input.select();
        return;
      }
    }
    this.router.navigate(['/search']);
  }

  // =========================================================================
  // Quick Create Issue Modal
  // =========================================================================
  openCreateModal(): void {
    this.createDraft = {
      project_id: 1,
      type: 'Task',
      title: '',
      description: '',
      priority: 'Medium',
      assignee_id: this.api.currentUser()?.id || null,
    };
    this.createModalError.set('');
    this.showCreateModal.set(true);
    setTimeout(() => {
      const input = document.getElementById('quick-create-title') as HTMLInputElement | null;
      input?.focus();
    }, 50);
  }

  async submitQuickCreate(): Promise<void> {
    if (!this.createDraft.title.trim()) {
      this.createModalError.set('Summary / Title is required');
      return;
    }
    this.createModalSubmitting.set(true);
    this.createModalError.set('');
    try {
      await this.api.createItem({
        project_id: this.createDraft.project_id,
        type: this.createDraft.type,
        title: this.createDraft.title.trim(),
        description: this.createDraft.description.trim(),
        priority: this.createDraft.priority,
        assignee_id: this.createDraft.assignee_id || undefined,
      });
      this.showCreateModal.set(false);
      if (this.router.url.startsWith('/board')) {
        window.dispatchEvent(new CustomEvent('jira:refresh-board'));
      }
    } catch (err: any) {
      this.createModalError.set(err?.message || 'Failed to create item');
    } finally {
      this.createModalSubmitting.set(false);
    }
  }

  // =========================================================================
  // Card / Item Navigation (j / k)
  // =========================================================================
  navigateCards(direction: 1 | -1): void {
    const cards = Array.from(
      document.querySelectorAll<HTMLElement>(
        '.kanban-card, .issue-card, .selectable-item, tr[data-item-key]'
      )
    );
    if (cards.length === 0) return;

    let idx = this.selectedCardIndex();
    idx += direction;
    if (idx < 0) idx = 0;
    if (idx >= cards.length) idx = cards.length - 1;

    this.selectedCardIndex.set(idx);

    for (const c of cards) {
      c.classList.remove('keyboard-selected');
    }

    const target = cards[idx];
    if (target) {
      target.classList.add('keyboard-selected');
      target.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
      const key =
        target.getAttribute('data-item-key') ||
        target.querySelector('.issue-key, .card-key, .item-key')?.textContent?.trim() ||
        '';
      const idStr = target.getAttribute('data-item-id') || '';
      this.selectedItemKey.set(key);
      this.selectedItemId.set(idStr ? Number(idStr) : 0);
    }
  }

  clearCardSelection(): void {
    this.selectedCardIndex.set(-1);
    this.selectedItemKey.set('');
    this.selectedItemId.set(0);
    const cards = document.querySelectorAll('.keyboard-selected');
    cards.forEach((c) => c.classList.remove('keyboard-selected'));
  }

  openQuickAssign(): void {
    this.assignTargetUser.set(this.api.currentUser()?.id || null);
    this.showQuickAssignModal.set(true);
  }

  async submitQuickAssign(): Promise<void> {
    const key = this.selectedItemKey();
    if (!key) return;
    try {
      await this.api.updateItem(key, {
        assignee_id: this.assignTargetUser() || undefined,
      });
      this.showQuickAssignModal.set(false);
      if (this.router.url.startsWith('/board')) {
        window.dispatchEvent(new CustomEvent('jira:refresh-board'));
      }
    } catch {
      // ignore
    }
  }

  // =========================================================================
  // Notifications & Mentions
  // =========================================================================
  async toggleNotificationsDropdown(): Promise<void> {
    const next = !this.showNotificationsDropdown();
    this.showNotificationsDropdown.set(next);
    if (next) {
      await this.api.refreshMentions();
      await this.api.refreshNotifications();
    }
  }

  async markNotificationRead(n: InAppNotification, event: MouseEvent): Promise<void> {
    event.stopPropagation();
    await this.api.markNotificationRead(n.id);
  }

  async markAllActivityRead(): Promise<void> {
    if (this.activeActivityTab() === 'notifications') {
      await this.api.markAllNotificationsRead();
    } else {
      await this.api.markAllMentionsRead();
    }
  }

  async openNotification(n: InAppNotification): Promise<void> {
    if (!n.is_read) {
      await this.api.markNotificationRead(n.id);
    }
    this.showNotificationsDropdown.set(false);
    if (n.link) {
      await this.router.navigateByUrl(n.link);
    } else if (n.item_key) {
      await this.router.navigate(['/' + n.item_key]);
    }
  }

  async deleteNotification(n: InAppNotification, event: MouseEvent): Promise<void> {
    event.stopPropagation();
    await this.api.deleteNotification(n.id);
  }

  async markMentionRead(m: UserMention, event: MouseEvent): Promise<void> {
    event.stopPropagation();
    await this.api.markMentionRead(m.id);
  }

  async openMention(m: UserMention): Promise<void> {
    await this.api.markMentionRead(m.id);
    this.showNotificationsDropdown.set(false);
    await this.router.navigate(['/' + m.item_key]);
  }

  getFilteredNotifications(): InAppNotification[] {
    const list = this.api.notifications();
    if (this.notificationFilter() === 'unread') {
      return list.filter((n) => !n.is_read);
    }
    return list;
  }

  async signOut(): Promise<void> {
    await this.api.logout();
    await this.router.navigate(['/login']);
  }
}
