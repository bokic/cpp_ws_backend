import { Component, OnInit, inject, signal } from '@angular/core';
import { CommonModule } from '@angular/common';
import { Router, RouterLink, RouterLinkActive, RouterOutlet } from '@angular/router';
import { ApiService } from './services/api.service';
import { UserMention } from './models/jira.models';

@Component({
  selector: 'app-root',
  standalone: true,
  imports: [CommonModule, RouterOutlet, RouterLink, RouterLinkActive],
  templateUrl: './app.html',
  styleUrl: './app.css',
})
export class App implements OnInit {
  readonly api = inject(ApiService);
  private readonly router = inject(Router);

  readonly showMentionsDropdown = signal(false);

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
      }
    } catch {
      // backend might not be reachable yet
    }
  }

  async toggleMentionsDropdown(): Promise<void> {
    const next = !this.showMentionsDropdown();
    this.showMentionsDropdown.set(next);
    if (next) {
      await this.api.refreshMentions();
    }
  }

  async markRead(m: UserMention, event: MouseEvent): Promise<void> {
    event.stopPropagation();
    await this.api.markMentionRead(m.id);
  }

  async markAllRead(): Promise<void> {
    await this.api.markAllMentionsRead();
  }

  async openMention(m: UserMention): Promise<void> {
    await this.api.markMentionRead(m.id);
    this.showMentionsDropdown.set(false);
    await this.router.navigate(['/' + m.item_key]);
  }

  async signOut(): Promise<void> {
    await this.api.logout();
    await this.router.navigate(['/login']);
  }
}
