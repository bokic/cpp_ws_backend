import { Component, OnInit, signal, computed } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { ApiService } from '../../services/api.service';
import { User } from '../../models/jira.models';

@Component({
  selector: 'app-users-admin',
  standalone: true,
  imports: [CommonModule, FormsModule],
  templateUrl: './users-admin.component.html',
  styleUrls: ['./users-admin.component.css']
})
export class UsersAdminComponent implements OnInit {
  users = signal<User[]>([]);
  loading = signal<boolean>(false);
  searchTerm = signal<string>('');
  roleFilter = signal<string>('ALL');

  // Modals
  showCreateModal = signal<boolean>(false);
  showEditModal = signal<boolean>(false);
  showDeleteModal = signal<boolean>(false);

  selectedUser: User | null = null;
  errorMessage = signal<string>('');
  successMessage = signal<string>('');

  createForm = {
    username: '',
    display_name: '',
    email: '',
    role: 'Developer',
    password: '',
    avatar_color: '#0052cc'
  };

  editForm = {
    display_name: '',
    email: '',
    role: 'Developer',
    password: '',
    avatar_color: '#0052cc'
  };

  avatarColors = ['#0052cc', '#00875a', '#ff991f', '#de350b', '#6554c0', '#00b8d9', '#42526e'];

  filteredUsers = computed(() => {
    const q = this.searchTerm().toLowerCase().trim();
    const rf = this.roleFilter();
    return this.users().filter(u => {
      const matchSearch = !q || 
        u.display_name.toLowerCase().includes(q) || 
        u.username.toLowerCase().includes(q) || 
        u.email.toLowerCase().includes(q);
      const matchRole = rf === 'ALL' || u.role === rf;
      return matchSearch && matchRole;
    });
  });

  constructor(public api: ApiService) {}

  async ngOnInit() {
    await this.loadUsers();
  }

  async loadUsers() {
    try {
      this.loading.set(true);
      const list = await this.api.getUsers();
      this.users.set(list);
    } catch (err: any) {
      this.errorMessage.set(err?.message || 'Failed to load users');
    } finally {
      this.loading.set(false);
    }
  }

  openCreateModal() {
    this.createForm = {
      username: '',
      display_name: '',
      email: '',
      role: 'Developer',
      password: '',
      avatar_color: '#0052cc'
    };
    this.showCreateModal.set(true);
  }

  async saveNewUser() {
    if (!this.createForm.username || !this.createForm.display_name) {
      this.errorMessage.set('Username and Display Name are required');
      return;
    }
    try {
      this.loading.set(true);
      await this.api.createUser(this.createForm);
      this.showCreateModal.set(false);
      this.showSuccess(`User @${this.createForm.username} created successfully`);
      await this.loadUsers();
    } catch (err: any) {
      this.errorMessage.set(err?.message || 'Failed to create user');
    } finally {
      this.loading.set(false);
    }
  }

  openEditModal(u: User) {
    this.selectedUser = u;
    this.editForm = {
      display_name: u.display_name,
      email: u.email,
      role: u.role,
      password: '',
      avatar_color: u.avatar_color || '#0052cc'
    };
    this.showEditModal.set(true);
  }

  async saveEditedUser() {
    if (!this.selectedUser) return;
    try {
      this.loading.set(true);
      await this.api.updateUser(this.selectedUser.id, this.editForm);
      this.showEditModal.set(false);
      this.showSuccess(`User @${this.selectedUser.username} updated successfully`);
      await this.loadUsers();
    } catch (err: any) {
      this.errorMessage.set(err?.message || 'Failed to update user');
    } finally {
      this.loading.set(false);
    }
  }

  openDeleteModal(u: User) {
    this.selectedUser = u;
    this.showDeleteModal.set(true);
  }

  async confirmDeleteUser() {
    if (!this.selectedUser) return;
    try {
      this.loading.set(true);
      await this.api.deleteUser(this.selectedUser.id);
      this.showDeleteModal.set(false);
      this.showSuccess(`User @${this.selectedUser.username} deleted successfully`);
      await this.loadUsers();
    } catch (err: any) {
      this.errorMessage.set(err?.message || 'Failed to delete user');
    } finally {
      this.loading.set(false);
    }
  }

  showSuccess(msg: string) {
    this.successMessage.set(msg);
    setTimeout(() => this.successMessage.set(''), 4000);
  }
}
