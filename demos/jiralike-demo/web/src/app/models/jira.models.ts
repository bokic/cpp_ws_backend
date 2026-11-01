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
  completed_at?: string;
  committed_points?: number;
  completed_points?: number;
  completed_items_count?: number;
  incomplete_items_count?: number;
  rollover_target_sprint_id?: number | null;
  todo_count?: number;
  in_progress_count?: number;
  done_points?: number;
  created_at?: string;
}

export type SwimlaneStrategy = 'none' | 'epic' | 'assignee' | 'priority' | 'story' | 'custom_jql';

export interface BoardColumn {
  id: number;
  board_id: number;
  name: string;
  mapped_statuses: string;
  statuses: string[];
  min_wip: number;
  max_wip: number;
  color: string;
  sort_order: number;
}

export interface BoardQuickFilter {
  id: number;
  board_id: number;
  name: string;
  jql_query: string;
  description: string;
  sort_order: number;
}

export interface BoardSwimlane {
  id: number;
  board_id: number;
  name: string;
  description: string;
  jql_query: string;
  sort_order: number;
}

export interface BoardConfig {
  id: number;
  project_id: number;
  project_key?: string;
  project_name?: string;
  name: string;
  board_type: 'scrum' | 'kanban';
  filter_jql: string;
  swimlane_strategy: SwimlaneStrategy;
  card_extra_fields: string;
  card_extra_fields_list: string[];
  card_color_strategy: 'priority' | 'type' | 'category' | 'none' | string;
  show_backlog_column: boolean;
  is_default: boolean;
  created_at?: string;
  updated_at?: string;
  columns: BoardColumn[];
  quick_filters: BoardQuickFilter[];
  swimlanes: BoardSwimlane[];
}

export interface BoardItemsPayload {
  board: BoardConfig;
  items: WorkItem[];
  swimlane_matches: Record<string, number[]>;
}

export interface CompleteSprintResponse {
  completed: boolean;
  sprint_id: number;
  sprint_name: string;
  committed_points: number;
  completed_points: number;
  incomplete_points: number;
  completed_items_count: number;
  incomplete_items_count: number;
  rollover_target: 'backlog' | 'sprint';
  target_sprint_id: number | null;
  target_sprint_name: string;
  rolled_over_keys: string[];
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
  original_estimate_seconds?: number;
  remaining_estimate_seconds?: number;
  time_spent_seconds?: number;
  original_estimate_formatted?: string;
  remaining_estimate_formatted?: string;
  time_spent_formatted?: string;
  tshirt_size?: string;
  worklog_count?: number;
  time_tracking_pct?: number;
  is_over_estimate?: boolean;
  bm25_score?: number;
}

export type WorklogAdjustmentMode = 'auto' | 'leave' | 'set' | 'reduce';

export interface WorklogItem {
  id: number;
  work_item_id: number;
  author_id: number | null;
  author_name: string;
  author_color: string;
  author_role?: string;
  time_spent_seconds: number;
  time_spent_formatted: string;
  started_at: string;
  description: string;
  adjustment_mode: string;
  adjustment_seconds: number;
  adjustment_formatted?: string;
  remaining_before: number;
  remaining_before_formatted: string;
  remaining_after: number;
  remaining_after_formatted: string;
  created_at?: string;
  updated_at?: string;
  work_item_key?: string;
  work_item_title?: string;
  work_item_type?: string;
  work_item_status?: string;
  project_id?: number;
}

export interface TimeRollup {
  own_original_estimate_seconds: number;
  own_original_estimate_formatted: string;
  own_remaining_estimate_seconds: number;
  own_remaining_estimate_formatted: string;
  own_time_spent_seconds: number;
  own_time_spent_formatted: string;
  subtasks_original_estimate_seconds: number;
  subtasks_original_estimate_formatted: string;
  subtasks_remaining_estimate_seconds: number;
  subtasks_remaining_estimate_formatted: string;
  subtasks_time_spent_seconds: number;
  subtasks_time_spent_formatted: string;
  aggregate_original_estimate_seconds: number;
  aggregate_original_estimate_formatted: string;
  aggregate_remaining_estimate_seconds: number;
  aggregate_remaining_estimate_formatted: string;
  aggregate_time_spent_seconds: number;
  aggregate_time_spent_formatted: string;
  aggregate_progress_pct: number;
  aggregate_is_over_estimate: boolean;
  subtask_count: number;
}

export interface TimesheetUserSummary {
  user_id: number;
  display_name: string;
  avatar_color: string;
  role: string;
  total_seconds: number;
  total_formatted: string;
  entry_count: number;
}

export interface TimesheetDaySummary {
  date: string;
  total_seconds: number;
  total_formatted: string;
}

export interface TimesheetItemSummary {
  work_item_id: number;
  key: string;
  title: string;
  type: string;
  status: string;
  total_seconds: number;
  total_formatted: string;
  entry_count: number;
}

export interface TimesheetResponse {
  total_seconds: number;
  total_formatted: string;
  worklogs: WorklogItem[];
  by_user: TimesheetUserSummary[];
  by_day: TimesheetDaySummary[];
  by_item: TimesheetItemSummary[];
}

export interface DurationParseResponse {
  valid: boolean;
  input?: string;
  seconds: number;
  formatted: string;
  error?: string;
  breakdown?: {
    weeks: number;
    days: number;
    hours: number;
    minutes: number;
    seconds: number;
  };
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
  worklogs?: WorklogItem[];
  time_rollup?: TimeRollup;
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

export interface BurndownDayPoint {
  date: string;
  day_index: number;
  is_weekend: boolean;
  ideal_points: number;
  ideal_hours: number;
  remaining_points: number;
  remaining_hours: number;
  total_scope_points: number;
  total_scope_hours: number;
  completed_points: number;
  completed_hours: number;
}

export interface ScopeChangeEvent {
  date: string;
  item_id: number;
  item_key: string;
  summary: string;
  event_type: 'added' | 'removed' | 'points_changed' | string;
  delta_points: number;
}

export interface BurndownReport {
  sprint_id: number;
  sprint_name: string;
  goal: string;
  status: string;
  start_date: string;
  end_date: string;
  committed_points: number;
  current_scope_points: number;
  completed_points: number;
  remaining_points: number;
  scope_creep_points: number;
  has_scope_creep: boolean;
  completion_rate_percent: number;
  points_burn_rate_per_day: number;
  projected_completion_date: string;
  committed_hours: number;
  current_scope_hours: number;
  completed_hours: number;
  remaining_hours: number;
  timeline: BurndownDayPoint[];
  scope_changes: ScopeChangeEvent[];
}

export type BurnupReport = BurndownReport;

export interface VelocitySprintEntry {
  sprint_id: number;
  name: string;
  status: string;
  start_date: string;
  end_date: string;
  completed_at: string;
  committed_points: number;
  completed_points: number;
  committed_items: number;
  completed_items: number;
  predictability_percent: number;
}

export interface VelocityReport {
  project_id: number;
  project_name: string;
  sprints_analyzed: number;
  rolling_average_velocity: number;
  last_3_sprints_average: number;
  suggested_capacity: number;
  max_velocity: number;
  min_velocity: number;
  velocity_std_dev: number;
  sprints: VelocitySprintEntry[];
}

export interface CumulativeFlowReport {
  board_id: number;
  project_id: number;
  days_analyzed: number;
  start_date: string;
  end_date: string;
  statuses: string[];
  dates: string[];
  series: Record<string, number[]>;
  current_wip: number;
  average_wip: number;
  bottleneck_status: string;
  flow_trend: string;
}

export interface CycleTimeIssue {
  id: number;
  key: string;
  summary: string;
  type: string;
  priority: string;
  assignee: string;
  created_at: string;
  started_at: string;
  resolved_at: string;
  cycle_time_days: number;
  lead_time_days: number;
  rolling_avg_days: number;
  is_outlier: boolean;
}

export interface CycleTimeReport {
  project_id: number;
  sample_size: number;
  mean_cycle_time_days: number;
  median_cycle_time_days: number;
  p85_cycle_time_days: number;
  p95_cycle_time_days: number;
  std_dev_days: number;
  upper_control_limit_1_sigma: number;
  upper_control_limit_2_sigma: number;
  lower_control_limit_1_sigma: number;
  issues: CycleTimeIssue[];
  outliers: { key: string; summary: string; cycle_time_days: number }[];
}

export interface CreatedVsResolvedBucket {
  period: string;
  created_count: number;
  resolved_count: number;
  net_change: number;
  cumulative_created: number;
  cumulative_resolved: number;
  backlog_growth: number;
}

export interface CreatedVsResolvedReport {
  project_id: number;
  days_analyzed: number;
  group_by: string;
  total_created: number;
  total_resolved: number;
  net_backlog_delta: number;
  resolution_rate_percent: number;
  health_status: string;
  buckets: CreatedVsResolvedBucket[];
}

export interface EpicChildIssue {
  id: number;
  key: string;
  summary: string;
  type: string;
  status: string;
  priority: string;
  assignee: string;
  points: number;
  remaining_estimate_hours: number;
  time_spent_hours: number;
}

export interface EpicReport {
  epic_id: number;
  key: string;
  summary: string;
  status: string;
  total_points: number;
  completed_points: number;
  remaining_points: number;
  progress_percent: number;
  total_items: number;
  completed_items: number;
  in_progress_items: number;
  unstarted_items: number;
  unestimated_items_count: number;
  completion_forecast: string;
  original_estimate_hours: number;
  remaining_estimate_hours: number;
  time_spent_hours: number;
  child_issues: EpicChildIssue[];
}

export interface EpicSummaryItem {
  id: number;
  key: string;
  summary: string;
  status: string;
  child_count: number;
  done_count: number;
  total_points: number;
  done_points: number;
  progress_percent: number;
}

export interface ReleaseBurndownItem {
  id: number;
  key: string;
  summary: string;
  type: string;
  status: string;
  points: number;
}

export interface ReleaseBurndownReport {
  version_id: number;
  version_name: string;
  release_date: string;
  status: string;
  total_items: number;
  completed_items: number;
  remaining_items: number;
  total_points: number;
  completed_points: number;
  remaining_points: number;
  completion_percent: number;
  forecast_status: string;
  items: ReleaseBurndownItem[];
}

export interface ReleaseSummaryItem {
  id: number;
  name: string;
  release_date: string;
  status: string;
  item_count: number;
  done_count: number;
  total_points: number;
  done_points: number;
  completion_percent: number;
}

export type DashboardLayout = '1_column' | '2_column_equal' | '2_column_split' | '3_column';
export type DashboardShareType = 'private' | 'project' | 'public';

export interface DashboardGadget {
  id: number;
  dashboard_id: number;
  gadget_type: string;
  title: string;
  column_index: number;
  row_index: number;
  preferences_json: string;
  preferences?: any;
  created_at?: string;
  updated_at?: string;
  data?: any;
  loading?: boolean;
  error?: string;
}

export interface Dashboard {
  id: number;
  owner_id: number;
  name: string;
  description: string;
  layout: DashboardLayout;
  is_favorite: boolean;
  share_type: DashboardShareType;
  project_id?: number | null;
  project_name?: string;
  project_key?: string;
  gadget_count?: number;
  created_at: string;
  updated_at: string;
  owner_name?: string;
  owner_avatar?: string;
  gadgets?: DashboardGadget[];
}

export interface GadgetCatalogItem {
  gadget_type: string;
  title: string;
  description: string;
  category: string;
  icon: string;
  default_preferences: string;
}




