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
  watchers_count?: number;
  votes_count?: number;
  is_watching?: boolean;
  has_voted?: boolean;
  security_level_id?: number | null;
  security_level_name?: string;
  service_desk_id?: number | null;
  service_desk_name?: string;
  request_type_id?: number | null;
  request_type_name?: string;
  customer_reporter_name?: string;
  customer_reporter_email?: string;
  first_response_at?: string;
  is_customer_request?: boolean;
  slas?: WorkItemSla[];
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

export interface WorkItemChangelog {
  id: number;
  work_item_id: number;
  changed_by_id: number | null;
  changed_by_name: string;
  changed_by_color: string;
  field_name: string;
  old_value: string;
  new_value: string;
  created_at: string;
}

export interface UserMention {
  id: number;
  work_item_id: number;
  item_key: string;
  item_title: string;
  comment_id: number | null;
  author_id: number;
  author_name: string;
  author_color: string;
  snippet: string;
  is_read: boolean;
  created_at: string;
}

export interface WatcherUser {
  user_id: number;
  username: string;
  display_name: string;
  avatar_color: string;
  created_at: string;
}

export interface WatchersPayload {
  watchers_count: number;
  is_watching: boolean;
  watchers: WatcherUser[];
}

export interface VoterUser {
  user_id: number;
  username: string;
  display_name: string;
  created_at: string;
}

export interface VotesPayload {
  votes_count: number;
  has_voted: boolean;
  voters: VoterUser[];
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
  changelog?: WorkItemChangelog[];
  watchers_info?: WatchersPayload;
  votes_info?: VotesPayload;
  development?: DevelopmentInfo;
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

// Epic 10 / Step 11: Permissions, Security Schemes, Groups & Personal Access Tokens
export interface PermissionGrant {
  id: number;
  scheme_id: number;
  permission_key: string;
  grantee_type: string; // 'role' | 'group' | 'dynamic' | 'user' | 'anyone'
  grantee_value: string;
  created_at?: string;
}

export interface PermissionScheme {
  id: number;
  name: string;
  description: string;
  is_default: boolean;
  grants_count?: number;
  projects_count?: number;
  grants?: PermissionGrant[];
  projects?: { id: number; key: string; name: string }[];
  created_at?: string;
  updated_at?: string;
}

export interface IssueSecurityGrant {
  id: number;
  level_id: number;
  grantee_type: string;
  grantee_value: string;
  created_at?: string;
}

export interface IssueSecurityLevel {
  id: number;
  scheme_id: number;
  name: string;
  description: string;
  is_default: boolean;
  can_access?: boolean;
  grants?: IssueSecurityGrant[];
  created_at?: string;
}

export interface IssueSecurityScheme {
  id: number;
  name: string;
  description: string;
  default_level_id?: number | null;
  levels_count?: number;
  projects_count?: number;
  levels?: IssueSecurityLevel[];
  projects?: { id: number; key: string; name: string }[];
  created_at?: string;
  updated_at?: string;
}

export interface UserGroupMember {
  user_id: number;
  username: string;
  display_name: string;
  email: string;
  role: string;
  avatar_color: string;
  added_at?: string;
}

export interface UserGroup {
  id: number;
  name: string;
  description: string;
  member_count?: number;
  members?: UserGroupMember[];
  created_at?: string;
}

export interface ProjectRoleMember {
  id: number;
  project_id: number;
  role_name: string;
  user_id: number;
  username: string;
  display_name: string;
  avatar_color: string;
  system_role: string;
  created_at?: string;
}

export interface PersonalAccessToken {
  id: number;
  name: string;
  token?: string;
  scopes: string;
  expires_at?: string;
  created_at?: string;
  last_used_at?: string;
}

export interface UserMyPermissions {
  user_id: number;
  username: string;
  role: string;
  is_admin: boolean;
  project_id: number;
  permissions: string[];
}

// ============================================================================
// Step 12: No-Code Automation Engine Interfaces
// ============================================================================

export interface AutomationTriggerConfig {
  to_status?: string;
  from_status?: string;
  field_name?: string;
  [key: string]: any;
}

export interface AutomationCondition {
  type: 'jql' | 'issue_field' | 'user_condition' | 'has_subtasks';
  jql?: string;
  field?: string;
  operator?: string;
  value?: string;
  check?: string;
  group?: string;
  [key: string]: any;
}

export interface AutomationAction {
  type: 'transition_issue' | 'edit_field' | 'create_subtask' | 'create_issue' | 'add_comment' | 'send_notification' | 'http_request' | 'branch';
  status?: string;
  field?: string;
  value?: string;
  title?: string;
  description?: string;
  body?: string;
  target?: string;
  jql?: string;
  url?: string;
  method?: string;
  issue_type?: string;
  priority?: string;
  actions?: AutomationAction[];
  [key: string]: any;
}

export interface AutomationRule {
  id: number;
  project_id?: number | null;
  project_key?: string;
  project_name?: string;
  name: string;
  description: string;
  is_enabled: boolean;
  trigger_type: string;
  trigger_config: AutomationTriggerConfig;
  conditions: AutomationCondition[];
  actions: AutomationAction[];
  created_by?: number | null;
  creator_name?: string;
  created_at: string;
  updated_at: string;
  execution_count: number;
  last_executed_at?: string;
}

export interface AutomationLog {
  id: number;
  rule_id?: number | null;
  rule_name: string;
  trigger_type: string;
  issue_id?: number | null;
  issue_key: string;
  status: 'SUCCESS' | 'FAILED' | 'NO_ACTIONS' | 'ERROR';
  duration_ms: number;
  log_trace: string;
  created_at: string;
}

// Step 13: Notifications, SMTP & Webhooks
export interface InAppNotification {
  id: number;
  work_item_id: number | null;
  event_type: string;
  title: string;
  message: string;
  link: string;
  is_read: boolean;
  created_at: string;
  item_key?: string;
  item_title?: string;
}

export interface NotificationSchemeEntry {
  id: number;
  scheme_id: number;
  event_type: string;
  recipient_type: string;
  recipient_parameter: string;
  created_at?: string;
}

export interface NotificationScheme {
  id: number;
  name: string;
  description: string;
  is_default: boolean;
  created_at: string;
  updated_at: string;
  entry_count?: number;
  project_count?: number;
  entries?: NotificationSchemeEntry[];
}

export interface Webhook {
  id: number;
  name: string;
  url: string;
  secret: string;
  has_secret?: boolean;
  events: string;
  is_active: boolean;
  created_at: string;
  updated_at: string;
  deliveries_count?: number;
  last_status?: string;
  last_delivery_at?: string;
}

export interface WebhookDelivery {
  id: number;
  webhook_id: number;
  webhook_name?: string;
  event_type: string;
  payload: string;
  response_status: number;
  response_body: string;
  duration_ms: number;
  attempt: number;
  status: string;
  error_message?: string;
  created_at: string;
}

export interface SmtpConfig {
  id: number;
  host: string;
  port: number;
  username: string;
  password?: string;
  from_email: string;
  from_name: string;
  tls_mode: string;
  is_enabled: boolean;
}

export interface OutboundEmail {
  id: number;
  to_email: string;
  to_name: string;
  subject: string;
  body_html: string;
  body_text: string;
  headers_json: string;
  status: string;
  error_message?: string;
  sent_at?: string;
  created_at: string;
}

// Step 14: Developer Tooling & Git Integration Models
export interface GitBranchItem {
  id: number;
  work_item_id: number;
  repository_id?: number;
  repository_name: string;
  branch_name: string;
  branch_url?: string;
  last_commit_hash?: string;
  created_at: string;
  updated_at: string;
}

export interface GitCommitItem {
  id: number;
  work_item_id: number;
  repository_id?: number;
  repository_name: string;
  commit_hash: string;
  short_hash: string;
  message: string;
  author_name: string;
  author_email: string;
  commit_url?: string;
  committed_at: string;
  created_at: string;
}

export interface GitPullRequestItem {
  id: number;
  work_item_id: number;
  repository_id?: number;
  repository_name: string;
  pr_number: number;
  title: string;
  source_branch: string;
  target_branch: string;
  status: 'OPEN' | 'MERGED' | 'DECLINED' | 'CLOSED';
  author_name: string;
  author_email: string;
  pr_url?: string;
  created_at: string;
  updated_at: string;
}

export interface GitBuildItem {
  id: number;
  work_item_id: number;
  repository_id?: number;
  repository_name: string;
  pipeline_id: string;
  build_number: string;
  status: 'SUCCESS' | 'FAILED' | 'IN_PROGRESS' | 'CANCELLED';
  commit_hash?: string;
  branch_name?: string;
  build_url?: string;
  duration_seconds: number;
  started_at: string;
  finished_at?: string;
  created_at: string;
}

export interface DevelopmentInfo {
  work_item_id: number;
  branches_count: number;
  commits_count: number;
  pull_requests_count: number;
  builds_count: number;
  branches: GitBranchItem[];
  commits: GitCommitItem[];
  pull_requests: GitPullRequestItem[];
  builds: GitBuildItem[];
}

export interface GitRepository {
  id: number;
  name: string;
  provider: 'github' | 'gitlab' | 'bitbucket' | 'gitea' | 'custom';
  repo_url: string;
  webhook_secret: string;
  is_active: boolean;
  created_at: string;
  updated_at: string;
  branches_count?: number;
  commits_count?: number;
  prs_count?: number;
  builds_count?: number;
}

export interface SmartCommitParsedCommand {
  raw_command: string;
  type: 'comment' | 'time' | 'transition';
  comment_body?: string;
  time_seconds?: number;
  formatted_duration?: string;
  time_comment?: string;
  target_status?: string;
}

export interface SmartCommitSimulationResult {
  commit_hash: string;
  short_hash: string;
  author_name: string;
  author_email: string;
  resolved_user_id: number;
  resolved_user_name: string;
  matched_keys: string[];
  parsed_commands: SmartCommitParsedCommand[];
  applied_actions?: string[];
  errors?: string[];
  executed: boolean;
}

export interface ServiceDesk {
  id: number;
  project_id: number;
  project_name?: string;
  project_key?: string;
  name: string;
  description: string;
  portal_key: string;
  is_open_access: boolean;
  created_at: string;
  request_types_count?: number;
  open_tickets_count?: number;
  queues_count?: number;
  request_types?: RequestType[];
  queues?: ServiceDeskQueue[];
  sla_metrics?: SlaMetric[];
}

export interface RequestType {
  id: number;
  service_desk_id: number;
  issue_type_id?: number | null;
  name: string;
  description: string;
  icon: string;
  help_text: string;
  display_order: number;
  created_at?: string;
}

export interface SlaCalendar {
  id: number;
  name: string;
  timezone: string;
  days_of_week: string;
  start_time: string;
  end_time: string;
  holidays_json?: string;
  is_24x7: boolean;
  created_at: string;
}

export interface SlaGoal {
  id: number;
  metric_id: number;
  priority: string;
  target_duration_seconds: number;
  target_formatted?: string;
  warning_duration_seconds: number;
}

export interface SlaMetric {
  id: number;
  service_desk_id: number;
  name: string;
  metric_key: string;
  description: string;
  calendar_id?: number | null;
  calendar_name?: string;
  start_condition?: string;
  pause_condition?: string;
  stop_condition?: string;
  goals?: SlaGoal[];
}

export interface WorkItemSla {
  id: number;
  metric_id: number;
  name: string;
  metric_key: string;
  status: 'within' | 'approaching' | 'breached' | 'paused' | 'completed' | string;
  target_duration_seconds: number;
  elapsed_work_seconds: number;
  remaining_work_seconds: number;
  target_formatted: string;
  elapsed_formatted: string;
  remaining_formatted: string;
  is_paused: boolean;
  is_completed: boolean;
  is_breached: boolean;
  started_at: string;
  paused_at?: string;
  completed_at?: string;
  calendar_name?: string;
  badge_color?: string;
}

export interface ServiceDeskQueue {
  id: number;
  service_desk_id: number;
  name: string;
  filter_type: 'unassigned' | 'open_incidents' | 'waiting_customer' | 'pending_reply' | 'all' | string;
  icon: string;
  jql_filter?: string;
  display_order: number;
  count?: number;
}

export interface CannedResponse {
  id: number;
  service_desk_id?: number | null;
  title: string;
  shortcut: string;
  content: string;
  created_by?: number | null;
  created_by_name?: string;
}

export interface CustomerPortalHome {
  instance_name: string;
  service_desks: Array<ServiceDesk & { request_types: RequestType[] }>;
}

export interface CustomerTicketDetail {
  id: number;
  key: string;
  title: string;
  description: string;
  status: string;
  priority: string;
  created_at: string;
  updated_at: string;
  customer_name: string;
  customer_email: string;
  request_type_name: string;
  service_desk_name: string;
  service_desk_id?: number | null;
  request_type_id?: number | null;
  first_response_at?: string;
  comments: Array<{
    id: number;
    body: string;
    created_at: string;
    author_name: string;
    author_id?: number | null;
  }>;
  slas: WorkItemSla[];
}





