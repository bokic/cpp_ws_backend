export type WorkItemType = 'Initiative' | 'Epic' | 'Story' | 'Task' | 'Subtask' | string;

export type WorkItemCategory = 'New Feature' | 'Improvement' | 'Bug' | 'Issue';

export type WorkflowState =
  | 'Backlog'
  | 'To Do'
  | 'In Progress'
  | 'In Review'
  | 'QA / Testing'
  | 'Done'
  | 'Blocked';

export type PriorityType = 'Lowest' | 'Low' | 'Medium' | 'High' | 'Highest' | 'Critical';

export const WORK_ITEM_TYPES: WorkItemType[] = [
  'Initiative',
  'Epic',
  'Story',
  'Task',
  'Subtask',
];

export const WORK_ITEM_CATEGORIES: WorkItemCategory[] = [
  'New Feature',
  'Improvement',
  'Bug',
  'Issue',
];

export const WORKFLOW_STATES: WorkflowState[] = [
  'Backlog',
  'To Do',
  'In Progress',
  'In Review',
  'QA / Testing',
  'Done',
  'Blocked',
];

export const PRIORITIES: PriorityType[] = [
  'Lowest',
  'Low',
  'Medium',
  'High',
  'Highest',
  'Critical',
];

export interface MigrationInfo {
  version: number;
  applied_at: string;
  description: string;
}

export interface SetupStatus {
  setup_complete: boolean;
  instance_name: string;
  schema_version: number;
  latest_schema_version: number;
  migrations: MigrationInfo[];
}

export interface User {
  id: number;
  username: string;
  email: string;
  display_name: string;
  role: string;
  avatar_color: string;
  created_at?: string;
}

export interface Project {
  id: number;
  key: string;
  name: string;
  description: string;
  lead_user_id: number | null;
  lead_name?: string;
  issue_seq: number;
  issue_type_scheme_id?: number;
  issue_type_scheme_name?: string;
  total_items?: number;
  created_at?: string;
}

export interface Resolution {
  id: number;
  name: string;
  description: string;
  sort_order: number;
  is_default: boolean;
}

export interface IssueTypeDef {
  id: number;
  name: string;
  description: string;
  icon: string;
  color: string;
  hierarchy_level: number;
  is_subtask: boolean;
  is_system: boolean;
}

export interface IssueTypeScheme {
  id: number;
  name: string;
  description: string;
  is_default: boolean;
  issue_types: {
    id: number;
    name: string;
    icon: string;
    color: string;
    hierarchy_level: number;
    is_subtask: boolean;
    is_default: boolean;
  }[];
}

export interface ProjectComponent {
  id: number;
  project_id: number;
  name: string;
  description: string;
  lead_user_id: number | null;
  lead_name: string;
  default_assignee_type: 'PROJECT_DEFAULT' | 'COMPONENT_LEAD' | 'PROJECT_LEAD' | 'UNASSIGNED';
  item_count?: number;
  created_at?: string;
}

export interface ProjectVersion {
  id: number;
  project_id: number;
  name: string;
  description: string;
  status: 'unreleased' | 'released' | 'archived';
  start_date: string;
  release_date: string;
  fix_count?: number;
  fix_done_count?: number;
  affects_count?: number;
  created_at?: string;
}

export interface ReleaseNotesResponse {
  version_id: number;
  version_name: string;
  status: string;
  release_date: string;
  project_key: string;
  project_name: string;
  total_items: number;
  markdown: string;
}

export interface Sprint {
  id: number;
  project_id: number;
  name: string;
  goal: string;
  status: 'planned' | 'active' | 'closed';
  start_date: string;
  end_date: string;
  is_current: boolean;
  item_count?: number;
  done_count?: number;
  total_points?: number;
  created_at?: string;
}

export interface WorkItem {
  id: number;
  project_id: number;
  key: string;
  type: WorkItemType;
  category: WorkItemCategory;
  status: WorkflowState;
  priority: PriorityType;
  title: string;
  description: string;
  parent_id: number | null;
  sprint_id: number | null;
  assignee_id: number | null;
  reporter_id: number | null;
  resolution_id?: number | null;
  resolution_name?: string;
  resolution_date?: string;
  components?: { id: number; name: string }[];
  component_ids?: number[];
  fix_versions?: { id: number; name: string; status: string }[];
  fix_version_ids?: number[];
  affects_versions?: { id: number; name: string; status: string }[];
  affects_version_ids?: number[];
  custom_fields?: Record<string, any>;
  custom_field_entries?: CustomFieldValueEntry[];
  story_points: number;
  progress: number;
  start_date: string;
  due_date: string;
  severity: string;
  steps_to_reproduce: string;
  labels: string;
  board_order: number;
  created_at: string;
  updated_at: string;
  assignee_name: string;
  assignee_color: string;
  reporter_name: string;
  parent_key: string;
  parent_title: string;
  sprint_name: string;
  subtask_count: number;
  comment_count: number;
  attachment_count: number;
  bm25_score?: number;
}

export interface WorkItemLink {
  id: number;
  source_item_id: number;
  target_item_id: number;
  link_type: 'blocks' | 'is_blocked_by' | 'relates_to' | 'duplicates';
  source_key: string;
  source_title: string;
  source_status: string;
  source_type: string;
  target_key: string;
  target_title: string;
  target_status: string;
  target_type: string;
}

export interface CommentItem {
  id: number;
  work_item_id: number;
  author_id: number | null;
  author_name: string;
  author_color: string;
  body: string;
  created_at: string;
}

export interface AttachmentItem {
  id: number;
  work_item_id: number;
  uploader_id: number | null;
  uploader_name: string;
  original_filename: string;
  mime_type: string;
  size_bytes: number;
  blob_filename: string;
  url: string;
  created_at?: string;
}

export interface WorkflowHistoryItem {
  id: number;
  from_status: string;
  to_status: string;
  changed_by_name: string;
  comment: string;
  created_at: string;
  transition_id?: number | null;
  transition_name?: string;
  executed_post_functions?: string;
}

export interface WorkItemDetail extends WorkItem {
  subtasks: WorkItem[];
  links: WorkItemLink[];
  comments: CommentItem[];
  attachments: AttachmentItem[];
  history: WorkflowHistoryItem[];
  workflow?: ItemTransitionsPayload;
}

export type CustomFieldType =
  | 'short_text'
  | 'long_text'
  | 'number'
  | 'date'
  | 'datetime'
  | 'select_single'
  | 'select_multi'
  | 'cascading_select'
  | 'user_picker_single'
  | 'user_picker_multi'
  | 'group_picker'
  | 'url'
  | 'checkbox'
  | 'formula';

export interface CustomFieldOption {
  id: number;
  custom_field_id: number;
  parent_option_id: number | null;
  value: string;
  color: string;
  sort_order: number;
  is_active: boolean;
  is_default: boolean;
}

export interface CustomFieldDef {
  id: number;
  name: string;
  field_key: string;
  field_type: CustomFieldType;
  type?: CustomFieldType;
  description: string;
  default_value: string;
  is_required: boolean;
  is_searchable: boolean;
  min_value: number | null;
  max_value: number | null;
  formula_expr: string;
  sort_order: number;
  created_at?: string;
  options: CustomFieldOption[];
}

export interface CustomFieldValueEntry {
  custom_field_id: number;
  field_key: string;
  name: string;
  field_type: CustomFieldType;
  is_required: boolean;
  value: any;
  display_value: string;
  user_value_id: number | null;
  user_display_name: string;
}

export interface FieldConfigurationItem {
  id: number;
  configuration_id: number;
  custom_field_id: number;
  field_name: string;
  field_key: string;
  field_type: CustomFieldType;
  issue_type: string;
  is_required: boolean;
  is_hidden: boolean;
  renderer: 'plain' | 'markdown';
  default_value: string;
  description_override: string;
}

export interface FieldConfiguration {
  id: number;
  name: string;
  description: string;
  is_default: boolean;
  created_at?: string;
  items: FieldConfigurationItem[];
}

export interface ScreenTabField {
  id: number;
  tab_id: number;
  field_key: string;
  custom_field_id: number | null;
  sort_order: number;
  name: string;
  field_type: CustomFieldType;
  description: string;
  is_required: boolean;
  is_hidden: boolean;
  renderer: 'plain' | 'markdown';
  default_value: string;
  min_value: number | null;
  max_value: number | null;
  formula_expr: string;
  options: CustomFieldOption[];
}

export interface ScreenTab {
  id: number;
  screen_id: number;
  name: string;
  sort_order: number;
  fields: ScreenTabField[];
}

export interface ScreenDef {
  id: number;
  name: string;
  description: string;
  screen_type: 'create' | 'edit' | 'view' | 'transition' | 'default' | string;
  created_at?: string;
  tabs: ScreenTab[];
  operation?: string;
  project_id?: number;
  issue_type?: string;
  configuration_id?: number;
}

export interface ScreenScheme {
  id: number;
  name: string;
  description: string;
  project_id: number | null;
  is_default: boolean;
  create_screen_id: number;
  create_screen_name?: string;
  edit_screen_id: number;
  edit_screen_name?: string;
  view_screen_id: number;
  view_screen_name?: string;
  transition_screen_id: number;
  transition_screen_name?: string;
  created_at?: string;
}

export type StatusCategory = 'To Do' | 'In Progress' | 'Done' | string;

export interface WorkflowStatusNode {
  id: number;
  workflow_id?: number;
  name: string;
  category: StatusCategory;
  color: string;
  pos_x: number;
  pos_y: number;
  sort_order: number;
  is_initial: boolean;
  item_count?: number;
}

export interface WorkflowTransitionRule {
  id: number;
  transition_id: number;
  rule_category: 'condition' | 'validator' | 'post_function' | string;
  rule_type:
    | 'only_assignee'
    | 'user_in_role'
    | 'subtasks_completed'
    | 'permission'
    | 'field_required'
    | 'comment_required'
    | 'regex_match'
    | 'set_field_value'
    | 'clear_field_value'
    | 'assign_to_user'
    | 'update_parent_status'
    | 'dispatch_event'
    | string;
  field_name: string;
  config_value: string;
  description: string;
  sort_order: number;
}

export interface WorkflowTransitionDef {
  id: number;
  workflow_id: number;
  name: string;
  from_status: string;
  to_status: string;
  screen_id: number | null;
  screen_name: string;
  sort_order: number;
  created_at?: string;
  rules: WorkflowTransitionRule[];
  conditions: WorkflowTransitionRule[];
  validators: WorkflowTransitionRule[];
  post_functions: WorkflowTransitionRule[];
}

export interface WorkflowDef {
  id: number;
  name: string;
  description: string;
  is_default: boolean;
  is_system: boolean;
  created_at?: string;
  statuses: WorkflowStatusNode[];
  transitions: WorkflowTransitionDef[];
}

export interface WorkflowSchemeBinding {
  id: number;
  name: string;
  description: string;
  project_id: number | null;
  project_name: string;
  issue_type: string;
  workflow_id: number;
  workflow_name: string;
  is_default: boolean;
  created_at?: string;
}

export interface AvailableTransition {
  id: number;
  workflow_id: number;
  name: string;
  from_status: string;
  to_status: WorkflowState | string;
  to_category: StatusCategory;
  to_color: string;
  screen_id: number | null;
  screen_name: string;
  allowed: boolean;
  requires_screen: boolean;
  blocked_reasons: string[];
  conditions: WorkflowTransitionRule[];
  validators: WorkflowTransitionRule[];
  post_functions: WorkflowTransitionRule[];
  screen: ScreenDef | null;
}

export interface ItemTransitionsPayload {
  item_id: number;
  workflow_id: number;
  workflow_name: string;
  current_status: string;
  current_category: StatusCategory;
  statuses: WorkflowStatusNode[];
  transitions: AvailableTransition[];
}

export interface JqlToken {
  type: string;
  text: string;
  raw_text: string;
  pos: number;
  length: number;
  line: number;
  col: number;
}

export interface JqlValidateResponse {
  valid: boolean;
  jql: string;
  where_sql?: string;
  order_by_sql?: string;
  compiled_sql?: string;
  bind_params?: string[];
  ast_summary?: string;
  uses_fts5?: boolean;
  execution_time_ms?: number;
  tokens?: JqlToken[];
  error?: string;
  error_pos?: number;
  error_length?: number;
  error_line?: number;
  error_col?: number;
}

export interface JqlSearchResponse extends JqlValidateResponse {
  total: number;
  limit: number;
  offset: number;
  items: WorkItem[];
}

export interface JqlFieldSuggestion {
  name: string;
  cf_syntax?: string;
  label: string;
  type: string;
  description: string;
}

export interface JqlFunctionSuggestion {
  signature: string;
  description: string;
}

export interface JqlSuggestions {
  fields: JqlFieldSuggestion[];
  operators: string[];
  functions: JqlFunctionSuggestion[];
  keywords: string[];
  values: Record<string, string[]>;
}

export interface FilterSubscription {
  id: number;
  filter_id: number;
  user_id: number | null;
  cron_schedule: string;
  frequency_label: string;
  recipient_scope: string;
  email_empty: boolean;
  is_active: boolean;
  last_run_at: string;
  created_at: string;
}

export interface SavedFilter {
  id: number;
  owner_id: number | null;
  owner_name: string;
  name: string;
  description: string;
  jql_query: string;
  is_favorite: boolean;
  is_system: boolean;
  share_type: 'public' | 'private' | 'role' | 'project' | string;
  share_target: string;
  view_columns: string;
  sort_order: number;
  created_at: string;
  updated_at: string;
  subscriptions: FilterSubscription[];
}


