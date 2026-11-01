import { Component, OnInit, computed, inject, signal } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { RouterLink } from '@angular/router';
import { ApiService } from '../../services/api.service';
import {
  WORKFLOW_STATES,
  WorkItem,
  WorkflowDef,
  WorkflowState,
  WorkflowStatusNode,
  WorkflowTransitionDef,
} from '../../models/jira.models';

@Component({
  selector: 'app-workflows',
  standalone: true,
  imports: [CommonModule, FormsModule, RouterLink],
  template: `
    <div class="page-container">
      <div class="page-toolbar">
        <div>
          <h2>🔀 Directed Graph Workflow Engine, Schemes &amp; Backlog</h2>
          <p class="text-muted">
            Design directed state graphs on an interactive SVG canvas, configure Transition Conditions, Validators &amp; Post-Functions, bind Workflow Schemes, and manage sprints.
          </p>
        </div>
        <div class="toolbar-actions">
          <a routerLink="/custom-fields" class="btn btn-secondary">🧬 Custom Fields &amp; Screens</a>
          <a routerLink="/create" class="btn btn-primary">+ Create Work Item</a>
        </div>
      </div>

      @if (toastMsg()) {
        <div class="toast-banner">{{ toastMsg() }}</div>
      }

      <!-- Top Navigation Tabs -->
      <div class="tabs-bar" style="margin-bottom: 18px;">
        <button
          type="button"
          class="tab-btn"
          [class.active]="activeTab() === 'designer'"
          (click)="activeTab.set('designer')"
        >
          🎨 Visual Directed Workflow Designer ({{ api.workflows().length }})
        </button>
        <button
          type="button"
          class="tab-btn"
          [class.active]="activeTab() === 'schemes'"
          (click)="activeTab.set('schemes')"
        >
          🗂 Workflow Schemes ({{ api.workflowSchemes().length }})
        </button>
        <button
          type="button"
          class="tab-btn"
          [class.active]="activeTab() === 'backlog'"
          (click)="activeTab.set('backlog')"
        >
          📋 Backlog, Sprints &amp; DB Migrations ({{ items().length }})
        </button>
      </div>

      <!-- TAB 1: VISUAL DIRECTED GRAPH WORKFLOW DESIGNER -->
      @if (activeTab() === 'designer') {
        <!-- Workflow Selector & Creator Bar -->
        <div class="card-panel" style="margin-bottom: 16px;">
          <div style="display: flex; align-items: center; justify-content: space-between; flex-wrap: wrap; gap: 12px;">
            <div style="display: flex; align-items: center; gap: 10px; flex-wrap: wrap;">
              <label style="font-weight: 600;">Active Workflow:</label>
              @for (wf of api.workflows(); track wf.id) {
                <button
                  type="button"
                  class="pill"
                  [class.active]="selectedWorkflowId() === wf.id"
                  (click)="selectWorkflow(wf.id)"
                >
                  {{ wf.name }}
                  @if (wf.is_default) {
                    <span class="badge badge-info" style="margin-left: 4px;">Default</span>
                  }
                </button>
              }
            </div>
            <div style="display: flex; gap: 8px; align-items: center;">
              <button type="button" class="btn btn-secondary btn-sm" (click)="saveCanvasLayout()">
                💾 Save Canvas Node Positions
              </button>
              <button
                type="button"
                class="btn btn-primary btn-sm"
                (click)="showCreateWfForm.set(!showCreateWfForm())"
              >
                + New Workflow
              </button>
            </div>
          </div>

          @if (showCreateWfForm()) {
            <form (ngSubmit)="createNewWorkflow()" class="inline-create-box" style="margin-top: 12px;">
              <div class="grid-3">
                <input
                  type="text"
                  name="new_wf_name"
                  [(ngModel)]="newWf.name"
                  placeholder="Workflow Name (e.g. Hotfix Fast-Track Workflow)"
                  required
                />
                <input
                  type="text"
                  name="new_wf_desc"
                  [(ngModel)]="newWf.description"
                  placeholder="Description"
                />
                <div style="display: flex; gap: 8px;">
                  <select name="new_wf_clone" [(ngModel)]="newWf.clone_from_id">
                    <option [ngValue]="1">Clone Default Agile 7-State</option>
                    <option [ngValue]="2">Clone Strict Bug &amp; QA Workflow</option>
                    <option [ngValue]="0">Blank 3-State (To Do → In Progress → Done)</option>
                  </select>
                  <button type="submit" class="btn btn-primary">Create</button>
                </div>
              </div>
            </form>
          }

          @if (activeWorkflow(); as wf) {
            <p class="text-muted" style="margin: 8px 0 0 0;">
              {{ wf.description }} —
              <strong>{{ wf.statuses.length }}</strong> statuses ·
              <strong>{{ wf.transitions.length }}</strong> directed transitions
            </p>
          }
        </div>

        @if (activeWorkflow(); as wf) {
          <!-- Interactive SVG Directed Graph Canvas -->
          <div class="card-panel" style="margin-bottom: 16px;">
            <div class="section-header" style="margin-bottom: 10px;">
              <div>
                <h3>Interactive Directed State Graph Canvas — {{ wf.name }}</h3>
                <small class="text-muted">
                  Drag status nodes to arrange layout. Click any directed transition edge or transition pill below to configure Conditions, Validators &amp; Post-Functions.
                </small>
              </div>
              <div style="display: flex; gap: 12px; align-items: center; font-size: 12px;">
                <span style="display: inline-flex; align-items: center; gap: 4px;">
                  <span style="width: 10px; height: 10px; border-radius: 50%; background: #0052CC;"></span>
                  To Do (Initial)
                </span>
                <span style="display: inline-flex; align-items: center; gap: 4px;">
                  <span style="width: 10px; height: 10px; border-radius: 50%; background: #FF991F;"></span>
                  In Progress
                </span>
                <span style="display: inline-flex; align-items: center; gap: 4px;">
                  <span style="width: 10px; height: 10px; border-radius: 50%; background: #00875A;"></span>
                  Done (Terminal)
                </span>
              </div>
            </div>

            <div
              style="background: #f8fafc; border: 1px solid #cbd5e1; border-radius: 10px; overflow-x: auto; position: relative;"
            >
              <svg
                width="1080"
                height="390"
                viewBox="0 0 1080 390"
                style="display: block; user-select: none;"
                (mousemove)="onSvgMouseMove($event)"
                (mouseup)="onSvgMouseUp()"
                (mouseleave)="onSvgMouseUp()"
              >
                <defs>
                  <marker
                    id="wf-arrow"
                    viewBox="0 0 10 10"
                    refX="9"
                    refY="5"
                    markerWidth="6"
                    markerHeight="6"
                    orient="auto-start-reverse"
                  >
                    <path d="M 0 1 L 10 5 L 0 9 z" fill="#64748b" />
                  </marker>
                  <marker
                    id="wf-arrow-active"
                    viewBox="0 0 10 10"
                    refX="9"
                    refY="5"
                    markerWidth="7"
                    markerHeight="7"
                    orient="auto-start-reverse"
                  >
                    <path d="M 0 1 L 10 5 L 0 9 z" fill="#0052cc" />
                  </marker>
                </defs>

                <!-- Subtle Canvas Grid -->
                <g stroke="#e2e8f0" stroke-width="1">
                  <line x1="0" y1="90" x2="1080" y2="90" stroke-dasharray="4 4" />
                  <line x1="0" y1="190" x2="1080" y2="190" stroke-dasharray="4 4" />
                  <line x1="0" y1="290" x2="1080" y2="290" stroke-dasharray="4 4" />
                </g>

                <!-- Directed Edges (Specific Status -> Status) -->
                @for (edge of specificEdges(); track edge.tr.id) {
                  <g
                    style="cursor: pointer;"
                    (click)="selectTransition(edge.tr)"
                  >
                    <path
                      [attr.d]="edge.path"
                      fill="none"
                      [attr.stroke]="selectedTransition()?.id === edge.tr.id ? '#0052cc' : '#64748b'"
                      [attr.stroke-width]="selectedTransition()?.id === edge.tr.id ? 2.8 : 1.8"
                      [attr.marker-end]="selectedTransition()?.id === edge.tr.id ? 'url(#wf-arrow-active)' : 'url(#wf-arrow)'"
                    />
                    <rect
                      [attr.x]="edge.midX - 58"
                      [attr.y]="edge.midY - 11"
                      width="116"
                      height="20"
                      rx="4"
                      [attr.fill]="selectedTransition()?.id === edge.tr.id ? '#eff6ff' : '#ffffff'"
                      [attr.stroke]="selectedTransition()?.id === edge.tr.id ? '#0052cc' : '#cbd5e1'"
                    />
                    <text
                      [attr.x]="edge.midX"
                      [attr.y]="edge.midY + 3"
                      text-anchor="middle"
                      font-size="10"
                      font-weight="600"
                      [attr.fill]="selectedTransition()?.id === edge.tr.id ? '#0052cc' : '#334155'"
                    >
                      {{ edge.tr.name.slice(0, 18) }}
                    </text>
                  </g>
                }

                <!-- Status Nodes -->
                @for (st of wf.statuses; track st.id) {
                  <g
                    [attr.transform]="'translate(' + st.pos_x + ',' + st.pos_y + ')'"
                    style="cursor: grab;"
                    (mousedown)="onNodeMouseDown($event, st)"
                  >
                    <rect
                      x="0"
                      y="0"
                      width="134"
                      height="64"
                      rx="10"
                      fill="#ffffff"
                      [attr.stroke]="st.color || '#0052CC'"
                      stroke-width="2.5"
                    />
                    <rect
                      x="0"
                      y="0"
                      width="134"
                      height="18"
                      rx="8"
                      [attr.fill]="st.color || '#0052CC'"
                    />
                    <text
                      x="8"
                      y="13"
                      font-size="9.5"
                      font-weight="700"
                      fill="#ffffff"
                    >
                      {{ st.category.toUpperCase() }}
                      {{ st.is_initial ? ' ★ INIT' : '' }}
                    </text>
                    <text
                      x="67"
                      y="38"
                      text-anchor="middle"
                      font-size="12.5"
                      font-weight="700"
                      fill="#0f172a"
                    >
                      {{ st.name }}
                    </text>
                    <text
                      x="67"
                      y="54"
                      text-anchor="middle"
                      font-size="10.5"
                      fill="#64748b"
                    >
                      {{ st.item_count ?? countState(st.name) }} item(s)
                    </text>
                  </g>
                }
              </svg>
            </div>

            <!-- Global (* -> Status) Transitions Bar -->
            <div style="margin-top: 12px; display: flex; align-items: center; gap: 8px; flex-wrap: wrap;">
              <strong style="font-size: 12.5px; color: #334155;">Global Transitions (from * any status):</strong>
              @for (tr of globalTransitions(); track tr.id) {
                <button
                  type="button"
                  class="pill"
                  [class.active]="selectedTransition()?.id === tr.id"
                  (click)="selectTransition(tr)"
                >
                  ✱ → <strong>{{ tr.to_status }}</strong>: {{ tr.name }}
                  @if (tr.screen_name) {
                    <span class="badge badge-info" style="margin-left: 4px;">🗔 {{ tr.screen_name }}</span>
                  }
                  <small style="margin-left: 4px; opacity: 0.8;">
                    ({{ tr.conditions.length }}C / {{ tr.validators.length }}V / {{ tr.post_functions.length }}PF)
                  </small>
                </button>
              }
            </div>
          </div>

          <!-- Two-Column Transitions Table + Transition Rule Inspector -->
          <div class="grid-2">
            <!-- Left: Transitions & Statuses Builder -->
            <div class="card-panel">
              <h3>Directed Transitions ({{ wf.transitions.length }})</h3>
              <div class="table-responsive" style="max-height: 360px; overflow-y: auto;">
                <table class="data-table">
                  <thead>
                    <tr>
                      <th>Transition Name</th>
                      <th>From → To</th>
                      <th>Screen</th>
                      <th>Rules</th>
                      <th></th>
                    </tr>
                  </thead>
                  <tbody>
                    @for (tr of wf.transitions; track tr.id) {
                      <tr
                        [style.background]="selectedTransition()?.id === tr.id ? '#eff6ff' : 'transparent'"
                        style="cursor: pointer;"
                        (click)="selectTransition(tr)"
                      >
                        <td><strong>{{ tr.name }}</strong></td>
                        <td>
                          <code>{{ tr.from_status }}</code> →
                          <span class="state-pill" [attr.data-state]="tr.to_status">{{ tr.to_status }}</span>
                        </td>
                        <td>
                          @if (tr.screen_name) {
                            <span class="badge badge-info">🗔 {{ tr.screen_name }}</span>
                          } @else {
                            <span class="text-muted">None</span>
                          }
                        </td>
                        <td>
                          <span class="badge" title="Conditions">{{ tr.conditions.length }}C</span>
                          <span class="badge" title="Validators" style="margin-left: 3px;">{{ tr.validators.length }}V</span>
                          <span class="badge" title="Post-Functions" style="margin-left: 3px;">{{ tr.post_functions.length }}PF</span>
                        </td>
                        <td>
                          <button
                            type="button"
                            class="btn btn-ghost btn-sm"
                            (click)="$event.stopPropagation(); removeTransition(tr.id)"
                          >
                            🗑
                          </button>
                        </td>
                      </tr>
                    }
                  </tbody>
                </table>
              </div>

              <!-- Add New Directed Transition Form -->
              <form (ngSubmit)="createTransition()" class="inline-create-box" style="margin-top: 12px;">
                <h4 style="margin: 0 0 8px 0; font-size: 13px;">+ Add Directed Transition</h4>
                <div class="grid-2">
                  <input
                    type="text"
                    name="tr_name"
                    [(ngModel)]="newTransition.name"
                    placeholder="Transition Name (e.g. Escalate to QA)"
                    required
                  />
                  <select name="tr_screen" [(ngModel)]="newTransition.screen_id">
                    <option [ngValue]="null">No Transition Screen</option>
                    @for (sc of api.screens(); track sc.id) {
                      <option [ngValue]="sc.id">🗔 {{ sc.name }}</option>
                    }
                  </select>
                </div>
                <div class="grid-3" style="margin-top: 8px;">
                  <select name="tr_from" [(ngModel)]="newTransition.from_status">
                    <option value="*">* (Any Status)</option>
                    @for (st of wf.statuses; track st.id) {
                      <option [value]="st.name">From: {{ st.name }}</option>
                    }
                  </select>
                  <select name="tr_to" [(ngModel)]="newTransition.to_status">
                    @for (st of wf.statuses; track st.id) {
                      <option [value]="st.name">To: {{ st.name }}</option>
                    }
                  </select>
                  <button type="submit" class="btn btn-secondary">+ Add Transition</button>
                </div>
              </form>

              <!-- Add Status Node Form -->
              <form (ngSubmit)="createStatusNode()" class="inline-create-box" style="margin-top: 10px;">
                <h4 style="margin: 0 0 8px 0; font-size: 13px;">+ Add Workflow Status Node</h4>
                <div class="grid-3">
                  <input
                    type="text"
                    name="st_name"
                    [(ngModel)]="newStatus.name"
                    placeholder="Status Name (e.g. UAT)"
                    required
                  />
                  <select name="st_cat" [(ngModel)]="newStatus.category">
                    <option value="To Do">Category: To Do (Blue)</option>
                    <option value="In Progress">Category: In Progress (Amber)</option>
                    <option value="Done">Category: Done (Green)</option>
                  </select>
                  <button type="submit" class="btn btn-secondary">+ Add Status</button>
                </div>
              </form>
            </div>

            <!-- Right: Selected Transition Inspector (Conditions, Validators, Post-Functions) -->
            <div class="card-panel">
              @if (selectedTransition(); as tr) {
                <div class="section-header">
                  <div>
                    <h3>⚙ Transition Controls: {{ tr.name }}</h3>
                    <small class="text-muted">
                      <code>{{ tr.from_status }}</code> → <strong>{{ tr.to_status }}</strong>
                    </small>
                  </div>
                  <div style="display: flex; align-items: center; gap: 6px;">
                    <label style="font-size: 12px;">Transition Screen:</label>
                    <select
                      name="edit_tr_screen"
                      [ngModel]="tr.screen_id"
                      (ngModelChange)="changeTransitionScreen(tr, $event)"
                      style="max-width: 220px;"
                    >
                      <option [ngValue]="null">— None —</option>
                      @for (sc of api.screens(); track sc.id) {
                        <option [ngValue]="sc.id">🗔 {{ sc.name }}</option>
                      }
                    </select>
                  </div>
                </div>

                <!-- 1. Conditions -->
                <div style="margin-top: 12px;">
                  <h4 style="margin: 0 0 6px 0; font-size: 13px; color: #0f172a;">
                    🛡 1. Workflow Conditions ({{ tr.conditions.length }})
                    <small class="text-muted" style="font-weight: normal;">
                      — Guard who &amp; when this transition can be executed
                    </small>
                  </h4>
                  <div class="subtask-list">
                    @for (r of tr.conditions; track r.id) {
                      <div class="subtask-row">
                        <span class="badge badge-info">{{ r.rule_type }}</span>
                        @if (r.config_value) {
                          <code>{{ r.config_value }}</code>
                        }
                        <span style="flex: 1; font-size: 12.5px;">{{ r.description }}</span>
                        <button type="button" class="btn btn-ghost btn-sm" (click)="removeRule(r.id)">×</button>
                      </div>
                    } @empty {
                      <div class="text-muted" style="font-size: 12px; padding: 4px 0;">
                        No conditions — any authenticated project member can trigger this transition.
                      </div>
                    }
                  </div>
                </div>

                <!-- 2. Validators -->
                <div style="margin-top: 14px;">
                  <h4 style="margin: 0 0 6px 0; font-size: 13px; color: #0f172a;">
                    ✔ 2. Workflow Validators ({{ tr.validators.length }})
                    <small class="text-muted" style="font-weight: normal;">
                      — Validate required fields &amp; comments before state change
                    </small>
                  </h4>
                  <div class="subtask-list">
                    @for (r of tr.validators; track r.id) {
                      <div class="subtask-row">
                        <span class="badge" style="background: #fef3c7; color: #92400e;">{{ r.rule_type }}</span>
                        @if (r.field_name) {
                          <code>{{ r.field_name }}</code>
                        }
                        <span style="flex: 1; font-size: 12.5px;">{{ r.description }}</span>
                        <button type="button" class="btn btn-ghost btn-sm" (click)="removeRule(r.id)">×</button>
                      </div>
                    } @empty {
                      <div class="text-muted" style="font-size: 12px; padding: 4px 0;">
                        No validators configured on this transition.
                      </div>
                    }
                  </div>
                </div>

                <!-- 3. Post-Functions -->
                <div style="margin-top: 14px;">
                  <h4 style="margin: 0 0 6px 0; font-size: 13px; color: #0f172a;">
                    ⚡ 3. Workflow Post-Functions ({{ tr.post_functions.length }})
                    <small class="text-muted" style="font-weight: normal;">
                      — Automated side-effects executed immediately after transition
                    </small>
                  </h4>
                  <div class="subtask-list">
                    @for (r of tr.post_functions; track r.id) {
                      <div class="subtask-row">
                        <span class="badge" style="background: #dcfce7; color: #166534;">{{ r.rule_type }}</span>
                        @if (r.field_name) {
                          <code>{{ r.field_name }}={{ r.config_value }}</code>
                        }
                        <span style="flex: 1; font-size: 12.5px;">{{ r.description }}</span>
                        <button type="button" class="btn btn-ghost btn-sm" (click)="removeRule(r.id)">×</button>
                      </div>
                    } @empty {
                      <div class="text-muted" style="font-size: 12px; padding: 4px 0;">
                        No post-functions configured on this transition.
                      </div>
                    }
                  </div>
                </div>

                <!-- Add Rule Form -->
                <form (ngSubmit)="addRuleToSelectedTransition()" class="inline-create-box" style="margin-top: 14px;">
                  <h4 style="margin: 0 0 8px 0; font-size: 13px;">+ Add Condition, Validator or Post-Function</h4>
                  <div class="grid-2">
                    <select name="rule_cat" [(ngModel)]="newRule.rule_category" (ngModelChange)="onRuleCategoryChange($event)">
                      <option value="condition">🛡 Condition (Guard)</option>
                      <option value="validator">✔ Validator (Input Check)</option>
                      <option value="post_function">⚡ Post-Function (Automated Action)</option>
                    </select>
                    <select name="rule_type" [(ngModel)]="newRule.rule_type">
                      @if (newRule.rule_category === 'condition') {
                        <option value="subtasks_completed">subtasks_completed (All subtasks Done)</option>
                        <option value="only_assignee">only_assignee (Only assigned user)</option>
                        <option value="user_in_role">user_in_role (Require specific role)</option>
                        <option value="permission">permission (Require permission)</option>
                      } @else if (newRule.rule_category === 'validator') {
                        <option value="field_required">field_required (Require non-empty field)</option>
                        <option value="comment_required">comment_required (Require transition comment)</option>
                        <option value="regex_match">regex_match (Validate field format)</option>
                      } @else {
                        <option value="set_field_value">set_field_value (Auto-set field value)</option>
                        <option value="clear_field_value">clear_field_value (Clear resolution/field)</option>
                        <option value="assign_to_user">assign_to_user (Reassign issue)</option>
                        <option value="update_parent_status">update_parent_status (Transition parent when subtasks Done)</option>
                        <option value="dispatch_event">dispatch_event (Emit workflow event)</option>
                      }
                    </select>
                  </div>
                  <div class="grid-3" style="margin-top: 8px;">
                    <input
                      type="text"
                      name="rule_field"
                      [(ngModel)]="newRule.field_name"
                      placeholder="Field (e.g. resolution_id, progress)"
                    />
                    <input
                      type="text"
                      name="rule_cfg"
                      [(ngModel)]="newRule.config_value"
                      placeholder="Value (e.g. Done, 100, QA Engineer)"
                    />
                    <input
                      type="text"
                      name="rule_desc"
                      [(ngModel)]="newRule.description"
                      placeholder="Rule description"
                    />
                  </div>
                  <div style="margin-top: 8px; display: flex; justify-content: flex-end;">
                    <button type="submit" class="btn btn-primary btn-sm">+ Add Rule to '{{ tr.name }}'</button>
                  </div>
                </form>
              } @else {
                <p class="text-muted">Select a transition from the graph or table to inspect and edit its rules.</p>
              }
            </div>
          </div>
        }
      }

      <!-- TAB 2: WORKFLOW SCHEMES (PROJECT & ISSUE TYPE BINDINGS) -->
      @if (activeTab() === 'schemes') {
        <div class="grid-2">
          <div class="card-panel">
            <h3>Active Workflow Scheme Bindings ({{ api.workflowSchemes().length }})</h3>
            <p class="text-muted">
              Bind different workflows to specific issue types or categories within a project (e.g., bind <code>Bug</code> issues to the <strong>Strict Bug &amp; QA Verification Workflow</strong>).
            </p>
            <div class="table-responsive">
              <table class="data-table">
                <thead>
                  <tr>
                    <th>Scheme Name</th>
                    <th>Project</th>
                    <th>Issue Type / Category</th>
                    <th>Bound Workflow</th>
                    <th></th>
                  </tr>
                </thead>
                <tbody>
                  @for (s of api.workflowSchemes(); track s.id) {
                    <tr>
                      <td>
                        <strong>{{ s.name }}</strong>
                        @if (s.description) {
                          <div class="text-muted" style="font-size: 11.5px;">{{ s.description }}</div>
                        }
                      </td>
                      <td>{{ s.project_name }}</td>
                      <td>
                        <span class="badge badge-info">{{ s.issue_type === '*' ? '* (All Default)' : s.issue_type }}</span>
                      </td>
                      <td><strong>{{ s.workflow_name }}</strong></td>
                      <td>
                        @if (s.issue_type !== '*') {
                          <button type="button" class="btn btn-ghost btn-sm" (click)="removeWorkflowScheme(s.id)">
                            🗑
                          </button>
                        }
                      </td>
                    </tr>
                  }
                </tbody>
              </table>
            </div>
          </div>

          <div class="card-panel">
            <h3>+ Bind Issue Type to Workflow</h3>
            <form (ngSubmit)="saveSchemeBinding()" class="inline-create-box">
              <div class="form-group">
                <label>Binding Name</label>
                <input
                  type="text"
                  name="ws_name"
                  [(ngModel)]="newScheme.name"
                  placeholder="e.g. Strict Bug QA Workflow Binding"
                  required
                />
              </div>
              <div class="grid-2">
                <div class="form-group">
                  <label>Issue Type / Category</label>
                  <select name="ws_type" [(ngModel)]="newScheme.issue_type">
                    <option value="*">* (Default for all unmapped types)</option>
                    <option value="Bug">Bug</option>
                    <option value="Story">Story</option>
                    <option value="Task">Task</option>
                    <option value="Subtask">Subtask</option>
                    <option value="Epic">Epic</option>
                    <option value="Initiative">Initiative</option>
                  </select>
                </div>
                <div class="form-group">
                  <label>Target Workflow</label>
                  <select name="ws_wf" [(ngModel)]="newScheme.workflow_id">
                    @for (wf of api.workflows(); track wf.id) {
                      <option [ngValue]="wf.id">{{ wf.name }}</option>
                    }
                  </select>
                </div>
              </div>
              <div class="form-group">
                <label>Description</label>
                <input
                  type="text"
                  name="ws_desc"
                  [(ngModel)]="newScheme.description"
                  placeholder="Why this issue type uses this workflow..."
                />
              </div>
              <button type="submit" class="btn btn-primary">Save Workflow Scheme Binding</button>
            </form>
          </div>
        </div>
      }

      <!-- TAB 3: SPRINTS, TEAM MEMBERS, BACKLOG & SCHEMA MIGRATIONS -->
      @if (activeTab() === 'backlog') {
        <div class="grid-2">
          <div class="card-panel">
            <h3>Sprints ({{ api.sprints().length }})</h3>
            <div class="subtask-list">
              @for (sp of api.sprints(); track sp.id) {
                <div class="subtask-row">
                  <strong>{{ sp.name }}</strong>
                  <span class="badge badge-info">{{ sp.status }}</span>
                  <span class="text-muted">{{ sp.start_date }} → {{ sp.end_date }}</span>
                </div>
              }
            </div>
            <form (ngSubmit)="createNewSprint()" class="inline-create-box">
              <div class="grid-2">
                <input
                  type="text"
                  id="wf-sprint-name"
                  name="sprint_title"
                  autocomplete="off"
                  [(ngModel)]="newSprint.name"
                  placeholder="New Sprint Name (e.g., Sprint 3)"
                  required
                />
                <input
                  type="text"
                  id="wf-sprint-goal"
                  name="sprint_goal"
                  autocomplete="off"
                  [(ngModel)]="newSprint.goal"
                  placeholder="Sprint Goal"
                />
              </div>
              <div class="grid-3" style="margin-top: 8px;">
                <input type="date" id="wf-sprint-start" name="spStart" [(ngModel)]="newSprint.start_date" />
                <input type="date" id="wf-sprint-end" name="spEnd" [(ngModel)]="newSprint.end_date" />
                <button type="submit" class="btn btn-secondary">+ Create Sprint</button>
              </div>
            </form>
          </div>

          <div class="card-panel">
            <h3>Team Members ({{ api.users().length }})</h3>
            <div class="subtask-list">
              @for (u of api.users(); track u.id) {
                <div class="subtask-row">
                  <div class="avatar-circle" [style.background]="u.avatar_color">
                    {{ u.display_name.slice(0, 2).toUpperCase() }}
                  </div>
                  <strong>{{ u.display_name }}</strong>
                  <span class="text-muted">&#64;{{ u.username }}</span>
                  <span class="badge">{{ u.role }}</span>
                </div>
              }
            </div>
            <form (ngSubmit)="createNewUser()" class="inline-create-box">
              <div class="grid-3">
                <input
                  type="text"
                  id="wf-user-username"
                  name="username"
                  autocomplete="username"
                  [(ngModel)]="newUser.username"
                  placeholder="username"
                  required
                />
                <input
                  type="text"
                  id="wf-user-display-name"
                  name="display_name"
                  autocomplete="name"
                  [(ngModel)]="newUser.display_name"
                  placeholder="Full Name"
                  required
                />
                <select id="wf-user-role" name="uRole" [(ngModel)]="newUser.role">
                  <option value="Developer">Developer</option>
                  <option value="Project Manager">Project Manager</option>
                  <option value="QA Engineer">QA Engineer</option>
                  <option value="Reporter">Reporter</option>
                  <option value="Admin">Admin</option>
                </select>
              </div>
              <div style="margin-top: 8px; display: flex; justify-content: flex-end;">
                <button type="submit" class="btn btn-secondary">+ Add Team Member</button>
              </div>
            </form>
          </div>
        </div>

        <!-- Full Backlog & Work Items Table -->
        <div class="card-panel">
          <h3>All Project Work Elements ({{ items().length }})</h3>
          <div class="table-responsive">
            <table class="data-table">
              <thead>
                <tr>
                  <th>Type</th>
                  <th>Category</th>
                  <th>Key</th>
                  <th>Title</th>
                  <th>Parent</th>
                  <th>Sprint</th>
                  <th>Assignee</th>
                  <th>Points</th>
                  <th>Workflow State</th>
                </tr>
              </thead>
              <tbody>
                @for (it of items(); track it.id) {
                  <tr>
                    <td><span class="type-badge" [attr.data-type]="it.type">{{ it.type }}</span></td>
                    <td><span class="cat-badge" [attr.data-cat]="it.category">{{ it.category }}</span></td>
                    <td><a [routerLink]="['/', it.key]" class="issue-key">{{ it.key }}</a></td>
                    <td>
                      <a [routerLink]="['/', it.key]" class="table-title-link">{{ it.title }}</a>
                    </td>
                    <td>{{ it.parent_key || '—' }}</td>
                    <td>{{ it.sprint_name || 'Backlog' }}</td>
                    <td>{{ it.assignee_name || 'Unassigned' }}</td>
                    <td>{{ it.story_points }} SP</td>
                    <td>
                      <select
                        class="state-select"
                        [id]="'wf-item-state-' + it.id"
                        [name]="'wf_item_state_' + it.id"
                        [attr.name]="'wf_item_state_' + it.id"
                        [attr.data-state]="it.status"
                        [ngModel]="it.status"
                        (ngModelChange)="changeItemState(it, $event)"
                      >
                        @for (st of states; track st) {
                          <option [value]="st">{{ st }}</option>
                        }
                      </select>
                    </td>
                  </tr>
                }
              </tbody>
            </table>
          </div>
        </div>

        <!-- SQLite Schema Version & Migrations Status -->
        <div class="card-panel">
          <h3>Database Schema Versioning (<code>schema_version</code> table)</h3>
          <p class="text-muted">
            Current Schema Version: <strong>v{{ api.setupStatus()?.schema_version }}</strong>
            (Target Latest: <strong>v{{ api.setupStatus()?.latest_schema_version }}</strong>)
          </p>
          <div class="subtask-list">
            @for (m of api.setupStatus()?.migrations; track m.version) {
              <div class="subtask-row">
                <span class="ver-pill">v{{ m.version }}</span>
                <span style="flex: 1;">{{ m.description }}</span>
                <span class="text-muted">Applied: {{ m.applied_at }}</span>
              </div>
            }
          </div>
        </div>
      }
    </div>
  `,
})
export class WorkflowsComponent implements OnInit {
  readonly api = inject(ApiService);
  readonly states = WORKFLOW_STATES;
  readonly items = signal<WorkItem[]>([]);
  readonly toastMsg = signal('');

  readonly activeTab = signal<'designer' | 'schemes' | 'backlog'>('designer');
  readonly selectedWorkflowId = signal<number>(1);
  readonly selectedTransitionId = signal<number>(0);
  readonly showCreateWfForm = signal(false);

  private draggingNode: WorkflowStatusNode | null = null;
  private dragOffsetX = 0;
  private dragOffsetY = 0;

  newWf = {
    name: '',
    description: '',
    clone_from_id: 1,
  };

  newTransition = {
    name: '',
    from_status: 'To Do',
    to_status: 'In Progress',
    screen_id: null as number | null,
  };

  newStatus = {
    name: '',
    category: 'In Progress',
  };

  newRule = {
    rule_category: 'condition',
    rule_type: 'subtasks_completed',
    field_name: '',
    config_value: 'Done',
    description: 'Prevent transition until all child subtasks are Done',
  };

  newScheme = {
    name: 'Strict Bug QA Workflow Binding',
    description: 'Routes Bug work items through strict QA verification & reproduction steps',
    project_id: 1,
    issue_type: 'Bug',
    workflow_id: 2,
  };

  newSprint = {
    project_id: 1,
    name: '',
    goal: '',
    status: 'planned',
    start_date: '2026-11-21',
    end_date: '2026-12-05',
  };

  newUser = {
    username: '',
    display_name: '',
    email: '',
    role: 'Developer',
    password: 'demo123',
    avatar_color: '#00B8D9',
  };

  readonly activeWorkflow = computed<WorkflowDef | null>(() => {
    const list = this.api.workflows();
    if (list.length === 0) return null;
    return list.find((w) => w.id === this.selectedWorkflowId()) || list[0];
  });

  readonly selectedTransition = computed<WorkflowTransitionDef | null>(() => {
    const wf = this.activeWorkflow();
    if (!wf || wf.transitions.length === 0) return null;
    return wf.transitions.find((t) => t.id === this.selectedTransitionId()) || wf.transitions[0];
  });

  readonly globalTransitions = computed<WorkflowTransitionDef[]>(() => {
    const wf = this.activeWorkflow();
    if (!wf) return [];
    return wf.transitions.filter((t) => t.from_status === '*');
  });

  readonly specificEdges = computed(() => {
    const wf = this.activeWorkflow();
    if (!wf) return [];
    const nodeMap = new Map<string, WorkflowStatusNode>();
    for (const st of wf.statuses) {
      nodeMap.set(st.name, st);
    }

    const edges: {
      tr: WorkflowTransitionDef;
      path: string;
      midX: number;
      midY: number;
    }[] = [];

    for (const tr of wf.transitions) {
      if (tr.from_status === '*') continue;
      const src = nodeMap.get(tr.from_status);
      const dst = nodeMap.get(tr.to_status);
      if (!src || !dst) continue;

      const x1 = src.pos_x + 67;
      const y1 = src.pos_y + 32;
      const x2 = dst.pos_x + 67;
      const y2 = dst.pos_y + 32;

      const forward = x2 >= x1;
      const startX = forward ? src.pos_x + 134 : src.pos_x;
      const startY = forward ? src.pos_y + 24 : src.pos_y + 46;
      const endX = forward ? dst.pos_x : dst.pos_x + 134;
      const endY = forward ? dst.pos_y + 24 : dst.pos_y + 46;

      const curveOffset = forward ? -26 : 34;
      const midX = (startX + endX) / 2;
      const midY = (startY + endY) / 2 + curveOffset;
      const path = `M ${startX} ${startY} Q ${midX} ${midY + curveOffset * 0.5} ${endX} ${endY}`;

      edges.push({ tr, path, midX, midY });
    }
    return edges;
  });

  async ngOnInit(): Promise<void> {
    await this.api.checkSetupStatus();
    await this.api.loadReferenceData();
    this.items.set(await this.api.getItems());
    const wf = this.activeWorkflow();
    if (wf && wf.transitions.length > 0) {
      this.selectedTransitionId.set(wf.transitions[0].id);
    }
  }

  selectWorkflow(id: number): void {
    this.selectedWorkflowId.set(id);
    const wf = this.api.workflows().find((w) => w.id === id);
    if (wf && wf.transitions.length > 0) {
      this.selectedTransitionId.set(wf.transitions[0].id);
    }
  }

  selectTransition(tr: WorkflowTransitionDef): void {
    this.selectedTransitionId.set(tr.id);
  }

  onNodeMouseDown(event: MouseEvent, node: WorkflowStatusNode): void {
    event.preventDefault();
    this.draggingNode = node;
    const svg = (event.currentTarget as SVGGElement).ownerSVGElement;
    if (svg) {
      const rect = svg.getBoundingClientRect();
      this.dragOffsetX = event.clientX - rect.left - node.pos_x;
      this.dragOffsetY = event.clientY - rect.top - node.pos_y;
    }
  }

  onSvgMouseMove(event: MouseEvent): void {
    if (!this.draggingNode) return;
    const svg = event.currentTarget as SVGSVGElement;
    const rect = svg.getBoundingClientRect();
    const nx = Math.max(10, Math.min(930, Math.round(event.clientX - rect.left - this.dragOffsetX)));
    const ny = Math.max(10, Math.min(315, Math.round(event.clientY - rect.top - this.dragOffsetY)));
    this.draggingNode.pos_x = nx;
    this.draggingNode.pos_y = ny;
    this.api.workflows.update((list) => [...list]);
  }

  onSvgMouseUp(): void {
    this.draggingNode = null;
  }

  async saveCanvasLayout(): Promise<void> {
    const wf = this.activeWorkflow();
    if (!wf) return;
    await this.api.updateWorkflow(wf.id, {
      name: wf.name,
      description: wf.description,
      statuses: wf.statuses,
    });
    this.showToast(`Saved canvas layout for '${wf.name}'`);
  }

  async createNewWorkflow(): Promise<void> {
    if (!this.newWf.name.trim()) return;
    const created = await this.api.createWorkflow(this.newWf);
    this.newWf.name = '';
    this.newWf.description = '';
    this.showCreateWfForm.set(false);
    this.selectWorkflow(created.id);
    this.showToast(`Created workflow '${created.name}'`);
  }

  async createTransition(): Promise<void> {
    const wf = this.activeWorkflow();
    if (!wf || !this.newTransition.name.trim()) return;
    const tr = await this.api.addWorkflowTransition(wf.id, this.newTransition);
    this.newTransition.name = '';
    this.selectedTransitionId.set(tr.id);
    this.showToast(`Added transition '${tr.name}'`);
  }

  async removeTransition(transitionId: number): Promise<void> {
    await this.api.deleteWorkflowTransition(transitionId);
    this.showToast('Deleted workflow transition');
  }

  async changeTransitionScreen(tr: WorkflowTransitionDef, screenId: number | null): Promise<void> {
    await this.api.updateWorkflowTransition(tr.id, {
      name: tr.name,
      from_status: tr.from_status,
      to_status: tr.to_status,
      screen_id: screenId || 0,
    });
    this.showToast(`Updated transition screen for '${tr.name}'`);
  }

  async createStatusNode(): Promise<void> {
    const wf = this.activeWorkflow();
    if (!wf || !this.newStatus.name.trim()) return;
    await this.api.addWorkflowStatus(wf.id, {
      name: this.newStatus.name.trim(),
      category: this.newStatus.category,
      pos_x: 120 + (wf.statuses.length % 5) * 180,
      pos_y: 250,
    });
    this.newStatus.name = '';
    this.showToast('Added status node to workflow');
  }

  onRuleCategoryChange(cat: string): void {
    if (cat === 'condition') {
      this.newRule.rule_type = 'subtasks_completed';
      this.newRule.field_name = '';
      this.newRule.config_value = 'Done';
      this.newRule.description = 'Prevent transition until all child subtasks are Done';
    } else if (cat === 'validator') {
      this.newRule.rule_type = 'field_required';
      this.newRule.field_name = 'resolution_id';
      this.newRule.config_value = 'required';
      this.newRule.description = 'Require Resolution before completing transition';
    } else {
      this.newRule.rule_type = 'set_field_value';
      this.newRule.field_name = 'progress';
      this.newRule.config_value = '100';
      this.newRule.description = 'Automatically update progress percentage';
    }
  }

  async addRuleToSelectedTransition(): Promise<void> {
    const tr = this.selectedTransition();
    if (!tr) return;
    await this.api.addWorkflowTransitionRule(tr.id, this.newRule);
    this.showToast(`Added ${this.newRule.rule_category} (${this.newRule.rule_type}) to '${tr.name}'`);
  }

  async removeRule(ruleId: number): Promise<void> {
    await this.api.deleteWorkflowTransitionRule(ruleId);
    this.showToast('Removed transition rule');
  }

  async saveSchemeBinding(): Promise<void> {
    if (!this.newScheme.name.trim()) return;
    await this.api.saveWorkflowScheme(this.newScheme);
    this.showToast(`Bound '${this.newScheme.issue_type}' to workflow`);
  }

  async removeWorkflowScheme(id: number): Promise<void> {
    await this.api.deleteWorkflowScheme(id);
    this.showToast('Removed workflow scheme binding');
  }

  countState(st: WorkflowState | string): number {
    return this.items().filter((i) => i.status === st).length;
  }

  async changeItemState(item: WorkItem, newState: WorkflowState): Promise<void> {
    if (item.status === newState) return;
    try {
      const defaultRes = this.api.resolutions()[0]?.id || 1;
      const updated = await this.api.transitionItemState(item.key, newState, {
        resolution_id: newState === 'Done' ? defaultRes : undefined,
        comment: `Transitioned to '${newState}' from Workflows & Backlog page`,
      });
      this.items.update((list) => list.map((i) => (i.id === item.id ? updated : i)));
      this.showToast(`${item.key} transitioned to ${newState}`);
    } catch (err: any) {
      this.items.update((list) => [...list]);
      this.showToast(`Transition blocked: ${err?.message || 'Rule check failed'}`);
    }
  }

  async createNewSprint(): Promise<void> {
    if (!this.newSprint.name.trim()) return;
    await this.api.createSprint(this.newSprint);
    this.newSprint.name = '';
    this.newSprint.goal = '';
    this.showToast('Sprint created');
  }

  async createNewUser(): Promise<void> {
    if (!this.newUser.username.trim() || !this.newUser.display_name.trim()) return;
    await this.api.createUser({
      ...this.newUser,
      email: `${this.newUser.username}@jiralike.local`,
    });
    this.newUser.username = '';
    this.newUser.display_name = '';
    this.showToast('Team member added');
  }

  private showToast(msg: string): void {
    this.toastMsg.set(msg);
    setTimeout(() => {
      if (this.toastMsg() === msg) this.toastMsg.set('');
    }, 3500);
  }
}
