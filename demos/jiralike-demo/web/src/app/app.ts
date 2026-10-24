import { Component, OnInit, inject } from '@angular/core';
import { CommonModule } from '@angular/common';
import { Router, RouterLink, RouterLinkActive, RouterOutlet } from '@angular/router';
import { ApiService } from './services/api.service';

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

  async ngOnInit(): Promise<void> {
    try {
      const status = await this.api.checkSetupStatus();
      if (!status.setup_complete) {
        await this.router.navigate(['/setup']);
      } else if (!this.api.currentUser()) {
        await this.router.navigate(['/login']);
      } else {
        await this.api.loadReferenceData();
      }
    } catch {
      // backend might not be reachable yet
    }
  }

  async signOut(): Promise<void> {
    await this.api.logout();
    await this.router.navigate(['/login']);
  }
}
