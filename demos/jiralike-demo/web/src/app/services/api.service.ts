import { Injectable, computed, signal } from '@angular/core';
import {
  AttachmentItem,
  CommentItem,
  Project,
  SetupStatus,
  Sprint,
  User,
  WorkItem,
  WorkItemDetail,
  WorkItemLink,
  WorkflowState,
} from '../models/jira.models';

@Injectable({
  providedIn: 'root',
})
export class ApiService {
  private readonly TOKEN_KEY = 'jiralike_auth_token';
  private readonly USER_KEY = 'jiralike_auth_user';

  readonly currentUser = signal<User | null>(this.loadStoredUser());
  readonly token = signal<string>(localStorage.getItem(this.TOKEN_KEY) || '');
  readonly setupStatus = signal<SetupStatus | null>(null);
  readonly projects = signal<Project[]>([]);
  readonly activeProject = signal<Project | null>(null);
  readonly users = signal<User[]>([]);
  readonly sprints = signal<Sprint[]>([]);
  readonly currentSprint = computed<Sprint | null>(() => {
    const list = this.sprints();
    return (
      list.find((s) => s.is_current) ||
      list.find((s) => s.status === 'active') ||
      (list.length > 0 ? list[0] : null)
    );
  });

  private loadStoredUser(): User | null {
    try {
      const raw = localStorage.getItem(this.USER_KEY);
      return raw ? (JSON.parse(raw) as User) : null;
    } catch {
      return null;
    }
  }

  setAuth(token: string, user: User): void {
    localStorage.setItem(this.TOKEN_KEY, token);
    localStorage.setItem(this.USER_KEY, JSON.stringify(user));
    this.token.set(token);
    this.currentUser.set(user);
  }

  clearAuth(): void {
    localStorage.removeItem(this.TOKEN_KEY);
    localStorage.removeItem(this.USER_KEY);
    this.token.set('');
    this.currentUser.set(null);
  }

  private async request<T>(path: string, options: RequestInit = {}): Promise<T> {
    const headers: Record<string, string> = {
      ...(options.headers as Record<string, string> | undefined),
    };
    if (!(options.body instanceof FormData) && !headers['Content-Type']) {
      headers['Content-Type'] = 'application/json';
    }
    const tk = this.token();
    if (tk) {
      headers['Authorization'] = `Bearer ${tk}`;
      headers['X-Auth-Token'] = tk;
    }

    const res = await fetch(path, {
      ...options,
      headers,
    });

    if (res.status === 401 && !path.includes('/api/auth/login')) {
      this.clearAuth();
    }

    const text = await res.text();
    let data: unknown = null;
    try {
      data = text ? JSON.parse(text) : null;
    } catch {
      data = { error: text || res.statusText };
    }

    if (!res.ok) {
      const errObj = data as { error?: string } | null;
      throw new Error(errObj?.error || `HTTP ${res.status}: ${res.statusText}`);
    }

    return data as T;
  }

  async checkSetupStatus(): Promise<SetupStatus> {
    const status = await this.request<SetupStatus>('/api/setup/status');
    this.setupStatus.set(status);
    return status;
  }

  async initSetup(payload: {
    instance_name: string;
    admin_username: string;
    admin_email: string;
    admin_display_name: string;
    admin_password: string;
    project_key: string;
    project_name: string;
    project_description: string;
    seed_demo_data: boolean;
  }): Promise<{ token: string; user: User; setup_complete: boolean }> {
    const res = await this.request<{
      token: string;
      user: User;
      setup_complete: boolean;
      instance_name: string;
    }>('/api/setup/init', {
      method: 'POST',
      body: JSON.stringify(payload),
    });
    this.setAuth(res.token, res.user);
    await this.checkSetupStatus();
    await this.loadReferenceData();
    return res;
  }

  async login(username: string, password: string): Promise<User> {
    const res = await this.request<{ token: string; user: User }>('/api/auth/login', {
      method: 'POST',
      body: JSON.stringify({ username, password }),
    });
    this.setAuth(res.token, res.user);
    await this.loadReferenceData();
    return res.user;
  }

  async logout(): Promise<void> {
    try {
      await this.request('/api/auth/logout', { method: 'POST' });
    } catch {
      // ignore
    }
    this.clearAuth();
  }

  async loadReferenceData(): Promise<void> {
    const [projs, usrs, sprs] = await Promise.all([
      this.getProjects(),
      this.getUsers(),
      this.getSprints(),
    ]);
    this.projects.set(projs);
    if (projs.length > 0 && !this.activeProject()) {
      this.activeProject.set(projs[0]);
    }
    this.users.set(usrs);
    this.sprints.set(sprs);
  }

  async getProjects(): Promise<Project[]> {
    return this.request<Project[]>('/api/projects');
  }

  async createProject(payload: { key: string; name: string; description: string }): Promise<Project> {
    const p = await this.request<Project>('/api/projects', {
      method: 'POST',
      body: JSON.stringify(payload),
    });
    const list = await this.getProjects();
    this.projects.set(list);
    return p;
  }

  async getUsers(): Promise<User[]> {
    return this.request<User[]>('/api/users');
  }

  async createUser(payload: {
    username: string;
    display_name: string;
    email: string;
    role: string;
    password: string;
    avatar_color: string;
  }): Promise<User> {
    const u = await this.request<User>('/api/users', {
      method: 'POST',
      body: JSON.stringify(payload),
    });
    const list = await this.getUsers();
    this.users.set(list);
    return u;
  }

  async getSprints(projectId?: number): Promise<Sprint[]> {
    const q = projectId ? `?project_id=${projectId}` : '';
    return this.request<Sprint[]>(`/api/sprints${q}`);
  }

  async createSprint(payload: {
    project_id: number;
    name: string;
    goal: string;
    status: string;
    start_date: string;
    end_date: string;
    is_current?: boolean;
  }): Promise<Sprint> {
    const s = await this.request<Sprint>('/api/sprints', {
      method: 'POST',
      body: JSON.stringify(payload),
    });
    const list = await this.getSprints();
    this.sprints.set(list);
    return s;
  }

  async updateSprint(
    id: number,
    payload: {
      project_id: number;
      name: string;
      goal: string;
      status: string;
      start_date: string;
      end_date: string;
      is_current: boolean;
    }
  ): Promise<Sprint> {
    const s = await this.request<Sprint>(`/api/sprints/${id}`, {
      method: 'PUT',
      body: JSON.stringify(payload),
    });
    const list = await this.getSprints();
    this.sprints.set(list);
    return s;
  }

  async setCurrentSprint(id: number): Promise<void> {
    await this.request(`/api/sprints/${id}/current`, {
      method: 'POST',
      body: JSON.stringify({}),
    });
    const list = await this.getSprints();
    this.sprints.set(list);
  }

  async deleteSprint(id: number): Promise<void> {
    await this.request(`/api/sprints/${id}`, { method: 'DELETE' });
    const list = await this.getSprints();
    this.sprints.set(list);
  }

  async getItems(filters: Record<string, string | number | undefined> = {}): Promise<WorkItem[]> {
    const params = new URLSearchParams();
    for (const [k, v] of Object.entries(filters)) {
      if (v !== undefined && v !== null && v !== '') {
        params.set(k, String(v));
      }
    }
    const qs = params.toString() ? `?${params.toString()}` : '';
    return this.request<WorkItem[]>(`/api/items${qs}`);
  }

  async getItemDetail(idOrKey: string | number): Promise<WorkItemDetail> {
    return this.request<WorkItemDetail>(`/api/items/${encodeURIComponent(String(idOrKey))}`);
  }

  async createItem(payload: Partial<WorkItem>): Promise<WorkItem> {
    return this.request<WorkItem>('/api/items', {
      method: 'POST',
      body: JSON.stringify(payload),
    });
  }

  async updateItem(idOrKey: string | number, payload: Partial<WorkItem>): Promise<WorkItem> {
    return this.request<WorkItem>(`/api/items/${encodeURIComponent(String(idOrKey))}`, {
      method: 'PUT',
      body: JSON.stringify(payload),
    });
  }

  async transitionItemState(
    idOrKey: string | number,
    status: WorkflowState,
    extra: {
      progress?: number;
      start_date?: string;
      due_date?: string;
      board_order?: number;
      comment?: string;
    } = {}
  ): Promise<WorkItem> {
    return this.request<WorkItem>(`/api/items/${encodeURIComponent(String(idOrKey))}/state`, {
      method: 'PATCH',
      body: JSON.stringify({ status, ...extra }),
    });
  }

  async deleteItem(idOrKey: string | number): Promise<void> {
    await this.request(`/api/items/${encodeURIComponent(String(idOrKey))}`, { method: 'DELETE' });
  }

  async addComment(itemIdOrKey: string | number, body: string): Promise<CommentItem> {
    return this.request<CommentItem>(
      `/api/items/${encodeURIComponent(String(itemIdOrKey))}/comments`,
      {
        method: 'POST',
        body: JSON.stringify({ body }),
      }
    );
  }

  async getAllLinks(): Promise<WorkItemLink[]> {
    return this.request<WorkItemLink[]>('/api/links');
  }

  async addLink(
    itemIdOrKey: string | number,
    targetItemId: number,
    linkType: string
  ): Promise<WorkItemLink> {
    return this.request<WorkItemLink>(
      `/api/items/${encodeURIComponent(String(itemIdOrKey))}/links`,
      {
        method: 'POST',
        body: JSON.stringify({ target_item_id: targetItemId, link_type: linkType }),
      }
    );
  }

  async deleteLink(linkId: number): Promise<void> {
    await this.request(`/api/links/${linkId}`, { method: 'DELETE' });
  }

  async uploadAttachment(itemIdOrKey: string | number, file: File): Promise<AttachmentItem> {
    const base64 = await new Promise<string>((resolve, reject) => {
      const reader = new FileReader();
      reader.onload = () => resolve(String(reader.result || ''));
      reader.onerror = () => reject(new Error('Failed to read file'));
      reader.readAsDataURL(file);
    });

    return this.request<AttachmentItem>(
      `/api/items/${encodeURIComponent(String(itemIdOrKey))}/attachments`,
      {
        method: 'POST',
        body: JSON.stringify({
          filename: file.name,
          mime_type: file.type || 'application/octet-stream',
          content_base64: base64,
        }),
      }
    );
  }

  async deleteAttachment(attachmentId: number): Promise<void> {
    await this.request(`/api/attachments/${attachmentId}`, { method: 'DELETE' });
  }
}
