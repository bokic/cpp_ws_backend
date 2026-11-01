import { Component, OnInit, inject, signal } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { ActivatedRoute, Router, RouterLink } from '@angular/router';
import { ApiService } from '../../services/api.service';
import { RichEditorComponent } from '../../components/rich-editor/rich-editor.component';
import { MarkdownViewerComponent } from '../../components/rich-editor/markdown-viewer.component';
import {
  AvailableTransition,
  PRIORITIES,
  ScreenDef,
  ScreenTab,
  TimeRollup,
  WORKFLOW_STATES,
  WORK_ITEM_CATEGORIES,
  WORK_ITEM_TYPES,
  WorkItem,
  WorkItemDetail,
  WorkflowState,
  WorklogAdjustmentMode,
  WorklogItem,
} from '../../models/jira.models';

@Component({
  selector: 'app-item-detail',
  standalone: true,
  imports: [CommonModule, FormsModule, RouterLink, RichEditorComponent, MarkdownViewerComponent],
  template: `
    @if (item(); as it) {
      <div class="page-container">
        <!-- Top Breadcrumb & Action Bar -->
        <div class="detail-top-bar">
          <div class="breadcrumb">
            <a routerLink="/board">Board</a>
            <span>/</span>
            @if (it.parent_key) {
              <a [routerLink]="['/', it.parent_key]" class="parent-link">
                {{ it.parent_key }}: {{ it.parent_title }}
              </a>
              <span>/</span>
            }
            <span class="type-badge" [attr.data-type]="it.type">{{ it.type }}</span>
            <span class="cat-badge" [attr.data-cat]="it.category">{{ it.category }}</span>
            <strong class="issue-key-lg">{{ it.key }}</strong>
          </div>
          <div class="toolbar-actions">
            <a
              routerLink="/create"
              [queryParams]="{ type: 'Subtask', parent_id: it.id }"
              class="btn btn-secondary btn-sm"
            >
              + Create Subtask
            </a>
            <button type="button" class="btn btn-danger btn-sm" (click)="deleteCurrentItem()">
              Delete
            </button>
          </div>
        </div>

        @if (toastMsg()) {
          <div class="toast-banner">{{ toastMsg() }}</div>
        }

        <!-- Interactive Jira Directed Workflow State & Transition Bar -->
        <div class="workflow-pipeline-bar" style="flex-direction: column; align-items: stretch; gap: 10px;">
          <div style="display: flex; align-items: center; justify-content: space-between; flex-wrap: wrap; gap: 8px;">
            <div style="display: flex; align-items: center; gap: 10px; flex-wrap: wrap;">
              <span class="pipeline-label">Current Status:</span>
              <span class="state-pill" [attr.data-state]="it.status" style="font-size: 13px; padding: 4px 12px;">
                {{ it.status }}
              </span>
              @if (it.workflow; as wf) {
                <span class="badge badge-info" title="Active Directed Workflow">
                  🔀 {{ wf.workflow_name }} ({{ wf.current_category }})
                </span>
              }
              @if (it.resolution_name) {
                <span class="badge" style="background: #dcfce7; color: #166534;">
                  ✔ Resolution: {{ it.resolution_name }}
                </span>
              }
            </div>
            <a routerLink="/workflows" class="btn btn-ghost btn-sm">
              ⚙ Edit Workflow Graph →
            </a>
          </div>

          <!-- Dynamic Available Next Transitions from Transition Guard -->
          @if (it.workflow && it.workflow.transitions.length > 0) {
            <div style="display: flex; align-items: center; gap: 8px; flex-wrap: wrap;">
              <span class="text-muted" style="font-size: 12px; font-weight: 600;">
                Available Transitions:
              </span>
              @for (tr of it.workflow.transitions; track tr.id) {
                @if (tr.allowed) {
                  <button
                    type="button"
                    class="pipeline-step"
                    [attr.data-state]="tr.to_status"
                    (click)="triggerWorkflowTransition(tr)"
                    [title]="'Transition to ' + tr.to_status"
                  >
                    <span class="state-dot" [attr.data-state]="tr.to_status"></span>
                    <strong>{{ tr.name }}</strong>
                    <span class="text-muted" style="font-size: 11px;">→ {{ tr.to_status }}</span>
                    @if (tr.requires_screen) {
                      <span title="Opens Transition Screen Modal" style="margin-left: 3px;">🗔</span>
                    }
                  </button>
                } @else {
                  <button
                    type="button"
                    class="pipeline-step"
                    style="opacity: 0.6; cursor: not-allowed; border-style: dashed;"
                    [title]="tr.blocked_reasons.join('; ')"
                    (click)="showToast(tr.blocked_reasons[0] || 'Transition blocked by workflow condition')"
                  >
                    🔒 {{ tr.name }} → {{ tr.to_status }}
                  </button>
                }
              }
            </div>
          } @else {
            <div class="pipeline-steps">
              @for (st of states; track st) {
                <button
                  type="button"
                  class="pipeline-step"
                  [attr.data-state]="st"
                  [class.active]="it.status === st"
                  (click)="transitionState(st)"
                >
                  <span class="state-dot" [attr.data-state]="st"></span>
                  {{ st }}
                </button>
              }
            </div>
          }
        </div>

        <!-- Transition Screen Modal -->
        @if (activeTransitionModal(); as tr) {
          <div
            style="position: fixed; inset: 0; background: rgba(15, 23, 42, 0.55); z-index: 1200; display: flex; align-items: center; justify-content: center; padding: 16px;"
          >
            <div
              class="card-panel"
              style="width: 100%; max-width: 640px; max-height: 90vh; overflow-y: auto; box-shadow: 0 20px 45px rgba(0,0,0,0.25);"
            >
              <div class="section-header">
                <div>
                  <h3>🗔 Transition Issue: {{ tr.name }}</h3>
                  <small class="text-muted">
                    <code>{{ it.key }}</code>:
                    <span class="state-pill" [attr.data-state]="it.status">{{ it.status }}</span>
                    →
                    <span class="state-pill" [attr.data-state]="tr.to_status">{{ tr.to_status }}</span>
                    @if (tr.screen_name) {
                      · Screen: <strong>{{ tr.screen_name }}</strong>
                    }
                  </small>
                </div>
                <button type="button" class="btn btn-ghost btn-sm" (click)="closeTransitionModal()">✕</button>
              </div>

              @if (transitionModalError()) {
                <div
                  style="margin: 10px 0; padding: 10px 12px; background: #fef2f2; border: 1px solid #fecaca; color: #b91c1c; border-radius: 6px; font-size: 13px;"
                >
                  ⚠ {{ transitionModalError() }}
                </div>
              }

              <!-- Active Validators & Post-Functions Summary -->
              @if (tr.validators.length > 0 || tr.post_functions.length > 0) {
                <div
                  style="margin: 12px 0; padding: 10px 12px; background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 6px; font-size: 12px;"
                >
                  @if (tr.validators.length > 0) {
                    <div style="margin-bottom: 4px;">
                      <strong>✔ Active Validators:</strong>
                      @for (v of tr.validators; track v.id) {
                        <span class="badge" style="margin-left: 6px; background: #fef3c7; color: #92400e;">
                          {{ v.rule_type }}{{ v.field_name ? ': ' + v.field_name : '' }}
                        </span>
                      }
                    </div>
                  }
                  @if (tr.post_functions.length > 0) {
                    <div>
                      <strong>⚡ Post-Functions to Execute:</strong>
                      @for (pf of tr.post_functions; track pf.id) {
                        <span class="badge" style="margin-left: 6px; background: #dcfce7; color: #166534;">
                          {{ pf.rule_type }}({{ pf.field_name }}{{ pf.config_value ? '=' + pf.config_value : '' }})
                        </span>
                      }
                    </div>
                  }
                </div>
              }

              <!-- Resolution Field (shown when transitioning to Done or when resolution_id validator exists) -->
              @if (tr.to_status === 'Done' || hasValidatorForField(tr, 'resolution_id')) {
                <div class="form-group">
                  <label>
                    Resolution
                    <span style="color: #de350b; font-weight: bold;">*</span>
                  </label>
                  <select name="tr_modal_resolution" [(ngModel)]="transitionDraft.resolution_id">
                    <option [ngValue]="0">— Select Resolution —</option>
                    @for (r of api.resolutions(); track r.id) {
                      <option [ngValue]="r.id">✔ {{ r.name }} — {{ r.description }}</option>
                    }
                  </select>
                </div>
              }

              <!-- Steps to Reproduce Field (shown when required by validator) -->
              @if (hasValidatorForField(tr, 'steps_to_reproduce')) {
                <div class="form-group">
                  <label>
                    Steps to Reproduce
                    <span style="color: #de350b; font-weight: bold;">*</span>
                  </label>
                  <textarea
                    rows="3"
                    name="tr_modal_steps"
                    [(ngModel)]="transitionDraft.steps_to_reproduce"
                    placeholder="Document step-by-step reproduction instructions..."
                  ></textarea>
                </div>
              }

              <!-- Dynamic Transition Screen Fields (if tr.screen has tabs/fields) -->
              @if (tr.screen && tr.screen.tabs.length > 0) {
                @for (tab of tr.screen.tabs; track tab.id) {
                  <div style="margin-top: 10px;">
                    <h4 style="margin: 0 0 8px 0; font-size: 13px; color: #334155;">
                      Tab: {{ tab.name }}
                    </h4>
                    <div class="grid-2">
                      @for (f of tab.fields; track f.field_key) {
                        <div
                          class="form-group"
                          [style.gridColumn]="f.field_type === 'long_text' ? '1 / -1' : 'auto'"
                        >
                          <label>
                            {{ f.name }}
                            @if (f.is_required || hasValidatorForField(tr, f.field_key)) {
                              <span style="color: #de350b; font-weight: bold;">*</span>
                            }
                          </label>
                          @switch (f.field_type) {
                            @case ('long_text') {
                              <textarea
                                rows="2"
                                [name]="'tr_cf_' + f.field_key"
                                [(ngModel)]="customFieldsDraft[f.field_key]"
                              ></textarea>
                            }
                            @case ('user_picker_single') {
                              <select
                                [name]="'tr_cf_' + f.field_key"
                                [(ngModel)]="customFieldsDraft[f.field_key]"
                              >
                                <option [ngValue]="null">— None —</option>
                                @for (u of api.users(); track u.id) {
                                  <option [ngValue]="u.id">{{ u.display_name }} ({{ u.role }})</option>
                                }
                              </select>
                            }
                            @case ('date') {
                              <input
                                type="date"
                                [name]="'tr_cf_' + f.field_key"
                                [(ngModel)]="customFieldsDraft[f.field_key]"
                              />
                            }
                            @case ('checkbox') {
                              <label style="display: inline-flex; align-items: center; gap: 8px; margin-top: 6px;">
                                <input
                                  type="checkbox"
                                  [name]="'tr_cf_' + f.field_key"
                                  [(ngModel)]="customFieldsDraft[f.field_key]"
                                />
                                <span>{{ f.description || f.name }}</span>
                              </label>
                            }
                            @default {
                              <input
                                type="text"
                                [name]="'tr_cf_' + f.field_key"
                                [(ngModel)]="customFieldsDraft[f.field_key]"
                              />
                            }
                          }
                        </div>
                      }
                    </div>
                  </div>
                }
              }

              <!-- Transition Comment -->
              <div class="form-group" style="margin-top: 10px;">
                <label>
                  Transition Comment
                  @if (hasCommentValidator(tr)) {
                    <span style="color: #de350b; font-weight: bold;">* (Required by Validator)</span>
                  }
                </label>
                <textarea
                  rows="3"
                  name="tr_modal_comment"
                  [(ngModel)]="transitionDraft.comment"
                  placeholder="Explain the context or verification notes for this state transition..."
                ></textarea>
                <label style="display: inline-flex; align-items: center; gap: 6px; margin-top: 6px; font-size: 12.5px;">
                  <input
                    type="checkbox"
                    name="tr_modal_post_comment"
                    [(ngModel)]="transitionDraft.post_comment"
                  />
                  Also post this note to the Issue Comments thread
                </label>
              </div>

              <div style="display: flex; justify-content: flex-end; gap: 10px; margin-top: 14px;">
                <button type="button" class="btn btn-secondary" (click)="closeTransitionModal()">
                  Cancel
                </button>
                <button type="button" class="btn btn-primary" (click)="submitTransitionModal()">
                  Execute '{{ tr.name }}' → {{ tr.to_status }}
                </button>
              </div>
            </div>
          </div>
        }

        <!-- Interactive Jira Log Work Modal -->
        @if (showLogWorkModal()) {
          <div class="modal-backdrop">
            <div class="modal-card">
              <div class="section-header">
                <div>
                  <h3>⏱ {{ editingWorklogId() ? 'Edit Worklog' : 'Log Work' }} — {{ it.key }}</h3>
                  <small class="text-muted">{{ it.title }}</small>
                </div>
                <button type="button" class="btn btn-ghost btn-sm" (click)="closeLogWorkModal()">✕</button>
              </div>

              @if (logWorkError()) {
                <div style="margin: 10px 0; padding: 10px 12px; background: #fef2f2; border: 1px solid #fecaca; color: #b91c1c; border-radius: 6px; font-size: 13px;">
                  ⚠ {{ logWorkError() }}
                </div>
              }

              <!-- Time Spent Input with Presets -->
              <div class="form-group" style="margin-top: 12px;">
                <label>
                  Time Spent *
                  <small class="text-muted">(e.g. 2w 3d 4h 30m, 1.5h, 45m)</small>
                </label>
                <input
                  type="text"
                  name="modal_time_spent"
                  [ngModel]="worklogDraft.duration"
                  (ngModelChange)="onDurationChange($event)"
                  placeholder="e.g. 2h 30m"
                  style="font-size: 14px; font-weight: 600;"
                />

                <!-- Quick Presets -->
                <div class="tt-quick-chips">
                  <span style="font-size: 11px; color: #5e6c84; align-self: center; margin-right: 4px;">Quick Add:</span>
                  @for (preset of ['15m', '30m', '1h', '2h', '4h', '1d']; track preset) {
                    <button type="button" class="tt-chip" (click)="applyDurationPreset(preset)">
                      +{{ preset }}
                    </button>
                  }
                </div>

                @if (durationPreview(); as prev) {
                  <div style="margin-top: 6px; font-size: 11.5px;">
                    @if (prev.valid) {
                      <span style="color: #00875a; font-weight: 600;">✔ Valid: {{ prev.formatted }} ({{ prev.seconds }}s)</span>
                    } @else if (prev.error) {
                      <span style="color: #de350b;">⚠ {{ prev.error }}</span>
                    }
                  </div>
                }
              </div>

              <!-- Date Started -->
              <div class="form-group">
                <label>Date Started</label>
                <input
                  type="datetime-local"
                  name="modal_started_at"
                  [(ngModel)]="worklogDraft.started_at"
                />
              </div>

              <!-- Remaining Estimate Adjustment Modes -->
              <div class="form-group">
                <label>Remaining Estimate Adjustment</label>
                <div class="radio-group-vertical">
                  <label class="radio-row">
                    <input
                      type="radio"
                      name="modal_adj_mode"
                      value="auto"
                      [(ngModel)]="worklogDraft.adjustment_mode"
                    />
                    <div>
                      <strong>Auto-adjust (Recommended)</strong>
                      <div class="text-muted" style="font-size: 11.5px;">
                        Automatically reduces remaining estimate by the logged time.
                      </div>
                    </div>
                  </label>

                  <label class="radio-row">
                    <input
                      type="radio"
                      name="modal_adj_mode"
                      value="leave"
                      [(ngModel)]="worklogDraft.adjustment_mode"
                    />
                    <div>
                      <strong>Leave unchanged</strong>
                      <div class="text-muted" style="font-size: 11.5px;">
                        Keep remaining estimate at {{ it.remaining_estimate_formatted || '0m' }}.
                      </div>
                    </div>
                  </label>

                  <label class="radio-row">
                    <input
                      type="radio"
                      name="modal_adj_mode"
                      value="set"
                      [(ngModel)]="worklogDraft.adjustment_mode"
                    />
                    <div style="flex: 1;">
                      <strong>Set to:</strong>
                      <div class="text-muted" style="font-size: 11.5px; margin-bottom: 4px;">
                        Set remaining estimate to a specific duration.
                      </div>
                      @if (worklogDraft.adjustment_mode === 'set') {
                        <input
                          type="text"
                          name="modal_new_estimate"
                          [(ngModel)]="worklogDraft.new_estimate"
                          placeholder="e.g. 4h 30m"
                          style="max-width: 200px; padding: 4px 8px; font-size: 12.5px;"
                        />
                      }
                    </div>
                  </label>

                  <label class="radio-row">
                    <input
                      type="radio"
                      name="modal_adj_mode"
                      value="reduce"
                      [(ngModel)]="worklogDraft.adjustment_mode"
                    />
                    <div style="flex: 1;">
                      <strong>Reduce by:</strong>
                      <div class="text-muted" style="font-size: 11.5px; margin-bottom: 4px;">
                        Decrease remaining estimate by custom amount.
                      </div>
                      @if (worklogDraft.adjustment_mode === 'reduce') {
                        <input
                          type="text"
                          name="modal_reduce_by"
                          [(ngModel)]="worklogDraft.reduce_by"
                          placeholder="e.g. 1h 30m"
                          style="max-width: 200px; padding: 4px 8px; font-size: 12.5px;"
                        />
                      }
                    </div>
                  </label>
                </div>
              </div>

              <!-- Work Description -->
              <div class="form-group">
                <label>Work Description</label>
                <textarea
                  rows="3"
                  name="modal_worklog_desc"
                  [(ngModel)]="worklogDraft.description"
                  placeholder="Describe the tasks completed during this work session..."
                ></textarea>
              </div>

              <div style="display: flex; justify-content: flex-end; gap: 10px; margin-top: 16px;">
                <button type="button" class="btn btn-secondary" (click)="closeLogWorkModal()">
                  Cancel
                </button>
                <button type="button" class="btn btn-primary" (click)="submitLogWork()">
                  ⏱ {{ editingWorklogId() ? 'Update Worklog' : 'Log Work' }}
                </button>
              </div>
            </div>
          </div>
        }

        <!-- Two Column Detail Layout -->
        <div class="detail-grid">
          <!-- Main Left Column -->
          <div class="detail-main">
            <div class="card-panel">
              <div class="form-group">
                <label for="detail-title">Title</label>
                <input
                  type="text"
                  id="detail-title"
                  name="detail_title"
                  autocomplete="off"
                  [(ngModel)]="it.title"
                  class="title-input"
                />
              </div>

              <div class="form-group">
                <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 6px;">
                  <label for="detail-description" style="margin: 0; font-weight: 600;">Description</label>
                  @if (!isEditingDescription()) {
                    <button type="button" class="btn btn-ghost btn-sm" (click)="startEditDescription(it.description)">
                      ✏ Edit Description
                    </button>
                  } @else {
                    <div style="display: flex; gap: 6px;">
                      <button type="button" class="btn btn-primary btn-sm" (click)="saveDescription()">
                        Save
                      </button>
                      <button type="button" class="btn btn-secondary btn-sm" (click)="cancelEditDescription()">
                        Cancel
                      </button>
                    </div>
                  }
                </div>

                @if (isEditingDescription()) {
                  <app-rich-editor
                    [(value)]="descriptionDraft"
                    [workItemId]="it.id"
                    placeholder="Detailed technical description, checklists, callouts, or paste images (Ctrl+V)..."
                  ></app-rich-editor>
                } @else {
                  <div class="desc-preview-container" (dblclick)="startEditDescription(it.description)" title="Double click to edit">
                    @if (it.description) {
                      <app-markdown-viewer [content]="it.description"></app-markdown-viewer>
                    } @else {
                      <p class="text-muted" style="margin: 0; font-style: italic;">No description provided. Click "Edit Description" to add one.</p>
                    }
                  </div>
                }
              </div>

              @if (it.category === 'Bug') {
                <div class="bug-fields-box">
                  <div class="grid-2">
                    <div class="form-group">
                      <label for="detail-severity">Severity</label>
                      <select id="detail-severity" name="detail_severity" [(ngModel)]="it.severity">
                        <option value="">None</option>
                        <option value="Minor">Minor</option>
                        <option value="Major">Major</option>
                        <option value="Critical">Critical</option>
                        <option value="Blocker">Blocker</option>
                      </select>
                    </div>
                    <div class="form-group">
                      <label for="detail-steps">Steps to Reproduce</label>
                      <textarea
                        id="detail-steps"
                        name="detail_steps"
                        [(ngModel)]="it.steps_to_reproduce"
                        rows="3"
                      ></textarea>
                    </div>
                  </div>
                </div>
              }

              <div class="form-actions-inline">
                <button type="button" class="btn btn-primary" (click)="saveChanges()">
                  Save Details
                </button>
              </div>
            </div>

            <!-- Dynamic Custom Fields & Screen Tabs Section -->
            @if (resolvedScreen(); as screen) {
              <div class="card-panel">
                <div class="section-header" style="flex-wrap: wrap; gap: 8px;">
                  <div>
                    <h3>🧬 Custom Fields — {{ screen.name }}</h3>
                    <small class="text-muted">
                      Dynamic EAV fields organized by active Screen Tabs &amp; Field Configuration
                    </small>
                  </div>
                  <a routerLink="/custom-fields" class="btn btn-ghost btn-sm">
                    Configure Fields &amp; Screens →
                  </a>
                </div>

                @if (screen.tabs.length > 0) {
                  <div class="tabs-bar" style="margin: 12px 0;">
                    @for (tab of screen.tabs; track tab.id) {
                      <button
                        type="button"
                        class="tab-btn"
                        [class.active]="activeScreenTabId() === tab.id"
                        (click)="activeScreenTabId.set(tab.id)"
                      >
                        {{ tab.name }} ({{ tab.fields.length }})
                      </button>
                    }
                  </div>

                  @if (activeScreenTab(); as curTab) {
                    <div class="grid-2">
                      @for (f of curTab.fields; track f.field_key) {
                        @if (!f.is_hidden) {
                          <div
                            class="form-group"
                            [style.gridColumn]="f.field_type === 'long_text' ? '1 / -1' : 'auto'"
                          >
                            <label>
                              {{ f.name }}
                              @if (f.is_required) {
                                <span style="color: #de350b; font-weight: bold;">*</span>
                              }
                              <small class="text-muted" style="font-weight: normal; margin-left: 6px;">
                                ({{ f.field_type }})
                              </small>
                            </label>

                            @switch (f.field_type) {
                              @case ('formula') {
                                <div
                                  style="padding: 8px 12px; background: #f1f5f9; border-radius: 6px; font-weight: 600; color: #0f172a;"
                                >
                                  ∑ {{ customFieldsDraft[f.field_key] ?? 0 }}
                                  <small class="text-muted" style="font-weight: normal; margin-left: 8px;">
                                    ({{ f.formula_expr }})
                                  </small>
                                </div>
                              }
                              @case ('long_text') {
                                <textarea
                                  rows="3"
                                  [name]="'detail_cf_' + f.field_key"
                                  [(ngModel)]="customFieldsDraft[f.field_key]"
                                  [placeholder]="f.description || ''"
                                ></textarea>
                              }
                              @case ('number') {
                                <input
                                  type="number"
                                  [name]="'detail_cf_' + f.field_key"
                                  [attr.min]="f.min_value"
                                  [attr.max]="f.max_value"
                                  [(ngModel)]="customFieldsDraft[f.field_key]"
                                />
                              }
                              @case ('date') {
                                <input
                                  type="date"
                                  [name]="'detail_cf_' + f.field_key"
                                  [(ngModel)]="customFieldsDraft[f.field_key]"
                                />
                              }
                              @case ('datetime') {
                                <input
                                  type="datetime-local"
                                  [name]="'detail_cf_' + f.field_key"
                                  [(ngModel)]="customFieldsDraft[f.field_key]"
                                />
                              }
                              @case ('select_single') {
                                <select
                                  [name]="'detail_cf_' + f.field_key"
                                  [(ngModel)]="customFieldsDraft[f.field_key]"
                                >
                                  <option value="">— None —</option>
                                  @for (opt of f.options; track opt.id) {
                                    <option [value]="opt.value">{{ opt.value }}</option>
                                  }
                                </select>
                              }
                              @case ('select_multi') {
                                <div style="display: flex; gap: 6px; flex-wrap: wrap; margin-top: 4px;">
                                  @for (opt of f.options; track opt.id) {
                                    <button
                                      type="button"
                                      class="pill"
                                      [class.active]="isMultiOptionSelected(f.field_key, opt.value)"
                                      (click)="toggleMultiOption(f.field_key, opt.value)"
                                    >
                                      {{ opt.value }}
                                    </button>
                                  }
                                </div>
                              }
                              @case ('user_picker_single') {
                                <select
                                  [name]="'detail_cf_' + f.field_key"
                                  [(ngModel)]="customFieldsDraft[f.field_key]"
                                >
                                  <option [ngValue]="null">— None —</option>
                                  @for (u of api.users(); track u.id) {
                                    <option [ngValue]="u.id">{{ u.display_name }} ({{ u.role }})</option>
                                  }
                                </select>
                              }
                              @case ('checkbox') {
                                <label
                                  style="display: inline-flex; align-items: center; gap: 8px; margin-top: 6px; cursor: pointer;"
                                >
                                  <input
                                    type="checkbox"
                                    [name]="'detail_cf_' + f.field_key"
                                    [(ngModel)]="customFieldsDraft[f.field_key]"
                                  />
                                  <span>{{ f.description || f.name }}</span>
                                </label>
                              }
                              @case ('url') {
                                <div style="display: flex; gap: 6px; align-items: center;">
                                  <input
                                    type="url"
                                    [name]="'detail_cf_' + f.field_key"
                                    [(ngModel)]="customFieldsDraft[f.field_key]"
                                    placeholder="https://..."
                                    style="flex: 1;"
                                  />
                                  @if (customFieldsDraft[f.field_key]) {
                                    <a
                                      [href]="customFieldsDraft[f.field_key]"
                                      target="_blank"
                                      class="btn btn-secondary btn-sm"
                                    >
                                      ↗
                                    </a>
                                  }
                                </div>
                              }
                              @default {
                                <input
                                  type="text"
                                  [name]="'detail_cf_' + f.field_key"
                                  [(ngModel)]="customFieldsDraft[f.field_key]"
                                />
                              }
                            }
                          </div>
                        }
                      }
                    </div>
                  }
                }

                <div class="form-actions-inline" style="margin-top: 10px;">
                  <button type="button" class="btn btn-primary" (click)="saveChanges()">
                    Save Custom Fields
                  </button>
                </div>
              </div>
            }

            <!-- Subtasks Section -->
            <div class="card-panel">
              <div class="section-header">
                <h3>Subtasks &amp; Child Work Items ({{ it.subtasks.length }})</h3>
              </div>
              @if (it.subtasks.length > 0) {
                <div class="subtask-list">
                  @for (sub of it.subtasks; track sub.id) {
                    <div class="subtask-row">
                      <span class="type-badge" [attr.data-type]="sub.type">{{ sub.type }}</span>
                      <a [routerLink]="['/', sub.key]" class="issue-key">{{ sub.key }}</a>
                      <a [routerLink]="['/', sub.key]" class="subtask-title">{{ sub.title }}</a>
                      <span class="state-pill" [attr.data-state]="sub.status">{{ sub.status }}</span>
                      <span class="text-muted">{{ sub.assignee_name || 'Unassigned' }}</span>
                    </div>
                  }
                </div>
              }
              <div class="quick-subtask-bar">
                <input
                  type="text"
                  id="detail-new-subtask"
                  name="new_subtask_title"
                  autocomplete="off"
                  [(ngModel)]="newSubtaskTitle"
                  placeholder="Quick add a Subtask under {{ it.key }}..."
                  (keyup.enter)="quickAddSubtask()"
                />
                <button type="button" class="btn btn-secondary" (click)="quickAddSubtask()">
                  + Add Subtask
                </button>
              </div>
            </div>

            <!-- Linked Work Items Section -->
            <div class="card-panel">
              <div class="section-header">
                <h3>Linked Issues ({{ it.links.length }})</h3>
              </div>
              @if (it.links.length > 0) {
                <div class="subtask-list">
                  @for (lk of it.links; track lk.id) {
                    <div class="subtask-row">
                      <span class="link-type-chip">{{ lk.link_type }}</span>
                      @if (lk.source_item_id === it.id) {
                        <a [routerLink]="['/', lk.target_key]" class="issue-key">
                          {{ lk.target_key }}
                        </a>
                        <span class="subtask-title">{{ lk.target_title }}</span>
                        <span class="state-pill" [attr.data-state]="lk.target_status">
                          {{ lk.target_status }}
                        </span>
                      } @else {
                        <a [routerLink]="['/', lk.source_key]" class="issue-key">
                          {{ lk.source_key }}
                        </a>
                        <span class="subtask-title">{{ lk.source_title }}</span>
                        <span class="state-pill" [attr.data-state]="lk.source_status">
                          {{ lk.source_status }}
                        </span>
                      }
                      <button
                        type="button"
                        class="btn btn-ghost btn-sm"
                        (click)="removeLink(lk.id)"
                      >
                        ×
                      </button>
                    </div>
                  }
                </div>
              }
              <div class="quick-subtask-bar">
                <select
                  id="detail-link-type"
                  name="new_link_type"
                  [(ngModel)]="newLinkType"
                  style="max-width: 160px;"
                >
                  <option value="blocks">blocks</option>
                  <option value="is_blocked_by">is blocked by</option>
                  <option value="relates_to">relates to</option>
                  <option value="duplicates">duplicates</option>
                </select>
                <select id="detail-link-target" name="new_link_target_id" [(ngModel)]="newLinkTargetId">
                  <option [ngValue]="0">Select work item to link...</option>
                  @for (other of allItems(); track other.id) {
                    @if (other.id !== it.id) {
                      <option [ngValue]="other.id">{{ other.key }} — {{ other.title }}</option>
                    }
                  }
                </select>
                <button type="button" class="btn btn-secondary" (click)="addIssueLink()">
                  Link Issue
                </button>
              </div>
            </div>

            <!-- File Attachments (blobs/) Section -->
            <div class="card-panel">
              <div class="section-header">
                <h3>File Attachments in <code>blobs/</code> ({{ it.attachments.length }})</h3>
                <label class="btn btn-secondary btn-sm upload-btn-label" for="detail-upload-file">
                  📎 Upload File to blobs/
                  <input
                    type="file"
                    id="detail-upload-file"
                    name="detail_attachment"
                    hidden
                    (change)="uploadFile($event)"
                  />
                </label>
              </div>

              @if (it.attachments.length > 0) {
                <div class="attachment-grid">
                  @for (att of it.attachments; track att.id) {
                    <div class="attachment-card">
                      <div class="att-icon">📄</div>
                      <div class="att-info">
                        <a [href]="att.url" target="_blank" class="att-name">
                          {{ att.original_filename }}
                        </a>
                        <div class="att-meta">
                          {{ (att.size_bytes / 1024) | number:'1.0-1' }} KB · {{ att.mime_type }} ·
                          Uploaded by {{ att.uploader_name }}
                        </div>
                        <div class="att-blob-path">
                          Blob: <code>blobs/{{ att.blob_filename }}</code>
                        </div>
                      </div>
                      <div class="att-actions">
                        <a [href]="att.url" [download]="att.original_filename" class="btn btn-ghost btn-sm">
                          ⬇
                        </a>
                        <button
                          type="button"
                          class="btn btn-ghost btn-sm"
                          (click)="deleteAttachment(att.id)"
                        >
                          🗑
                        </button>
                      </div>
                    </div>
                  }
                </div>
              } @else {
                <p class="text-muted">No files attached yet. Click "Upload File to blobs/" above.</p>
              }
            </div>

            <!-- Comments, Worklogs & Workflow Audit Log -->
            <div class="card-panel">
              <div class="tabs-bar">
                <button
                  type="button"
                  class="tab-btn"
                  [class.active]="activeTab() === 'comments'"
                  (click)="activeTab.set('comments')"
                >
                  Comments ({{ it.comments.length }})
                </button>
                <button
                  type="button"
                  class="tab-btn"
                  [class.active]="activeTab() === 'worklogs'"
                  (click)="activeTab.set('worklogs')"
                >
                  ⏱ Worklogs ({{ it.worklogs?.length || 0 }})
                </button>
                <button
                  type="button"
                  class="tab-btn"
                  [class.active]="activeTab() === 'history'"
                  (click)="activeTab.set('history')"
                >
                  Workflow State History ({{ it.history.length }})
                </button>
                <button
                  type="button"
                  class="tab-btn"
                  [class.active]="activeTab() === 'changelog'"
                  (click)="activeTab.set('changelog')"
                >
                  📜 History & Changelog ({{ it.changelog?.length || 0 }})
                </button>
              </div>

              @if (activeTab() === 'comments') {
                <div class="comments-list">
                  @for (c of it.comments; track c.id) {
                    <div class="comment-item">
                      <div class="avatar-circle" [style.background]="c.author_color">
                        {{ c.author_name.slice(0, 2).toUpperCase() }}
                      </div>
                      <div class="comment-content">
                        <div class="comment-header">
                          <strong>{{ c.author_name }}</strong>
                          <span class="text-muted">{{ c.created_at }}</span>
                        </div>
                        <app-markdown-viewer [content]="c.body"></app-markdown-viewer>
                      </div>
                    </div>
                  }
                </div>
                <div class="rich-comment-box" style="margin-top: 14px;">
                  <app-rich-editor
                    [(value)]="newCommentText"
                    [compact]="true"
                    [workItemId]="it.id"
                    placeholder="Add a comment... (Type @ to mention teammates, Ctrl+V to paste images)"
                  ></app-rich-editor>
                  <div style="display: flex; justify-content: flex-end; margin-top: 8px;">
                    <button
                      type="button"
                      class="btn btn-primary"
                      [disabled]="!newCommentText.trim()"
                      (click)="postComment()"
                    >
                      Post Comment
                    </button>
                  </div>
                </div>
              } @else if (activeTab() === 'worklogs') {
                <div style="display: flex; align-items: center; justify-content: space-between; margin-bottom: 12px; padding-bottom: 8px; border-bottom: 1px solid #ebecf0;">
                  <div>
                    <span style="font-size: 13px; color: #5e6c84;">Total Time Spent:</span>
                    <strong style="margin-left: 6px; color: #0052cc; font-size: 14px;">{{ it.time_spent_formatted || '0m' }}</strong>
                    <span style="margin: 0 8px; color: #dfe1e6;">|</span>
                    <span style="font-size: 13px; color: #5e6c84;">Remaining:</span>
                    <strong style="margin-left: 6px; color: #00875a; font-size: 14px;">{{ it.remaining_estimate_formatted || '0m' }}</strong>
                  </div>
                  <button type="button" class="btn btn-primary btn-sm" (click)="openLogWorkModal()">
                    ⏱ + Log Work
                  </button>
                </div>

                @if ((it.worklogs || []).length > 0) {
                  <div class="worklogs-list">
                    @for (wl of it.worklogs; track wl.id) {
                      <div class="worklog-timeline-row">
                        <div class="worklog-author-avatar" [style.background]="wl.author_color">
                          {{ wl.author_name.slice(0, 2).toUpperCase() }}
                        </div>
                        <div class="worklog-body">
                          <div class="worklog-meta">
                            <strong>{{ wl.author_name }}</strong>
                            <span class="worklog-spent-badge">⏱ {{ wl.time_spent_formatted }}</span>
                            <span class="text-muted">logged on {{ wl.started_at }}</span>
                            <span class="worklog-adj-note">
                              ({{ wl.adjustment_mode }}: {{ wl.remaining_before_formatted }} → {{ wl.remaining_after_formatted }})
                            </span>
                          </div>
                          @if (wl.description) {
                            <p class="worklog-desc">{{ wl.description }}</p>
                          }
                        </div>
                        <div class="worklog-actions">
                          <button type="button" class="btn btn-ghost btn-sm" (click)="openLogWorkModal(wl)" title="Edit Worklog">
                            ✏
                          </button>
                          <button type="button" class="btn btn-ghost btn-sm" (click)="deleteWorklog(wl.id)" title="Delete Worklog">
                            🗑
                          </button>
                        </div>
                      </div>
                    }
                  </div>
                } @else {
                  <div style="padding: 24px 16px; text-align: center; color: #6b778c;">
                    <div style="font-size: 28px; margin-bottom: 6px;">⏱</div>
                    <p style="margin: 0 0 10px;">No work has been logged on this issue yet.</p>
                    <button type="button" class="btn btn-secondary btn-sm" (click)="openLogWorkModal()">
                      Log your first work session
                    </button>
                  </div>
                }
              } @else if (activeTab() === 'history') {
                <div class="history-list">
                  @for (h of it.history; track h.id) {
                    <div class="history-row">
                      <strong>{{ h.changed_by_name }}</strong>
                      @if (h.transition_name) {
                        executed <span class="badge badge-info">{{ h.transition_name }}</span>:
                      } @else {
                        transitioned state from
                      }
                      <span class="state-pill" [attr.data-state]="h.from_status">{{ h.from_status }}</span>
                      →
                      <span class="state-pill" [attr.data-state]="h.to_status">{{ h.to_status }}</span>
                      <span class="text-muted">({{ h.created_at }})</span>
                      @if (h.executed_post_functions) {
                        <div style="margin-top: 4px; font-size: 11.5px; color: #166534;">
                          ⚡ Post-Functions: <code>{{ h.executed_post_functions }}</code>
                        </div>
                      }
                      @if (h.comment) {
                        <div class="history-note">{{ h.comment }}</div>
                      }
                    </div>
                  }
                </div>
              } @else if (activeTab() === 'changelog') {
                <div class="changelog-list">
                  @for (entry of it.changelog || []; track entry.id) {
                    <div class="changelog-row">
                      <span class="avatar-circle-sm" [style.background]="entry.changed_by_color">
                        {{ entry.changed_by_name.slice(0, 2).toUpperCase() }}
                      </span>
                      <div class="changelog-body">
                        <div class="changelog-header">
                          <strong>{{ entry.changed_by_name }}</strong>
                          <span class="text-muted">modified</span>
                          <code class="changelog-field">{{ entry.field_name }}</code>
                          <span class="text-muted">· {{ entry.created_at }}</span>
                        </div>
                        <div class="changelog-diff">
                          @if (entry.old_value) {
                            <span class="diff-old"><del>{{ entry.old_value }}</del></span>
                            <span class="diff-arrow">→</span>
                          }
                          <span class="diff-new"><ins>{{ entry.new_value }}</ins></span>
                        </div>
                      </div>
                    </div>
                  } @empty {
                    <div style="padding: 24px; text-align: center; color: #6b778c;">
                      <p>No audit changelog entries yet. Any updates to this issue will appear here.</p>
                    </div>
                  }
                </div>
              }
            </div>
          </div>

          <!-- Right Metadata Sidebar -->
          <div class="detail-sidebar">
            <!-- Collaboration: Watchers & Voting Panel -->
            <div class="collab-panel">
              <div class="collab-actions">
                <button
                  type="button"
                  class="btn btn-sm collab-btn"
                  [class.active]="it.is_watching"
                  [disabled]="isTogglingWatch()"
                  (click)="toggleWatch(it)"
                  [title]="it.is_watching ? 'Stop watching this issue' : 'Watch this issue for notifications'"
                >
                  👁 {{ it.is_watching ? 'Watching' : 'Watch' }}
                </button>
                <button
                  type="button"
                  class="collab-count-btn"
                  (click)="showWatchersModal.set(true)"
                  title="View list of watchers"
                >
                  {{ it.watchers_count || 0 }}
                </button>

                <div style="width: 1px; height: 22px; background: #ebecf0; margin: 0 4px;"></div>

                <button
                  type="button"
                  class="btn btn-sm collab-btn"
                  [class.active]="it.has_voted"
                  [disabled]="isTogglingVote()"
                  (click)="toggleVote(it)"
                  [title]="it.has_voted ? 'Remove your vote' : 'Vote for this issue'"
                >
                  👍 {{ it.has_voted ? 'Voted' : 'Vote' }}
                </button>
                <span class="collab-count-badge" title="Total votes">
                  {{ it.votes_count || 0 }}
                </span>
              </div>
            </div>

            <!-- Prominent Time Tracking & Estimation Panel -->
            <div class="tt-panel">
              <div class="tt-header">
                <h3>⏱ Time Tracking</h3>
                <button type="button" class="btn btn-primary btn-sm" (click)="openLogWorkModal()">
                  + Log Work
                </button>
              </div>

              <!-- Visual Jira Time Tracking Progress Bar -->
              <div class="tt-progress-track" [title]="'Logged: ' + (it.time_spent_formatted || '0m') + ' / Remaining: ' + (it.remaining_estimate_formatted || '0m')">
                <div
                  class="tt-progress-spent"
                  [style.width.%]="getSpentPercent(it)"
                  [title]="'Logged: ' + (it.time_spent_formatted || '0m')"
                ></div>
                <div
                  class="tt-progress-rem"
                  [style.width.%]="getRemainingPercent(it)"
                  [title]="'Remaining: ' + (it.remaining_estimate_formatted || '0m')"
                ></div>
                @if (it.is_over_estimate) {
                  <div class="tt-progress-over" style="width: 100%;" title="Logged time exceeds original estimate!"></div>
                }
              </div>

              <div class="tt-legend-row">
                <div class="tt-legend-item">
                  <span class="tt-legend-dot spent"></span>
                  <span>Logged: <strong>{{ it.time_spent_formatted || '0m' }}</strong></span>
                </div>
                <div class="tt-legend-item">
                  <span class="tt-legend-dot remaining"></span>
                  <span>Remaining: <strong>{{ it.remaining_estimate_formatted || '0m' }}</strong></span>
                </div>
                <div class="tt-legend-item">
                  <span class="text-muted">Original: <strong>{{ it.original_estimate_formatted || '0m' }}</strong></span>
                </div>
              </div>

              @if (it.is_over_estimate) {
                <div style="margin-bottom: 12px; padding: 6px 10px; background: #fff1f0; border: 1px solid #ffa39e; border-radius: 4px; font-size: 11.5px; color: #cf1322;">
                  ⚠ Logged time exceeds the original estimate!
                </div>
              }

              <!-- Subtasks Rollup (if subtasks exist) -->
              @if (it.time_rollup && it.time_rollup.subtask_count > 0) {
                <div style="margin: 10px 0; padding: 10px; background: #f8fafc; border-radius: 6px; border: 1px solid #e2e8f0; font-size: 12px;">
                  <div style="display: flex; justify-content: space-between; margin-bottom: 4px;">
                    <strong>⧉ Subtask Rollup ({{ it.time_rollup.subtask_count }})</strong>
                    <span>{{ it.time_rollup.aggregate_progress_pct }}%</span>
                  </div>
                  <div class="tt-progress-track" style="height: 6px; margin-bottom: 6px;">
                    <div class="tt-progress-spent" [style.width.%]="it.time_rollup.aggregate_progress_pct"></div>
                  </div>
                  <div style="display: flex; justify-content: space-between; color: #64748b; font-size: 11px;">
                    <span>Total: {{ it.time_rollup.aggregate_time_spent_formatted }}</span>
                    <span>Rem: {{ it.time_rollup.aggregate_remaining_estimate_formatted }}</span>
                    <span>Orig: {{ it.time_rollup.aggregate_original_estimate_formatted }}</span>
                  </div>
                </div>
              }

              <!-- Inline Editable Estimates -->
              <div style="border-top: 1px solid #ebecf0; padding-top: 10px; margin-top: 10px;">
                <div class="grid-2">
                  <div class="form-group">
                    <label style="font-size: 12px;">Original Estimate</label>
                    <input
                      type="text"
                      name="tt_original_est"
                      [ngModel]="originalEstimateInput()"
                      (ngModelChange)="originalEstimateInput.set($event)"
                      (blur)="saveEstimateChanges()"
                      placeholder="e.g. 2d 4h"
                      style="font-size: 12.5px; padding: 4px 8px;"
                    />
                  </div>
                  <div class="form-group">
                    <label style="font-size: 12px;">Remaining Estimate</label>
                    <input
                      type="text"
                      name="tt_remaining_est"
                      [ngModel]="remainingEstimateInput()"
                      (ngModelChange)="remainingEstimateInput.set($event)"
                      (blur)="saveEstimateChanges()"
                      placeholder="e.g. 1d 2h"
                      style="font-size: 12.5px; padding: 4px 8px;"
                    />
                  </div>
                </div>

                <div class="form-group" style="margin-top: 6px;">
                  <label style="font-size: 12px;">T-Shirt Size Estimation</label>
                  <select
                    name="tt_tshirt_size"
                    [ngModel]="tshirtSizeInput()"
                    (ngModelChange)="onTshirtChange($event)"
                    style="font-size: 12.5px; padding: 4px 8px;"
                  >
                    <option value="">— None —</option>
                    <option value="XS">XS (Extra Small · ~2h)</option>
                    <option value="S">S (Small · ~4h)</option>
                    <option value="M">M (Medium · ~1d)</option>
                    <option value="L">L (Large · ~2d)</option>
                    <option value="XL">XL (Extra Large · ~1w)</option>
                    <option value="XXL">XXL (Epic Scope · ~2w)</option>
                  </select>
                </div>
              </div>
            </div>

            <div class="card-panel">
              <h3>Attributes &amp; Scheduling</h3>

              <div class="form-group">
                <label for="detail-type">Work Element Type (Hierarchy)</label>
                <select id="detail-type" name="detail_type" [(ngModel)]="it.type" (change)="saveChanges()">
                  @for (t of itemTypes; track t) {
                    <option [value]="t">{{ t }}</option>
                  }
                </select>
              </div>

              <div class="form-group">
                <label for="detail-category">Work Category</label>
                <select
                  id="detail-category"
                  name="detail_category"
                  [(ngModel)]="it.category"
                  (change)="saveChanges()"
                >
                  @for (c of itemCategories; track c) {
                    <option [value]="c">{{ c }}</option>
                  }
                </select>
              </div>

              <div class="form-group">
                <label for="detail-priority">Priority</label>
                <select
                  id="detail-priority"
                  name="detail_priority"
                  [(ngModel)]="it.priority"
                  (change)="saveChanges()"
                >
                  @for (p of priorities; track p) {
                    <option [value]="p">{{ p }}</option>
                  }
                </select>
              </div>

              <div class="form-group">
                <label for="detail-assignee">Assignee</label>
                <select
                  id="detail-assignee"
                  name="detail_assignee_id"
                  [(ngModel)]="it.assignee_id"
                  (change)="saveChanges()"
                >
                  <option [ngValue]="null">— Unassigned —</option>
                  @for (u of api.users(); track u.id) {
                    <option [ngValue]="u.id">{{ u.display_name }} ({{ u.role }})</option>
                  }
                </select>
              </div>

              <div class="form-group">
                <label for="detail-sprint">Sprint</label>
                <select
                  id="detail-sprint"
                  name="detail_sprint_id"
                  [(ngModel)]="it.sprint_id"
                  (change)="saveChanges()"
                >
                  <option [ngValue]="null">— Backlog —</option>
                  @for (s of api.sprints(); track s.id) {
                    <option [ngValue]="s.id">{{ s.name }}</option>
                  }
                </select>
              </div>

              <div class="form-group">
                <label for="detail-parent">Parent Item</label>
                <select
                  id="detail-parent"
                  name="detail_parent_id"
                  [(ngModel)]="it.parent_id"
                  (change)="saveChanges()"
                >
                  <option [ngValue]="null">— None —</option>
                  @for (other of allItems(); track other.id) {
                    @if (other.id !== it.id) {
                      <option [ngValue]="other.id">[{{ other.type }}] {{ other.key }}: {{ other.title }}</option>
                    }
                  }
                </select>
              </div>

              <div class="grid-2">
                <div class="form-group">
                  <label for="detail-story-points">Story Points</label>
                  <input
                    type="number"
                    id="detail-story-points"
                    name="detail_story_points"
                    [(ngModel)]="it.story_points"
                    (change)="saveChanges()"
                  />
                </div>
                <div class="form-group">
                  <label for="detail-progress">Progress ({{ it.progress }}%)</label>
                  <input
                    type="number"
                    id="detail-progress"
                    name="detail_progress"
                    min="0"
                    max="100"
                    [(ngModel)]="it.progress"
                    (change)="saveChanges()"
                  />
                </div>
              </div>

              <div class="grid-2">
                <div class="form-group">
                  <label for="detail-start-date">Start Date</label>
                  <input
                    type="date"
                    id="detail-start-date"
                    name="detail_start_date"
                    [(ngModel)]="it.start_date"
                    (change)="saveChanges()"
                  />
                </div>
                <div class="form-group">
                  <label for="detail-due-date">Due Date</label>
                  <input
                    type="date"
                    id="detail-due-date"
                    name="detail_due_date"
                    [(ngModel)]="it.due_date"
                    (change)="saveChanges()"
                  />
                </div>
              </div>

              <div class="form-group">
                <label for="detail-labels">Labels</label>
                <input
                  type="text"
                  id="detail-labels"
                  name="detail_labels"
                  autocomplete="off"
                  [(ngModel)]="it.labels"
                  (change)="saveChanges()"
                />
              </div>

              <div class="form-group">
                <label for="detail-resolution">Resolution</label>
                <select
                  id="detail-resolution"
                  name="detail_resolution_id"
                  [(ngModel)]="it.resolution_id"
                  (change)="saveChanges()"
                >
                  <option [ngValue]="null">— Unresolved —</option>
                  @for (r of api.resolutions(); track r.id) {
                    <option [ngValue]="r.id">✔ {{ r.name }}</option>
                  }
                </select>
                @if (it.resolution_date) {
                  <small class="text-muted" style="display: block; margin-top: 4px;">
                    Resolved at: {{ it.resolution_date }}
                  </small>
                }
              </div>

              <div class="form-group">
                <label>Components</label>
                <div style="display: flex; gap: 6px; flex-wrap: wrap; margin-top: 4px;">
                  @for (c of projectComponents(); track c.id) {
                    <button
                      type="button"
                      class="pill"
                      [class.active]="(it.component_ids || []).includes(c.id)"
                      (click)="toggleRelation('component_ids', c.id)"
                    >
                      🧩 {{ c.name }}
                    </button>
                  }
                </div>
              </div>

              <div class="form-group">
                <label>Fix Version/s</label>
                <div style="display: flex; gap: 6px; flex-wrap: wrap; margin-top: 4px;">
                  @for (v of projectVersions(); track v.id) {
                    <button
                      type="button"
                      class="pill"
                      [class.active]="(it.fix_version_ids || []).includes(v.id)"
                      (click)="toggleRelation('fix_version_ids', v.id)"
                    >
                      🚀 {{ v.name }}
                    </button>
                  }
                </div>
              </div>

              <div class="form-group">
                <label>Affects Version/s</label>
                <div style="display: flex; gap: 6px; flex-wrap: wrap; margin-top: 4px;">
                  @for (v of projectVersions(); track v.id) {
                    <button
                      type="button"
                      class="pill"
                      [class.active]="(it.affects_version_ids || []).includes(v.id)"
                      (click)="toggleRelation('affects_version_ids', v.id)"
                    >
                      🏷 {{ v.name }}
                    </button>
                  }
                </div>
              </div>

              <div class="meta-timestamps">
                <div>Reporter: <strong>{{ it.reporter_name || 'System' }}</strong></div>
                <div>Created: {{ it.created_at }}</div>
                <div>Updated: {{ it.updated_at }}</div>
              </div>
            </div>
          </div>
        </div>

        <!-- Watchers List Modal -->
        @if (showWatchersModal()) {
          <div
            style="position: fixed; inset: 0; background: rgba(15, 23, 42, 0.55); z-index: 1200; display: flex; align-items: center; justify-content: center; padding: 16px;"
            (click)="showWatchersModal.set(false)"
          >
            <div
              class="card-panel"
              style="width: 100%; max-width: 460px; max-height: 80vh; overflow-y: auto; box-shadow: 0 20px 45px rgba(0,0,0,0.25);"
              (click)="$event.stopPropagation()"
            >
              <div class="section-header" style="margin-bottom: 12px;">
                <div>
                  <h3 style="margin: 0;">👁 Watchers ({{ it.watchers_count || 0 }})</h3>
                  <small class="text-muted">{{ it.key }}: {{ it.title }}</small>
                </div>
                <button type="button" class="btn btn-ghost btn-sm" (click)="showWatchersModal.set(false)">✕</button>
              </div>

              <div class="watchers-modal-list">
                @for (w of it.watchers_info?.watchers || []; track w.user_id) {
                  <div style="display: flex; align-items: center; gap: 10px; padding: 8px 0; border-bottom: 1px solid #ebecf0;">
                    <div class="avatar-circle" [style.background]="w.avatar_color" style="width: 28px; height: 28px; font-size: 11px;">
                      {{ w.display_name.slice(0, 2).toUpperCase() }}
                    </div>
                    <div>
                      <div style="font-weight: 600; font-size: 13px;">{{ w.display_name }}</div>
                      <small class="text-muted">&#64;{{ w.username }} · Watching since {{ w.created_at }}</small>
                    </div>
                  </div>
                } @empty {
                  <p class="text-muted" style="padding: 12px 0;">No teammates are watching this issue yet.</p>
                }
              </div>

              <div style="display: flex; justify-content: flex-end; margin-top: 16px;">
                <button type="button" class="btn btn-secondary btn-sm" (click)="showWatchersModal.set(false)">Close</button>
              </div>
            </div>
          </div>
        }
      </div>
    }
  `,
})
export class ItemDetailComponent implements OnInit {
  readonly api = inject(ApiService);
  private readonly route = inject(ActivatedRoute);
  private readonly router = inject(Router);

  readonly states = WORKFLOW_STATES;
  readonly itemTypes = WORK_ITEM_TYPES;
  readonly itemCategories = WORK_ITEM_CATEGORIES;
  readonly priorities = PRIORITIES;

  readonly item = signal<WorkItemDetail | null>(null);
  readonly allItems = signal<WorkItem[]>([]);
  readonly activeTab = signal<'comments' | 'worklogs' | 'history' | 'changelog'>('comments');
  readonly toastMsg = signal('');
  readonly resolvedScreen = signal<ScreenDef | null>(null);
  readonly activeScreenTabId = signal<number>(0);
  readonly activeTransitionModal = signal<AvailableTransition | null>(null);
  readonly transitionModalError = signal('');

  // Step 10: Rich Text & Collaboration Signals
  readonly isEditingDescription = signal(false);
  descriptionDraft = '';
  readonly showWatchersModal = signal(false);
  readonly isTogglingWatch = signal(false);
  readonly isTogglingVote = signal(false);

  // Step 7: Time Tracking & Worklogs Signals & Draft State
  readonly showLogWorkModal = signal(false);
  readonly logWorkError = signal('');
  readonly editingWorklogId = signal<number | null>(null);
  readonly originalEstimateInput = signal('');
  readonly remainingEstimateInput = signal('');
  readonly tshirtSizeInput = signal('');
  readonly durationPreview = signal<{ valid: boolean; formatted: string; seconds: number; error: string }>({
    valid: true,
    formatted: '2h',
    seconds: 7200,
    error: '',
  });

  worklogDraft = {
    duration: '2h',
    started_at: '',
    description: '',
    adjustment_mode: 'auto' as WorklogAdjustmentMode,
    new_estimate: '',
    reduce_by: '',
  };

  customFieldsDraft: Record<string, any> = {};

  transitionDraft = {
    resolution_id: 0,
    steps_to_reproduce: '',
    comment: '',
    post_comment: true,
  };

  newSubtaskTitle = '';
  newCommentText = '';
  newLinkType = 'relates_to';
  newLinkTargetId = 0;

  async ngOnInit(): Promise<void> {
    await this.api.loadReferenceData();
    const list = await this.api.getItems();
    this.allItems.set(list);

    this.route.paramMap.subscribe(async (params) => {
      const idOrKey = params.get('id')?.trim();
      if (idOrKey) {
        await this.reloadDetail(idOrKey);
      }
    });
  }

  async reloadDetail(idOrKey: string | number): Promise<void> {
    const d = await this.api.getItemDetail(idOrKey);
    this.item.set(d);
    this.customFieldsDraft = { ...(d.custom_fields || {}) };
    this.originalEstimateInput.set(d.original_estimate_formatted || '');
    this.remainingEstimateInput.set(d.remaining_estimate_formatted || '');
    this.tshirtSizeInput.set(d.tshirt_size || '');
    try {
      const sc = await this.api.resolveScreen(d.project_id, 'edit', d.type);
      this.resolvedScreen.set(sc);
      if (sc.tabs.length > 0 && !sc.tabs.some((t) => t.id === this.activeScreenTabId())) {
        this.activeScreenTabId.set(sc.tabs[0].id);
      }
    } catch {
      // ignore
    }
  }

  activeScreenTab(): ScreenTab | null {
    const sc = this.resolvedScreen();
    if (!sc || sc.tabs.length === 0) return null;
    return sc.tabs.find((t) => t.id === this.activeScreenTabId()) || sc.tabs[0];
  }

  isMultiOptionSelected(fieldKey: string, val: string): boolean {
    const cur = this.customFieldsDraft[fieldKey];
    return Array.isArray(cur) && cur.includes(val);
  }

  toggleMultiOption(fieldKey: string, val: string): void {
    const cur = Array.isArray(this.customFieldsDraft[fieldKey])
      ? [...this.customFieldsDraft[fieldKey]]
      : [];
    const idx = cur.indexOf(val);
    if (idx >= 0) {
      cur.splice(idx, 1);
    } else {
      cur.push(val);
    }
    this.customFieldsDraft[fieldKey] = cur;
  }

  projectComponents() {
    const cur = this.item();
    if (!cur) return [];
    return this.api.components().filter((c) => c.project_id === cur.project_id);
  }

  projectVersions() {
    const cur = this.item();
    if (!cur) return [];
    return this.api.versions().filter((v) => v.project_id === cur.project_id);
  }

  async toggleRelation(
    field: 'component_ids' | 'fix_version_ids' | 'affects_version_ids',
    id: number
  ): Promise<void> {
    const cur = this.item();
    if (!cur) return;
    const arr = [...(cur[field] || [])];
    const idx = arr.indexOf(id);
    if (idx >= 0) {
      arr.splice(idx, 1);
    } else {
      arr.push(id);
    }
    cur[field] = arr;
    await this.saveChanges();
  }

  hasValidatorForField(tr: AvailableTransition, fieldName: string): boolean {
    return tr.validators.some((v) => v.rule_type === 'field_required' && v.field_name === fieldName);
  }

  hasCommentValidator(tr: AvailableTransition): boolean {
    return tr.validators.some((v) => v.rule_type === 'comment_required');
  }

  async triggerWorkflowTransition(tr: AvailableTransition): Promise<void> {
    const cur = this.item();
    if (!cur) return;
    if (!tr.allowed) {
      this.showToast(tr.blocked_reasons[0] || 'Transition blocked by workflow condition');
      return;
    }

    if (tr.requires_screen) {
      const defRes = cur.resolution_id || this.api.resolutions().find((r) => r.is_default)?.id || 1;
      this.transitionDraft = {
        resolution_id: tr.to_status === 'Done' ? defRes : cur.resolution_id || 0,
        steps_to_reproduce: cur.steps_to_reproduce || '',
        comment: '',
        post_comment: true,
      };
      this.transitionModalError.set('');
      this.activeTransitionModal.set(tr);
      return;
    }

    try {
      await this.api.transitionItemState(cur.key, tr.to_status, {
        transition_id: tr.id,
        comment: `Executed '${tr.name}' (${cur.status} → ${tr.to_status})`,
      });
      await this.reloadDetail(cur.key);
      this.showToast(`${cur.key}: ${tr.name} → ${tr.to_status}`);
    } catch (err: any) {
      this.showToast(`Transition failed: ${err?.message || 'Error'}`);
    }
  }

  closeTransitionModal(): void {
    this.activeTransitionModal.set(null);
    this.transitionModalError.set('');
  }

  async submitTransitionModal(): Promise<void> {
    const cur = this.item();
    const tr = this.activeTransitionModal();
    if (!cur || !tr) return;

    try {
      await this.api.transitionItemState(cur.key, tr.to_status, {
        transition_id: tr.id,
        resolution_id: this.transitionDraft.resolution_id || undefined,
        steps_to_reproduce: this.transitionDraft.steps_to_reproduce || undefined,
        comment: this.transitionDraft.comment.trim() || undefined,
        post_comment: this.transitionDraft.post_comment,
        custom_fields: this.customFieldsDraft,
      });
      this.closeTransitionModal();
      await this.reloadDetail(cur.key);
      this.showToast(`${cur.key} transitioned via '${tr.name}' to ${tr.to_status}`);
    } catch (err: any) {
      this.transitionModalError.set(err?.message || 'Transition validation failed');
    }
  }

  async transitionState(newState: WorkflowState): Promise<void> {
    const cur = this.item();
    if (!cur || cur.status === newState) return;
    const tr = cur.workflow?.transitions.find((t) => t.to_status === newState);
    if (tr) {
      await this.triggerWorkflowTransition(tr);
      return;
    }
    try {
      await this.api.transitionItemState(cur.key, newState, {
        comment: `Transitioned to '${newState}' from Item Detail page`,
      });
      await this.reloadDetail(cur.key);
      this.showToast(`${cur.key} transitioned to ${newState}`);
    } catch (err: any) {
      this.showToast(`Transition blocked: ${err?.message || 'Rule check failed'}`);
    }
  }

  async saveChanges(): Promise<void> {
    const cur = this.item();
    if (!cur) return;
    try {
      await this.api.updateItem(cur.key, {
        type: cur.type,
        category: cur.category,
        status: cur.status,
        priority: cur.priority,
        title: cur.title,
        description: cur.description,
        parent_id: cur.parent_id || 0,
        sprint_id: cur.sprint_id || 0,
        assignee_id: cur.assignee_id || 0,
        resolution_id: cur.resolution_id || 0,
        component_ids: cur.component_ids || [],
        fix_version_ids: cur.fix_version_ids || [],
        affects_version_ids: cur.affects_version_ids || [],
        story_points: cur.story_points,
        progress: cur.progress,
        start_date: cur.start_date,
        due_date: cur.due_date,
        severity: cur.severity,
        steps_to_reproduce: cur.steps_to_reproduce,
        labels: cur.labels,
        custom_fields: this.customFieldsDraft,
      });
      await this.reloadDetail(cur.key);
      this.showToast(`Saved changes to ${cur.key}`);
    } catch (err: any) {
      this.showToast(`Validation error: ${err?.message || 'Failed to save changes'}`);
    }
  }

  async quickAddSubtask(): Promise<void> {
    const cur = this.item();
    if (!cur || !this.newSubtaskTitle.trim()) return;
    await this.api.createItem({
      project_id: cur.project_id,
      type: 'Subtask',
      category: cur.category || 'New Feature',
      status: 'To Do',
      priority: 'Medium',
      title: this.newSubtaskTitle.trim(),
      parent_id: cur.id,
      sprint_id: cur.sprint_id || 0,
      assignee_id: cur.assignee_id || 0,
      start_date: cur.start_date,
      due_date: cur.due_date,
    });
    this.newSubtaskTitle = '';
    await this.reloadDetail(cur.key);
    this.showToast('Subtask created');
  }

  async addIssueLink(): Promise<void> {
    const cur = this.item();
    if (!cur || this.newLinkTargetId <= 0) return;
    await this.api.addLink(cur.key, this.newLinkTargetId, this.newLinkType);
    this.newLinkTargetId = 0;
    await this.reloadDetail(cur.key);
    this.showToast('Issue link added');
  }

  async removeLink(linkId: number): Promise<void> {
    const cur = this.item();
    if (!cur) return;
    await this.api.deleteLink(linkId);
    await this.reloadDetail(cur.key);
  }

  async uploadFile(event: Event): Promise<void> {
    const cur = this.item();
    const input = event.target as HTMLInputElement;
    if (!cur || !input.files || input.files.length === 0) return;
    const file = input.files[0];
    await this.api.uploadAttachment(cur.key, file);
    input.value = '';
    await this.reloadDetail(cur.key);
    this.showToast(`Uploaded '${file.name}' to blobs/`);
  }

  async deleteAttachment(attId: number): Promise<void> {
    const cur = this.item();
    if (!cur) return;
    await this.api.deleteAttachment(attId);
    await this.reloadDetail(cur.key);
    this.showToast('Attachment removed from blobs/');
  }

  async postComment(): Promise<void> {
    const cur = this.item();
    if (!cur || !this.newCommentText.trim()) return;
    await this.api.addComment(cur.key, this.newCommentText.trim());
    this.newCommentText = '';
    await this.reloadDetail(cur.key);
  }

  async deleteCurrentItem(): Promise<void> {
    const cur = this.item();
    if (!cur) return;
    await this.api.deleteItem(cur.key);
    await this.router.navigate(['/board']);
  }

  // Step 10: Rich Text & Collaboration Handlers
  startEditDescription(currentDesc: string): void {
    this.descriptionDraft = currentDesc || '';
    this.isEditingDescription.set(true);
  }

  cancelEditDescription(): void {
    this.isEditingDescription.set(false);
  }

  async saveDescription(): Promise<void> {
    const cur = this.item();
    if (!cur) return;
    cur.description = this.descriptionDraft;
    await this.saveChanges();
    this.isEditingDescription.set(false);
    this.showToast('Description updated successfully');
  }

  async toggleWatch(it: WorkItemDetail): Promise<void> {
    if (this.isTogglingWatch()) return;
    this.isTogglingWatch.set(true);
    try {
      const res = it.is_watching
        ? await this.api.unwatchItem(it.key)
        : await this.api.watchItem(it.key);
      this.item.update((prev) =>
        prev
          ? {
              ...prev,
              is_watching: res.is_watching,
              watchers_count: res.watchers_count,
              watchers_info: res,
            }
          : prev
      );
      this.showToast(res.is_watching ? 'Now watching this issue' : 'Stopped watching this issue');
    } catch (err: any) {
      this.showToast(err.message || 'Failed to update watch status');
    } finally {
      this.isTogglingWatch.set(false);
    }
  }

  async toggleVote(it: WorkItemDetail): Promise<void> {
    if (this.isTogglingVote()) return;
    this.isTogglingVote.set(true);
    try {
      const res = it.has_voted
        ? await this.api.unvoteItem(it.key)
        : await this.api.voteItem(it.key);
      this.item.update((prev) =>
        prev
          ? {
              ...prev,
              has_voted: res.has_voted,
              votes_count: res.votes_count,
              votes_info: res,
            }
          : prev
      );
      this.showToast(res.has_voted ? 'Vote registered!' : 'Vote removed');
    } catch (err: any) {
      this.showToast(err.message || 'Failed to update vote');
    } finally {
      this.isTogglingVote.set(false);
    }
  }

  // Step 7: Time Tracking & Estimation Methods
  getSpentPercent(it: WorkItemDetail): number {
    const orig = it.original_estimate_seconds || 0;
    const rem = it.remaining_estimate_seconds || 0;
    const spent = it.time_spent_seconds || 0;
    const total = Math.max(orig, spent + rem);
    if (total <= 0) return 0;
    return Math.min(100, Math.round((spent / total) * 100));
  }

  getRemainingPercent(it: WorkItemDetail): number {
    const orig = it.original_estimate_seconds || 0;
    const rem = it.remaining_estimate_seconds || 0;
    const spent = it.time_spent_seconds || 0;
    const total = Math.max(orig, spent + rem);
    if (total <= 0) return 0;
    return Math.min(100 - this.getSpentPercent(it), Math.round((rem / total) * 100));
  }

  openLogWorkModal(wl?: WorklogItem): void {
    const now = new Date();
    // format as YYYY-MM-DDTHH:MM
    const localIso = new Date(now.getTime() - now.getTimezoneOffset() * 60000)
      .toISOString()
      .slice(0, 16);

    if (wl) {
      this.editingWorklogId.set(wl.id);
      this.worklogDraft = {
        duration: wl.time_spent_formatted || '1h',
        started_at: wl.started_at ? wl.started_at.replace(' ', 'T').slice(0, 16) : localIso,
        description: wl.description || '',
        adjustment_mode: (wl.adjustment_mode as WorklogAdjustmentMode) || 'auto',
        new_estimate: '',
        reduce_by: '',
      };
      this.onDurationChange(this.worklogDraft.duration);
    } else {
      this.editingWorklogId.set(null);
      this.worklogDraft = {
        duration: '2h',
        started_at: localIso,
        description: '',
        adjustment_mode: 'auto',
        new_estimate: '',
        reduce_by: '',
      };
      this.onDurationChange('2h');
    }
    this.logWorkError.set('');
    this.showLogWorkModal.set(true);
  }

  closeLogWorkModal(): void {
    this.showLogWorkModal.set(false);
    this.editingWorklogId.set(null);
    this.logWorkError.set('');
  }

  async onDurationChange(val: string): Promise<void> {
    this.worklogDraft.duration = val;
    if (!val.trim()) {
      this.durationPreview.set({ valid: false, formatted: '', seconds: 0, error: 'Duration cannot be empty' });
      return;
    }
    try {
      const res = await this.api.parseDuration({ duration: val });
      this.durationPreview.set({
        valid: res.valid,
        formatted: res.formatted,
        seconds: res.seconds,
        error: res.error || '',
      });
    } catch (e: any) {
      this.durationPreview.set({ valid: false, formatted: '', seconds: 0, error: e?.message || 'Invalid format' });
    }
  }

  applyDurationPreset(preset: string): void {
    const cur = this.worklogDraft.duration.trim();
    if (!cur) {
      this.worklogDraft.duration = preset;
    } else {
      this.worklogDraft.duration = `${cur} ${preset}`;
    }
    this.onDurationChange(this.worklogDraft.duration);
  }

  async submitLogWork(): Promise<void> {
    const cur = this.item();
    if (!cur) return;
    this.logWorkError.set('');

    const dur = this.worklogDraft.duration.trim();
    if (!dur) {
      this.logWorkError.set('Time spent duration is required (e.g. 2h 30m)');
      return;
    }

    try {
      const payload: any = {
        duration: dur,
        started_at: this.worklogDraft.started_at ? this.worklogDraft.started_at.replace('T', ' ') : '',
        description: this.worklogDraft.description,
        adjustment_mode: this.worklogDraft.adjustment_mode,
      };

      if (this.worklogDraft.adjustment_mode === 'set') {
        if (!this.worklogDraft.new_estimate.trim()) {
          this.logWorkError.set("Please provide a new remaining estimate duration (e.g. '4h')");
          return;
        }
        payload.new_estimate = this.worklogDraft.new_estimate.trim();
      } else if (this.worklogDraft.adjustment_mode === 'reduce') {
        if (!this.worklogDraft.reduce_by.trim()) {
          this.logWorkError.set("Please provide an amount to reduce the remaining estimate by (e.g. '1h')");
          return;
        }
        payload.reduce_by = this.worklogDraft.reduce_by.trim();
      }

      const wlId = this.editingWorklogId();
      if (wlId) {
        await this.api.updateWorklog(wlId, payload);
        this.showToast('Worklog updated successfully');
      } else {
        await this.api.addWorklog(cur.key, payload);
        this.showToast('Logged work successfully');
      }

      this.closeLogWorkModal();
      await this.reloadDetail(cur.key);
    } catch (err: any) {
      this.logWorkError.set(err?.message || 'Failed to log work');
    }
  }

  async deleteWorklog(wlId: number): Promise<void> {
    const cur = this.item();
    if (!cur) return;
    if (!confirm('Are you sure you want to delete this worklog?')) return;
    try {
      await this.api.deleteWorklog(wlId, 'auto');
      await this.reloadDetail(cur.key);
      this.showToast('Worklog deleted');
    } catch (err: any) {
      this.showToast(`Error deleting worklog: ${err?.message || 'Failed'}`);
    }
  }

  async saveEstimateChanges(): Promise<void> {
    const cur = this.item();
    if (!cur) return;
    const origStr = this.originalEstimateInput().trim();
    const remStr = this.remainingEstimateInput().trim();
    try {
      await this.api.updateItem(cur.key, {
        original_estimate: origStr,
        remaining_estimate: remStr,
        tshirt_size: this.tshirtSizeInput() || '',
      } as any);
      await this.reloadDetail(cur.key);
      this.showToast('Updated estimates');
    } catch (err: any) {
      this.showToast(`Estimate error: ${err?.message || 'Invalid format'}`);
    }
  }

  async onTshirtChange(newSize: string): Promise<void> {
    this.tshirtSizeInput.set(newSize);
    // Auto-suggest estimates based on T-Shirt size if estimates are currently empty
    const sizeToHours: Record<string, string> = {
      XS: '2h',
      S: '4h',
      M: '1d',
      L: '2d',
      XL: '1w',
      XXL: '2w',
    };
    if (newSize && sizeToHours[newSize] && !this.originalEstimateInput()) {
      this.originalEstimateInput.set(sizeToHours[newSize]);
      this.remainingEstimateInput.set(sizeToHours[newSize]);
    }
    await this.saveEstimateChanges();
  }

  showToast(msg: string): void {
    this.toastMsg.set(msg);
    setTimeout(() => {
      if (this.toastMsg() === msg) this.toastMsg.set('');
    }, 3000);
  }
}
