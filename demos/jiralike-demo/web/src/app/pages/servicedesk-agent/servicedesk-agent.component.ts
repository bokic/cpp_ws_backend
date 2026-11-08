import { Component, OnInit, inject, signal } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { Router, RouterLink } from '@angular/router';
import { ApiService } from '../../services/api.service';
import {
  CannedResponse,
  CustomerOrganization,
  CustomerOrganizationMember,
  ServiceDesk,
  ServiceDeskQueue,
  SlaCalendar,
  SlaMetric,
  User,
  WorkItem,
  WorkItemSla,
} from '../../models/jira.models';

@Component({
  selector: 'app-servicedesk-agent',
  standalone: true,
  imports: [CommonModule, FormsModule, RouterLink],
  template: `
    <div class="agent-workspace">
      <!-- Top Bar: Desk Selector & Quick Actions -->
      <header class="agent-topbar">
        <div class="topbar-left">
          <span class="desk-badge">ITSM AGENT</span>
          <div class="desk-selector-group">
            <label>Service Desk:</label>
            <select
              [ngModel]="selectedDeskId()"
              (ngModelChange)="onDeskSelected($event)"
              class="form-control select-desk"
            >
              @for (d of serviceDesks(); track d.id) {
                <option [value]="d.id">{{ d.name }} ({{ d.portal_key }})</option>
              }
            </select>
          </div>
        </div>

        <div class="topbar-right">
          <button
            type="button"
            class="btn btn-secondary btn-sm"
            (click)="evaluateAllSlas()"
            [disabled]="evaluatingSlas()"
          >
            {{ evaluatingSlas() ? 'Evaluating...' : '⚡ Recalculate SLAs' }}
          </button>
          <a routerLink="/servicedesk" class="btn btn-primary btn-sm" target="_blank">
            🎧 Customer Portal &rarr;
          </a>
        </div>
      </header>

      <!-- Feedback Banner -->
      @if (feedback()) {
        <div class="agent-alert" [class.alert-success]="feedback()!.type === 'success'" [class.alert-error]="feedback()!.type === 'error'">
          {{ feedback()!.message }}
          <button type="button" class="close-alert" (click)="feedback.set(null)">&times;</button>
        </div>
      }

      <div class="agent-layout">
        <!-- Sidebar: Queues & Admin Sections -->
        <aside class="agent-sidebar">
          <div class="sidebar-section-title">
            <span>QUEUES</span>
            <button type="button" class="btn-icon-add" (click)="openCreateQueueModal()" title="Create New Queue">+</button>
          </div>

          <div class="queue-nav-list">
            @for (q of queues(); track q.id) {
              <button
                type="button"
                class="queue-nav-item"
                [class.active]="selectedQueueId() === q.id"
                (click)="selectQueue(q.id)"
              >
                <span class="queue-icon">{{ getIcon(q.icon) }}</span>
                <span class="queue-name">{{ q.name }}</span>
                <span class="queue-count" [class.count-alert]="(q.count || 0) > 0 && q.filter_type === 'open_incidents'">
                  {{ q.count || 0 }}
                </span>
              </button>
            }
          </div>

          <div class="sidebar-section-title" style="margin-top: 24px;">
            <span>SERVICE MANAGEMENT</span>
          </div>

          <div class="queue-nav-list">
            <button
              type="button"
              class="queue-nav-item"
              [class.active]="activeSubView() === 'slas'"
              (click)="activeSubView.set('slas')"
            >
              <span class="queue-icon">⏱️</span>
              <span class="queue-name">SLA Goals &amp; Calendars</span>
            </button>
            <button
              type="button"
              class="queue-nav-item"
              [class.active]="activeSubView() === 'canned'"
              (click)="activeSubView.set('canned')"
            >
              <span class="queue-icon">💬</span>
              <span class="queue-name">Canned Responses</span>
              <span class="queue-count">{{ cannedResponses().length }}</span>
            </button>
            <button
              type="button"
              class="queue-nav-item"
              [class.active]="activeSubView() === 'organizations'"
              (click)="switchToOrganizations()"
            >
              <span class="queue-icon">🏢</span>
              <span class="queue-name">Customer Organizations</span>
              <span class="queue-count">{{ organizations().length }}</span>
            </button>
          </div>
        </aside>

        <!-- Main Triage Content -->
        <main class="agent-content">
          <!-- --------------------------------------------------------------- -->
          <!-- SUBVIEW 1: QUEUE TICKETS LIST                                   -->
          <!-- --------------------------------------------------------------- -->
          @if (activeSubView() === 'tickets') {
            <div class="queue-view-header">
              <div>
                <h2>{{ activeQueue()?.name || 'All Requests' }}</h2>
                <p class="queue-desc">
                  Displaying {{ queueItems().length }} requests matching queue filter
                  <span class="code-tag">{{ activeQueue()?.filter_type }}</span>
                </p>
              </div>
              <div class="queue-header-actions">
                <button type="button" class="btn btn-secondary btn-sm" (click)="loadCurrentQueueItems()">
                  🔄 Refresh
                </button>
              </div>
            </div>

            <!-- Triage Queue Table -->
            <div class="triage-table-card">
              <table class="triage-table">
                <thead>
                  <tr>
                    <th>Key</th>
                    <th>Summary</th>
                    <th>Customer</th>
                    <th>Status</th>
                    <th>Priority</th>
                    <th>Assignee</th>
                    <th>Time to First Response</th>
                    <th>Time to Resolution</th>
                    <th>Actions</th>
                  </tr>
                </thead>
                <tbody>
                  @for (it of queueItems(); track it.id) {
                    <tr class="triage-row">
                      <td>
                        <a [routerLink]="['/items', it.key]" class="item-key-link">
                          {{ it.key }}
                        </a>
                      </td>
                      <td>
                        <div class="item-summary-cell">
                          <strong class="item-title">{{ it.title }}</strong>
                          @if (it.request_type_name) {
                            <span class="item-rt-tag">{{ it.request_type_name }}</span>
                          }
                        </div>
                      </td>
                      <td>
                        <div class="customer-cell">
                          <span>{{ it.customer_reporter_name || 'Customer' }}</span>
                          @if (it.customer_reporter_email) {
                            <small class="customer-email">{{ it.customer_reporter_email }}</small>
                          }
                        </div>
                      </td>
                      <td>
                        <span class="status-pill" [class.pill-done]="it.status === 'Done'" [class.pill-wait]="it.status.includes('Waiting')">
                          {{ it.status }}
                        </span>
                      </td>
                      <td>
                        <span class="priority-pill pill-{{ it.priority.toLowerCase() }}">
                          {{ it.priority }}
                        </span>
                      </td>
                      <td>
                        @if (it.assignee_id) {
                          <span class="assignee-badge">
                            <span class="avatar-dot" [style.background-color]="it.assignee_color"></span>
                            {{ it.assignee_name }}
                          </span>
                        } @else {
                          <button
                            type="button"
                            class="btn-assign-self"
                            (click)="assignToMe(it)"
                            title="Assign to myself"
                          >
                            + Assign to me
                          </button>
                        }
                      </td>
                      <!-- SLA 1: First Response -->
                      <td>
                        @if (getSla(it, 'first_response'); as frSla) {
                          <span
                            class="sla-indicator"
                            [style.background-color]="getSlaBg(frSla)"
                            [style.color]="getSlaFg(frSla)"
                          >
                            @if (frSla.is_completed) {
                              ✓ Met ({{ frSla.elapsed_formatted }})
                            } @else if (frSla.is_paused) {
                              ⏸ Paused
                            } @else if (frSla.is_breached) {
                              🔥 Breached ({{ frSla.remaining_formatted }})
                            } @else {
                              ⏱ {{ frSla.remaining_formatted }}
                            }
                          </span>
                        } @else {
                          <span class="text-muted">-</span>
                        }
                      </td>
                      <!-- SLA 2: Resolution -->
                      <td>
                        @if (getSla(it, 'resolution'); as resSla) {
                          <span
                            class="sla-indicator"
                            [style.background-color]="getSlaBg(resSla)"
                            [style.color]="getSlaFg(resSla)"
                          >
                            @if (resSla.is_completed) {
                              ✓ Met ({{ resSla.elapsed_formatted }})
                            } @else if (resSla.is_paused) {
                              ⏸ Paused
                            } @else if (resSla.is_breached) {
                              🔥 Breached ({{ resSla.remaining_formatted }})
                            } @else {
                              ⏱ {{ resSla.remaining_formatted }}
                            }
                          </span>
                        } @else {
                          <span class="text-muted">-</span>
                        }
                      </td>
                      <td>
                        <div class="row-actions">
                          <button
                            type="button"
                            class="btn btn-xs btn-primary"
                            (click)="openCannedResponseModal(it)"
                            title="Quick reply using canned response"
                          >
                            💬 Reply
                          </button>
                          <a [routerLink]="['/items', it.key]" class="btn btn-xs btn-secondary">
                            View &rarr;
                          </a>
                        </div>
                      </td>
                    </tr>
                  } @empty {
                    <tr>
                      <td colspan="9" class="text-center" style="padding: 40px; color: #5e6c84;">
                        🎉 All caught up! No requests pending in this queue.
                      </td>
                    </tr>
                  }
                </tbody>
              </table>
            </div>
          }

          <!-- --------------------------------------------------------------- -->
          <!-- SUBVIEW 2: SLA MANAGEMENT & CALENDARS                           -->
          <!-- --------------------------------------------------------------- -->
          @else if (activeSubView() === 'slas') {
            <div class="sla-management-view">
              <div class="queue-view-header">
                <div>
                  <h2>SLA Metrics &amp; Service Level Agreements</h2>
                  <p class="queue-desc">Configure business calendars, target goals, and breach alert countdown thresholds.</p>
                </div>
                <div class="queue-header-actions">
                  <button type="button" class="btn btn-primary btn-sm" (click)="openCreateSlaModal()">
                    + Add SLA Metric
                  </button>
                </div>
              </div>

              <!-- SLA Metrics Cards -->
              <div class="sla-cards-grid">
                @for (m of slas(); track m.id) {
                  <div class="sla-config-card">
                    <div class="sla-card-header">
                      <h3>{{ m.name }}</h3>
                      <span class="badge badge-info">{{ m.metric_key }}</span>
                    </div>
                    <p class="sla-card-desc">{{ m.description }}</p>
                    <div class="sla-calendar-tag">
                      📅 Working Calendar: <strong>{{ m.calendar_name || 'Standard 9-5 Mon-Fri' }}</strong>
                    </div>

                    <!-- Goals table -->
                    <div class="goals-mini-table">
                      <div class="goals-title">Configured Target Durations:</div>
                      @for (g of m.goals; track g.id) {
                        <div class="goal-row">
                          <span class="goal-prio">{{ g.priority }} Priority:</span>
                          <strong class="goal-time">{{ g.target_formatted || (g.target_duration_seconds / 3600 + 'h') }}</strong>
                          <small class="goal-warn">(Warning: {{ g.warning_duration_seconds / 60 }}m)</small>
                        </div>
                      }
                    </div>
                  </div>
                }
              </div>

              <!-- Calendars Section -->
              <div class="calendars-section" style="margin-top: 32px;">
                <div class="queue-view-header">
                  <div>
                    <h3>Working Business Calendars</h3>
                    <p class="queue-desc">Defines active business hours so SLAs pause overnight and during weekends.</p>
                  </div>
                  <button type="button" class="btn btn-secondary btn-sm" (click)="openCreateCalendarModal()">
                    + New Working Calendar
                  </button>
                </div>

                <div class="triage-table-card">
                  <table class="triage-table">
                    <thead>
                      <tr>
                        <th>Calendar Name</th>
                        <th>Working Days</th>
                        <th>Daily Hours</th>
                        <th>Timezone</th>
                        <th>Coverage</th>
                      </tr>
                    </thead>
                    <tbody>
                      @for (cal of calendars(); track cal.id) {
                        <tr>
                          <td><strong>{{ cal.name }}</strong></td>
                          <td>{{ cal.is_24x7 ? 'Every day (7 days)' : 'Monday - Friday (1-5)' }}</td>
                          <td>{{ cal.is_24x7 ? '24 Hours' : cal.start_time + ' - ' + cal.end_time }}</td>
                          <td><span class="badge badge-info">{{ cal.timezone }}</span></td>
                          <td>
                            <span class="status-pill" [class.pill-done]="cal.is_24x7">
                              {{ cal.is_24x7 ? '24x7 Round-the-Clock' : '8h Business Day' }}
                            </span>
                          </td>
                        </tr>
                      }
                    </tbody>
                  </table>
                </div>
              </div>
            </div>
          }

          <!-- --------------------------------------------------------------- -->
          <!-- SUBVIEW 3: CANNED RESPONSES TEMPLATES                           -->
          <!-- --------------------------------------------------------------- -->
          @else if (activeSubView() === 'canned') {
            <div class="canned-responses-view">
              <div class="queue-view-header">
                <div>
                  <h2>Canned Responses &amp; Quick Reply Templates</h2>
                  <p class="queue-desc">Pre-defined responses with smart variable placeholders for rapid customer resolution.</p>
                </div>
                <div class="queue-header-actions">
                  <button type="button" class="btn btn-primary btn-sm" (click)="openCreateCannedModal()">
                    + New Canned Response
                  </button>
                </div>
              </div>

              <div class="canned-grid">
                @for (cr of cannedResponses(); track cr.id) {
                  <div class="canned-card">
                    <div class="canned-card-header">
                      <h4>{{ cr.title }}</h4>
                      <span class="shortcut-tag">{{ cr.shortcut }}</span>
                    </div>
                    <pre class="canned-content-box">{{ cr.content }}</pre>
                    <div class="canned-card-footer">
                      <span class="text-muted">Placeholders available for substitution</span>
                      <button type="button" class="btn btn-danger btn-xs" (click)="deleteCanned(cr.id)">
                        Delete
                      </button>
                    </div>
                  </div>
                }
              </div>
            </div>
          }

          <!-- --------------------------------------------------------------- -->
          <!-- SUBVIEW 4: CUSTOMER ORGANIZATIONS                               -->
          <!-- --------------------------------------------------------------- -->
          @else if (activeSubView() === 'organizations') {
            <div class="organizations-view">
              <div class="queue-view-header">
                <div>
                  <h2>Customer Organizations &amp; Multi-Tenant Segregation</h2>
                  <p class="queue-desc">Manage external client accounts, auto-join email domains, and shared portal ticket visibility.</p>
                </div>
                <div class="queue-header-actions">
                  <button type="button" class="btn btn-primary btn-sm" (click)="openCreateOrgModal()">
                    + Add Organization
                  </button>
                </div>
              </div>

              <div class="orgs-layout" style="display: grid; grid-template-columns: 320px 1fr; gap: 20px; align-items: start;">
                <!-- Left: Organizations list -->
                <div class="orgs-list-card" style="background: #ffffff; border: 1px solid #dfe1e6; border-radius: 6px; overflow: hidden;">
                  <div style="padding: 12px 16px; border-bottom: 1px solid #dfe1e6; background: #fafbfc; font-weight: 700; font-size: 13px;">
                    Organizations ({{ organizations().length }})
                  </div>
                  <div class="orgs-nav-list" style="display: flex; flex-direction: column;">
                    @for (org of organizations(); track org.id) {
                      <div
                        (click)="selectOrganization(org)"
                        style="padding: 12px 16px; border-bottom: 1px solid #f4f5f7; cursor: pointer; display: flex; justify-content: space-between; align-items: center;"
                        [style.background]="activeOrg()?.id === org.id ? '#ebf2ff' : 'transparent'"
                        [style.border-left]="activeOrg()?.id === org.id ? '3px solid #0052cc' : '3px solid transparent'"
                      >
                        <div>
                          <div style="font-weight: 600; font-size: 13px; color: #172b4d;">🏢 {{ org.name }}</div>
                          @if (org.domain) {
                            <div style="font-size: 11px; color: #5e6c84;">Domain: &#64;{{ org.domain }}</div>
                          }
                        </div>
                        <span class="badge" style="background: #dfe1e6; color: #172b4d; font-size: 11px;">
                          {{ org.member_count || 0 }} members
                        </span>
                      </div>
                    } @empty {
                      <div style="padding: 24px; text-align: center; color: #5e6c84; font-size: 13px;">
                        No organizations created for this desk yet.
                      </div>
                    }
                  </div>
                </div>

                <!-- Right: Active Organization Detail & Members -->
                @if (activeOrg()) {
                  <div class="org-detail-card" style="background: #ffffff; border: 1px solid #dfe1e6; border-radius: 6px; padding: 20px;">
                    <div style="display: flex; justify-content: space-between; align-items: flex-start; margin-bottom: 16px; border-bottom: 1px solid #ebecf0; padding-bottom: 16px;">
                      <div>
                        <h3 style="margin: 0 0 6px 0; font-size: 18px;">🏢 {{ activeOrg()!.name }}</h3>
                        <div style="font-size: 12px; color: #5e6c84; display: flex; gap: 16px;">
                          @if (activeOrg()!.domain) {
                            <span>Domain: <strong style="color: #0052cc;">&#64;{{ activeOrg()!.domain }}</strong> (Auto-enrolls incoming emails)</span>
                          }
                          <span>Created: {{ activeOrg()!.created_at }}</span>
                        </div>
                      </div>
                      <div style="display: flex; gap: 8px;">
                        <button type="button" class="btn btn-primary btn-sm" (click)="openAddMemberModal()">
                          + Add Member
                        </button>
                        <button type="button" class="btn btn-danger btn-sm" (click)="deleteSelectedOrg()">
                          Delete Org
                        </button>
                      </div>
                    </div>

                    <h4 style="margin: 0 0 12px 0; font-size: 14px;">Organization Members</h4>
                    <div class="table-container" style="border: 1px solid #ebecf0; border-radius: 4px; overflow: hidden;">
                      <table class="triage-table" style="margin: 0;">
                        <thead>
                          <tr>
                            <th>User</th>
                            <th>Email</th>
                            <th>User Type</th>
                            <th>Added At</th>
                            <th>Actions</th>
                          </tr>
                        </thead>
                        <tbody>
                          @for (m of activeOrgMembers(); track (m.user_id || m.id)) {
                            <tr>
                              <td>
                                <strong style="color: #172b4d;">{{ m.display_name }}</strong>
                                <span style="font-size: 11px; color: #6b778c; margin-left: 4px;">(&#64;{{ m.username }})</span>
                              </td>
                              <td>{{ m.email }}</td>
                              <td>
                                <span class="badge" [style.background]="m.user_type === 'PORTAL_CUSTOMER' ? '#deebff' : '#eae6ff'" [style.color]="m.user_type === 'PORTAL_CUSTOMER' ? '#0747a6' : '#403294'">
                                  {{ m.user_type || 'PORTAL_CUSTOMER' }}
                                </span>
                              </td>
                              <td style="font-size: 12px; color: #5e6c84;">{{ m.added_at }}</td>
                              <td>
                                <button type="button" class="btn btn-danger btn-xs" (click)="removeOrgMember(m.user_id || m.id)">
                                  Remove
                                </button>
                              </td>
                            </tr>
                          } @empty {
                            <tr>
                              <td colspan="5" style="text-align: center; padding: 24px; color: #5e6c84;">
                                No members in this organization yet. Add users to allow them to view each other's tickets.
                              </td>
                            </tr>
                          }
                        </tbody>
                      </table>
                    </div>
                  </div>
                } @else {
                  <div style="background: #ffffff; border: 1px solid #dfe1e6; border-radius: 6px; padding: 40px; text-align: center; color: #5e6c84;">
                    Select an organization from the list or create a new one.
                  </div>
                }
              </div>
            </div>
          }
        </main>
      </div>

      <!-- ------------------------------------------------------------------- -->
      <!-- MODAL: QUICK REPLY WITH CANNED RESPONSE                             -->
      <!-- ------------------------------------------------------------------- -->
      @if (showReplyModal() && activeItemForReply()) {
        <div class="agent-modal-backdrop" (click)="closeReplyModal()">
          <div class="agent-modal" (click)="$event.stopPropagation()">
            <div class="agent-modal-header">
              <h3>💬 Quick Reply to {{ activeItemForReply()!.key }}</h3>
              <button type="button" class="close-modal-btn" (click)="closeReplyModal()">&times;</button>
            </div>
            <div class="agent-modal-body">
              <div class="form-group">
                <label>Choose Canned Template (or write custom reply):</label>
                <select
                  [(ngModel)]="selectedCannedId"
                  (ngModelChange)="onCannedTemplateSelected($event)"
                  class="form-control"
                >
                  <option [value]="0">-- Select a Template --</option>
                  @for (cr of cannedResponses(); track cr.id) {
                    <option [value]="cr.id">{{ cr.title }} ({{ cr.shortcut }})</option>
                  }
                </select>
              </div>

              <div class="form-group">
                <label>Message Content:</label>
                <textarea
                  rows="7"
                  [(ngModel)]="replyMessage"
                  class="form-control"
                  placeholder="Message to customer..."
                ></textarea>
              </div>

              <div class="form-group">
                <label>Post-action / Next Status:</label>
                <select [(ngModel)]="nextStatusAfterReply" class="form-control">
                  <option value="">Leave current status ({{ activeItemForReply()!.status }})</option>
                  <option value="Waiting for customer">Transition to 'Waiting for customer' (Pauses SLA)</option>
                  <option value="Done">Transition to 'Done' (Completes Resolution SLA)</option>
                </select>
              </div>
            </div>
            <div class="agent-modal-footer">
              <button type="button" class="btn btn-secondary" (click)="closeReplyModal()">
                Cancel
              </button>
              <button
                type="button"
                class="btn btn-primary"
                [disabled]="!replyMessage.trim() || sendingReply()"
                (click)="sendAgentReply()"
              >
                {{ sendingReply() ? 'Sending...' : 'Send Reply' }}
              </button>
            </div>
          </div>
        </div>
      }

      <!-- ------------------------------------------------------------------- -->
      <!-- MODAL: CREATE QUEUE                                                 -->
      <!-- ------------------------------------------------------------------- -->
      @if (showQueueModal()) {
        <div class="agent-modal-backdrop" (click)="closeQueueModal()">
          <div class="agent-modal" (click)="$event.stopPropagation()">
            <div class="agent-modal-header">
              <h3>+ Create Triage Queue</h3>
              <button type="button" class="close-modal-btn" (click)="closeQueueModal()">&times;</button>
            </div>
            <div class="agent-modal-body">
              <div class="form-group">
                <label>Queue Name *</label>
                <input type="text" [(ngModel)]="queueForm.name" class="form-control" placeholder="e.g., High Severity Incidents" />
              </div>
              <div class="form-group">
                <label>Filter Type</label>
                <select [(ngModel)]="queueForm.filter_type" class="form-control">
                  <option value="all">All Requests</option>
                  <option value="unassigned">Unassigned Requests</option>
                  <option value="open_incidents">High / Highest Severity Incidents</option>
                  <option value="waiting_customer">Waiting on Customer</option>
                  <option value="pending_reply">Pending First Response</option>
                </select>
              </div>
              <div class="form-group">
                <label>Icon</label>
                <select [(ngModel)]="queueForm.icon" class="form-control">
                  <option value="inbox">📥 Inbox</option>
                  <option value="report_problem">⚠️ Incident</option>
                  <option value="hourglass_empty">⏳ Waiting</option>
                  <option value="mark_email_unread">✉️ Pending Reply</option>
                  <option value="list_alt">📋 List</option>
                </select>
              </div>
            </div>
            <div class="agent-modal-footer">
              <button type="button" class="btn btn-secondary" (click)="closeQueueModal()">Cancel</button>
              <button type="button" class="btn btn-primary" [disabled]="!queueForm.name.trim()" (click)="saveNewQueue()">
                Create Queue
              </button>
            </div>
          </div>
        </div>
      }

      <!-- ------------------------------------------------------------------- -->
      <!-- MODAL: CREATE CANNED RESPONSE                                       -->
      <!-- ------------------------------------------------------------------- -->
      @if (showCannedModal()) {
        <div class="agent-modal-backdrop" (click)="showCannedModal.set(false)">
          <div class="agent-modal" (click)="$event.stopPropagation()">
            <div class="agent-modal-header">
              <h3>+ New Canned Response Template</h3>
              <button type="button" class="close-modal-btn" (click)="showCannedModal.set(false)">&times;</button>
            </div>
            <div class="agent-modal-body">
              <div class="form-group">
                <label>Template Title *</label>
                <input type="text" [(ngModel)]="cannedForm.title" class="form-control" placeholder="e.g., Acknowledge Outage" />
              </div>
              <div class="form-group">
                <label>Shortcut</label>
                <input type="text" [(ngModel)]="cannedForm.shortcut" class="form-control" placeholder="e.g., /outage" />
              </div>
              <div class="form-group">
                <label>Message Content *</label>
                <textarea rows="6" [(ngModel)]="cannedForm.content" class="form-control" placeholder="Hi customer, ..."></textarea>
                <small class="text-muted">Placeholders: {{ '{{customer_name}}, {{issue_key}}, {{issue_summary}}, {{agent_name}}' }}</small>
              </div>
            </div>
            <div class="agent-modal-footer">
              <button type="button" class="btn btn-secondary" (click)="showCannedModal.set(false)">Cancel</button>
              <button type="button" class="btn btn-primary" [disabled]="!cannedForm.title.trim() || !cannedForm.content.trim()" (click)="saveNewCanned()">
                Save Template
              </button>
            </div>
          </div>
        </div>
      }

      <!-- ------------------------------------------------------------------- -->
      <!-- MODAL: CREATE CUSTOMER ORGANIZATION                                 -->
      <!-- ------------------------------------------------------------------- -->
      @if (showCreateOrgModal()) {
        <div class="agent-modal-backdrop" (click)="closeCreateOrgModal()">
          <div class="agent-modal" (click)="$event.stopPropagation()">
            <div class="agent-modal-header">
              <h3>+ Add Customer Organization</h3>
              <button type="button" class="close-modal-btn" (click)="closeCreateOrgModal()">&times;</button>
            </div>
            <div class="agent-modal-body">
              <div class="form-group">
                <label>Organization Name *</label>
                <input
                  type="text"
                  [(ngModel)]="orgForm.name"
                  class="form-control"
                  placeholder="e.g., Acme Corporation"
                />
              </div>
              <div class="form-group">
                <label>Email Domain (Optional)</label>
                <input
                  type="text"
                  [(ngModel)]="orgForm.domain"
                  class="form-control"
                  placeholder="e.g., acme.com (auto-assigns incoming email senders)"
                />
                <small class="text-muted">Customers sending emails from this domain will automatically join this organization.</small>
              </div>
            </div>
            <div class="agent-modal-footer">
              <button type="button" class="btn btn-secondary" (click)="closeCreateOrgModal()">Cancel</button>
              <button
                type="button"
                class="btn btn-primary"
                [disabled]="!orgForm.name.trim() || savingOrg()"
                (click)="submitCreateOrg()"
              >
                {{ savingOrg() ? 'Saving...' : 'Create Organization' }}
              </button>
            </div>
          </div>
        </div>
      }

      <!-- ------------------------------------------------------------------- -->
      <!-- MODAL: ADD ORGANIZATION MEMBER                                      -->
      <!-- ------------------------------------------------------------------- -->
      @if (showAddMemberModal() && activeOrg()) {
        <div class="agent-modal-backdrop" (click)="closeAddMemberModal()">
          <div class="agent-modal" (click)="$event.stopPropagation()">
            <div class="agent-modal-header">
              <h3>+ Add Member to {{ activeOrg()!.name }}</h3>
              <button type="button" class="close-modal-btn" (click)="closeAddMemberModal()">&times;</button>
            </div>
            <div class="agent-modal-body">
              <div class="form-group">
                <label>Select User:</label>
                <select [(ngModel)]="newMemberUserId" class="form-control">
                  <option [value]="0">-- Choose user by name/email --</option>
                  @for (u of availableUsersToAdd(); track u.id) {
                    <option [value]="u.id">
                      {{ u.display_name }} ({{ u.email }}) [{{ u.user_type || 'INTERNAL' }}]
                    </option>
                  }
                </select>
              </div>
              <div class="form-group">
                <label>Or Enter User Email Address:</label>
                <input
                  type="email"
                  [(ngModel)]="newMemberEmail"
                  class="form-control"
                  placeholder="user@example.com"
                />
              </div>
            </div>
            <div class="agent-modal-footer">
              <button type="button" class="btn btn-secondary" (click)="closeAddMemberModal()">Cancel</button>
              <button
                type="button"
                class="btn btn-primary"
                [disabled]="(!newMemberUserId && !newMemberEmail.trim()) || addingMember()"
                (click)="submitAddMember()"
              >
                {{ addingMember() ? 'Adding...' : 'Add Member' }}
              </button>
            </div>
          </div>
        </div>
      }
    </div>
  `,
  styles: [`
    .agent-workspace {
      min-height: calc(100vh - 56px);
      background: #f4f5f7;
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
      color: #172b4d;
    }
    .agent-topbar {
      background: #ffffff;
      border-bottom: 1px solid #dfe1e6;
      padding: 12px 24px;
      display: flex;
      justify-content: space-between;
      align-items: center;
    }
    .topbar-left {
      display: flex;
      align-items: center;
      gap: 16px;
    }
    .desk-badge {
      background: #0052cc;
      color: #ffffff;
      font-size: 11px;
      font-weight: 700;
      padding: 3px 8px;
      border-radius: 3px;
      letter-spacing: 0.5px;
    }
    .desk-selector-group {
      display: flex;
      align-items: center;
      gap: 8px;
      font-size: 13px;
      font-weight: 600;
    }
    .select-desk {
      min-width: 240px;
    }
    .topbar-right {
      display: flex;
      gap: 10px;
      align-items: center;
    }
    .agent-layout {
      display: flex;
      min-height: calc(100vh - 120px);
    }
    .agent-sidebar {
      width: 260px;
      background: #fafbfc;
      border-right: 1px solid #dfe1e6;
      padding: 16px 12px;
      flex-shrink: 0;
    }
    .sidebar-section-title {
      font-size: 11px;
      font-weight: 700;
      color: #5e6c84;
      text-transform: uppercase;
      letter-spacing: 0.5px;
      padding: 0 8px 8px;
      display: flex;
      justify-content: space-between;
      align-items: center;
    }
    .btn-icon-add {
      background: transparent;
      border: none;
      font-size: 16px;
      cursor: pointer;
      color: #0052cc;
      font-weight: 700;
    }
    .queue-nav-list {
      display: flex;
      flex-direction: column;
      gap: 4px;
    }
    .queue-nav-item {
      display: flex;
      align-items: center;
      gap: 10px;
      padding: 8px 12px;
      background: transparent;
      border: none;
      border-radius: 4px;
      cursor: pointer;
      font-size: 13px;
      font-weight: 500;
      color: #344563;
      text-align: left;
      width: 100%;
      transition: background 0.15s;
    }
    .queue-nav-item:hover {
      background: #ebecf0;
    }
    .queue-nav-item.active {
      background: #deebff;
      color: #0052cc;
      font-weight: 600;
    }
    .queue-icon {
      font-size: 16px;
      flex-shrink: 0;
    }
    .queue-name {
      flex: 1;
      overflow: hidden;
      text-overflow: ellipsis;
      white-space: nowrap;
    }
    .queue-count {
      background: #ebecf0;
      color: #42526e;
      font-size: 11px;
      font-weight: 700;
      padding: 2px 7px;
      border-radius: 10px;
    }
    .queue-count.count-alert {
      background: #ffebe6;
      color: #de350b;
    }
    .agent-content {
      flex: 1;
      padding: 24px;
      overflow-x: auto;
    }
    .queue-view-header {
      display: flex;
      justify-content: space-between;
      align-items: flex-start;
      margin-bottom: 20px;
    }
    .queue-view-header h2 {
      margin: 0;
      font-size: 20px;
      color: #172b4d;
    }
    .queue-desc {
      margin: 4px 0 0 0;
      font-size: 13px;
      color: #5e6c84;
    }
    .code-tag {
      background: #ebecf0;
      padding: 1px 5px;
      border-radius: 3px;
      font-family: monospace;
      font-size: 12px;
    }
    .triage-table-card {
      background: #ffffff;
      border: 1px solid #dfe1e6;
      border-radius: 6px;
      box-shadow: 0 1px 3px rgba(9, 30, 66, 0.05);
      overflow-x: auto;
    }
    .triage-table {
      width: 100%;
      border-collapse: collapse;
      font-size: 13px;
    }
    .triage-table th {
      background: #f4f5f7;
      text-align: left;
      padding: 10px 14px;
      border-bottom: 2px solid #dfe1e6;
      color: #5e6c84;
      font-size: 12px;
      white-space: nowrap;
    }
    .triage-table td {
      padding: 12px 14px;
      border-bottom: 1px solid #ebecf0;
      vertical-align: middle;
    }
    .triage-row:hover {
      background: #f8f9fa;
    }
    .item-key-link {
      color: #0052cc;
      font-weight: 700;
      text-decoration: none;
    }
    .item-key-link:hover {
      text-decoration: underline;
    }
    .item-summary-cell {
      display: flex;
      flex-direction: column;
      gap: 3px;
    }
    .item-title {
      color: #172b4d;
    }
    .item-rt-tag {
      font-size: 11px;
      color: #5e6c84;
    }
    .customer-cell {
      display: flex;
      flex-direction: column;
    }
    .customer-email {
      color: #5e6c84;
      font-size: 11px;
    }
    .status-pill {
      background: #dfe1e6;
      color: #42526e;
      padding: 3px 8px;
      border-radius: 12px;
      font-size: 11px;
      font-weight: 700;
      white-space: nowrap;
    }
    .status-pill.pill-done {
      background: #e3fcef;
      color: #006644;
    }
    .status-pill.pill-wait {
      background: #fffae6;
      color: #ff8b00;
    }
    .priority-pill {
      font-size: 11px;
      font-weight: 600;
      padding: 2px 6px;
      border-radius: 3px;
      white-space: nowrap;
    }
    .pill-highest, .pill-high, .pill-critical {
      background: #ffebe6;
      color: #de350b;
    }
    .pill-medium {
      background: #fff0b3;
      color: #172b4d;
    }
    .pill-low, .pill-lowest {
      background: #e3fcef;
      color: #006644;
    }
    .assignee-badge {
      display: inline-flex;
      align-items: center;
      gap: 6px;
      font-size: 12px;
    }
    .avatar-dot {
      width: 16px;
      height: 16px;
      border-radius: 50%;
    }
    .btn-assign-self {
      background: #ebecf0;
      border: none;
      color: #0052cc;
      font-size: 11px;
      padding: 3px 8px;
      border-radius: 3px;
      cursor: pointer;
      font-weight: 600;
    }
    .sla-indicator {
      display: inline-block;
      padding: 3px 8px;
      border-radius: 4px;
      font-size: 11px;
      font-weight: 700;
      white-space: nowrap;
    }
    .row-actions {
      display: flex;
      gap: 6px;
      align-items: center;
    }
    /* SLAs view */
    .sla-cards-grid {
      display: grid;
      grid-template-columns: repeat(auto-fill, minmax(360px, 1fr));
      gap: 20px;
    }
    .sla-config-card {
      background: #ffffff;
      border: 1px solid #dfe1e6;
      border-radius: 6px;
      padding: 18px;
      box-shadow: 0 1px 2px rgba(9, 30, 66, 0.05);
    }
    .sla-card-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 8px;
    }
    .sla-card-header h3 {
      margin: 0;
      font-size: 16px;
    }
    .sla-card-desc {
      font-size: 13px;
      color: #5e6c84;
      margin: 0 0 12px 0;
    }
    .sla-calendar-tag {
      font-size: 12px;
      background: #fafbfc;
      border: 1px solid #ebecf0;
      padding: 6px 10px;
      border-radius: 4px;
      margin-bottom: 12px;
    }
    .goals-mini-table {
      font-size: 12px;
      border-top: 1px solid #ebecf0;
      padding-top: 10px;
    }
    .goals-title {
      font-weight: 700;
      color: #5e6c84;
      margin-bottom: 6px;
    }
    .goal-row {
      display: flex;
      justify-content: space-between;
      padding: 4px 0;
      border-bottom: 1px dashed #f4f5f7;
    }
    /* Canned view */
    .canned-grid {
      display: grid;
      grid-template-columns: repeat(auto-fill, minmax(340px, 1fr));
      gap: 16px;
    }
    .canned-card {
      background: #ffffff;
      border: 1px solid #dfe1e6;
      border-radius: 6px;
      padding: 16px;
    }
    .canned-card-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 10px;
    }
    .canned-card-header h4 {
      margin: 0;
      font-size: 14px;
    }
    .shortcut-tag {
      background: #deebff;
      color: #0747a6;
      font-family: monospace;
      font-size: 11px;
      font-weight: 700;
      padding: 2px 6px;
      border-radius: 3px;
    }
    .canned-content-box {
      background: #fafbfc;
      border: 1px solid #ebecf0;
      border-radius: 4px;
      padding: 10px;
      font-size: 12px;
      line-height: 1.4;
      white-space: pre-wrap;
      max-height: 120px;
      overflow-y: auto;
      margin: 0 0 10px 0;
    }
    .canned-card-footer {
      display: flex;
      justify-content: space-between;
      align-items: center;
      font-size: 11px;
    }
    /* Modals */
    .agent-modal-backdrop {
      position: fixed;
      top: 0;
      left: 0;
      right: 0;
      bottom: 0;
      background: rgba(9, 30, 66, 0.54);
      display: flex;
      align-items: center;
      justify-content: center;
      z-index: 1000;
      padding: 16px;
    }
    .agent-modal {
      background: #ffffff;
      border-radius: 8px;
      width: 100%;
      max-width: 600px;
      box-shadow: 0 10px 30px rgba(9, 30, 66, 0.2);
      overflow: hidden;
    }
    .agent-modal-header {
      padding: 16px 20px;
      background: #f4f5f7;
      border-bottom: 1px solid #dfe1e6;
      display: flex;
      justify-content: space-between;
      align-items: center;
    }
    .agent-modal-header h3 {
      margin: 0;
      font-size: 16px;
    }
    .close-modal-btn {
      background: transparent;
      border: none;
      font-size: 20px;
      cursor: pointer;
    }
    .agent-modal-body {
      padding: 20px;
    }
    .agent-modal-footer {
      padding: 14px 20px;
      background: #f4f5f7;
      border-top: 1px solid #dfe1e6;
      display: flex;
      justify-content: flex-end;
      gap: 10px;
    }
    .agent-alert {
      margin: 12px 24px 0;
      padding: 10px 16px;
      border-radius: 4px;
      font-size: 13px;
      display: flex;
      justify-content: space-between;
      align-items: center;
    }
    .alert-success {
      background: #e3fcef;
      border: 1px solid #abf5d1;
      color: #006644;
    }
    .alert-error {
      background: #ffebe6;
      border: 1px solid #ffbdad;
      color: #bf2600;
    }
  `],
})
export class ServiceDeskAgentComponent implements OnInit {
  private readonly api = inject(ApiService);
  private readonly router = inject(Router);

  readonly serviceDesks = signal<ServiceDesk[]>([]);
  readonly selectedDeskId = signal<number>(1);
  readonly queues = signal<ServiceDeskQueue[]>([]);
  readonly selectedQueueId = signal<number>(1);
  readonly queueItems = signal<WorkItem[]>([]);

  readonly slas = signal<SlaMetric[]>([]);
  readonly calendars = signal<SlaCalendar[]>([]);
  readonly cannedResponses = signal<CannedResponse[]>([]);

  readonly activeSubView = signal<'tickets' | 'slas' | 'canned' | 'organizations'>('tickets');
  readonly evaluatingSlas = signal<boolean>(false);
  readonly feedback = signal<{ type: 'success' | 'error'; message: string } | null>(null);

  // Organizations
  readonly organizations = signal<CustomerOrganization[]>([]);
  readonly activeOrg = signal<CustomerOrganization | null>(null);
  readonly activeOrgMembers = signal<CustomerOrganizationMember[]>([]);
  readonly showCreateOrgModal = signal<boolean>(false);
  readonly savingOrg = signal<boolean>(false);
  orgForm = { name: '', domain: '' };

  readonly showAddMemberModal = signal<boolean>(false);
  readonly addingMember = signal<boolean>(false);
  newMemberUserId = 0;
  newMemberEmail = '';

  // Reply Modal
  readonly showReplyModal = signal<boolean>(false);
  readonly activeItemForReply = signal<WorkItem | null>(null);
  selectedCannedId = 0;
  replyMessage = '';
  nextStatusAfterReply = '';
  readonly sendingReply = signal<boolean>(false);

  // Queue Modal
  readonly showQueueModal = signal<boolean>(false);
  queueForm = {
    name: '',
    filter_type: 'unassigned',
    icon: 'inbox',
  };

  // Canned Modal
  readonly showCannedModal = signal<boolean>(false);
  cannedForm = {
    title: '',
    shortcut: '',
    content: '',
  };

  async ngOnInit(): Promise<void> {
    await this.loadDesks();
    await this.loadCalendars();
    await this.loadCanned();
  }

  async loadDesks(): Promise<void> {
    try {
      const desks = await this.api.getServiceDesks();
      this.serviceDesks.set(desks);
      if (desks.length > 0) {
        this.selectedDeskId.set(desks[0].id);
        await this.loadDeskData(desks[0].id);
      }
    } catch (err: any) {
      this.feedback.set({ type: 'error', message: 'Failed to load service desks: ' + (err.message || err) });
    }
  }

  async loadDeskData(deskId: number): Promise<void> {
    try {
      const qList = await this.api.getServiceDeskQueues(deskId);
      this.queues.set(qList);
      if (qList.length > 0) {
        this.selectedQueueId.set(qList[0].id);
        await this.loadQueueItems(qList[0].id);
      } else {
        this.queueItems.set([]);
      }

      const slaList = await this.api.getServiceDeskSlas(deskId);
      this.slas.set(slaList);

      await this.loadOrganizations(deskId);
    } catch (err: any) {
      this.feedback.set({ type: 'error', message: 'Failed to load desk queues: ' + (err.message || err) });
    }
  }

  async loadQueueItems(queueId: number): Promise<void> {
    try {
      const items = await this.api.getServiceDeskQueueItems(queueId);
      this.queueItems.set(items);
    } catch (err: any) {
      this.feedback.set({ type: 'error', message: 'Failed to load queue items: ' + (err.message || err) });
    }
  }

  async loadCurrentQueueItems(): Promise<void> {
    if (this.selectedQueueId()) {
      await this.loadQueueItems(this.selectedQueueId());
    }
  }

  async loadCalendars(): Promise<void> {
    try {
      const list = await this.api.getSlaCalendars();
      this.calendars.set(list);
    } catch (err) {}
  }

  async loadCanned(): Promise<void> {
    try {
      const list = await this.api.getCannedResponses();
      this.cannedResponses.set(list);
    } catch (err) {}
  }

  async onDeskSelected(id: number): Promise<void> {
    this.selectedDeskId.set(id);
    await this.loadDeskData(id);
  }

  async selectQueue(queueId: number): Promise<void> {
    this.selectedQueueId.set(queueId);
    this.activeSubView.set('tickets');
    await this.loadQueueItems(queueId);
  }

  activeQueue(): ServiceDeskQueue | undefined {
    return this.queues().find((q) => q.id === this.selectedQueueId());
  }

  getSla(it: WorkItem, metricKey: string): WorkItemSla | undefined {
    if (!it.slas) return undefined;
    return it.slas.find((s) => s.metric_key === metricKey);
  }

  getSlaBg(sla: WorkItemSla): string {
    if (sla.is_completed) return '#deebff';
    if (sla.is_paused) return '#f4f5f7';
    if (sla.is_breached) return '#ffebe6';
    if (sla.status === 'approaching') return '#fff0b3';
    return '#e3fcef';
  }

  getSlaFg(sla: WorkItemSla): string {
    if (sla.is_completed) return '#0052cc';
    if (sla.is_paused) return '#5e6c84';
    if (sla.is_breached) return '#de350b';
    if (sla.status === 'approaching') return '#172b4d';
    return '#006644';
  }

  async evaluateAllSlas(): Promise<void> {
    this.evaluatingSlas.set(true);
    try {
      const res = await this.api.evaluateSlas();
      this.feedback.set({
        type: 'success',
        message: `Successfully evaluated SLA clocks for ${res.evaluated_count} active tickets.`,
      });
      await this.loadCurrentQueueItems();
    } catch (err: any) {
      this.feedback.set({ type: 'error', message: 'Failed to evaluate SLAs: ' + (err.message || err) });
    } finally {
      this.evaluatingSlas.set(false);
    }
  }

  async assignToMe(item: WorkItem): Promise<void> {
    const user = this.api.currentUser();
    if (!user) return;
    try {
      await this.api.updateItem(item.id, { assignee_id: user.id });
      await this.loadCurrentQueueItems();
      this.feedback.set({ type: 'success', message: `Assigned ${item.key} to ${user.display_name}.` });
    } catch (err: any) {
      this.feedback.set({ type: 'error', message: 'Assign failed: ' + (err.message || err) });
    }
  }

  openCannedResponseModal(item: WorkItem): void {
    this.activeItemForReply.set(item);
    this.selectedCannedId = 0;
    this.replyMessage = '';
    this.nextStatusAfterReply = '';
    this.showReplyModal.set(true);
  }

  closeReplyModal(): void {
    this.showReplyModal.set(false);
    this.activeItemForReply.set(null);
  }

  async onCannedTemplateSelected(cannedId: number): Promise<void> {
    const item = this.activeItemForReply();
    if (!item || cannedId <= 0) return;

    try {
      const res = await this.api.applyCannedResponse({
        canned_response_id: cannedId,
        item_id: item.id,
      });
      this.replyMessage = res.content;
    } catch (err) {}
  }

  async sendAgentReply(): Promise<void> {
    const item = this.activeItemForReply();
    if (!item || !this.replyMessage.trim()) return;

    this.sendingReply.set(true);
    try {
      await this.api.addComment(item.key, this.replyMessage.trim());

      // If nextStatus is selected, execute transition
      if (this.nextStatusAfterReply) {
        await this.api.transitionItemState(item.key, this.nextStatusAfterReply);
      }

      this.closeReplyModal();
      this.feedback.set({
        type: 'success',
        message: `Reply sent on ${item.key}. First Response SLA paused/completed!`,
      });
      await this.loadCurrentQueueItems();
      await this.loadDeskData(this.selectedDeskId());
    } catch (err: any) {
      this.feedback.set({ type: 'error', message: 'Failed to send reply: ' + (err.message || err) });
    } finally {
      this.sendingReply.set(false);
    }
  }

  openCreateQueueModal(): void {
    this.queueForm.name = '';
    this.queueForm.filter_type = 'unassigned';
    this.queueForm.icon = 'inbox';
    this.showQueueModal.set(true);
  }

  closeQueueModal(): void {
    this.showQueueModal.set(false);
  }

  async saveNewQueue(): Promise<void> {
    if (!this.queueForm.name.trim()) return;
    try {
      await this.api.createServiceDeskQueue(this.selectedDeskId(), {
        name: this.queueForm.name.trim(),
        filter_type: this.queueForm.filter_type,
        icon: this.queueForm.icon,
      });
      this.closeQueueModal();
      await this.loadDeskData(this.selectedDeskId());
      this.feedback.set({ type: 'success', message: 'Queue created successfully.' });
    } catch (err: any) {
      this.feedback.set({ type: 'error', message: 'Failed to create queue: ' + (err.message || err) });
    }
  }

  openCreateCannedModal(): void {
    this.cannedForm.title = '';
    this.cannedForm.shortcut = '';
    this.cannedForm.content = '';
    this.showCannedModal.set(true);
  }

  async saveNewCanned(): Promise<void> {
    if (!this.cannedForm.title.trim() || !this.cannedForm.content.trim()) return;
    try {
      await this.api.createCannedResponse({
        title: this.cannedForm.title.trim(),
        shortcut: this.cannedForm.shortcut.trim(),
        content: this.cannedForm.content.trim(),
        service_desk_id: this.selectedDeskId(),
      });
      this.showCannedModal.set(false);
      await this.loadCanned();
      this.feedback.set({ type: 'success', message: 'Canned response saved.' });
    } catch (err: any) {
      this.feedback.set({ type: 'error', message: 'Failed to save canned response: ' + (err.message || err) });
    }
  }

  async deleteCanned(id: number): Promise<void> {
    if (!confirm('Are you sure you want to delete this canned response?')) return;
    try {
      await this.api.deleteCannedResponse(id);
      await this.loadCanned();
      this.feedback.set({ type: 'success', message: 'Canned response deleted.' });
    } catch (err: any) {
      this.feedback.set({ type: 'error', message: 'Delete failed: ' + (err.message || err) });
    }
  }

  openCreateSlaModal(): void {
    const name = prompt('Enter SLA Metric Name (e.g. "Time to first response", "VIP Resolution"):');
    if (!name) return;
    const targetHours = prompt('Enter Target Duration in hours (e.g. 4):', '4');
    const targetSec = (parseFloat(targetHours || '4') || 4) * 3600;

    this.api
      .createServiceDeskSla(this.selectedDeskId(), {
        name,
        metric_key: name.toLowerCase().replace(/[^a-z0-9]+/g, '_'),
        target_duration_seconds: targetSec,
        description: 'Target ' + targetHours + ' hours SLA metric',
        calendar_id: 1,
      })
      .then(() => {
        this.loadDeskData(this.selectedDeskId());
        this.feedback.set({ type: 'success', message: 'SLA metric created.' });
      })
      .catch((err) => {
        this.feedback.set({ type: 'error', message: 'Failed: ' + err });
      });
  }

  openCreateCalendarModal(): void {
    const name = prompt('Enter Calendar Name:');
    if (!name) return;
    this.api
      .createSlaCalendar({
        name,
        days_of_week: '1,2,3,4,5',
        start_time: '09:00',
        end_time: '17:00',
        timezone: 'UTC',
        is_24x7: false,
      })
      .then(() => {
        this.loadCalendars();
        this.feedback.set({ type: 'success', message: 'Calendar created.' });
      });
  }

  availableUsersToAdd(): User[] {
    const existing = new Set(this.activeOrgMembers().map((m) => m.user_id || m.id));
    return this.api.users().filter((u) => !existing.has(u.id));
  }

  async loadOrganizations(deskId: number): Promise<void> {
    try {
      const orgs = await this.api.getServiceDeskOrganizations(deskId);
      this.organizations.set(orgs);
      if (orgs.length > 0) {
        if (!this.activeOrg() || !orgs.some((o) => o.id === this.activeOrg()!.id)) {
          await this.selectOrganization(orgs[0]);
        } else {
          await this.loadOrgMembers(this.activeOrg()!.id);
        }
      } else {
        this.activeOrg.set(null);
        this.activeOrgMembers.set([]);
      }
    } catch {
      this.organizations.set([]);
    }
  }

  async switchToOrganizations(): Promise<void> {
    this.activeSubView.set('organizations');
    await this.loadOrganizations(this.selectedDeskId());
  }

  async selectOrganization(org: CustomerOrganization): Promise<void> {
    this.activeOrg.set(org);
    await this.loadOrgMembers(org.id);
  }

  async loadOrgMembers(orgId: number): Promise<void> {
    try {
      const members = await this.api.getOrganizationMembers(orgId);
      this.activeOrgMembers.set(members);
    } catch {
      this.activeOrgMembers.set([]);
    }
  }

  openCreateOrgModal(): void {
    this.orgForm.name = '';
    this.orgForm.domain = '';
    this.showCreateOrgModal.set(true);
  }

  closeCreateOrgModal(): void {
    this.showCreateOrgModal.set(false);
  }

  async submitCreateOrg(): Promise<void> {
    if (!this.orgForm.name.trim()) return;
    this.savingOrg.set(true);
    try {
      const created = await this.api.createServiceDeskOrganization(
        this.selectedDeskId(),
        this.orgForm.name.trim(),
        this.orgForm.domain.trim() || undefined
      );
      this.closeCreateOrgModal();
      await this.loadOrganizations(this.selectedDeskId());
      await this.selectOrganization(created);
      this.feedback.set({ type: 'success', message: `Organization "${created.name}" created successfully.` });
    } catch (err: any) {
      this.feedback.set({ type: 'error', message: 'Failed to create organization: ' + (err.message || err) });
    } finally {
      this.savingOrg.set(false);
    }
  }

  async deleteSelectedOrg(): Promise<void> {
    const org = this.activeOrg();
    if (!org) return;
    if (!confirm(`Are you sure you want to delete organization "${org.name}"?`)) return;
    try {
      await this.api.deleteOrganization(org.id);
      this.activeOrg.set(null);
      await this.loadOrganizations(this.selectedDeskId());
      this.feedback.set({ type: 'success', message: `Organization "${org.name}" deleted.` });
    } catch (err: any) {
      this.feedback.set({ type: 'error', message: 'Failed to delete organization: ' + (err.message || err) });
    }
  }

  async openAddMemberModal(): Promise<void> {
    this.newMemberUserId = 0;
    this.newMemberEmail = '';
    if (this.api.users().length === 0) {
      await this.api.getUsers();
    }
    this.showAddMemberModal.set(true);
  }

  closeAddMemberModal(): void {
    this.showAddMemberModal.set(false);
  }

  async submitAddMember(): Promise<void> {
    const org = this.activeOrg();
    if (!org) return;
    this.addingMember.set(true);
    try {
      await this.api.addOrganizationMember(org.id, {
        user_id: this.newMemberUserId ? Number(this.newMemberUserId) : undefined,
        email: this.newMemberEmail.trim() || undefined,
      });
      this.closeAddMemberModal();
      await this.loadOrgMembers(org.id);
      await this.loadOrganizations(this.selectedDeskId());
      this.feedback.set({ type: 'success', message: 'Member added to organization.' });
    } catch (err: any) {
      this.feedback.set({ type: 'error', message: 'Failed to add member: ' + (err.message || err) });
    } finally {
      this.addingMember.set(false);
    }
  }

  async removeOrgMember(userId: number): Promise<void> {
    const org = this.activeOrg();
    if (!org) return;
    if (!confirm('Are you sure you want to remove this member from the organization?')) return;
    try {
      await this.api.removeOrganizationMember(org.id, userId);
      await this.loadOrgMembers(org.id);
      await this.loadOrganizations(this.selectedDeskId());
      this.feedback.set({ type: 'success', message: 'Member removed from organization.' });
    } catch (err: any) {
      this.feedback.set({ type: 'error', message: 'Failed to remove member: ' + (err.message || err) });
    }
  }

  getIcon(icon: string): string {
    const map: Record<string, string> = {
      inbox: '📥',
      report_problem: '⚠️',
      hourglass_empty: '⏳',
      mark_email_unread: '✉️',
      list_alt: '📋',
    };
    return map[icon] || '📁';
  }
}
