import { Component, OnInit, OnDestroy, inject, signal, computed } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { ActivatedRoute, Router, RouterLink } from '@angular/router';
import { ApiService } from '../../services/api.service';
import { RichEditorComponent } from '../../components/rich-editor/rich-editor.component';
import { MarkdownViewerComponent } from '../../components/rich-editor/markdown-viewer.component';
import {
  AvailableTransition,
  IssueSecurityLevel,
  PRIORITIES,
  ScreenDef,
  ScreenTab,
  SmartCommitSimulationResult,
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
            @if (it.security_level_name) {
              <span class="badge" style="background: #fffbe6; color: #d46b08; border: 1px solid #ffe58f; font-weight: 600; padding: 2px 8px; border-radius: 4px;">
                🔒 {{ it.security_level_name }}
              </span>
            }
            <strong class="issue-key-lg">{{ it.key }}</strong>
          </div>
          <div class="toolbar-actions">
            <button type="button" class="btn btn-secondary btn-sm" (click)="openCloneModal()" title="Duplicate this work item">
              📋 Clone
            </button>
            <button type="button" class="btn btn-secondary btn-sm" (click)="openMoveModal()" title="Move project or convert issue type">
              ↗ Move
            </button>
            <a
              routerLink="/create"
              [queryParams]="{ type: 'Subtask', parent_id: it.id }"
              class="btn btn-secondary btn-sm"
            >
              + Create Subtask
            </a>
            <button type="button" class="btn btn-danger btn-sm" (click)="deleteCurrentItem()" title="Move issue to Trash Bin (30-day retention)">
              🗑 Delete
            </button>
          </div>
        </div>

        <!-- Step 16 / Epic 14.3: Real-Time Concurrent Viewers Alert -->
        @if (activeOtherViewers().length > 0) {
          <div style="margin: 8px 0;">
            <div class="concurrent-viewers-bar">
              <div class="viewers-avatars">
                @for (u of activeOtherViewers(); track u.user_id) {
                  <span class="avatar-sm" [style.background]="u.avatar_color" [title]="u.display_name + ' is ' + u.action">
                    {{ u.display_name.slice(0, 2).toUpperCase() }}
                  </span>
                }
              </div>
              <span class="viewers-text">
                <strong>{{ formatViewersText() }}</strong>
              </span>
            </div>
          </div>
        }

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
                <button
                  type="button"
                  class="tab-btn"
                  [class.active]="activeTab() === 'development'"
                  (click)="activeTab.set('development')"
                >
                  🛠 Development ({{ getDevCount(it) }})
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
                  <div style="display: flex; justify-content: space-between; align-items: center; margin-top: 8px;">
                    <div style="display: flex; gap: 6px; align-items: center;">
                      @if (api.cannedResponses().length > 0) {
                        <select
                          class="form-control"
                          style="font-size: 12px; padding: 4px 8px; width: auto;"
                          (change)="insertCannedResponseIntoComment($event)"
                        >
                          <option value="">💬 Insert Canned Response...</option>
                          @for (cr of api.cannedResponses(); track cr.id) {
                            <option [value]="cr.id">{{ cr.title }} ({{ cr.shortcut }})</option>
                          }
                        </select>
                      }
                    </div>
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
              } @else if (activeTab() === 'development') {
                <div class="dev-tab-container">
                  <div style="display: flex; align-items: center; justify-content: space-between; margin-bottom: 12px; padding-bottom: 8px; border-bottom: 1px solid #ebecf0;">
                    <div>
                      <span style="font-size: 13px; color: #5e6c84;">Linked Development Assets:</span>
                      <strong style="margin-left: 6px; color: #0052cc; font-size: 14px;">{{ getDevCount(it) }} items</strong>
                    </div>
                    <div style="display: flex; gap: 8px;">
                      <button type="button" class="btn btn-primary btn-sm" (click)="openLinkDevModal()">
                        + Link VCS Entity
                      </button>
                      <button type="button" class="btn btn-secondary btn-sm" (click)="openSmartCommitModal()">
                        ⚡ Simulate Smart Commit
                      </button>
                      <button type="button" class="btn btn-ghost btn-sm" (click)="reloadDevOnly(it.key)">
                        ↻ Refresh
                      </button>
                    </div>
                  </div>

                  <!-- Branches -->
                  <div class="dev-section-card">
                    <div class="dev-section-header">
                      <h4>🌿 Branches ({{ it.development?.branches_count || 0 }})</h4>
                      <small class="text-muted">Linked Git branches for this issue</small>
                    </div>
                    @if (it.development?.branches && it.development!.branches.length > 0) {
                      <table class="dev-table">
                        <thead>
                          <tr>
                            <th>Repository</th>
                            <th>Branch Name</th>
                            <th>Last Commit</th>
                            <th>Updated</th>
                            <th style="text-align: right;">Action</th>
                          </tr>
                        </thead>
                        <tbody>
                          @for (b of it.development!.branches; track b.id) {
                            <tr>
                              <td><strong>{{ b.repository_name || 'main' }}</strong></td>
                              <td>
                                @if (b.branch_url) {
                                  <a [href]="b.branch_url" target="_blank" class="dev-mini-link">{{ b.branch_name }}</a>
                                } @else {
                                  <code>{{ b.branch_name }}</code>
                                }
                              </td>
                              <td>
                                @if (b.last_commit_hash) {
                                  <span class="commit-hash-pill">{{ b.last_commit_hash.slice(0, 7) }}</span>
                                } @else {
                                  <span class="text-muted">—</span>
                                }
                              </td>
                              <td class="text-muted">{{ b.updated_at }}</td>
                              <td style="text-align: right;">
                                <button type="button" class="btn-unlink" (click)="unlinkDev('branches', b.id)" title="Unlink branch">✕</button>
                              </td>
                            </tr>
                          }
                        </tbody>
                      </table>
                    } @else {
                      <div class="dev-empty-hint">No branches linked yet. Branches containing issue key (e.g. <code>feature/{{ it.key }}-login</code>) link automatically.</div>
                    }
                  </div>

                  <!-- Commits -->
                  <div class="dev-section-card">
                    <div class="dev-section-header">
                      <h4>📦 Commits ({{ it.development?.commits_count || 0 }})</h4>
                      <small class="text-muted">Commits referencing {{ it.key }}</small>
                    </div>
                    @if (it.development?.commits && it.development!.commits.length > 0) {
                      <table class="dev-table">
                        <thead>
                          <tr>
                            <th>Hash</th>
                            <th>Commit Message</th>
                            <th>Author</th>
                            <th>Committed</th>
                            <th style="text-align: right;">Action</th>
                          </tr>
                        </thead>
                        <tbody>
                          @for (c of it.development!.commits; track c.id) {
                            <tr>
                              <td>
                                @if (c.commit_url) {
                                  <a [href]="c.commit_url" target="_blank" class="commit-hash-pill">{{ c.short_hash || c.commit_hash.slice(0, 7) }}</a>
                                } @else {
                                  <span class="commit-hash-pill">{{ c.short_hash || c.commit_hash.slice(0, 7) }}</span>
                                }
                              </td>
                              <td>
                                <div>{{ c.message }}</div>
                                <small class="text-muted">{{ c.repository_name }}</small>
                              </td>
                              <td>
                                <div><strong>{{ c.author_name }}</strong></div>
                                <small class="text-muted">{{ c.author_email }}</small>
                              </td>
                              <td class="text-muted">{{ c.committed_at }}</td>
                              <td style="text-align: right;">
                                <button type="button" class="btn-unlink" (click)="unlinkDev('commits', c.id)" title="Unlink commit">✕</button>
                              </td>
                            </tr>
                          }
                        </tbody>
                      </table>
                    } @else {
                      <div class="dev-empty-hint">No commits linked yet. Mention <code>{{ it.key }}</code> in commit messages to link them.</div>
                    }
                  </div>

                  <!-- Pull Requests -->
                  <div class="dev-section-card">
                    <div class="dev-section-header">
                      <h4>🔀 Pull Requests ({{ it.development?.pull_requests_count || 0 }})</h4>
                      <small class="text-muted">PRs / Merge Requests linked to this issue</small>
                    </div>
                    @if (it.development?.pull_requests && it.development!.pull_requests.length > 0) {
                      <table class="dev-table">
                        <thead>
                          <tr>
                            <th>PR</th>
                            <th>Title &amp; Branches</th>
                            <th>Status</th>
                            <th>Author</th>
                            <th style="text-align: right;">Action</th>
                          </tr>
                        </thead>
                        <tbody>
                          @for (pr of it.development!.pull_requests; track pr.id) {
                            <tr>
                              <td><strong>#{{ pr.pr_number }}</strong></td>
                              <td>
                                <div>
                                  @if (pr.pr_url) {
                                    <a [href]="pr.pr_url" target="_blank" class="dev-mini-link">{{ pr.title }}</a>
                                  } @else {
                                    <span>{{ pr.title }}</span>
                                  }
                                </div>
                                <small class="text-muted"><code>{{ pr.source_branch }}</code> → <code>{{ pr.target_branch }}</code></small>
                              </td>
                              <td>
                                <span class="pr-pill" [class]="pr.status.toLowerCase()">{{ pr.status }}</span>
                              </td>
                              <td>{{ pr.author_name || 'Git User' }}</td>
                              <td style="text-align: right;">
                                <button type="button" class="btn-unlink" (click)="unlinkDev('pull-requests', pr.id)" title="Unlink PR">✕</button>
                              </td>
                            </tr>
                          }
                        </tbody>
                      </table>
                    } @else {
                      <div class="dev-empty-hint">No pull requests linked yet. Mention <code>{{ it.key }}</code> in PR titles or source branch names.</div>
                    }
                  </div>

                  <!-- Builds -->
                  <div class="dev-section-card">
                    <div class="dev-section-header">
                      <h4>⚡ CI/CD Builds ({{ it.development?.builds_count || 0 }})</h4>
                      <small class="text-muted">Automated test &amp; pipeline builds</small>
                    </div>
                    @if (it.development?.builds && it.development!.builds.length > 0) {
                      <table class="dev-table">
                        <thead>
                          <tr>
                            <th>Pipeline</th>
                            <th>Build #</th>
                            <th>Status</th>
                            <th>Commit / Branch</th>
                            <th>Duration</th>
                            <th style="text-align: right;">Action</th>
                          </tr>
                        </thead>
                        <tbody>
                          @for (bd of it.development!.builds; track bd.id) {
                            <tr>
                              <td><strong>{{ bd.pipeline_id }}</strong></td>
                              <td>#{{ bd.build_number }}</td>
                              <td>
                                <span class="build-badge" [class]="bd.status.toLowerCase()">
                                  {{ bd.status === 'SUCCESS' ? '✓ Passed' : bd.status === 'FAILED' ? '✗ Failed' : bd.status }}
                                </span>
                              </td>
                              <td>
                                @if (bd.commit_hash) {
                                  <span class="commit-hash-pill">{{ bd.commit_hash.slice(0, 7) }}</span>
                                }
                                @if (bd.branch_name) {
                                  <small class="text-muted" style="margin-left: 6px;">{{ bd.branch_name }}</small>
                                }
                              </td>
                              <td>{{ bd.duration_seconds }}s</td>
                              <td style="text-align: right;">
                                <button type="button" class="btn-unlink" (click)="unlinkDev('builds', bd.id)" title="Unlink build">✕</button>
                              </td>
                            </tr>
                          }
                        </tbody>
                      </table>
                    } @else {
                      <div class="dev-empty-hint">No build runs recorded yet.</div>
                    }
                  </div>
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

            <!-- ITSM & Service Desk SLA Panel (Step 15) -->
            @if (it.service_desk_id || (it.slas && it.slas.length > 0) || it.is_customer_request) {
              <div class="card-panel" style="border-left: 4px solid #0052cc;">
                <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px;">
                  <h3 style="margin: 0; font-size: 14px; font-weight: 700; color: #172b4d;">⏱️ SLA Targets &amp; Service Desk</h3>
                  @if (it.service_desk_name) {
                    <span class="badge badge-info" style="font-size: 10px;">{{ it.service_desk_name }}</span>
                  }
                </div>

                @if (it.customer_reporter_name) {
                  <div style="font-size: 12px; margin-bottom: 10px; color: #42526e; display: flex; flex-direction: column; gap: 2px;">
                    <span>Customer: <strong>{{ it.customer_reporter_name }}</strong></span>
                    @if (it.customer_reporter_email) {
                      <span style="color: #5e6c84;">{{ it.customer_reporter_email }}</span>
                    }
                  </div>
                }

                @if (it.slas && it.slas.length > 0) {
                  <div style="display: flex; flex-direction: column; gap: 8px;">
                    @for (sla of it.slas; track sla.id) {
                      <div [style.border-left]="'3px solid ' + (sla.badge_color || '#36b37e')" style="background: #fafbfc; padding: 6px 10px; border-radius: 4px; border: 1px solid #ebecf0; border-left-width: 4px;">
                        <div style="display: flex; justify-content: space-between; font-size: 11px; margin-bottom: 3px;">
                          <span style="font-weight: 600; color: #344563;">{{ sla.name }}</span>
                          <span [style.color]="sla.badge_color || '#36b37e'" style="font-weight: 700;">
                            @if (sla.is_completed) {
                              ✓ Met
                            } @else if (sla.is_paused) {
                              ⏸ Paused
                            } @else if (sla.is_breached) {
                              🔥 Breached
                            } @else {
                              ⏱ Within
                            }
                          </span>
                        </div>
                        <div style="display: flex; justify-content: space-between; font-size: 12px;">
                          <span style="color: #5e6c84;">{{ sla.elapsed_formatted }} / {{ sla.target_formatted }}</span>
                          <strong [style.color]="sla.badge_color || '#36b37e'">{{ sla.remaining_formatted }}</strong>
                        </div>
                      </div>
                    }
                  </div>
                }
              </div>
            }

            <!-- Development Panel in Sidebar (Step 14) -->
            <div class="card-panel dev-sidebar-panel">
              <div class="dev-header">
                <h3>🛠 Development</h3>
                <div class="dev-quick-actions">
                  <button type="button" class="btn-subtle" (click)="openLinkDevModal()" title="Link Branch, Commit, PR, or Build">+ Link</button>
                  <button type="button" class="btn-subtle" (click)="openSmartCommitModal()" title="Simulate / Run Smart Commit">⚡ Commit</button>
                </div>
              </div>

              <div class="dev-rollup-grid">
                <div class="dev-stat-box" (click)="activeTab.set('development')" title="View Branches">
                  <div class="dev-stat-val">{{ it.development?.branches_count || 0 }}</div>
                  <div class="dev-stat-label">Branches</div>
                </div>
                <div class="dev-stat-box" (click)="activeTab.set('development')" title="View Commits">
                  <div class="dev-stat-val">{{ it.development?.commits_count || 0 }}</div>
                  <div class="dev-stat-label">Commits</div>
                </div>
                <div class="dev-stat-box" (click)="activeTab.set('development')" title="View Pull Requests">
                  <div class="dev-stat-val">{{ it.development?.pull_requests_count || 0 }}</div>
                  <div class="dev-stat-label">PRs</div>
                </div>
                <div class="dev-stat-box" (click)="activeTab.set('development')" title="View Builds">
                  <div class="dev-stat-val">{{ it.development?.builds_count || 0 }}</div>
                  <div class="dev-stat-label">Builds</div>
                </div>
              </div>

              @if (it.development && (it.development.branches_count > 0 || it.development.commits_count > 0 || it.development.pull_requests_count > 0 || it.development.builds_count > 0)) {
                <div class="dev-recent-mini">
                  @for (pr of it.development.pull_requests.slice(0, 2); track pr.id) {
                    <div class="dev-mini-item">
                      <span class="pr-pill" [class]="pr.status.toLowerCase()">{{ pr.status }}</span>
                      <a [href]="pr.pr_url || '#'" target="_blank" class="dev-mini-link">PR #{{ pr.pr_number }}: {{ pr.title }}</a>
                    </div>
                  }
                  @for (b of it.development.builds.slice(0, 2); track b.id) {
                    <div class="dev-mini-item">
                      <span class="build-badge" [class]="b.status.toLowerCase()">{{ b.status === 'SUCCESS' ? '✓ Passed' : b.status === 'FAILED' ? '✗ Failed' : b.status }}</span>
                      <span class="dev-mini-link">{{ b.pipeline_id }} #{{ b.build_number }}</span>
                    </div>
                  }
                </div>
              } @else {
                <div class="dev-empty-hint">
                  <span>No branches, commits, or pull requests linked.</span>
                </div>
              }
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
                <label for="detail-security-level">Security Level</label>
                <select
                  id="detail-security-level"
                  name="detail_security_level_id"
                  [(ngModel)]="it.security_level_id"
                  (change)="saveChanges()"
                >
                  <option [ngValue]="null">— None (Public) —</option>
                  @for (sec of securityLevels(); track sec.id) {
                    <option [ngValue]="sec.id">🔒 {{ sec.name }}</option>
                  }
                </select>
                @if (it.security_level_name) {
                  <small class="text-muted" style="display: block; margin-top: 4px;">
                    Current level: <strong>{{ it.security_level_name }}</strong>
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

        <!-- Link VCS Entity Modal -->
        @if (showLinkDevModal()) {
          <div
            style="position: fixed; inset: 0; background: rgba(15, 23, 42, 0.55); z-index: 1200; display: flex; align-items: center; justify-content: center; padding: 16px;"
            (click)="closeLinkDevModal()"
          >
            <div
              class="card-panel"
              style="width: 100%; max-width: 520px; max-height: 85vh; overflow-y: auto; box-shadow: 0 20px 45px rgba(0,0,0,0.25);"
              (click)="$event.stopPropagation()"
            >
              <div class="section-header" style="margin-bottom: 12px;">
                <div>
                  <h3 style="margin: 0;">Link Development Asset</h3>
                  <small class="text-muted">Link a branch, commit, PR, or build run to {{ it.key }}</small>
                </div>
                <button type="button" class="btn btn-ghost btn-sm" (click)="closeLinkDevModal()">✕</button>
              </div>

              @if (linkDevError()) {
                <div style="margin-bottom: 12px; padding: 8px 12px; background: #ffebe6; color: #de350b; border-radius: 4px; font-size: 12px;">
                  {{ linkDevError() }}
                </div>
              }

              <div class="form-group">
                <label>Asset Type</label>
                <select [(ngModel)]="linkDevDraft.type" name="link_dev_type">
                  <option value="branch">🌿 Branch</option>
                  <option value="commit">📦 Commit</option>
                  <option value="pull_request">🔀 Pull Request</option>
                  <option value="build">⚡ CI/CD Build</option>
                </select>
              </div>

              <div class="form-group">
                <label>Repository Name</label>
                <input type="text" [(ngModel)]="linkDevDraft.repository_name" name="link_dev_repo" placeholder="e.g. org/repo" />
              </div>

              @if (linkDevDraft.type === 'branch') {
                <div class="form-group">
                  <label>Branch Name *</label>
                  <input type="text" [(ngModel)]="linkDevDraft.branch_name" name="link_branch_name" placeholder="e.g. feature/{{ it.key }}-login" />
                </div>
                <div class="form-group">
                  <label>Branch URL (optional)</label>
                  <input type="text" [(ngModel)]="linkDevDraft.branch_url" name="link_branch_url" placeholder="https://github.com/..." />
                </div>
                <div class="form-group">
                  <label>Last Commit Hash (optional)</label>
                  <input type="text" [(ngModel)]="linkDevDraft.last_commit_hash" name="link_branch_hash" placeholder="e.g. a1b2c3d4" />
                </div>
              } @else if (linkDevDraft.type === 'commit') {
                <div class="form-group">
                  <label>Commit Hash *</label>
                  <input type="text" [(ngModel)]="linkDevDraft.commit_hash" name="link_commit_hash" placeholder="e.g. 7f8a9b0c..." />
                </div>
                <div class="form-group">
                  <label>Commit Message</label>
                  <textarea [(ngModel)]="linkDevDraft.message" name="link_commit_msg" rows="2" placeholder="e.g. {{ it.key }} #time 1h #comment implemented feature"></textarea>
                </div>
                <div class="grid-2">
                  <div class="form-group">
                    <label>Author Name</label>
                    <input type="text" [(ngModel)]="linkDevDraft.author_name" name="link_commit_author" placeholder="Marcus Vance" />
                  </div>
                  <div class="form-group">
                    <label>Author Email</label>
                    <input type="text" [(ngModel)]="linkDevDraft.author_email" name="link_commit_email" placeholder="marcus@example.com" />
                  </div>
                </div>
                <div class="form-group">
                  <label>Commit URL (optional)</label>
                  <input type="text" [(ngModel)]="linkDevDraft.commit_url" name="link_commit_url" placeholder="https://github.com/.../commit/..." />
                </div>
              } @else if (linkDevDraft.type === 'pull_request') {
                <div class="grid-2">
                  <div class="form-group">
                    <label>PR Number *</label>
                    <input type="number" [(ngModel)]="linkDevDraft.pr_number" name="link_pr_num" />
                  </div>
                  <div class="form-group">
                    <label>Status</label>
                    <select [(ngModel)]="linkDevDraft.status" name="link_pr_status">
                      <option value="OPEN">OPEN</option>
                      <option value="MERGED">MERGED</option>
                      <option value="DECLINED">DECLINED</option>
                    </select>
                  </div>
                </div>
                <div class="form-group">
                  <label>PR Title *</label>
                  <input type="text" [(ngModel)]="linkDevDraft.title" name="link_pr_title" placeholder="e.g. [{{ it.key }}] Add authentication endpoints" />
                </div>
                <div class="grid-2">
                  <div class="form-group">
                    <label>Source Branch</label>
                    <input type="text" [(ngModel)]="linkDevDraft.source_branch" name="link_pr_src" placeholder="feature/{{ it.key }}" />
                  </div>
                  <div class="form-group">
                    <label>Target Branch</label>
                    <input type="text" [(ngModel)]="linkDevDraft.target_branch" name="link_pr_tgt" placeholder="main" />
                  </div>
                </div>
                <div class="form-group">
                  <label>PR URL (optional)</label>
                  <input type="text" [(ngModel)]="linkDevDraft.pr_url" name="link_pr_url" placeholder="https://github.com/.../pull/1" />
                </div>
              } @else if (linkDevDraft.type === 'build') {
                <div class="grid-2">
                  <div class="form-group">
                    <label>Pipeline Name</label>
                    <input type="text" [(ngModel)]="linkDevDraft.pipeline_id" name="link_bd_pipe" placeholder="e.g. GitHub Actions" />
                  </div>
                  <div class="form-group">
                    <label>Build / Run #</label>
                    <input type="text" [(ngModel)]="linkDevDraft.build_number" name="link_bd_num" placeholder="e.g. 142" />
                  </div>
                </div>
                <div class="grid-2">
                  <div class="form-group">
                    <label>Status</label>
                    <select [(ngModel)]="linkDevDraft.build_status" name="link_bd_status">
                      <option value="SUCCESS">SUCCESS</option>
                      <option value="FAILED">FAILED</option>
                      <option value="IN_PROGRESS">IN_PROGRESS</option>
                      <option value="CANCELLED">CANCELLED</option>
                    </select>
                  </div>
                  <div class="form-group">
                    <label>Duration (seconds)</label>
                    <input type="number" [(ngModel)]="linkDevDraft.duration_seconds" name="link_bd_dur" />
                  </div>
                </div>
                <div class="form-group">
                  <label>Build URL (optional)</label>
                  <input type="text" [(ngModel)]="linkDevDraft.build_url" name="link_bd_url" placeholder="https://..." />
                </div>
              }

              <div style="display: flex; justify-content: flex-end; gap: 8px; margin-top: 16px;">
                <button type="button" class="btn btn-secondary btn-sm" (click)="closeLinkDevModal()">Cancel</button>
                <button type="button" class="btn btn-primary btn-sm" [disabled]="isLinkingDev()" (click)="submitLinkDev()">
                  {{ isLinkingDev() ? 'Linking...' : 'Link Asset' }}
                </button>
              </div>
            </div>
          </div>
        }

        <!-- Clone Issue Modal (Step 17) -->
        @if (showCloneModal()) {
          <div
            style="position: fixed; inset: 0; background: rgba(15, 23, 42, 0.55); z-index: 1200; display: flex; align-items: center; justify-content: center; padding: 16px;"
            (click)="closeCloneModal()"
          >
            <div
              class="card-panel"
              style="width: 100%; max-width: 520px; max-height: 85vh; overflow-y: auto; box-shadow: 0 20px 45px rgba(0,0,0,0.25); background: var(--card-bg, #ffffff); border-radius: 8px; padding: 20px;"
              (click)="$event.stopPropagation()"
            >
              <div class="section-header" style="display: flex; justify-content: space-between; align-items: flex-start; margin-bottom: 14px;">
                <div>
                  <h3 style="margin: 0; font-size: 18px;">📋 Clone Issue ({{ it.key }})</h3>
                  <small class="text-muted" style="color: #6b778c; font-size: 12px;">Create an exact duplicate with customizable links and subtasks</small>
                </div>
                <button type="button" class="btn btn-ghost btn-sm" (click)="closeCloneModal()">✕</button>
              </div>

              @if (cloneError()) {
                <div style="margin-bottom: 12px; padding: 8px 12px; background: #ffebe6; color: #de350b; border-radius: 4px; font-size: 12px;">
                  {{ cloneError() }}
                </div>
              }

              <div class="form-group" style="margin-bottom: 12px;">
                <label style="font-size: 12px; font-weight: 600; display: block; margin-bottom: 4px;">Cloned Summary *</label>
                <input
                  type="text"
                  [(ngModel)]="cloneDraft.summary"
                  name="clone_summary"
                  class="form-control"
                  style="width: 100%; padding: 8px 10px; border: 1px solid #dfe1e6; border-radius: 4px; font-size: 13px;"
                  placeholder="e.g. CLONE - {{ it.title }}"
                />
              </div>

              <div class="form-group" style="margin-bottom: 12px;">
                <label style="font-size: 12px; font-weight: 600; display: block; margin-bottom: 4px;">Target Project</label>
                <select [(ngModel)]="cloneDraft.project_id" name="clone_project_id" class="form-control" style="width: 100%; padding: 8px 10px; border: 1px solid #dfe1e6; border-radius: 4px; font-size: 13px;">
                  @for (p of api.projects(); track p.id) {
                    <option [value]="p.id">{{ p.name }} ({{ p.key }})</option>
                  }
                </select>
              </div>

              <div style="display: flex; flex-direction: column; gap: 10px; margin: 16px 0; background: var(--bg-subtle, #f4f5f7); padding: 12px; border-radius: 6px;">
                <label style="display: flex; align-items: center; gap: 8px; font-size: 13px; cursor: pointer;">
                  <input type="checkbox" [(ngModel)]="cloneDraft.clone_subtasks" name="clone_subtasks" />
                  <span>Clone child subtasks ({{ it.subtasks.length || 0 }} subtasks)</span>
                </label>
                <label style="display: flex; align-items: center; gap: 8px; font-size: 13px; cursor: pointer;">
                  <input type="checkbox" [(ngModel)]="cloneDraft.clone_attachments" name="clone_attachments" />
                  <span>Clone attachments &amp; files ({{ it.attachments.length || 0 }} files)</span>
                </label>
                <label style="display: flex; align-items: center; gap: 8px; font-size: 13px; cursor: pointer;">
                  <input type="checkbox" [(ngModel)]="cloneDraft.clone_links" name="clone_links" />
                  <span>Clone existing issue links (relates to, blocks, etc.)</span>
                </label>
              </div>

              <div style="display: flex; justify-content: flex-end; gap: 10px; margin-top: 16px;">
                <button type="button" class="btn btn-secondary btn-sm" (click)="closeCloneModal()">Cancel</button>
                <button type="button" class="btn btn-primary btn-sm" (click)="submitClone()" [disabled]="isCloning()">
                  {{ isCloning() ? "Cloning..." : "Create Clone" }}
                </button>
              </div>
            </div>
          </div>
        }

        <!-- Move / Convert Issue Modal (Step 17) -->
        @if (showMoveModal()) {
          <div
            style="position: fixed; inset: 0; background: rgba(15, 23, 42, 0.55); z-index: 1200; display: flex; align-items: center; justify-content: center; padding: 16px;"
            (click)="closeMoveModal()"
          >
            <div
              class="card-panel"
              style="width: 100%; max-width: 520px; max-height: 85vh; overflow-y: auto; box-shadow: 0 20px 45px rgba(0,0,0,0.25); background: var(--card-bg, #ffffff); border-radius: 8px; padding: 20px;"
              (click)="$event.stopPropagation()"
            >
              <div class="section-header" style="display: flex; justify-content: space-between; align-items: flex-start; margin-bottom: 14px;">
                <div>
                  <h3 style="margin: 0; font-size: 18px;">↗ Move / Convert Issue ({{ it.key }})</h3>
                  <small class="text-muted" style="color: #6b778c; font-size: 12px;">Transfer across projects or change issue type &amp; status</small>
                </div>
                <button type="button" class="btn btn-ghost btn-sm" (click)="closeMoveModal()">✕</button>
              </div>

              @if (moveError()) {
                <div style="margin-bottom: 12px; padding: 8px 12px; background: #ffebe6; color: #de350b; border-radius: 4px; font-size: 12px;">
                  {{ moveError() }}
                </div>
              }

              <div class="form-group" style="margin-bottom: 12px;">
                <label style="font-size: 12px; font-weight: 600; display: block; margin-bottom: 4px;">Target Project</label>
                <select [(ngModel)]="moveDraft.target_project_id" name="move_project_id" class="form-control" style="width: 100%; padding: 8px 10px; border: 1px solid #dfe1e6; border-radius: 4px; font-size: 13px;">
                  @for (p of api.projects(); track p.id) {
                    <option [value]="p.id">{{ p.name }} ({{ p.key }})</option>
                  }
                </select>
                @if (moveDraft.target_project_id !== it.project_id) {
                  <small style="display: block; margin-top: 4px; color: #ff8b00; font-size: 11px;">
                    ⚠️ Moving to another project re-assigns the issue key using the target project sequence.
                  </small>
                }
              </div>

              <div class="form-group" style="margin-bottom: 12px;">
                <label style="font-size: 12px; font-weight: 600; display: block; margin-bottom: 4px;">Target Issue Type</label>
                <select [(ngModel)]="moveDraft.target_type" name="move_type" class="form-control" style="width: 100%; padding: 8px 10px; border: 1px solid #dfe1e6; border-radius: 4px; font-size: 13px;">
                  <option value="Epic">Epic</option>
                  <option value="Story">Story</option>
                  <option value="Task">Task</option>
                  <option value="Bug">Bug</option>
                  <option value="Subtask">Subtask</option>
                </select>
              </div>

              <div class="form-group" style="margin-bottom: 12px;">
                <label style="font-size: 12px; font-weight: 600; display: block; margin-bottom: 4px;">Target Workflow Status</label>
                <select [(ngModel)]="moveDraft.target_status" name="move_status" class="form-control" style="width: 100%; padding: 8px 10px; border: 1px solid #dfe1e6; border-radius: 4px; font-size: 13px;">
                  <option value="Backlog">Backlog</option>
                  <option value="To Do">To Do</option>
                  <option value="In Progress">In Progress</option>
                  <option value="In Review">In Review</option>
                  <option value="QA / Testing">QA / Testing</option>
                  <option value="Done">Done</option>
                </select>
              </div>

              <div style="display: flex; justify-content: flex-end; gap: 10px; margin-top: 16px;">
                <button type="button" class="btn btn-secondary btn-sm" (click)="closeMoveModal()">Cancel</button>
                <button type="button" class="btn btn-primary btn-sm" (click)="submitMove()" [disabled]="isMoving()">
                  {{ isMoving() ? "Moving..." : "Execute Move" }}
                </button>
              </div>
            </div>
          </div>
        }

        <!-- Smart Commit Simulator & Runner Modal -->
        @if (showSmartCommitModal()) {
          <div
            style="position: fixed; inset: 0; background: rgba(15, 23, 42, 0.55); z-index: 1200; display: flex; align-items: center; justify-content: center; padding: 16px;"
            (click)="closeSmartCommitModal()"
          >
            <div
              class="card-panel"
              style="width: 100%; max-width: 580px; max-height: 85vh; overflow-y: auto; box-shadow: 0 20px 45px rgba(0,0,0,0.25);"
              (click)="$event.stopPropagation()"
            >
              <div class="section-header" style="margin-bottom: 12px;">
                <div>
                  <h3 style="margin: 0;">⚡ Smart Commit Simulator &amp; Runner</h3>
                  <small class="text-muted">Test syntax like <code>{{ it.key }} #resolve #time 1h 30m #comment done</code></small>
                </div>
                <button type="button" class="btn btn-ghost btn-sm" (click)="closeSmartCommitModal()">✕</button>
              </div>

              @if (smartCommitError()) {
                <div style="margin-bottom: 12px; padding: 8px 12px; background: #ffebe6; color: #de350b; border-radius: 4px; font-size: 12px;">
                  {{ smartCommitError() }}
                </div>
              }

              <div class="form-group">
                <label>Commit Message *</label>
                <textarea
                  [(ngModel)]="smartCommitDraft.message"
                  name="sc_msg"
                  rows="3"
                  placeholder="e.g. {{ it.key }} #resolve #time 1h 30m #comment Fixed null pointer and added tests"
                  style="font-family: monospace; font-size: 12.5px;"
                ></textarea>
                <small class="text-muted" style="display: block; margin-top: 4px;">
                  Supported commands: <code>#comment &lt;text&gt;</code>, <code>#time &lt;dur&gt; [note]</code>, <code>#&lt;transition&gt;</code> (e.g. <code>#resolve</code>, <code>#in-progress</code>, <code>#close</code>, <code>#done</code>)
                </small>
              </div>

              <div class="grid-2">
                <div class="form-group">
                  <label>Committer Email</label>
                  <input type="text" [(ngModel)]="smartCommitDraft.author_email" name="sc_author_email" placeholder="e.g. {{ api.currentUser()?.email || 'dev@example.com' }}" />
                </div>
                <div class="form-group">
                  <label>Committer Name</label>
                  <input type="text" [(ngModel)]="smartCommitDraft.author_name" name="sc_author_name" placeholder="e.g. {{ api.currentUser()?.display_name || 'Marcus Vance' }}" />
                </div>
              </div>

              <div style="display: flex; gap: 8px; margin-top: 12px;">
                <button type="button" class="btn btn-secondary btn-sm" [disabled]="isSimulating()" (click)="runSmartCommitSimulate(false)">
                  {{ isSimulating() ? 'Analyzing...' : '🔍 Parse & Preview' }}
                </button>
                <button type="button" class="btn btn-primary btn-sm" [disabled]="isExecutingSmartCommit()" (click)="runSmartCommitSimulate(true)">
                  {{ isExecutingSmartCommit() ? 'Executing...' : '⚡ Execute Smart Commit' }}
                </button>
              </div>

              <!-- Simulation Output Preview -->
              @if (smartCommitSimulation(); as sim) {
                <div style="margin-top: 16px; padding: 12px; background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 6px; font-size: 12px;">
                  <div style="display: flex; justify-content: space-between; margin-bottom: 8px; font-weight: 600;">
                    <span>Execution Preview</span>
                    <span [style.color]="sim.executed ? '#00875a' : '#0052cc'">
                      {{ sim.executed ? '✓ Actions Applied' : 'ℹ Dry Run (Not Saved)' }}
                    </span>
                  </div>

                  <div style="margin-bottom: 6px;">
                    Matched Keys:
                    @for (k of sim.matched_keys; track k) {
                      <strong style="margin-right: 6px; color: #0052cc;">{{ k }}</strong>
                    } @empty {
                      <span class="text-muted">None</span>
                    }
                  </div>

                  <div style="margin-bottom: 8px;">
                    Resolved User: <strong>{{ sim.resolved_user_name }}</strong> (ID: {{ sim.resolved_user_id }})
                  </div>

                  <div style="margin-bottom: 8px;">
                    <strong>Parsed Commands:</strong>
                    @for (c of sim.parsed_commands; track c.raw_command) {
                      <div style="margin: 4px 0; padding: 4px 8px; background: #ffffff; border-radius: 4px; border: 1px solid #ebecf0;">
                        <span class="smart-cmd-tag">{{ c.type }}</span>
                        @if (c.type === 'comment') {
                          <span>Comment: "{{ c.comment_body }}"</span>
                        } @else if (c.type === 'time') {
                          <span>Time: <strong>{{ c.formatted_duration }}</strong> ({{ c.time_seconds }}s) {{ c.time_comment ? '— ' + c.time_comment : '' }}</span>
                        } @else if (c.type === 'transition') {
                          <span>Transition to: <strong>{{ c.target_status }}</strong></span>
                        }
                      </div>
                    } @empty {
                      <div class="text-muted">No commands detected. Enter <code>#comment</code>, <code>#time</code>, or <code>#resolve</code>.</div>
                    }
                  </div>

                  @if (sim.applied_actions && sim.applied_actions.length > 0) {
                    <div style="margin-top: 8px; padding-top: 8px; border-top: 1px solid #e2e8f0;">
                      <strong style="color: #00875a;">Applied Actions:</strong>
                      <ul style="margin: 4px 0 0 16px; padding: 0;">
                        @for (a of sim.applied_actions; track a) {
                          <li>{{ a }}</li>
                        }
                      </ul>
                    </div>
                  }

                  @if (sim.errors && sim.errors.length > 0) {
                    <div style="margin-top: 8px; padding-top: 8px; border-top: 1px solid #ffa39e; color: #cf1322;">
                      <strong>Errors:</strong>
                      <ul style="margin: 4px 0 0 16px; padding: 0;">
                        @for (e of sim.errors; track e) {
                          <li>{{ e }}</li>
                        }
                      </ul>
                    </div>
                  }
                </div>
              }

              <div style="display: flex; justify-content: flex-end; margin-top: 16px;">
                <button type="button" class="btn btn-secondary btn-sm" (click)="closeSmartCommitModal()">Close</button>
              </div>
            </div>
          </div>
        }
      </div>
    }
  `,
})
export class ItemDetailComponent implements OnInit, OnDestroy {
  readonly api = inject(ApiService);
  private readonly route = inject(ActivatedRoute);
  private readonly router = inject(Router);

  private presenceHeartbeat: any = null;

  // Step 16 / Epic 14.3: Real-Time Concurrent Viewers
  readonly activeOtherViewers = computed(() => {
    const curItem = this.item();
    const curUser = this.api.currentUser();
    if (!curItem || !curUser) return [];
    return this.api.activePresences().filter(
      (p) => p.item_id === curItem.id && p.user_id !== curUser.id
    );
  });

  formatViewersText(): string {
    const viewers = this.activeOtherViewers();
    if (viewers.length === 0) return '';
    const names = viewers.map((v) => v.display_name).join(', ');
    const editing = viewers.some((v) => v.action === 'editing' || v.action === 'typing');
    if (editing) {
      return `${names} is currently editing this issue`;
    }
    return `${names} is currently viewing this issue`;
  }

  ngOnDestroy(): void {
    if (this.presenceHeartbeat) {
      clearInterval(this.presenceHeartbeat);
      this.presenceHeartbeat = null;
    }
    const cur = this.item();
    if (cur) {
      this.api.updatePresence(cur.id, cur.key, 'viewing', false);
    }
  }

  readonly states = WORKFLOW_STATES;
  readonly itemTypes = WORK_ITEM_TYPES;
  readonly itemCategories = WORK_ITEM_CATEGORIES;
  readonly priorities = PRIORITIES;

  readonly item = signal<WorkItemDetail | null>(null);
  readonly allItems = signal<WorkItem[]>([]);
  readonly securityLevels = signal<IssueSecurityLevel[]>([]);
  readonly activeTab = signal<'comments' | 'worklogs' | 'history' | 'changelog' | 'development'>('comments');
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

  // Step 14: Developer Tooling & Git Integration Signals
  readonly showLinkDevModal = signal(false);
  readonly showSmartCommitModal = signal(false);
  readonly isLinkingDev = signal(false);
  readonly linkDevError = signal('');
  readonly isSimulating = signal(false);
  readonly isExecutingSmartCommit = signal(false);
  readonly smartCommitError = signal('');
  readonly smartCommitSimulation = signal<SmartCommitSimulationResult | null>(null);

  linkDevDraft = {
    type: 'branch',
    repository_name: 'main-repo',
    branch_name: '',
    branch_url: '',
    last_commit_hash: '',
    commit_hash: '',
    message: '',
    author_name: '',
    author_email: '',
    commit_url: '',
    pr_number: 1,
    title: '',
    source_branch: '',
    target_branch: 'main',
    status: 'OPEN',
    pr_url: '',
    pipeline_id: 'GitHub Actions',
    build_number: '1',
    build_status: 'SUCCESS',
    build_url: '',
    duration_seconds: 60,
  };

  smartCommitDraft = {
    message: '',
    author_email: '',
    author_name: '',
  };

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
    this.api.getCannedResponses().catch(() => {});

    this.route.paramMap.subscribe(async (params) => {
      const idOrKey = params.get('id')?.trim();
      if (idOrKey) {
        await this.reloadDetail(idOrKey);
      }
    });
  }

  async insertCannedResponseIntoComment(event: any): Promise<void> {
    const id = parseInt(event?.target?.value, 10);
    if (!id || !this.item()) return;
    try {
      const res = await this.api.applyCannedResponse({
        canned_response_id: id,
        item_id: this.item()!.id,
      });
      if (res && res.content) {
        this.newCommentText = (this.newCommentText ? this.newCommentText + '\n\n' : '') + res.content;
      }
      if (event?.target) {
        event.target.value = '';
      }
    } catch {}
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

    try {
      const secLevels = await this.api.getSecurityLevels(d.project_id);
      this.securityLevels.set(secLevels);
    } catch {
      this.securityLevels.set([]);
    }

    this.api.updatePresence(d.id, d.key, 'viewing', true);
    if (!this.presenceHeartbeat && typeof window !== 'undefined') {
      this.presenceHeartbeat = setInterval(() => {
        const cur = this.item();
        if (cur) {
          this.api.updatePresence(cur.id, cur.key, 'viewing', true);
        }
      }, 15000);
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
        security_level_id: cur.security_level_id || 0,
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

  readonly showCloneModal = signal(false);
  readonly isCloning = signal(false);
  readonly cloneError = signal('');
  cloneDraft = {
    summary: '',
    project_id: 1,
    clone_subtasks: false,
    clone_attachments: true,
    clone_links: true,
  };

  readonly showMoveModal = signal(false);
  readonly isMoving = signal(false);
  readonly moveError = signal('');
  moveDraft = {
    target_project_id: 1,
    target_type: 'Task',
    target_status: 'To Do',
  };

  openCloneModal(): void {
    const cur = this.item();
    if (!cur) return;
    this.cloneDraft = {
      summary: `CLONE - ${cur.title}`,
      project_id: cur.project_id || 1,
      clone_subtasks: (cur.subtasks && cur.subtasks.length > 0) || false,
      clone_attachments: (cur.attachments && cur.attachments.length > 0) || false,
      clone_links: true,
    };
    this.cloneError.set('');
    this.showCloneModal.set(true);
  }

  closeCloneModal(): void {
    this.showCloneModal.set(false);
  }

  async submitClone(): Promise<void> {
    const cur = this.item();
    if (!cur) return;
    this.isCloning.set(true);
    this.cloneError.set('');
    try {
      const res = await this.api.cloneItem(cur.key, this.cloneDraft);
      this.closeCloneModal();
      await this.router.navigate(['/items', res.key]);
      await this.reloadDetail(res.key);
    } catch (err: any) {
      this.cloneError.set(err.message || 'Cloning failed');
    } finally {
      this.isCloning.set(false);
    }
  }

  openMoveModal(): void {
    const cur = this.item();
    if (!cur) return;
    this.moveDraft = {
      target_project_id: cur.project_id || 1,
      target_type: cur.type || 'Task',
      target_status: cur.status || 'To Do',
    };
    this.moveError.set('');
    this.showMoveModal.set(true);
  }

  closeMoveModal(): void {
    this.showMoveModal.set(false);
  }

  async submitMove(): Promise<void> {
    const cur = this.item();
    if (!cur) return;
    this.isMoving.set(true);
    this.moveError.set('');
    try {
      const res = await this.api.moveItem(cur.key, this.moveDraft);
      this.closeMoveModal();
      await this.router.navigate(['/items', res.new_key]);
      await this.reloadDetail(res.new_key);
    } catch (err: any) {
      this.moveError.set(err.message || 'Move failed');
    } finally {
      this.isMoving.set(false);
    }
  }

  async deleteCurrentItem(): Promise<void> {
    const cur = this.item();
    if (!cur) return;
    if (!confirm(`Are you sure you want to move ${cur.key} to the Trash Bin? You can restore it anytime within 30 days.`)) {
      return;
    }
    await this.api.deleteItem(cur.key);
    await this.router.navigate(['/trash']);
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

  // Step 14: Developer Tooling & Git Integration Methods
  getDevCount(it: WorkItemDetail | null): number {
    if (!it || !it.development) return 0;
    return (
      (it.development.branches_count || 0) +
      (it.development.commits_count || 0) +
      (it.development.pull_requests_count || 0) +
      (it.development.builds_count || 0)
    );
  }

  async reloadDevOnly(key: string): Promise<void> {
    try {
      const dev = await this.api.getItemDevelopment(key);
      const cur = this.item();
      if (cur) {
        this.item.set({ ...cur, development: dev });
      }
      this.showToast('Development data refreshed');
    } catch (err: any) {
      this.showToast(`Failed to refresh dev data: ${err?.message || 'Error'}`);
    }
  }

  openLinkDevModal(): void {
    const cur = this.item();
    const curUser = this.api.currentUser();
    this.linkDevDraft = {
      type: 'branch',
      repository_name: 'main-repo',
      branch_name: cur ? `feature/${cur.key}-impl` : '',
      branch_url: '',
      last_commit_hash: '',
      commit_hash: '',
      message: cur ? `${cur.key} Implement feature` : '',
      author_name: curUser?.display_name || '',
      author_email: curUser?.email || '',
      commit_url: '',
      pr_number: 1,
      title: cur ? `[${cur.key}] ${cur.title}` : '',
      source_branch: cur ? `feature/${cur.key}` : '',
      target_branch: 'main',
      status: 'OPEN',
      pr_url: '',
      pipeline_id: 'GitHub Actions',
      build_number: '1',
      build_status: 'SUCCESS',
      build_url: '',
      duration_seconds: 60,
    };
    this.linkDevError.set('');
    this.showLinkDevModal.set(true);
  }

  closeLinkDevModal(): void {
    this.showLinkDevModal.set(false);
    this.linkDevError.set('');
  }

  async submitLinkDev(): Promise<void> {
    const cur = this.item();
    if (!cur) return;
    this.isLinkingDev.set(true);
    this.linkDevError.set('');
    try {
      const payload: any = {
        type: this.linkDevDraft.type,
        repository_name: this.linkDevDraft.repository_name || 'main-repo',
      };
      if (this.linkDevDraft.type === 'branch') {
        if (!this.linkDevDraft.branch_name.trim()) {
          this.linkDevError.set('Branch name is required');
          return;
        }
        payload.branch_name = this.linkDevDraft.branch_name.trim();
        payload.branch_url = this.linkDevDraft.branch_url.trim();
        payload.last_commit_hash = this.linkDevDraft.last_commit_hash.trim();
      } else if (this.linkDevDraft.type === 'commit') {
        if (!this.linkDevDraft.commit_hash.trim()) {
          this.linkDevError.set('Commit hash is required');
          return;
        }
        payload.commit_hash = this.linkDevDraft.commit_hash.trim();
        payload.message = this.linkDevDraft.message;
        payload.author_name = this.linkDevDraft.author_name;
        payload.author_email = this.linkDevDraft.author_email;
        payload.commit_url = this.linkDevDraft.commit_url;
        payload.execute_smart_commits = true;
      } else if (this.linkDevDraft.type === 'pull_request') {
        if (!this.linkDevDraft.title.trim()) {
          this.linkDevError.set('PR title is required');
          return;
        }
        payload.pr_number = Number(this.linkDevDraft.pr_number) || 1;
        payload.title = this.linkDevDraft.title.trim();
        payload.source_branch = this.linkDevDraft.source_branch.trim();
        payload.target_branch = this.linkDevDraft.target_branch.trim() || 'main';
        payload.status = this.linkDevDraft.status;
        payload.author_name = this.linkDevDraft.author_name;
        payload.author_email = this.linkDevDraft.author_email;
        payload.pr_url = this.linkDevDraft.pr_url;
      } else if (this.linkDevDraft.type === 'build') {
        payload.pipeline_id = this.linkDevDraft.pipeline_id || 'CI';
        payload.build_number = String(this.linkDevDraft.build_number || '1');
        payload.status = this.linkDevDraft.build_status;
        payload.duration_seconds = Number(this.linkDevDraft.duration_seconds) || 60;
        payload.build_url = this.linkDevDraft.build_url;
      }

      await this.api.linkItemDevelopment(cur.key, payload);
      await this.reloadDetail(cur.key);
      this.showToast('Development asset linked successfully');
      this.closeLinkDevModal();
    } catch (err: any) {
      this.linkDevError.set(err?.message || 'Failed to link asset');
    } finally {
      this.isLinkingDev.set(false);
    }
  }

  async unlinkDev(type: 'branches' | 'commits' | 'pull-requests' | 'builds' | string, id: number): Promise<void> {
    const cur = this.item();
    if (!cur) return;
    if (!confirm(`Are you sure you want to unlink this item?`)) return;
    try {
      await this.api.unlinkItemDevelopment(cur.key, type, id);
      await this.reloadDetail(cur.key);
      this.showToast('Unlinked item');
    } catch (err: any) {
      this.showToast(`Error unlinking: ${err?.message || 'Failed'}`);
    }
  }

  openSmartCommitModal(): void {
    const cur = this.item();
    const curUser = this.api.currentUser();
    this.smartCommitDraft = {
      message: cur ? `${cur.key} #time 45m #comment Implemented initial logic` : '',
      author_email: curUser?.email || 'dev@example.com',
      author_name: curUser?.display_name || 'Developer',
    };
    this.smartCommitError.set('');
    this.smartCommitSimulation.set(null);
    this.showSmartCommitModal.set(true);
  }

  closeSmartCommitModal(): void {
    this.showSmartCommitModal.set(false);
    this.smartCommitError.set('');
    this.smartCommitSimulation.set(null);
  }

  async runSmartCommitSimulate(execute: boolean): Promise<void> {
    if (!this.smartCommitDraft.message.trim()) {
      this.smartCommitError.set('Commit message cannot be empty');
      return;
    }
    if (execute) this.isExecutingSmartCommit.set(true);
    else this.isSimulating.set(true);
    this.smartCommitError.set('');
    try {
      const res = await this.api.simulateSmartCommit({
        message: this.smartCommitDraft.message.trim(),
        author_email: this.smartCommitDraft.author_email,
        author_name: this.smartCommitDraft.author_name,
        execute,
      });
      this.smartCommitSimulation.set(res);
      if (execute) {
        const cur = this.item();
        if (cur) await this.reloadDetail(cur.key);
        this.showToast('Smart commit executed successfully');
      }
    } catch (err: any) {
      this.smartCommitError.set(err?.message || 'Smart commit simulation failed');
    } finally {
      this.isSimulating.set(false);
      this.isExecutingSmartCommit.set(false);
    }
  }
}
