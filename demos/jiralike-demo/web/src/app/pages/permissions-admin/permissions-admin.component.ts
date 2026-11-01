import { Component, OnInit, computed, inject, signal } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { RouterLink } from '@angular/router';
import { ApiService } from '../../services/api.service';
import {
  IssueSecurityGrant,
  IssueSecurityLevel,
  IssueSecurityScheme,
  PermissionGrant,
  PermissionScheme,
  PersonalAccessToken,
  ProjectRoleMember,
  User,
  UserGroup,
  UserGroupMember,
} from '../../models/jira.models';

interface PermissionMeta {
  key: string;
  name: string;
  category: string;
  description: string;
}

const ALL_PERMISSION_METAS: PermissionMeta[] = [
  // Project
  { key: 'BROWSE_PROJECT', name: 'Browse Projects', category: 'Project', description: 'View project issues, boards, backlog, reports, and Gantt charts' },
  { key: 'ADMINISTER_PROJECTS', name: 'Administer Projects', category: 'Project', description: 'Configure project domain schemes, components, versions, and roles' },

  // Issues
  { key: 'CREATE_ISSUE', name: 'Create Issues', category: 'Issues', description: 'Create new tasks, stories, bugs, epics, and subtasks' },
  { key: 'EDIT_ISSUE', name: 'Edit Issues', category: 'Issues', description: 'Modify work item title, description, custom fields, and estimates' },
  { key: 'ASSIGN_ISSUE', name: 'Assign Issues', category: 'Issues', description: 'Assign work items to other project team members' },
  { key: 'RESOLVE_ISSUE', name: 'Resolve Issues', category: 'Issues', description: 'Transition work items to resolved states and record resolutions' },
  { key: 'CLOSE_ISSUE', name: 'Close Issues', category: 'Issues', description: 'Transition work items to final closed/done workflow states' },
  { key: 'DELETE_ISSUE', name: 'Delete Issues', category: 'Issues', description: 'Permanently remove work items and their attachments' },
  { key: 'MOVE_ISSUE', name: 'Move Issues', category: 'Issues', description: 'Move issues between projects' },
  { key: 'LINK_ISSUE', name: 'Link Issues', category: 'Issues', description: 'Create and remove dependencies and issue relationships' },

  // Comments
  { key: 'ADD_COMMENTS', name: 'Add Comments', category: 'Comments', description: 'Post comments and feedback on work items' },
  { key: 'EDIT_OWN_COMMENTS', name: 'Edit Own Comments', category: 'Comments', description: 'Edit comments authored by oneself' },
  { key: 'EDIT_ALL_COMMENTS', name: 'Edit All Comments', category: 'Comments', description: 'Modify comments authored by any user' },
  { key: 'DELETE_OWN_COMMENTS', name: 'Delete Own Comments', category: 'Comments', description: 'Delete comments authored by oneself' },
  { key: 'DELETE_ALL_COMMENTS', name: 'Delete All Comments', category: 'Comments', description: 'Delete comments posted by any user' },

  // Attachments
  { key: 'CREATE_ATTACHMENTS', name: 'Create Attachments', category: 'Attachments', description: 'Upload files and paste screenshots into issues' },
  { key: 'DELETE_OWN_ATTACHMENTS', name: 'Delete Own Attachments', category: 'Attachments', description: 'Remove attachments uploaded by oneself' },
  { key: 'DELETE_ALL_ATTACHMENTS', name: 'Delete All Attachments', category: 'Attachments', description: 'Remove attachments uploaded by any user' },

  // Time Tracking
  { key: 'WORK_ON_ISSUES', name: 'Work On Issues', category: 'Time Tracking', description: 'Log hours worked and update remaining estimates' },
  { key: 'EDIT_OWN_WORKLOGS', name: 'Edit Own Worklogs', category: 'Time Tracking', description: 'Modify time logged by oneself' },
  { key: 'EDIT_ALL_WORKLOGS', name: 'Edit All Worklogs', category: 'Time Tracking', description: 'Modify worklogs submitted by any user' },
  { key: 'DELETE_WORKLOGS', name: 'Delete Worklogs', category: 'Time Tracking', description: 'Delete logged work records' },

  // Agile Sprints
  { key: 'MANAGE_SPRINTS', name: 'Manage Sprints', category: 'Agile', description: 'Create, start, edit, and complete Scrum sprints' },
];

@Component({
  selector: 'app-permissions-admin',
  standalone: true,
  imports: [CommonModule, FormsModule, RouterLink],
  templateUrl: './permissions-admin.component.html',
  styleUrls: ['./permissions-admin.component.css'],
})
export class PermissionsAdminComponent implements OnInit {
  readonly api = inject(ApiService);

  activeTab = signal<'permissions' | 'security' | 'groups' | 'tokens' | 'project-schemes'>('permissions');
  toastMsg = signal<string>('');
  errorMsg = signal<string>('');

  // Permission Schemes
  permissionSchemes = signal<PermissionScheme[]>([]);
  selectedPermissionScheme = signal<PermissionScheme | null>(null);
  newSchemeName = signal<string>('');
  newSchemeDesc = signal<string>('');
  copyFromSchemeId = signal<number | null>(null);
  showNewSchemeModal = signal<boolean>(false);

  // Grant Modal
  showGrantModal = signal<boolean>(false);
  grantTargetPermKey = signal<string>('');
  grantType = signal<'role' | 'group' | 'dynamic' | 'user' | 'anyone'>('role');
  grantRoleValue = signal<string>('Developers');
  grantGroupValue = signal<string>('jira-software-users');
  grantDynamicValue = signal<string>('assignee');
  grantUserId = signal<number | null>(null);

  // Security Schemes
  securitySchemes = signal<IssueSecurityScheme[]>([]);
  selectedSecurityScheme = signal<IssueSecurityScheme | null>(null);
  newSecSchemeName = signal<string>('');
  newSecSchemeDesc = signal<string>('');
  showNewSecSchemeModal = signal<boolean>(false);

  // Security Level Modal
  showNewSecLevelModal = signal<boolean>(false);
  newSecLevelName = signal<string>('');
  newSecLevelDesc = signal<string>('');
  newSecLevelIsDefault = signal<boolean>(false);

  // Security Level Grant Modal
  showSecGrantModal = signal<boolean>(false);
  secGrantLevelId = signal<number | null>(null);
  secGrantType = signal<'role' | 'group' | 'dynamic' | 'user' | 'anyone'>('role');
  secGrantRole = signal<string>('Developers');
  secGrantGroup = signal<string>('jira-software-users');
  secGrantDynamic = signal<string>('assignee');
  secGrantUserId = signal<number | null>(null);

  // User Groups
  userGroups = signal<UserGroup[]>([]);
  selectedGroup = signal<UserGroup | null>(null);
  newGroupName = signal<string>('');
  newGroupDesc = signal<string>('');
  showNewGroupModal = signal<boolean>(false);
  addUserToGroupId = signal<number | null>(null);

  // Project Role Members & Schemes
  selectedProjectId = signal<number>(1);
  projectRoleMembers = signal<ProjectRoleMember[]>([]);
  assignPermSchemeId = signal<number>(1);
  assignSecSchemeId = signal<number>(1);
  newRoleMemberName = signal<string>('Developers');
  newRoleMemberUserId = signal<number | null>(null);

  // Personal Access Tokens
  tokens = signal<PersonalAccessToken[]>([]);
  newTokenName = signal<string>('');
  newTokenScopes = signal<string>('read,write');
  newTokenExpires = signal<string>('');
  createdRawToken = signal<string>('');
  showNewTokenModal = signal<boolean>(false);

  readonly permissionMetas = ALL_PERMISSION_METAS;
  readonly categories = ['Project', 'Issues', 'Comments', 'Attachments', 'Time Tracking', 'Agile'];
  readonly standardRoles = ['Administrators', 'Developers', 'QA', 'Product Managers', 'Viewers'];

  ngOnInit(): void {
    this.loadAll();
  }

  async loadAll(): Promise<void> {
    try {
      await Promise.all([
        this.loadPermissionSchemes(),
        this.loadSecuritySchemes(),
        this.loadUserGroups(),
        this.loadTokens(),
        this.loadProjectDetails(),
      ]);
    } catch (err: any) {
      this.errorMsg.set(err?.message || 'Failed to load directory and security data');
    }
  }

  private showToast(msg: string): void {
    this.toastMsg.set(msg);
    setTimeout(() => this.toastMsg.set(''), 3500);
  }

  // --- Permission Schemes ---
  async loadPermissionSchemes(): Promise<void> {
    const list = await this.api.getPermissionSchemes();
    this.permissionSchemes.set(list);
    if (list.length > 0 && !this.selectedPermissionScheme()) {
      await this.selectPermissionScheme(list[0].id);
    } else if (this.selectedPermissionScheme()) {
      await this.selectPermissionScheme(this.selectedPermissionScheme()!.id);
    }
  }

  async selectPermissionScheme(id: number): Promise<void> {
    try {
      const full = await this.api.getPermissionScheme(id);
      this.selectedPermissionScheme.set(full);
    } catch (err: any) {
      this.errorMsg.set(err?.message || 'Failed to load permission scheme');
    }
  }

  async createPermissionScheme(): Promise<void> {
    const name = this.newSchemeName().trim();
    if (!name) return;
    try {
      const created = await this.api.createPermissionScheme({
        name,
        description: this.newSchemeDesc(),
        copy_from_scheme_id: this.copyFromSchemeId() || undefined,
      });
      this.showToast(`Permission Scheme "${name}" created!`);
      this.newSchemeName.set('');
      this.newSchemeDesc.set('');
      this.copyFromSchemeId.set(null);
      this.showNewSchemeModal.set(false);
      await this.loadPermissionSchemes();
      await this.selectPermissionScheme(created.id);
    } catch (err: any) {
      this.errorMsg.set(err?.message || 'Failed to create scheme');
    }
  }

  async deletePermissionScheme(scheme: PermissionScheme): Promise<void> {
    if (!confirm(`Delete permission scheme "${scheme.name}"? Projects using it will revert to Default.`)) return;
    try {
      await this.api.deletePermissionScheme(scheme.id);
      this.showToast(`Deleted permission scheme "${scheme.name}".`);
      this.selectedPermissionScheme.set(null);
      await this.loadPermissionSchemes();
    } catch (err: any) {
      this.errorMsg.set(err?.message || 'Failed to delete scheme');
    }
  }

  openGrantModal(permKey: string): void {
    this.grantTargetPermKey.set(permKey);
    this.showGrantModal.set(true);
  }

  async submitGrant(): Promise<void> {
    const scheme = this.selectedPermissionScheme();
    if (!scheme) return;
    const permKey = this.grantTargetPermKey();
    const type = this.grantType();
    let val = '';
    if (type === 'role') val = this.grantRoleValue();
    else if (type === 'group') val = this.grantGroupValue();
    else if (type === 'dynamic') val = this.grantDynamicValue();
    else if (type === 'user') val = String(this.grantUserId() || '');
    else if (type === 'anyone') val = '*';

    if (!val) return;

    try {
      await this.api.addPermissionGrant(scheme.id, {
        permission_key: permKey,
        grantee_type: type,
        grantee_value: val,
      });
      this.showToast(`Granted ${type}:${val} to ${permKey}`);
      this.showGrantModal.set(false);
      await this.selectPermissionScheme(scheme.id);
    } catch (err: any) {
      this.errorMsg.set(err?.message || 'Failed to grant permission');
    }
  }

  async revokeGrant(grant: PermissionGrant): Promise<void> {
    const scheme = this.selectedPermissionScheme();
    if (!scheme) return;
    try {
      await this.api.deletePermissionGrant(scheme.id, grant.id);
      this.showToast(`Revoked grant`);
      await this.selectPermissionScheme(scheme.id);
    } catch (err: any) {
      this.errorMsg.set(err?.message || 'Failed to revoke grant');
    }
  }

  getGrantsForPerm(permKey: string): PermissionGrant[] {
    const scheme = this.selectedPermissionScheme();
    if (!scheme || !scheme.grants) return [];
    return scheme.grants.filter((g) => g.permission_key === permKey);
  }

  // --- Issue Security Schemes ---
  async loadSecuritySchemes(): Promise<void> {
    const list = await this.api.getSecuritySchemes();
    this.securitySchemes.set(list);
    if (list.length > 0 && !this.selectedSecurityScheme()) {
      await this.selectSecurityScheme(list[0].id);
    } else if (this.selectedSecurityScheme()) {
      await this.selectSecurityScheme(this.selectedSecurityScheme()!.id);
    }
  }

  async selectSecurityScheme(id: number): Promise<void> {
    try {
      const full = await this.api.getSecurityScheme(id);
      this.selectedSecurityScheme.set(full);
    } catch (err: any) {
      this.errorMsg.set(err?.message || 'Failed to load security scheme');
    }
  }

  async createSecurityScheme(): Promise<void> {
    const name = this.newSecSchemeName().trim();
    if (!name) return;
    try {
      const created = await this.api.createSecurityScheme({
        name,
        description: this.newSecSchemeDesc(),
      });
      this.showToast(`Issue Security Scheme "${name}" created!`);
      this.newSecSchemeName.set('');
      this.newSecSchemeDesc.set('');
      this.showNewSecSchemeModal.set(false);
      await this.loadSecuritySchemes();
      await this.selectSecurityScheme(created.id);
    } catch (err: any) {
      this.errorMsg.set(err?.message || 'Failed to create security scheme');
    }
  }

  async addSecurityLevel(): Promise<void> {
    const scheme = this.selectedSecurityScheme();
    if (!scheme) return;
    const name = this.newSecLevelName().trim();
    if (!name) return;
    try {
      await this.api.addSecurityLevel(scheme.id, {
        name,
        description: this.newSecLevelDesc(),
        is_default: this.newSecLevelIsDefault(),
      });
      this.showToast(`Security level "${name}" added!`);
      this.newSecLevelName.set('');
      this.newSecLevelDesc.set('');
      this.newSecLevelIsDefault.set(false);
      this.showNewSecLevelModal.set(false);
      await this.selectSecurityScheme(scheme.id);
    } catch (err: any) {
      this.errorMsg.set(err?.message || 'Failed to add security level');
    }
  }

  async deleteSecurityLevel(level: IssueSecurityLevel): Promise<void> {
    const scheme = this.selectedSecurityScheme();
    if (!scheme) return;
    if (!confirm(`Delete security level "${level.name}"? Associated issues will become unclassified.`)) return;
    try {
      await this.api.deleteSecurityLevel(scheme.id, level.id);
      this.showToast(`Deleted security level "${level.name}"`);
      await this.selectSecurityScheme(scheme.id);
    } catch (err: any) {
      this.errorMsg.set(err?.message || 'Failed to delete security level');
    }
  }

  openSecGrantModal(levelId: number): void {
    this.secGrantLevelId.set(levelId);
    this.showSecGrantModal.set(true);
  }

  async submitSecGrant(): Promise<void> {
    const scheme = this.selectedSecurityScheme();
    const levelId = this.secGrantLevelId();
    if (!scheme || !levelId) return;

    const type = this.secGrantType();
    let val = '';
    if (type === 'role') val = this.secGrantRole();
    else if (type === 'group') val = this.secGrantGroup();
    else if (type === 'dynamic') val = this.secGrantDynamic();
    else if (type === 'user') val = String(this.secGrantUserId() || '');
    else if (type === 'anyone') val = '*';

    if (!val) return;

    try {
      await this.api.addSecurityLevelGrant(scheme.id, levelId, {
        grantee_type: type,
        grantee_value: val,
      });
      this.showToast(`Added grant to security level!`);
      this.showSecGrantModal.set(false);
      await this.selectSecurityScheme(scheme.id);
    } catch (err: any) {
      this.errorMsg.set(err?.message || 'Failed to add security grant');
    }
  }

  async revokeSecGrant(levelId: number, grant: IssueSecurityGrant): Promise<void> {
    const scheme = this.selectedSecurityScheme();
    if (!scheme) return;
    try {
      await this.api.deleteSecurityLevelGrant(scheme.id, levelId, grant.id);
      this.showToast(`Revoked grant from security level`);
      await this.selectSecurityScheme(scheme.id);
    } catch (err: any) {
      this.errorMsg.set(err?.message || 'Failed to revoke security grant');
    }
  }

  // --- User Groups ---
  async loadUserGroups(): Promise<void> {
    const list = await this.api.getUserGroups();
    this.userGroups.set(list);
    if (list.length > 0 && !this.selectedGroup()) {
      await this.selectGroup(list[0].id);
    } else if (this.selectedGroup()) {
      await this.selectGroup(this.selectedGroup()!.id);
    }
  }

  async selectGroup(id: number): Promise<void> {
    try {
      const full = await this.api.getUserGroup(id);
      this.selectedGroup.set(full);
    } catch (err: any) {
      this.errorMsg.set(err?.message || 'Failed to load group details');
    }
  }

  async createGroup(): Promise<void> {
    const name = this.newGroupName().trim();
    if (!name) return;
    try {
      const created = await this.api.createUserGroup({
        name,
        description: this.newGroupDesc(),
      });
      this.showToast(`Group "${name}" created!`);
      this.newGroupName.set('');
      this.newGroupDesc.set('');
      this.showNewGroupModal.set(false);
      await this.loadUserGroups();
      await this.selectGroup(created.id);
    } catch (err: any) {
      this.errorMsg.set(err?.message || 'Failed to create group');
    }
  }

  async deleteGroup(group: UserGroup): Promise<void> {
    if (!confirm(`Delete group "${group.name}"?`)) return;
    try {
      await this.api.deleteUserGroup(group.id);
      this.showToast(`Deleted group "${group.name}".`);
      this.selectedGroup.set(null);
      await this.loadUserGroups();
    } catch (err: any) {
      this.errorMsg.set(err?.message || 'Failed to delete group');
    }
  }

  async addUserToGroup(): Promise<void> {
    const group = this.selectedGroup();
    const userId = this.addUserToGroupId();
    if (!group || !userId) return;
    try {
      await this.api.addUserToGroup(group.id, userId);
      this.showToast(`Added user to group "${group.name}"`);
      this.addUserToGroupId.set(null);
      await this.selectGroup(group.id);
      await this.loadUserGroups();
    } catch (err: any) {
      this.errorMsg.set(err?.message || 'Failed to add user to group');
    }
  }

  async removeUserFromGroup(userId: number): Promise<void> {
    const group = this.selectedGroup();
    if (!group) return;
    try {
      await this.api.removeUserFromGroup(group.id, userId);
      this.showToast(`Removed user from group`);
      await this.selectGroup(group.id);
      await this.loadUserGroups();
    } catch (err: any) {
      this.errorMsg.set(err?.message || 'Failed to remove user');
    }
  }

  // --- Project Schemes & Roles ---
  async loadProjectDetails(): Promise<void> {
    const projId = this.selectedProjectId();
    if (!projId) return;

    try {
      const [roles, perms] = await Promise.all([
        this.api.getProjectRoleMembers(projId),
        this.api.getMyPermissions(projId),
      ]);
      this.projectRoleMembers.set(roles);

      const projects = this.api.projects();
      const curProj = projects.find((p) => p.id === projId);
      if (curProj) {
        this.assignPermSchemeId.set((curProj as any).permission_scheme_id || 1);
        this.assignSecSchemeId.set((curProj as any).security_scheme_id || 1);
      }
    } catch (err: any) {
      // non-blocking
    }
  }

  async onProjectSelectChange(projId: number): Promise<void> {
    this.selectedProjectId.set(projId);
    await this.loadProjectDetails();
  }

  async saveProjectPermissionScheme(): Promise<void> {
    const projId = this.selectedProjectId();
    const schemeId = this.assignPermSchemeId();
    try {
      await this.api.setProjectPermissionScheme(projId, schemeId);
      this.showToast('Updated project permission scheme!');
      await this.loadPermissionSchemes();
    } catch (err: any) {
      this.errorMsg.set(err?.message || 'Failed to update project permission scheme');
    }
  }

  async saveProjectSecurityScheme(): Promise<void> {
    const projId = this.selectedProjectId();
    const schemeId = this.assignSecSchemeId();
    try {
      await this.api.setProjectSecurityScheme(projId, schemeId);
      this.showToast('Updated project security scheme!');
      await this.loadSecuritySchemes();
    } catch (err: any) {
      this.errorMsg.set(err?.message || 'Failed to update project security scheme');
    }
  }

  async addProjectRoleMember(): Promise<void> {
    const projId = this.selectedProjectId();
    const role = this.newRoleMemberName();
    const userId = this.newRoleMemberUserId();
    if (!userId || !role) return;

    try {
      await this.api.addProjectRoleMember(projId, role, userId);
      this.showToast(`Assigned role ${role}!`);
      this.newRoleMemberUserId.set(null);
      await this.loadProjectDetails();
    } catch (err: any) {
      this.errorMsg.set(err?.message || 'Failed to assign role');
    }
  }

  async deleteProjectRoleMember(m: ProjectRoleMember): Promise<void> {
    try {
      await this.api.deleteProjectRoleMember(m.project_id, m.role_name, m.user_id);
      this.showToast(`Removed role ${m.role_name}`);
      await this.loadProjectDetails();
    } catch (err: any) {
      this.errorMsg.set(err?.message || 'Failed to remove role');
    }
  }

  // --- Personal Access Tokens ---
  async loadTokens(): Promise<void> {
    try {
      const list = await this.api.getPersonalAccessTokens();
      this.tokens.set(list);
    } catch {
      // ignore
    }
  }

  async createToken(): Promise<void> {
    const name = this.newTokenName().trim();
    if (!name) return;
    try {
      const res = await this.api.createPersonalAccessToken({
        name,
        scopes: this.newTokenScopes(),
        expires_at: this.newTokenExpires() || undefined,
      });
      this.createdRawToken.set(res.token || '');
      this.showToast('Personal access token generated!');
      this.newTokenName.set('');
      this.newTokenExpires.set('');
      await this.loadTokens();
    } catch (err: any) {
      this.errorMsg.set(err?.message || 'Failed to generate token');
    }
  }

  async deleteToken(token: PersonalAccessToken): Promise<void> {
    if (!confirm(`Revoke token "${token.name}"?`)) return;
    try {
      await this.api.deletePersonalAccessToken(token.id);
      this.showToast(`Revoked token "${token.name}".`);
      await this.loadTokens();
    } catch (err: any) {
      this.errorMsg.set(err?.message || 'Failed to revoke token');
    }
  }

  copyTokenToClipboard(tok: string): void {
    navigator.clipboard.writeText(tok);
    this.showToast('Copied token to clipboard!');
  }
}
