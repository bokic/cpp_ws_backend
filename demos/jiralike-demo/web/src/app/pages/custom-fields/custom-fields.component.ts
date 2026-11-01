import { Component, OnInit, computed, inject, signal } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { RouterLink } from '@angular/router';
import { ApiService } from '../../services/api.service';
import {
  CustomFieldDef,
  CustomFieldType,
  FieldConfigurationItem,
  ScreenDef,
  ScreenScheme,
  ScreenTab,
  ScreenTabField,
} from '../../models/jira.models';

@Component({
  selector: 'app-custom-fields',
  standalone: true,
  imports: [CommonModule, FormsModule, RouterLink],
  template: `
    <div class="page-container">
      <div class="page-toolbar">
        <div>
          <h2>Custom Fields Engine &amp; Screen Designer</h2>
          <p class="text-muted">
            Define EAV custom fields, configure Required/Hidden field rules, design multi-tab Screens, and map Screen Schemes.
          </p>
        </div>
        <div class="toolbar-actions">
          <a routerLink="/project-settings" class="btn btn-secondary">Releases &amp; Components</a>
          <a routerLink="/board" class="btn btn-secondary">Back to Board</a>
        </div>
      </div>

      @if (toastMsg()) {
        <div class="toast-banner">{{ toastMsg() }}</div>
      }
      @if (errorMsg()) {
        <div class="alert alert-error">{{ errorMsg() }}</div>
      }

      <!-- Navigation Tabs -->
      <div class="tabs-bar" style="margin-bottom: 18px;">
        <button
          type="button"
          class="tab-btn"
          [class.active]="activeTab() === 'fields'"
          (click)="activeTab.set('fields')"
        >
          🧬 Custom Fields ({{ api.customFields().length }})
        </button>
        <button
          type="button"
          class="tab-btn"
          [class.active]="activeTab() === 'configurations'"
          (click)="activeTab.set('configurations')"
        >
          ⚙️ Field Configurations (Required / Hidden)
        </button>
        <button
          type="button"
          class="tab-btn"
          [class.active]="activeTab() === 'screens'"
          (click)="activeTab.set('screens')"
        >
          🖥 Screen Designer ({{ api.screens().length }})
        </button>
        <button
          type="button"
          class="tab-btn"
          [class.active]="activeTab() === 'schemes'"
          (click)="activeTab.set('schemes')"
        >
          🗺 Screen Schemes ({{ api.screenSchemes().length }})
        </button>
      </div>

      <!-- TAB 1: CUSTOM FIELDS CATALOG & CREATOR -->
      @if (activeTab() === 'fields') {
        <div class="detail-grid">
          <div class="detail-main">
            <div class="card-panel">
              <h3>Defined Custom Fields ({{ api.customFields().length }})</h3>
              <p class="text-muted" style="margin-bottom: 12px;">
                Normalized EAV custom fields stored in <code>custom_field_values</code> and batch-loaded onto work items.
              </p>

              <div class="subtask-list">
                @for (cf of api.customFields(); track cf.id) {
                  <div
                    class="subtask-row"
                    style="flex-direction: column; align-items: stretch; gap: 8px; padding: 12px;"
                  >
                    <div style="display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 8px;">
                      <div style="display: flex; align-items: center; gap: 8px; flex-wrap: wrap;">
                        <strong style="font-size: 1rem;">{{ cf.name }}</strong>
                        <code style="font-size: 0.78rem; background: #f1f5f9; padding: 2px 6px; border-radius: 4px;">
                          {{ cf.field_key }}
                        </code>
                        <span class="type-badge" data-type="Story">{{ cf.field_type }}</span>
                        @if (cf.is_required) {
                          <span class="cat-badge" data-cat="Bug">REQUIRED</span>
                        }
                        @if (cf.min_value !== null || cf.max_value !== null) {
                          <span class="state-pill">
                            Bounds: {{ cf.min_value ?? '-∞' }} .. {{ cf.max_value ?? '+∞' }}
                          </span>
                        }
                        @if (cf.formula_expr) {
                          <span class="link-type-chip">Formula: {{ cf.formula_expr }}</span>
                        }
                      </div>

                      <div style="display: flex; align-items: center; gap: 8px;">
                        <button
                          type="button"
                          class="btn btn-secondary btn-sm"
                          (click)="toggleFieldRequired(cf)"
                        >
                          {{ cf.is_required ? 'Mark Optional' : 'Mark Required' }}
                        </button>
                        <button
                          type="button"
                          class="btn btn-ghost btn-sm"
                          (click)="deleteCustomField(cf)"
                        >
                          🗑
                        </button>
                      </div>
                    </div>

                    <div class="text-muted" style="font-size: 0.83rem;">
                      {{ cf.description || 'No description' }}
                      @if (cf.default_value) {
                        · Default: <code>{{ cf.default_value }}</code>
                      }
                    </div>

                    @if (
                      cf.field_type === 'select_single' ||
                      cf.field_type === 'select_multi' ||
                      cf.field_type === 'cascading_select'
                    ) {
                      <div style="margin-top: 4px; display: flex; align-items: center; gap: 6px; flex-wrap: wrap;">
                        <small class="text-muted">Options:</small>
                        @for (opt of cf.options; track opt.id) {
                          <span
                            class="pill"
                            [style.borderColor]="opt.color"
                            style="display: inline-flex; align-items: center; gap: 4px; font-size: 0.76rem; padding: 2px 8px;"
                          >
                            <span
                              style="width: 8px; height: 8px; border-radius: 50%; display: inline-block;"
                              [style.background]="opt.color"
                            ></span>
                            {{ opt.value }}
                            <button
                              type="button"
                              style="border: none; background: transparent; cursor: pointer; font-weight: bold; color: #64748b;"
                              (click)="removeOption(opt.id)"
                            >
                              ×
                            </button>
                          </span>
                        }
                        <div style="display: inline-flex; gap: 4px; align-items: center;">
                          <input
                            type="text"
                            [placeholder]="'Add option to ' + cf.field_key + '...'"
                            [(ngModel)]="newOptionInputs[cf.id]"
                            [name]="'opt_in_' + cf.id"
                            style="padding: 3px 8px; font-size: 0.78rem; width: 150px;"
                            (keyup.enter)="addOption(cf.id)"
                          />
                          <button
                            type="button"
                            class="btn btn-secondary btn-sm"
                            style="padding: 2px 8px; font-size: 0.75rem;"
                            (click)="addOption(cf.id)"
                          >
                            + Add
                          </button>
                        </div>
                      </div>
                    }
                  </div>
                }
              </div>
            </div>
          </div>

          <!-- Create Custom Field Sidebar -->
          <div class="detail-sidebar">
            <form (ngSubmit)="createField()" class="card-panel">
              <h3>Create Custom Field</h3>

              <div class="form-group">
                <label for="cf-name">Field Name *</label>
                <input
                  type="text"
                  id="cf-name"
                  name="cf_name"
                  [(ngModel)]="newField.name"
                  (ngModelChange)="autoFillKey($event)"
                  required
                  placeholder="e.g., SLA Target Tier, Root Cause"
                />
              </div>

              <div class="form-group">
                <label for="cf-key">Field Key (snake_case) *</label>
                <input
                  type="text"
                  id="cf-key"
                  name="cf_key"
                  [(ngModel)]="newField.field_key"
                  required
                  placeholder="e.g., sla_target_tier"
                />
              </div>

              <div class="form-group">
                <label for="cf-type">Field Data Type *</label>
                <select id="cf-type" name="cf_type" [(ngModel)]="newField.field_type">
                  @for (ft of fieldTypes; track ft.value) {
                    <option [value]="ft.value">{{ ft.label }}</option>
                  }
                </select>
              </div>

              <div class="form-group">
                <label for="cf-desc">Description / Help Text</label>
                <textarea
                  id="cf-desc"
                  name="cf_desc"
                  [(ngModel)]="newField.description"
                  rows="2"
                  placeholder="Displayed below the input on screens..."
                ></textarea>
              </div>

              <div class="form-group">
                <label for="cf-def">Default Value</label>
                <input
                  type="text"
                  id="cf-def"
                  name="cf_def"
                  [(ngModel)]="newField.default_value"
                  placeholder="Optional default value on creation"
                />
              </div>

              @if (newField.field_type === 'number') {
                <div class="grid-2">
                  <div class="form-group">
                    <label for="cf-min">Min Value</label>
                    <input
                      type="number"
                      id="cf-min"
                      name="cf_min"
                      [(ngModel)]="newField.min_value"
                    />
                  </div>
                  <div class="form-group">
                    <label for="cf-max">Max Value</label>
                    <input
                      type="number"
                      id="cf-max"
                      name="cf_max"
                      [(ngModel)]="newField.max_value"
                    />
                  </div>
                </div>
              }

              @if (newField.field_type === 'formula') {
                <div class="form-group">
                  <label for="cf-formula">Formula Expression</label>
                  <input
                    type="text"
                    id="cf-formula"
                    name="cf_formula"
                    [(ngModel)]="newField.formula_expr"
                    placeholder="e.g., story_points * 4"
                  />
                </div>
              }

              @if (
                newField.field_type === 'select_single' ||
                newField.field_type === 'select_multi' ||
                newField.field_type === 'cascading_select'
              ) {
                <div class="form-group">
                  <label for="cf-opts">Initial Options (comma-separated)</label>
                  <input
                    type="text"
                    id="cf-opts"
                    name="cf_opts"
                    [(ngModel)]="newField.options_csv"
                    placeholder="Option A, Option B, Option C"
                  />
                </div>
              }

              <div class="form-group" style="display: flex; gap: 14px; align-items: center;">
                <label style="display: inline-flex; align-items: center; gap: 6px; cursor: pointer;">
                  <input type="checkbox" name="cf_req" [(ngModel)]="newField.is_required" />
                  Required Field
                </label>
                <label style="display: inline-flex; align-items: center; gap: 6px; cursor: pointer;">
                  <input
                    type="checkbox"
                    name="cf_screens"
                    [(ngModel)]="newField.add_to_default_screens"
                  />
                  Add to Default Screens
                </label>
              </div>

              <button type="submit" class="btn btn-primary" style="width: 100%;">
                + Create Custom Field
              </button>
            </form>
          </div>
        </div>
      }

      <!-- TAB 2: FIELD CONFIGURATIONS (REQUIRED / HIDDEN / RENDERERS) -->
      @if (activeTab() === 'configurations') {
        @if (activeConfig(); as cfg) {
          <div class="card-panel">
            <div class="section-header">
              <div>
                <h3>{{ cfg.name }}</h3>
                <p class="text-muted">{{ cfg.description }}</p>
              </div>
              <span class="state-pill" data-state="Done">Active Configuration</span>
            </div>

            <div class="subtask-list" style="margin-top: 12px;">
              @for (item of cfg.items; track item.custom_field_id) {
                <div
                  class="subtask-row"
                  style="justify-content: space-between; gap: 12px; flex-wrap: wrap;"
                >
                  <div style="flex: 1; min-width: 220px;">
                    <strong>{{ item.field_name }}</strong>
                    <code style="margin-left: 8px; font-size: 0.78rem;">{{ item.field_key }}</code>
                    <span class="type-badge" data-type="Task" style="margin-left: 8px;">
                      {{ item.field_type }}
                    </span>
                  </div>

                  <div style="display: flex; align-items: center; gap: 12px; flex-wrap: wrap;">
                    <label style="display: inline-flex; align-items: center; gap: 5px; font-size: 0.85rem;">
                      Renderer:
                      <select
                        [ngModel]="item.renderer"
                        (ngModelChange)="updateConfigItem(item, { renderer: $event })"
                        [name]="'rend_' + item.custom_field_id"
                        style="padding: 3px 8px; font-size: 0.82rem;"
                      >
                        <option value="plain">Plain Input</option>
                        <option value="markdown">Markdown / Rich Text</option>
                      </select>
                    </label>

                    <button
                      type="button"
                      class="btn btn-sm"
                      [class.btn-danger]="item.is_required"
                      [class.btn-secondary]="!item.is_required"
                      (click)="updateConfigItem(item, { is_required: !item.is_required })"
                    >
                      {{ item.is_required ? '★ Required' : 'Optional' }}
                    </button>

                    <button
                      type="button"
                      class="btn btn-sm"
                      [class.btn-primary]="!item.is_hidden"
                      [class.btn-secondary]="item.is_hidden"
                      (click)="updateConfigItem(item, { is_hidden: !item.is_hidden })"
                    >
                      {{ item.is_hidden ? '🙈 Hidden on Screens' : '👁 Visible on Screens' }}
                    </button>
                  </div>
                </div>
              }
            </div>
          </div>
        }
      }

      <!-- TAB 3: SCREEN DESIGNER -->
      @if (activeTab() === 'screens') {
        <div class="detail-grid">
          <div class="detail-main">
            <div class="card-panel">
              <div class="section-header" style="flex-wrap: wrap; gap: 10px;">
                <div>
                  <h3>Visual Screen Designer</h3>
                  <p class="text-muted">
                    Organize custom fields across tabs for Create, Edit, View, and Transition screens.
                  </p>
                </div>
                <select
                  id="designer-screen-select"
                  name="selected_screen_id"
                  [ngModel]="selectedScreenId()"
                  (ngModelChange)="onSelectScreen($event)"
                  style="min-width: 260px;"
                >
                  @for (sc of api.screens(); track sc.id) {
                    <option [ngValue]="sc.id">{{ sc.name }} ({{ sc.screen_type }})</option>
                  }
                </select>
              </div>

              @if (selectedScreen(); as screen) {
                <!-- Screen Tabs Bar -->
                <div
                  style="display: flex; align-items: center; justify-content: space-between; gap: 8px; flex-wrap: wrap; margin: 14px 0; border-bottom: 1px solid #e2e8f0; padding-bottom: 10px;"
                >
                  <div style="display: flex; gap: 6px; flex-wrap: wrap;">
                    @for (tab of screen.tabs; track tab.id) {
                      <button
                        type="button"
                        class="pill"
                        [class.active]="selectedTabId() === tab.id"
                        (click)="selectedTabId.set(tab.id)"
                      >
                        🗂 {{ tab.name }} ({{ tab.fields.length }})
                      </button>
                    }
                  </div>

                  <div style="display: flex; gap: 6px; align-items: center;">
                    <input
                      type="text"
                      placeholder="New tab name..."
                      [(ngModel)]="newTabName"
                      name="new_tab_name"
                      style="padding: 4px 8px; font-size: 0.82rem; width: 150px;"
                    />
                    <button type="button" class="btn btn-secondary btn-sm" (click)="addTabToScreen(screen.id)">
                      + Add Tab
                    </button>
                  </div>
                </div>

                @if (selectedTab(); as tab) {
                  <div
                    style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px;"
                  >
                    <h4>
                      Fields in Tab: <strong>{{ tab.name }}</strong> ({{ tab.fields.length }})
                    </h4>
                    @if (screen.tabs.length > 1) {
                      <button
                        type="button"
                        class="btn btn-ghost btn-sm"
                        (click)="deleteTab(tab.id)"
                      >
                        🗑 Delete Tab
                      </button>
                    }
                  </div>

                  @if (tab.fields.length === 0) {
                    <p class="text-muted">No fields placed on this tab yet. Add one below.</p>
                  } @else {
                    <div class="subtask-list">
                      @for (f of tab.fields; track f.id; let idx = $index) {
                        <div class="subtask-row" style="justify-content: space-between;">
                          <div style="display: flex; align-items: center; gap: 10px;">
                            <span class="state-pill">#{{ idx + 1 }}</span>
                            <strong>{{ f.name }}</strong>
                            <code>{{ f.field_key }}</code>
                            <span class="type-badge" data-type="Task">{{ f.field_type }}</span>
                            @if (f.is_required) {
                              <span class="cat-badge" data-cat="Bug">REQUIRED</span>
                            }
                            @if (f.is_hidden) {
                              <span class="state-pill">HIDDEN</span>
                            }
                          </div>

                          <div style="display: flex; align-items: center; gap: 6px;">
                            <button
                              type="button"
                              class="btn btn-ghost btn-sm"
                              [disabled]="idx === 0"
                              (click)="moveFieldOrder(tab, f, -15)"
                              title="Move Up"
                            >
                              ↑
                            </button>
                            <button
                              type="button"
                              class="btn btn-ghost btn-sm"
                              [disabled]="idx === tab.fields.length - 1"
                              (click)="moveFieldOrder(tab, f, 15)"
                              title="Move Down"
                            >
                              ↓
                            </button>
                            <button
                              type="button"
                              class="btn btn-ghost btn-sm"
                              (click)="removeFieldFromTab(f.id)"
                              title="Remove from Tab"
                            >
                              ×
                            </button>
                          </div>
                        </div>
                      }
                    </div>
                  }

                  <div class="quick-subtask-bar" style="margin-top: 14px;">
                    <select
                      name="field_to_add_id"
                      [(ngModel)]="fieldIdToAdd"
                      style="flex: 1;"
                    >
                      <option [ngValue]="0">Select custom field to place onto "{{ tab.name }}"...</option>
                      @for (cf of api.customFields(); track cf.id) {
                        <option [ngValue]="cf.id">
                          {{ cf.name }} ({{ cf.field_key }} — {{ cf.field_type }})
                        </option>
                      }
                    </select>
                    <button
                      type="button"
                      class="btn btn-primary"
                      (click)="addFieldToCurrentTab(tab)"
                    >
                      + Place Field on Tab
                    </button>
                  </div>
                }
              }
            </div>
          </div>

          <div class="detail-sidebar">
            <form (ngSubmit)="createNewScreen()" class="card-panel">
              <h3>Create New Screen</h3>
              <div class="form-group">
                <label for="sc-name">Screen Name *</label>
                <input
                  type="text"
                  id="sc-name"
                  name="sc_name"
                  [(ngModel)]="newScreen.name"
                  required
                  placeholder="e.g., Bug Triage Screen"
                />
              </div>
              <div class="form-group">
                <label for="sc-type">Screen Operation Type</label>
                <select id="sc-type" name="sc_type" [(ngModel)]="newScreen.screen_type">
                  <option value="create">Create Issue Screen</option>
                  <option value="edit">Edit / View Issue Screen</option>
                  <option value="transition">Transition Screen</option>
                  <option value="default">General Screen</option>
                </select>
              </div>
              <div class="form-group">
                <label for="sc-tabs">Initial Tabs (comma-separated)</label>
                <input
                  type="text"
                  id="sc-tabs"
                  name="sc_tabs"
                  [(ngModel)]="newScreen.tabs_csv"
                  placeholder="General, QA & Testing"
                />
              </div>
              <div class="form-group">
                <label for="sc-desc">Description</label>
                <textarea
                  id="sc-desc"
                  name="sc_desc"
                  [(ngModel)]="newScreen.description"
                  rows="2"
                ></textarea>
              </div>
              <button type="submit" class="btn btn-primary" style="width: 100%;">
                + Create Screen
              </button>
            </form>
          </div>
        </div>
      }

      <!-- TAB 4: SCREEN SCHEMES -->
      @if (activeTab() === 'schemes') {
        <div class="card-panel">
          <h3>Screen Schemes (Operation-to-Screen Mapping)</h3>
          <p class="text-muted" style="margin-bottom: 14px;">
            Map Create Issue, Edit Issue, View Issue, and Workflow Transition operations to specific tabbed Screens.
          </p>

          <div class="subtask-list">
            @for (ss of api.screenSchemes(); track ss.id) {
              <div
                class="subtask-row"
                style="flex-direction: column; align-items: stretch; gap: 12px; padding: 14px;"
              >
                <div style="display: flex; justify-content: space-between; align-items: center;">
                  <div>
                    <strong style="font-size: 1.02rem;">{{ ss.name }}</strong>
                    <div class="text-muted" style="font-size: 0.83rem;">{{ ss.description }}</div>
                  </div>
                  @if (ss.is_default) {
                    <span class="state-pill" data-state="Done">DEFAULT SCHEME</span>
                  }
                </div>

                <div class="grid-4">
                  <div class="form-group">
                    <label>Create Issue Operation</label>
                    <select
                      [(ngModel)]="ss.create_screen_id"
                      [name]="'ss_create_' + ss.id"
                      (change)="saveScheme(ss)"
                    >
                      @for (sc of api.screens(); track sc.id) {
                        <option [ngValue]="sc.id">{{ sc.name }}</option>
                      }
                    </select>
                  </div>

                  <div class="form-group">
                    <label>Edit Issue Operation</label>
                    <select
                      [(ngModel)]="ss.edit_screen_id"
                      [name]="'ss_edit_' + ss.id"
                      (change)="saveScheme(ss)"
                    >
                      @for (sc of api.screens(); track sc.id) {
                        <option [ngValue]="sc.id">{{ sc.name }}</option>
                      }
                    </select>
                  </div>

                  <div class="form-group">
                    <label>View Issue Operation</label>
                    <select
                      [(ngModel)]="ss.view_screen_id"
                      [name]="'ss_view_' + ss.id"
                      (change)="saveScheme(ss)"
                    >
                      @for (sc of api.screens(); track sc.id) {
                        <option [ngValue]="sc.id">{{ sc.name }}</option>
                      }
                    </select>
                  </div>

                  <div class="form-group">
                    <label>Workflow Transition Operation</label>
                    <select
                      [(ngModel)]="ss.transition_screen_id"
                      [name]="'ss_trans_' + ss.id"
                      (change)="saveScheme(ss)"
                    >
                      @for (sc of api.screens(); track sc.id) {
                        <option [ngValue]="sc.id">{{ sc.name }}</option>
                      }
                    </select>
                  </div>
                </div>
              </div>
            }
          </div>
        </div>
      }
    </div>
  `,
})
export class CustomFieldsComponent implements OnInit {
  readonly api = inject(ApiService);

  readonly activeTab = signal<'fields' | 'configurations' | 'screens' | 'schemes'>('fields');
  readonly toastMsg = signal('');
  readonly errorMsg = signal('');

  readonly selectedScreenId = signal<number>(1);
  readonly selectedTabId = signal<number>(1);

  readonly activeConfig = computed(() => {
    const list = this.api.fieldConfigurations();
    return list.length > 0 ? list[0] : null;
  });

  readonly selectedScreen = computed<ScreenDef | null>(() => {
    const list = this.api.screens();
    return list.find((s) => s.id === this.selectedScreenId()) || (list.length > 0 ? list[0] : null);
  });

  readonly selectedTab = computed<ScreenTab | null>(() => {
    const sc = this.selectedScreen();
    if (!sc || sc.tabs.length === 0) return null;
    return sc.tabs.find((t) => t.id === this.selectedTabId()) || sc.tabs[0];
  });

  readonly fieldTypes: { value: CustomFieldType; label: string }[] = [
    { value: 'short_text', label: 'Short Text (Single-line)' },
    { value: 'long_text', label: 'Long Text (Markdown / Multi-line)' },
    { value: 'number', label: 'Number (Numeric with Min/Max bounds)' },
    { value: 'date', label: 'Date (YYYY-MM-DD)' },
    { value: 'datetime', label: 'Date & Time (Timestamp)' },
    { value: 'select_single', label: 'Select List (Single choice dropdown)' },
    { value: 'select_multi', label: 'Multi-Select (Multiple choice pills)' },
    { value: 'user_picker_single', label: 'User Picker (Single user)' },
    { value: 'url', label: 'URL (Validated web link)' },
    { value: 'checkbox', label: 'Checkbox (Boolean toggle)' },
    { value: 'formula', label: 'Formula (Calculated expression)' },
  ];

  newOptionInputs: Record<number, string> = {};
  newTabName = '';
  fieldIdToAdd = 0;

  newField = {
    name: '',
    field_key: '',
    field_type: 'short_text' as CustomFieldType,
    description: '',
    default_value: '',
    is_required: false,
    min_value: null as number | null,
    max_value: null as number | null,
    formula_expr: '',
    options_csv: '',
    add_to_default_screens: true,
  };

  newScreen = {
    name: '',
    description: '',
    screen_type: 'edit',
    tabs_csv: 'General, Technical Details',
  };

  async ngOnInit(): Promise<void> {
    await this.api.loadReferenceData();
    const screens = this.api.screens();
    if (screens.length > 0) {
      this.selectedScreenId.set(screens[0].id);
      if (screens[0].tabs.length > 0) {
        this.selectedTabId.set(screens[0].tabs[0].id);
      }
    }
  }

  autoFillKey(name: string): void {
    this.newField.field_key = name
      .toLowerCase()
      .replace(/[^a-z0-9]+/g, '_')
      .replace(/^_+|_+$/g, '');
  }

  async createField(): Promise<void> {
    this.errorMsg.set('');
    if (!this.newField.name.trim()) return;
    try {
      const opts = this.newField.options_csv
        .split(',')
        .map((s) => s.trim())
        .filter((s) => s.length > 0);
      await this.api.createCustomField({
        name: this.newField.name.trim(),
        field_key: this.newField.field_key.trim(),
        field_type: this.newField.field_type,
        description: this.newField.description.trim(),
        default_value: this.newField.default_value.trim(),
        is_required: this.newField.is_required,
        min_value: this.newField.min_value,
        max_value: this.newField.max_value,
        formula_expr: this.newField.formula_expr.trim(),
        options: opts,
        add_to_default_screens: this.newField.add_to_default_screens,
      });
      this.newField = {
        name: '',
        field_key: '',
        field_type: 'short_text',
        description: '',
        default_value: '',
        is_required: false,
        min_value: null,
        max_value: null,
        formula_expr: '',
        options_csv: '',
        add_to_default_screens: true,
      };
      this.showToast('Custom field created and registered on screens');
    } catch (err: any) {
      this.errorMsg.set(err?.message || 'Failed to create custom field');
    }
  }

  async toggleFieldRequired(cf: CustomFieldDef): Promise<void> {
    await this.api.updateCustomField(cf.id, { is_required: !cf.is_required });
    this.showToast(`${cf.name} marked as ${!cf.is_required ? 'Required' : 'Optional'}`);
  }

  async deleteCustomField(cf: CustomFieldDef): Promise<void> {
    await this.api.deleteCustomField(cf.id);
    this.showToast(`Deleted custom field '${cf.name}'`);
  }

  async addOption(fieldId: number): Promise<void> {
    const val = (this.newOptionInputs[fieldId] || '').trim();
    if (!val) return;
    await this.api.addCustomFieldOption(fieldId, { value: val });
    this.newOptionInputs[fieldId] = '';
    this.showToast(`Added option '${val}'`);
  }

  async removeOption(optionId: number): Promise<void> {
    await this.api.deleteCustomFieldOption(optionId);
    this.showToast('Option removed');
  }

  async updateConfigItem(
    item: FieldConfigurationItem,
    patch: Partial<FieldConfigurationItem>
  ): Promise<void> {
    await this.api.updateFieldConfigurationItem({
      configuration_id: item.configuration_id,
      custom_field_id: item.custom_field_id,
      issue_type: item.issue_type || '*',
      is_required: patch.is_required ?? item.is_required,
      is_hidden: patch.is_hidden ?? item.is_hidden,
      renderer: (patch.renderer ?? item.renderer) as 'plain' | 'markdown',
      default_value: item.default_value,
      description_override: item.description_override,
    });
    this.showToast(`Updated configuration for ${item.field_name}`);
  }

  onSelectScreen(screenId: number): void {
    this.selectedScreenId.set(screenId);
    const sc = this.api.screens().find((s) => s.id === screenId);
    if (sc && sc.tabs.length > 0) {
      this.selectedTabId.set(sc.tabs[0].id);
    }
  }

  async addTabToScreen(screenId: number): Promise<void> {
    if (!this.newTabName.trim()) return;
    const t = await this.api.addScreenTab(screenId, {
      name: this.newTabName.trim(),
      sort_order: 50,
    });
    this.newTabName = '';
    this.selectedTabId.set(t.id);
    this.showToast(`Added tab '${t.name}'`);
  }

  async deleteTab(tabId: number): Promise<void> {
    await this.api.deleteScreenTab(tabId);
    const sc = this.selectedScreen();
    if (sc && sc.tabs.length > 0) {
      this.selectedTabId.set(sc.tabs[0].id);
    }
    this.showToast('Deleted screen tab');
  }

  async addFieldToCurrentTab(tab: ScreenTab): Promise<void> {
    if (this.fieldIdToAdd <= 0) return;
    await this.api.addScreenTabField(tab.id, {
      custom_field_id: this.fieldIdToAdd,
      sort_order: (tab.fields.length + 1) * 10,
    });
    this.fieldIdToAdd = 0;
    this.showToast(`Added field to '${tab.name}'`);
  }

  async moveFieldOrder(tab: ScreenTab, field: ScreenTabField, delta: number): Promise<void> {
    await this.api.addScreenTabField(tab.id, {
      custom_field_id: field.custom_field_id || undefined,
      field_key: field.field_key,
      sort_order: Math.max(1, field.sort_order + delta),
    });
    this.showToast(`Reordered '${field.name}'`);
  }

  async removeFieldFromTab(tabFieldId: number): Promise<void> {
    await this.api.removeScreenTabField(tabFieldId);
    this.showToast('Field removed from tab');
  }

  async createNewScreen(): Promise<void> {
    if (!this.newScreen.name.trim()) return;
    const tabs = this.newScreen.tabs_csv
      .split(',')
      .map((s) => s.trim())
      .filter((s) => s.length > 0);
    const sc = await this.api.createScreen({
      name: this.newScreen.name.trim(),
      description: this.newScreen.description.trim(),
      screen_type: this.newScreen.screen_type,
      tab_names: tabs,
    });
    this.newScreen = {
      name: '',
      description: '',
      screen_type: 'edit',
      tabs_csv: 'General, Technical Details',
    };
    this.onSelectScreen(sc.id);
    this.showToast(`Created screen '${sc.name}'`);
  }

  async saveScheme(ss: ScreenScheme): Promise<void> {
    await this.api.saveScreenScheme({
      id: ss.id,
      name: ss.name,
      description: ss.description,
      project_id: ss.project_id,
      is_default: ss.is_default,
      create_screen_id: ss.create_screen_id,
      edit_screen_id: ss.edit_screen_id,
      view_screen_id: ss.view_screen_id,
      transition_screen_id: ss.transition_screen_id,
    });
    this.showToast(`Updated Screen Scheme '${ss.name}'`);
  }

  private showToast(msg: string): void {
    this.toastMsg.set(msg);
    setTimeout(() => {
      if (this.toastMsg() === msg) this.toastMsg.set('');
    }, 3000);
  }
}
