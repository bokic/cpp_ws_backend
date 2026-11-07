import { Component, OnInit, signal, computed } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { ApiService } from '../../services/api.service';
import { AdminTableDescriptor, AdminTableData, AdminColumnMeta } from '../../models/jira.models';

@Component({
  selector: 'app-database-admin',
  standalone: true,
  imports: [CommonModule, FormsModule],
  templateUrl: './database-admin.component.html',
  styleUrls: ['./database-admin.component.css']
})
export class DatabaseAdminComponent implements OnInit {
  tables = signal<AdminTableDescriptor[]>([]);
  selectedTable = signal<string>('');
  tableSearch = signal<string>('');

  currentTableData = signal<AdminTableData | null>(null);
  loading = signal<boolean>(false);
  rowSearch = signal<string>('');
  currentPage = signal<number>(1);
  pageSize = signal<number>(50);
  sortBy = signal<string>('');
  sortDir = signal<string>('ASC');

  // Modals
  showAddModal = signal<boolean>(false);
  showEditModal = signal<boolean>(false);
  showDeleteModal = signal<boolean>(false);

  formData: Record<string, any> = {};
  selectedRow: Record<string, any> | null = null;
  errorMessage = signal<string>('');
  successMessage = signal<string>('');

  filteredTables = computed(() => {
    const q = this.tableSearch().toLowerCase().trim();
    if (!q) return this.tables();
    return this.tables().filter(t => t.name.toLowerCase().includes(q));
  });

  currentColumns = computed<AdminColumnMeta[]>(() => {
    const data = this.currentTableData();
    if (data && data.columns && data.columns.length > 0) return data.columns;
    const curName = this.selectedTable();
    const desc = this.tables().find(t => t.name === curName);
    return desc ? desc.columns : [];
  });

  totalPages = computed(() => {
    const total = this.currentTableData()?.total || 0;
    const ps = this.pageSize();
    return Math.max(1, Math.ceil(total / ps));
  });

  constructor(public api: ApiService) {}

  async ngOnInit() {
    await this.loadTables();
  }

  async loadTables() {
    try {
      this.loading.set(true);
      const list = await this.api.getAdminTables();
      this.tables.set(list);
      if (list.length > 0 && !this.selectedTable()) {
        const preferred = list.find(t => t.name === 'work_items') || list[0];
        await this.selectTable(preferred.name);
      }
    } catch (err: any) {
      this.errorMessage.set(err?.message || 'Failed to load database tables');
    } finally {
      this.loading.set(false);
    }
  }

  async selectTable(tableName: string) {
    this.selectedTable.set(tableName);
    this.currentPage.set(1);
    this.rowSearch.set('');
    this.sortBy.set('');
    this.sortDir.set('ASC');
    await this.loadRows();
  }

  async loadRows() {
    const t = this.selectedTable();
    if (!t) return;
    try {
      this.loading.set(true);
      this.errorMessage.set('');
      const data = await this.api.getAdminTableData(t, {
        page: this.currentPage(),
        pageSize: this.pageSize(),
        search: this.rowSearch(),
        sortBy: this.sortBy(),
        sortDir: this.sortDir(),
      });
      this.currentTableData.set(data);
    } catch (err: any) {
      this.errorMessage.set(err?.message || `Failed to query table ${t}`);
    } finally {
      this.loading.set(false);
    }
  }

  onSort(colName: string) {
    if (this.sortBy() === colName) {
      this.sortDir.set(this.sortDir() === 'ASC' ? 'DESC' : 'ASC');
    } else {
      this.sortBy.set(colName);
      this.sortDir.set('ASC');
    }
    this.loadRows();
  }

  onPageChange(page: number) {
    if (page < 1 || page > this.totalPages()) return;
    this.currentPage.set(page);
    this.loadRows();
  }

  // --- Add Record ---
  openAddModal() {
    this.formData = {};
    const cols = this.currentColumns();
    for (const c of cols) {
      if (c.default_value && c.default_value !== 'NULL') {
        this.formData[c.name] = c.default_value;
      } else {
        this.formData[c.name] = '';
      }
    }
    this.showAddModal.set(true);
  }

  async saveNewRecord() {
    const t = this.selectedTable();
    if (!t) return;
    try {
      this.loading.set(true);
      // Clean payload: if empty string on non-required number, convert to null
      const payload: Record<string, any> = {};
      const cols = this.currentColumns();
      for (const c of cols) {
        const val = this.formData[c.name];
        if (val !== undefined && val !== '') {
          const upperType = (c.type || '').toUpperCase();
          if (upperType.includes('INT') || upperType.includes('REAL') || upperType.includes('FLOAT') || upperType.includes('NUM')) {
            const num = Number(val);
            payload[c.name] = isNaN(num) ? val : num;
          } else {
            payload[c.name] = val;
          }
        }
      }

      await this.api.insertAdminTableRow(t, payload);
      this.showAddModal.set(false);
      this.showSuccess(`Row successfully inserted into ${t}`);
      await this.loadRows();
      await this.refreshTableCounts();
    } catch (err: any) {
      this.errorMessage.set(err?.message || 'Failed to insert row');
    } finally {
      this.loading.set(false);
    }
  }

  // --- Edit Record ---
  openEditModal(row: Record<string, any>) {
    this.selectedRow = row;
    this.formData = { ...row };
    this.showEditModal.set(true);
  }

  async saveEditedRecord() {
    const t = this.selectedTable();
    if (!t || !this.selectedRow) return;
    try {
      this.loading.set(true);
      const cols = this.currentColumns();
      const pkCols = cols.filter(c => c.pk > 0);
      const keys: Record<string, any> = {};

      if (pkCols.length > 0) {
        for (const pk of pkCols) {
          keys[pk.name] = this.selectedRow[pk.name];
        }
      } else if (this.selectedRow['id'] !== undefined) {
        keys['id'] = this.selectedRow['id'];
      } else {
        // fallback to full row match
        for (const c of cols) {
          keys[c.name] = this.selectedRow[c.name];
        }
      }

      const values: Record<string, any> = {};
      for (const c of cols) {
        const val = this.formData[c.name];
        const upperType = (c.type || '').toUpperCase();
        if (val === null || val === '') {
          values[c.name] = c.notnull ? '' : null;
        } else if (upperType.includes('INT') || upperType.includes('REAL') || upperType.includes('FLOAT') || upperType.includes('NUM')) {
          const num = Number(val);
          values[c.name] = isNaN(num) ? val : num;
        } else {
          values[c.name] = val;
        }
      }

      await this.api.updateAdminTableRow(t, { keys, values });
      this.showEditModal.set(false);
      this.showSuccess(`Row in ${t} successfully updated`);
      await this.loadRows();
    } catch (err: any) {
      this.errorMessage.set(err?.message || 'Failed to update row');
    } finally {
      this.loading.set(false);
    }
  }

  // --- Delete Record ---
  openDeleteModal(row: Record<string, any>) {
    this.selectedRow = row;
    this.showDeleteModal.set(true);
  }

  async confirmDelete() {
    const t = this.selectedTable();
    if (!t || !this.selectedRow) return;
    try {
      this.loading.set(true);
      const cols = this.currentColumns();
      const pkCols = cols.filter(c => c.pk > 0);
      const keys: Record<string, any> = {};

      if (pkCols.length > 0) {
        for (const pk of pkCols) {
          keys[pk.name] = this.selectedRow[pk.name];
        }
      } else if (this.selectedRow['id'] !== undefined) {
        keys['id'] = this.selectedRow['id'];
      } else {
        for (const c of cols) {
          keys[c.name] = this.selectedRow[c.name];
        }
      }

      await this.api.deleteAdminTableRow(t, keys);
      this.showDeleteModal.set(false);
      this.showSuccess(`Row successfully deleted from ${t}`);
      await this.loadRows();
      await this.refreshTableCounts();
    } catch (err: any) {
      this.errorMessage.set(err?.message || 'Failed to delete row');
    } finally {
      this.loading.set(false);
    }
  }

  async refreshTableCounts() {
    try {
      const list = await this.api.getAdminTables();
      this.tables.set(list);
    } catch {}
  }

  showSuccess(msg: string) {
    this.successMessage.set(msg);
    setTimeout(() => this.successMessage.set(''), 4000);
  }

  exportJson() {
    const data = this.currentTableData();
    if (!data) return;
    const blob = new Blob([JSON.stringify(data.rows, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `${this.selectedTable()}_export.json`;
    a.click();
    URL.revokeObjectURL(url);
  }

  isPrimaryKey(colName: string): boolean {
    const col = this.currentColumns().find(c => c.name === colName);
    return !!col && col.pk > 0;
  }
}
