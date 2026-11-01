import { Injectable, computed, signal } from '@angular/core';
import {
  AttachmentItem,
  BoardColumn,
  BoardConfig,
  BoardItemsPayload,
  BoardQuickFilter,
  BoardSwimlane,
  BurndownReport,
  BurnupReport,
  CommentItem,
  CompleteSprintResponse,
  CreatedVsResolvedReport,
  CumulativeFlowReport,
  CustomFieldDef,
  CustomFieldOption,
  CycleTimeReport,
  Dashboard,
  DashboardGadget,
  DurationParseResponse,
  EpicReport,
  EpicSummaryItem,
  FieldConfiguration,
  GadgetCatalogItem,
  IssueTypeDef,
  IssueTypeScheme,
  ItemTransitionsPayload,
  JqlSearchResponse,
  JqlSuggestions,
  JqlValidateResponse,
  Project,
  ProjectComponent,
  ProjectVersion,
  ReleaseBurndownReport,
  ReleaseNotesResponse,
  ReleaseSummaryItem,
  Resolution,
  SavedFilter,
  ScreenDef,
  ScreenScheme,
  ScreenTab,
  SetupStatus,
  Sprint,
  TimeRollup,
  TimesheetResponse,
  User,
  VelocityReport,
  WorkItem,
  WorkItemDetail,
  WorkItemLink,
  WorklogItem,
  WorkflowDef,
  WorkflowSchemeBinding,
  WorkflowState,
  WorkflowStatusNode,
  WorkflowTransitionDef,
  WorkflowTransitionRule,
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
  readonly boards = signal<BoardConfig[]>([]);
  readonly resolutions = signal<Resolution[]>([]);
  readonly issueTypes = signal<IssueTypeDef[]>([]);
  readonly issueTypeSchemes = signal<IssueTypeScheme[]>([]);
  readonly components = signal<ProjectComponent[]>([]);
  readonly versions = signal<ProjectVersion[]>([]);
  readonly customFields = signal<CustomFieldDef[]>([]);
  readonly fieldConfigurations = signal<FieldConfiguration[]>([]);
  readonly screens = signal<ScreenDef[]>([]);
  readonly screenSchemes = signal<ScreenScheme[]>([]);
  readonly workflows = signal<WorkflowDef[]>([]);
  readonly workflowSchemes = signal<WorkflowSchemeBinding[]>([]);
  readonly savedFilters = signal<SavedFilter[]>([]);
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
      wfList,
      wfsList,
      sfList,
      boardsList,
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
      this.getWorkflows().catch(() => [] as WorkflowDef[]),
      this.getWorkflowSchemes().catch(() => [] as WorkflowSchemeBinding[]),
      this.getSavedFilters().catch(() => [] as SavedFilter[]),
      this.getBoards().catch(() => [] as BoardConfig[]),
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
    this.workflows.set(wfList);
    this.workflowSchemes.set(wfsList);
    this.savedFilters.set(sfList);
    this.boards.set(boardsList);
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
    status: WorkflowState | string,
    extra: {
      transition_id?: number;
      progress?: number;
      start_date?: string;
      due_date?: string;
      board_order?: number;
      resolution_id?: number;
      assignee_id?: number;
      steps_to_reproduce?: string;
      comment?: string;
      post_comment?: boolean;
      custom_fields?: Record<string, any>;
    } = {}
  ): Promise<WorkItem> {
    return this.request<WorkItem>(`/api/items/${encodeURIComponent(String(idOrKey))}/state`, {
      method: 'PATCH',
      body: JSON.stringify({ status, ...extra }),
    });
  }

  async getItemTransitions(idOrKey: string | number): Promise<ItemTransitionsPayload> {
    return this.request<ItemTransitionsPayload>(
      `/api/items/${encodeURIComponent(String(idOrKey))}/transitions`
    );
  }

  async getWorkflows(): Promise<WorkflowDef[]> {
    return this.request<WorkflowDef[]>('/api/workflows');
  }

  async createWorkflow(payload: {
    name: string;
    description: string;
    is_default?: boolean;
    clone_from_id?: number;
  }): Promise<WorkflowDef> {
    const wf = await this.request<WorkflowDef>('/api/workflows', {
      method: 'POST',
      body: JSON.stringify(payload),
    });
    this.workflows.set(await this.getWorkflows());
    return wf;
  }

  async updateWorkflow(
    workflowId: number,
    payload: {
      name?: string;
      description?: string;
      statuses?: Partial<WorkflowStatusNode>[];
    }
  ): Promise<WorkflowDef> {
    const wf = await this.request<WorkflowDef>(`/api/workflows/${workflowId}`, {
      method: 'PUT',
      body: JSON.stringify(payload),
    });
    this.workflows.set(await this.getWorkflows());
    return wf;
  }

  async deleteWorkflow(workflowId: number): Promise<void> {
    await this.request(`/api/workflows/${workflowId}`, { method: 'DELETE' });
    this.workflows.set(await this.getWorkflows());
  }

  async addWorkflowStatus(
    workflowId: number,
    payload: {
      name: string;
      category: string;
      color?: string;
      pos_x?: number;
      pos_y?: number;
      sort_order?: number;
      is_initial?: boolean;
    }
  ): Promise<WorkflowDef> {
    const wf = await this.request<WorkflowDef>(`/api/workflows/${workflowId}/statuses`, {
      method: 'POST',
      body: JSON.stringify(payload),
    });
    this.workflows.set(await this.getWorkflows());
    return wf;
  }

  async deleteWorkflowStatus(statusId: number): Promise<void> {
    await this.request(`/api/workflow-statuses/${statusId}`, { method: 'DELETE' });
    this.workflows.set(await this.getWorkflows());
  }

  async addWorkflowTransition(
    workflowId: number,
    payload: {
      name: string;
      from_status: string;
      to_status: string;
      screen_id?: number | null;
      sort_order?: number;
    }
  ): Promise<WorkflowTransitionDef> {
    const tr = await this.request<WorkflowTransitionDef>(
      `/api/workflows/${workflowId}/transitions`,
      {
        method: 'POST',
        body: JSON.stringify(payload),
      }
    );
    this.workflows.set(await this.getWorkflows());
    return tr;
  }

  async updateWorkflowTransition(
    transitionId: number,
    payload: {
      name?: string;
      from_status?: string;
      to_status?: string;
      screen_id?: number | null;
    }
  ): Promise<void> {
    await this.request(`/api/workflow-transitions/${transitionId}`, {
      method: 'PUT',
      body: JSON.stringify(payload),
    });
    this.workflows.set(await this.getWorkflows());
  }

  async deleteWorkflowTransition(transitionId: number): Promise<void> {
    await this.request(`/api/workflow-transitions/${transitionId}`, { method: 'DELETE' });
    this.workflows.set(await this.getWorkflows());
  }

  async addWorkflowTransitionRule(
    transitionId: number,
    payload: {
      rule_category: 'condition' | 'validator' | 'post_function' | string;
      rule_type: string;
      field_name?: string;
      config_value?: string;
      description?: string;
      sort_order?: number;
    }
  ): Promise<WorkflowTransitionRule> {
    const r = await this.request<WorkflowTransitionRule>(
      `/api/workflow-transitions/${transitionId}/rules`,
      {
        method: 'POST',
        body: JSON.stringify(payload),
      }
    );
    this.workflows.set(await this.getWorkflows());
    return r;
  }

  async deleteWorkflowTransitionRule(ruleId: number): Promise<void> {
    await this.request(`/api/workflow-transition-rules/${ruleId}`, { method: 'DELETE' });
    this.workflows.set(await this.getWorkflows());
  }

  async getWorkflowSchemes(): Promise<WorkflowSchemeBinding[]> {
    return this.request<WorkflowSchemeBinding[]>('/api/workflow-schemes');
  }

  async saveWorkflowScheme(payload: {
    name: string;
    description: string;
    project_id: number;
    issue_type: string;
    workflow_id: number;
    is_default?: boolean;
  }): Promise<WorkflowSchemeBinding> {
    const s = await this.request<WorkflowSchemeBinding>('/api/workflow-schemes', {
      method: 'POST',
      body: JSON.stringify(payload),
    });
    this.workflowSchemes.set(await this.getWorkflowSchemes());
    return s;
  }

  async deleteWorkflowScheme(schemeId: number): Promise<void> {
    await this.request(`/api/workflow-schemes/${schemeId}`, { method: 'DELETE' });
    this.workflowSchemes.set(await this.getWorkflowSchemes());
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

  // JQL Search & Saved Filters API
  async searchJql(jql: string, limit = 200, offset = 0): Promise<JqlSearchResponse> {
    return this.request<JqlSearchResponse>('/api/search/jql', {
      method: 'POST',
      body: JSON.stringify({ jql, limit, offset }),
    });
  }

  async validateJql(jql: string): Promise<JqlValidateResponse> {
    return this.request<JqlValidateResponse>('/api/search/validate', {
      method: 'POST',
      body: JSON.stringify({ jql }),
    });
  }

  async getJqlSuggestions(): Promise<JqlSuggestions> {
    return this.request<JqlSuggestions>('/api/search/suggestions');
  }

  getJqlExportUrl(jql: string, format: 'csv' | 'markdown' | 'html'): string {
    const params = new URLSearchParams();
    params.set('jql', jql);
    params.set('format', format);
    if (this.token()) {
      params.set('token', this.token());
    }
    return `/api/search/jql?${params.toString()}`;
  }

  async getSavedFilters(): Promise<SavedFilter[]> {
    return this.request<SavedFilter[]>('/api/filters');
  }

  async createSavedFilter(payload: {
    name: string;
    description: string;
    jql_query: string;
    is_favorite?: boolean;
    share_type?: string;
    share_target?: string;
    view_columns?: string;
  }): Promise<SavedFilter> {
    const f = await this.request<SavedFilter>('/api/filters', {
      method: 'POST',
      body: JSON.stringify(payload),
    });
    this.savedFilters.set(await this.getSavedFilters());
    return f;
  }

  async updateSavedFilter(
    filterId: number,
    payload: {
      name: string;
      description: string;
      jql_query: string;
      is_favorite?: boolean;
      share_type?: string;
      share_target?: string;
      view_columns?: string;
    }
  ): Promise<SavedFilter> {
    const f = await this.request<SavedFilter>(`/api/filters/${filterId}`, {
      method: 'PUT',
      body: JSON.stringify(payload),
    });
    this.savedFilters.set(await this.getSavedFilters());
    return f;
  }

  async deleteSavedFilter(filterId: number): Promise<void> {
    await this.request(`/api/filters/${filterId}`, { method: 'DELETE' });
    this.savedFilters.set(await this.getSavedFilters());
  }

  async toggleSavedFilterStar(filterId: number): Promise<SavedFilter> {
    const f = await this.request<SavedFilter>(`/api/filters/${filterId}/star`, {
      method: 'POST',
      body: JSON.stringify({}),
    });
    this.savedFilters.set(await this.getSavedFilters());
    return f;
  }

  async createFilterSubscription(
    filterId: number,
    payload: {
      cron_schedule: string;
      frequency_label: string;
      recipient_scope: string;
      email_empty?: boolean;
    }
  ): Promise<SavedFilter> {
    const f = await this.request<SavedFilter>(`/api/filters/${filterId}/subscriptions`, {
      method: 'POST',
      body: JSON.stringify(payload),
    });
    this.savedFilters.set(await this.getSavedFilters());
    return f;
  }

  async deleteFilterSubscription(subId: number): Promise<void> {
    await this.request(`/api/filter-subscriptions/${subId}`, { method: 'DELETE' });
    this.savedFilters.set(await this.getSavedFilters());
  }

  // Step 6: Multi-Board Engine & Agile Backlog APIs
  async getBoards(projectId?: number): Promise<BoardConfig[]> {
    const q = projectId ? `?project_id=${projectId}` : '';
    return this.request<BoardConfig[]>(`/api/boards${q}`);
  }

  async getBoard(boardId: number): Promise<BoardConfig> {
    return this.request<BoardConfig>(`/api/boards/${boardId}`);
  }

  async createBoard(payload: Partial<BoardConfig>): Promise<BoardConfig> {
    const b = await this.request<BoardConfig>('/api/boards', {
      method: 'POST',
      body: JSON.stringify(payload),
    });
    this.boards.set(await this.getBoards());
    return b;
  }

  async updateBoard(boardId: number, payload: Partial<BoardConfig>): Promise<BoardConfig> {
    const b = await this.request<BoardConfig>(`/api/boards/${boardId}`, {
      method: 'PUT',
      body: JSON.stringify(payload),
    });
    this.boards.set(await this.getBoards());
    return b;
  }

  async deleteBoard(boardId: number): Promise<void> {
    await this.request(`/api/boards/${boardId}`, { method: 'DELETE' });
    this.boards.set(await this.getBoards());
  }

  async getBoardItems(
    boardId: number,
    quickFilterIds: number[] = [],
    extraJql = ''
  ): Promise<BoardItemsPayload> {
    const params = new URLSearchParams();
    if (quickFilterIds.length > 0) {
      params.set('quick_filter_ids', quickFilterIds.join(','));
    }
    if (extraJql.trim()) {
      params.set('jql', extraJql.trim());
    }
    const qs = params.toString() ? `?${params.toString()}` : '';
    return this.request<BoardItemsPayload>(`/api/boards/${boardId}/items${qs}`);
  }

  async addBoardColumn(boardId: number, payload: Partial<BoardColumn>): Promise<BoardConfig> {
    const b = await this.request<BoardConfig>(`/api/boards/${boardId}/columns`, {
      method: 'POST',
      body: JSON.stringify(payload),
    });
    this.boards.set(await this.getBoards());
    return b;
  }

  async updateBoardColumn(colId: number, payload: Partial<BoardColumn>): Promise<BoardConfig> {
    const b = await this.request<BoardConfig>(`/api/board-columns/${colId}`, {
      method: 'PUT',
      body: JSON.stringify(payload),
    });
    this.boards.set(await this.getBoards());
    return b;
  }

  async deleteBoardColumn(colId: number): Promise<BoardConfig> {
    const b = await this.request<BoardConfig>(`/api/board-columns/${colId}`, {
      method: 'DELETE',
    });
    this.boards.set(await this.getBoards());
    return b;
  }

  async addBoardQuickFilter(
    boardId: number,
    payload: Partial<BoardQuickFilter>
  ): Promise<BoardConfig> {
    const b = await this.request<BoardConfig>(`/api/boards/${boardId}/quick-filters`, {
      method: 'POST',
      body: JSON.stringify(payload),
    });
    this.boards.set(await this.getBoards());
    return b;
  }

  async deleteBoardQuickFilter(qfId: number): Promise<BoardConfig> {
    const b = await this.request<BoardConfig>(`/api/board-quick-filters/${qfId}`, {
      method: 'DELETE',
    });
    this.boards.set(await this.getBoards());
    return b;
  }

  async addBoardSwimlane(boardId: number, payload: Partial<BoardSwimlane>): Promise<BoardConfig> {
    const b = await this.request<BoardConfig>(`/api/boards/${boardId}/swimlanes`, {
      method: 'POST',
      body: JSON.stringify(payload),
    });
    this.boards.set(await this.getBoards());
    return b;
  }

  async deleteBoardSwimlane(swId: number): Promise<BoardConfig> {
    const b = await this.request<BoardConfig>(`/api/board-swimlanes/${swId}`, {
      method: 'DELETE',
    });
    this.boards.set(await this.getBoards());
    return b;
  }

  async startSprint(
    sprintId: number,
    payload: {
      name?: string;
      goal?: string;
      start_date?: string;
      end_date?: string;
      move_backlog_to_todo?: boolean;
    }
  ): Promise<any> {
    const res = await this.request(`/api/sprints/${sprintId}/start`, {
      method: 'POST',
      body: JSON.stringify(payload),
    });
    this.sprints.set(await this.getSprints());
    return res;
  }

  async completeSprint(
    sprintId: number,
    payload: {
      rollover_target: 'backlog' | 'sprint' | 'new_sprint';
      target_sprint_id?: number;
      next_sprint_name?: string;
      activate_target_sprint?: boolean;
    }
  ): Promise<CompleteSprintResponse> {
    const res = await this.request<CompleteSprintResponse>(`/api/sprints/${sprintId}/complete`, {
      method: 'POST',
      body: JSON.stringify(payload),
    });
    this.sprints.set(await this.getSprints());
    return res;
  }

  async rankItems(payload: {
    item_id?: number;
    item_ids?: number[];
    sprint_id?: number | null;
    parent_id?: number | null;
    fix_version_id?: number | null;
    ordered_item_ids?: number[];
    board_order?: number;
  }): Promise<any> {
    return this.request('/api/items/rank', {
      method: 'POST',
      body: JSON.stringify(payload),
    });
  }

  // Step 7: Time Tracking & Worklogs Methods
  async getItemWorklogs(
    idOrKey: string | number
  ): Promise<{ work_item_id: number; worklogs: WorklogItem[]; time_rollup: TimeRollup }> {
    return this.request<{ work_item_id: number; worklogs: WorklogItem[]; time_rollup: TimeRollup }>(
      `/api/items/${encodeURIComponent(String(idOrKey))}/worklogs`
    );
  }

  async addWorklog(
    idOrKey: string | number,
    payload: {
      time_spent?: string;
      time_spent_seconds?: number;
      duration?: string;
      started_at?: string;
      description?: string;
      adjustment_mode?: string;
      new_estimate?: string;
      new_estimate_seconds?: number;
      reduce_by?: string;
      reduce_by_seconds?: number;
      adjustment_duration?: string;
      adjustment_seconds?: number;
    }
  ): Promise<WorklogItem> {
    return this.request<WorklogItem>(`/api/items/${encodeURIComponent(String(idOrKey))}/worklogs`, {
      method: 'POST',
      body: JSON.stringify(payload),
    });
  }

  async updateWorklog(
    worklogId: number,
    payload: {
      time_spent?: string;
      time_spent_seconds?: number;
      started_at?: string;
      description?: string;
      adjustment_mode?: string;
      new_estimate?: string;
      new_estimate_seconds?: number;
      reduce_by?: string;
      reduce_by_seconds?: number;
    }
  ): Promise<WorklogItem> {
    return this.request<WorklogItem>(`/api/worklogs/${worklogId}`, {
      method: 'PUT',
      body: JSON.stringify(payload),
    });
  }

  async deleteWorklog(
    worklogId: number,
    adjustment_mode: string = 'auto'
  ): Promise<{
    deleted: boolean;
    work_item_id: number;
    item_time_spent_seconds: number;
    item_time_spent_formatted: string;
    item_remaining_estimate_seconds: number;
    item_remaining_estimate_formatted: string;
    time_rollup: TimeRollup;
  }> {
    return this.request(`/api/worklogs/${worklogId}?adjustment_mode=${encodeURIComponent(adjustment_mode)}`, {
      method: 'DELETE',
    });
  }

  async getTimesheet(
    filters: {
      project_id?: number;
      user_id?: number;
      sprint_id?: number;
      work_item_id?: number;
      from_date?: string;
      to_date?: string;
    } = {}
  ): Promise<TimesheetResponse> {
    const params = new URLSearchParams();
    for (const [k, v] of Object.entries(filters)) {
      if (v !== undefined && v !== null && v !== '') {
        params.set(k, String(v));
      }
    }
    const qs = params.toString() ? `?${params.toString()}` : '';
    return this.request<TimesheetResponse>(`/api/worklogs${qs}`);
  }

  async parseDuration(payload: {
    duration?: string;
    seconds?: number;
  }): Promise<DurationParseResponse> {
    return this.request<DurationParseResponse>('/api/time-tracking/parse', {
      method: 'POST',
      body: JSON.stringify(payload),
    });
  }

  // ============================================================================
  // Epic 5 & Step 8: Agile Analytics & Reporting Suite APIs
  // ============================================================================

  async getBurndownReport(
    sprintId?: number,
    unit: 'points' | 'hours' = 'points',
    weekendFlattening = true
  ): Promise<BurndownReport> {
    const params = new URLSearchParams();
    if (sprintId) params.set('sprint_id', String(sprintId));
    params.set('unit', unit);
    params.set('weekend_flattening', String(weekendFlattening));
    return this.request<BurndownReport>(`/api/reports/burndown?${params.toString()}`);
  }

  async getBurnupReport(
    sprintId?: number,
    unit: 'points' | 'hours' = 'points'
  ): Promise<BurndownReport> {
    const params = new URLSearchParams();
    if (sprintId) params.set('sprint_id', String(sprintId));
    params.set('unit', unit);
    return this.request<BurndownReport>(`/api/reports/burnup?${params.toString()}`);
  }

  async getVelocityReport(projectId = 1, limit = 10): Promise<VelocityReport> {
    return this.request<VelocityReport>(
      `/api/reports/velocity?project_id=${projectId}&limit=${limit}`
    );
  }

  async getCumulativeFlowReport(
    projectId = 1,
    days = 30,
    boardId?: number
  ): Promise<CumulativeFlowReport> {
    const params = new URLSearchParams();
    params.set('project_id', String(projectId));
    params.set('days', String(days));
    if (boardId) params.set('board_id', String(boardId));
    return this.request<CumulativeFlowReport>(
      `/api/reports/cumulative-flow?${params.toString()}`
    );
  }

  async getCycleTimeReport(
    projectId = 1,
    days = 60,
    boardId?: number
  ): Promise<CycleTimeReport> {
    const params = new URLSearchParams();
    params.set('project_id', String(projectId));
    params.set('days', String(days));
    if (boardId) params.set('board_id', String(boardId));
    return this.request<CycleTimeReport>(
      `/api/reports/cycle-time?${params.toString()}`
    );
  }

  async getCreatedVsResolvedReport(
    projectId = 1,
    days = 30,
    groupBy: 'day' | 'week' = 'day'
  ): Promise<CreatedVsResolvedReport> {
    return this.request<CreatedVsResolvedReport>(
      `/api/reports/created-vs-resolved?project_id=${projectId}&days=${days}&group_by=${groupBy}`
    );
  }

  async getEpicReport(epicId?: number): Promise<EpicReport> {
    const qs = epicId ? `?epic_id=${epicId}` : '';
    return this.request<EpicReport>(`/api/reports/epic-report${qs}`);
  }

  async getEpicsList(projectId = 1): Promise<{ project_id: number; epics: EpicSummaryItem[] }> {
    return this.request<{ project_id: number; epics: EpicSummaryItem[] }>(
      `/api/reports/epics?project_id=${projectId}`
    );
  }

  async getReleaseBurndownReport(versionId?: number): Promise<ReleaseBurndownReport> {
    const qs = versionId ? `?version_id=${versionId}` : '';
    return this.request<ReleaseBurndownReport>(`/api/reports/release-burndown${qs}`);
  }

  async getReleasesList(projectId = 1): Promise<{ project_id: number; releases: ReleaseSummaryItem[] }> {
    return this.request<{ project_id: number; releases: ReleaseSummaryItem[] }>(
      `/api/reports/releases?project_id=${projectId}`
    );
  }

  downloadReportCsv(endpoint: string, params: Record<string, any>, defaultFilename: string): void {
    const q = new URLSearchParams();
    for (const [k, v] of Object.entries(params)) {
      if (v !== undefined && v !== null && v !== '') {
        q.set(k, String(v));
      }
    }
    q.set('format', 'csv');
    const url = `${endpoint}?${q.toString()}`;
    const link = document.createElement('a');
    link.href = url;
    link.setAttribute('download', defaultFilename);
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
  }

  // ============================================================================
  // Epic 7 & Step 9: Dashboards & Gadgets Framework Methods
  // ============================================================================

  async getGadgetCatalog(): Promise<GadgetCatalogItem[]> {
    return this.request<GadgetCatalogItem[]>('/api/gadgets/catalog');
  }

  async executeGadget(gadgetType: string, preferences: any): Promise<any> {
    return this.request<any>('/api/gadgets/execute', {
      method: 'POST',
      body: JSON.stringify({
        gadget_type: gadgetType,
        preferences,
      }),
    });
  }

  async getDashboards(favoriteOnly = false, projectId = 0): Promise<{ dashboards: Dashboard[] }> {
    const params = new URLSearchParams();
    if (favoriteOnly) params.set('favorite_only', '1');
    if (projectId > 0) params.set('project_id', String(projectId));
    const qs = params.toString() ? `?${params.toString()}` : '';
    return this.request<{ dashboards: Dashboard[] }>(`/api/dashboards${qs}`);
  }

  async getDashboard(id: number): Promise<Dashboard> {
    return this.request<Dashboard>(`/api/dashboards/${id}`);
  }

  async createDashboard(data: {
    name: string;
    description?: string;
    layout?: string;
    share_type?: string;
    project_id?: number | null;
    is_favorite?: boolean;
  }): Promise<Dashboard> {
    return this.request<Dashboard>('/api/dashboards', {
      method: 'POST',
      body: JSON.stringify(data),
    });
  }

  async updateDashboard(id: number, data: Partial<Dashboard>): Promise<Dashboard> {
    return this.request<Dashboard>(`/api/dashboards/${id}`, {
      method: 'PUT',
      body: JSON.stringify(data),
    });
  }

  async deleteDashboard(id: number): Promise<{ success: boolean; deleted_id: number }> {
    return this.request<{ success: boolean; deleted_id: number }>(`/api/dashboards/${id}`, {
      method: 'DELETE',
    });
  }

  async copyDashboard(id: number): Promise<Dashboard> {
    return this.request<Dashboard>(`/api/dashboards/${id}/copy`, {
      method: 'POST',
    });
  }

  async toggleDashboardStar(id: number): Promise<{ id: number; is_favorite: boolean }> {
    return this.request<{ id: number; is_favorite: boolean }>(`/api/dashboards/${id}/star`, {
      method: 'POST',
    });
  }

  async addDashboardGadget(
    dashboardId: number,
    gadget: {
      gadget_type: string;
      title?: string;
      column_index?: number;
      row_index?: number;
      preferences?: any;
    }
  ): Promise<DashboardGadget> {
    return this.request<DashboardGadget>(`/api/dashboards/${dashboardId}/gadgets`, {
      method: 'POST',
      body: JSON.stringify(gadget),
    });
  }

  async updateDashboardGadget(
    gadgetId: number,
    update: { title?: string; preferences?: any }
  ): Promise<DashboardGadget> {
    return this.request<DashboardGadget>(`/api/dashboard-gadgets/${gadgetId}`, {
      method: 'PUT',
      body: JSON.stringify(update),
    });
  }

  async updateDashboardGadgetPosition(
    gadgetId: number,
    pos: { column_index: number; row_index: number }
  ): Promise<{ id: number; column_index: number; row_index: number }> {
    return this.request<{ id: number; column_index: number; row_index: number }>(
      `/api/dashboard-gadgets/${gadgetId}/position`,
      {
        method: 'PUT',
        body: JSON.stringify(pos),
      }
    );
  }

  async deleteDashboardGadget(gadgetId: number): Promise<{ success: boolean; deleted_id: number }> {
    return this.request<{ success: boolean; deleted_id: number }>(
      `/api/dashboard-gadgets/${gadgetId}`,
      {
        method: 'DELETE',
      }
    );
  }

  async getDashboardGadgetData(gadgetId: number): Promise<{
    gadget_id: number;
    gadget_type: string;
    title: string;
    data: any;
  }> {
    return this.request<{
      gadget_id: number;
      gadget_type: string;
      title: string;
      data: any;
    }>(`/api/dashboard-gadgets/${gadgetId}/data`);
  }
}
