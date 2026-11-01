import { Injectable, computed, signal } from '@angular/core';
import {
  AttachmentItem,
  CommentItem,
  CustomFieldDef,
  CustomFieldOption,
  FieldConfiguration,
  IssueTypeDef,
  IssueTypeScheme,
  Project,
  ProjectComponent,
  ProjectVersion,
  ReleaseNotesResponse,
  Resolution,
  ScreenDef,
  ScreenScheme,
  ScreenTab,
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
  readonly resolutions = signal<Resolution[]>([]);
  readonly issueTypes = signal<IssueTypeDef[]>([]);
  readonly issueTypeSchemes = signal<IssueTypeScheme[]>([]);
  readonly components = signal<ProjectComponent[]>([]);
  readonly versions = signal<ProjectVersion[]>([]);
  readonly customFields = signal<CustomFieldDef[]>([]);
  readonly fieldConfigurations = signal<FieldConfiguration[]>([]);
  readonly screens = signal<ScreenDef[]>([]);
  readonly screenSchemes = signal<ScreenScheme[]>([]);
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
    const [
      projs,
      usrs,
      sprs,
      resList,
      typesList,
      schemesList,
      compsList,
      versList,
      cfList,
      fcList,
      scrList,
      ssList,
    ] = await Promise.all([
      this.getProjects(),
      this.getUsers(),
      this.getSprints(),
      this.getResolutions().catch(() => [] as Resolution[]),
      this.getIssueTypes().catch(() => [] as IssueTypeDef[]),
      this.getIssueTypeSchemes().catch(() => [] as IssueTypeScheme[]),
      this.getComponents().catch(() => [] as ProjectComponent[]),
      this.getVersions().catch(() => [] as ProjectVersion[]),
      this.getCustomFields().catch(() => [] as CustomFieldDef[]),
      this.getFieldConfigurations().catch(() => [] as FieldConfiguration[]),
      this.getScreens().catch(() => [] as ScreenDef[]),
      this.getScreenSchemes().catch(() => [] as ScreenScheme[]),
    ]);
    this.projects.set(projs);
    if (projs.length > 0 && !this.activeProject()) {
      this.activeProject.set(projs[0]);
    }
    this.users.set(usrs);
    this.sprints.set(sprs);
    this.resolutions.set(resList);
    this.issueTypes.set(typesList);
    this.issueTypeSchemes.set(schemesList);
    this.components.set(compsList);
    this.versions.set(versList);
    this.customFields.set(cfList);
    this.fieldConfigurations.set(fcList);
    this.screens.set(scrList);
    this.screenSchemes.set(ssList);
  }

  async getResolutions(): Promise<Resolution[]> {
    return this.request<Resolution[]>('/api/resolutions');
  }

  async createResolution(payload: {
    name: string;
    description: string;
    sort_order?: number;
    is_default?: boolean;
  }): Promise<Resolution> {
    const r = await this.request<Resolution>('/api/resolutions', {
      method: 'POST',
      body: JSON.stringify(payload),
    });
    this.resolutions.set(await this.getResolutions());
    return r;
  }

  async getIssueTypes(): Promise<IssueTypeDef[]> {
    return this.request<IssueTypeDef[]>('/api/issue-types');
  }

  async createIssueType(payload: {
    name: string;
    description: string;
    icon?: string;
    color?: string;
    hierarchy_level?: number;
    is_subtask?: boolean;
  }): Promise<IssueTypeDef> {
    const t = await this.request<IssueTypeDef>('/api/issue-types', {
      method: 'POST',
      body: JSON.stringify(payload),
    });
    this.issueTypes.set(await this.getIssueTypes());
    return t;
  }

  async getIssueTypeSchemes(): Promise<IssueTypeScheme[]> {
    return this.request<IssueTypeScheme[]>('/api/issue-type-schemes');
  }

  async createIssueTypeScheme(payload: {
    name: string;
    description: string;
    issue_type_ids: number[];
  }): Promise<IssueTypeScheme> {
    const s = await this.request<IssueTypeScheme>('/api/issue-type-schemes', {
      method: 'POST',
      body: JSON.stringify(payload),
    });
    this.issueTypeSchemes.set(await this.getIssueTypeSchemes());
    return s;
  }

  async getComponents(projectId?: number): Promise<ProjectComponent[]> {
    const url = projectId ? `/api/projects/${projectId}/components` : '/api/components';
    return this.request<ProjectComponent[]>(url);
  }

  async createComponent(payload: {
    project_id: number;
    name: string;
    description: string;
    lead_user_id: number | null;
    default_assignee_type: string;
  }): Promise<ProjectComponent> {
    const c = await this.request<ProjectComponent>(`/api/projects/${payload.project_id}/components`, {
      method: 'POST',
      body: JSON.stringify(payload),
    });
    this.components.set(await this.getComponents());
    return c;
  }

  async updateComponent(
    id: number,
    payload: {
      name: string;
      description: string;
      lead_user_id: number | null;
      default_assignee_type: string;
    }
  ): Promise<ProjectComponent> {
    const c = await this.request<ProjectComponent>(`/api/components/${id}`, {
      method: 'PUT',
      body: JSON.stringify(payload),
    });
    this.components.set(await this.getComponents());
    return c;
  }

  async deleteComponent(id: number): Promise<void> {
    await this.request(`/api/components/${id}`, { method: 'DELETE' });
    this.components.set(await this.getComponents());
  }

  async getVersions(projectId?: number): Promise<ProjectVersion[]> {
    const url = projectId ? `/api/projects/${projectId}/versions` : '/api/versions';
    return this.request<ProjectVersion[]>(url);
  }

  async createVersion(payload: {
    project_id: number;
    name: string;
    description: string;
    status: string;
    start_date: string;
    release_date: string;
  }): Promise<ProjectVersion> {
    const v = await this.request<ProjectVersion>(`/api/projects/${payload.project_id}/versions`, {
      method: 'POST',
      body: JSON.stringify(payload),
    });
    this.versions.set(await this.getVersions());
    return v;
  }

  async updateVersion(
    id: number,
    payload: {
      name: string;
      description: string;
      status: string;
      start_date: string;
      release_date: string;
    }
  ): Promise<ProjectVersion> {
    const v = await this.request<ProjectVersion>(`/api/versions/${id}`, {
      method: 'PUT',
      body: JSON.stringify(payload),
    });
    this.versions.set(await this.getVersions());
    return v;
  }

  async deleteVersion(id: number): Promise<void> {
    await this.request(`/api/versions/${id}`, { method: 'DELETE' });
    this.versions.set(await this.getVersions());
  }

  async getReleaseNotes(versionId: number): Promise<ReleaseNotesResponse> {
    return this.request<ReleaseNotesResponse>(`/api/versions/${versionId}/release-notes`);
  }

  async getCustomFields(): Promise<CustomFieldDef[]> {
    return this.request<CustomFieldDef[]>('/api/custom-fields');
  }

  async createCustomField(payload: {
    name: string;
    field_key?: string;
    field_type: string;
    description?: string;
    default_value?: string;
    is_required?: boolean;
    is_searchable?: boolean;
    min_value?: number | null;
    max_value?: number | null;
    formula_expr?: string;
    sort_order?: number;
    options?: (string | { value: string; color?: string; is_default?: boolean })[];
    add_to_default_screens?: boolean;
  }): Promise<CustomFieldDef> {
    const cf = await this.request<CustomFieldDef>('/api/custom-fields', {
      method: 'POST',
      body: JSON.stringify(payload),
    });
    const [cfList, fcList, scrList] = await Promise.all([
      this.getCustomFields(),
      this.getFieldConfigurations(),
      this.getScreens(),
    ]);
    this.customFields.set(cfList);
    this.fieldConfigurations.set(fcList);
    this.screens.set(scrList);
    return cf;
  }

  async updateCustomField(id: number, payload: Partial<CustomFieldDef>): Promise<CustomFieldDef> {
    const cf = await this.request<CustomFieldDef>(`/api/custom-fields/${id}`, {
      method: 'PUT',
      body: JSON.stringify(payload),
    });
    const [cfList, fcList, scrList] = await Promise.all([
      this.getCustomFields(),
      this.getFieldConfigurations(),
      this.getScreens(),
    ]);
    this.customFields.set(cfList);
    this.fieldConfigurations.set(fcList);
    this.screens.set(scrList);
    return cf;
  }

  async deleteCustomField(id: number): Promise<void> {
    await this.request(`/api/custom-fields/${id}`, { method: 'DELETE' });
    const [cfList, fcList, scrList] = await Promise.all([
      this.getCustomFields(),
      this.getFieldConfigurations(),
      this.getScreens(),
    ]);
    this.customFields.set(cfList);
    this.fieldConfigurations.set(fcList);
    this.screens.set(scrList);
  }

  async addCustomFieldOption(
    fieldId: number,
    payload: { value: string; color?: string; is_default?: boolean; sort_order?: number }
  ): Promise<CustomFieldOption> {
    const opt = await this.request<CustomFieldOption>(`/api/custom-fields/${fieldId}/options`, {
      method: 'POST',
      body: JSON.stringify(payload),
    });
    this.customFields.set(await this.getCustomFields());
    this.screens.set(await this.getScreens());
    return opt;
  }

  async deleteCustomFieldOption(optionId: number): Promise<void> {
    await this.request(`/api/custom-field-options/${optionId}`, { method: 'DELETE' });
    this.customFields.set(await this.getCustomFields());
    this.screens.set(await this.getScreens());
  }

  async getFieldConfigurations(): Promise<FieldConfiguration[]> {
    return this.request<FieldConfiguration[]>('/api/field-configurations');
  }

  async updateFieldConfigurationItem(payload: {
    configuration_id: number;
    custom_field_id: number;
    issue_type?: string;
    is_required: boolean;
    is_hidden: boolean;
    renderer: 'plain' | 'markdown';
    default_value?: string;
    description_override?: string;
  }): Promise<void> {
    await this.request('/api/field-configurations', {
      method: 'POST',
      body: JSON.stringify(payload),
    });
    const [cfList, fcList, scrList] = await Promise.all([
      this.getCustomFields(),
      this.getFieldConfigurations(),
      this.getScreens(),
    ]);
    this.customFields.set(cfList);
    this.fieldConfigurations.set(fcList);
    this.screens.set(scrList);
  }

  async getScreens(): Promise<ScreenDef[]> {
    return this.request<ScreenDef[]>('/api/screens');
  }

  async createScreen(payload: {
    name: string;
    description: string;
    screen_type: string;
    tab_names?: string[];
  }): Promise<ScreenDef> {
    const sc = await this.request<ScreenDef>('/api/screens', {
      method: 'POST',
      body: JSON.stringify(payload),
    });
    this.screens.set(await this.getScreens());
    return sc;
  }

  async deleteScreen(screenId: number): Promise<void> {
    await this.request(`/api/screens/${screenId}`, { method: 'DELETE' });
    this.screens.set(await this.getScreens());
  }

  async addScreenTab(screenId: number, payload: { name: string; sort_order?: number }): Promise<ScreenTab> {
    const t = await this.request<ScreenTab>(`/api/screens/${screenId}/tabs`, {
      method: 'POST',
      body: JSON.stringify(payload),
    });
    this.screens.set(await this.getScreens());
    return t;
  }

  async deleteScreenTab(tabId: number): Promise<void> {
    await this.request(`/api/screen-tabs/${tabId}`, { method: 'DELETE' });
    this.screens.set(await this.getScreens());
  }

  async addScreenTabField(
    tabId: number,
    payload: { custom_field_id?: number; field_key?: string; sort_order?: number }
  ): Promise<void> {
    await this.request(`/api/screen-tabs/${tabId}/fields`, {
      method: 'POST',
      body: JSON.stringify(payload),
    });
    this.screens.set(await this.getScreens());
  }

  async removeScreenTabField(tabFieldId: number): Promise<void> {
    await this.request(`/api/screen-tab-fields/${tabFieldId}`, { method: 'DELETE' });
    this.screens.set(await this.getScreens());
  }

  async getScreenSchemes(): Promise<ScreenScheme[]> {
    return this.request<ScreenScheme[]>('/api/screen-schemes');
  }

  async saveScreenScheme(payload: {
    id?: number;
    name: string;
    description: string;
    project_id?: number | null;
    is_default?: boolean;
    create_screen_id: number;
    edit_screen_id: number;
    view_screen_id: number;
    transition_screen_id: number;
  }): Promise<ScreenScheme> {
    const ss = await this.request<ScreenScheme>('/api/screen-schemes', {
      method: 'POST',
      body: JSON.stringify(payload),
    });
    this.screenSchemes.set(await this.getScreenSchemes());
    return ss;
  }

  async resolveScreen(
    projectId: number,
    operation: 'create' | 'edit' | 'view' | 'transition',
    issueType = '*'
  ): Promise<ScreenDef> {
    const params = new URLSearchParams({
      project_id: String(projectId || 1),
      operation,
      issue_type: issueType || '*',
    });
    return this.request<ScreenDef>(`/api/screens/resolve?${params.toString()}`);
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
      resolution_id?: number;
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
