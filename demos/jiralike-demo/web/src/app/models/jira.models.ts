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
}

export interface WorkItemDetail extends WorkItem {
  subtasks: WorkItem[];
  links: WorkItemLink[];
  comments: CommentItem[];
  attachments: AttachmentItem[];
  history: WorkflowHistoryItem[];
}
