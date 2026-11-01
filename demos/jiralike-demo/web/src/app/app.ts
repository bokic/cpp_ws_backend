import { Component, OnInit, OnDestroy, inject, signal } from '@angular/core';
import { CommonModule } from '@angular/common';
import { Router, RouterLink, RouterLinkActive, RouterOutlet } from '@angular/router';
import { ApiService } from './services/api.service';
import { InAppNotification, UserMention } from './models/jira.models';

@Component({
  selector: 'app-root',
  standalone: true,
  imports: [CommonModule, RouterOutlet, RouterLink, RouterLinkActive],
  templateUrl: './app.html',
  styleUrl: './app.css',
})
export class App implements OnInit, OnDestroy {
  readonly api = inject(ApiService);
  private readonly router = inject(Router);

  readonly showNotificationsDropdown = signal(false);
  readonly activeActivityTab = signal<'notifications' | 'mentions'>('notifications');
  readonly notificationFilter = signal<'all' | 'unread'>('all');

  private pollInterval: any = null;

  async ngOnInit(): Promise<void> {
    try {
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
  }

  ngOnDestroy(): void {
    if (this.pollInterval) {
      clearInterval(this.pollInterval);
    }
  }

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
