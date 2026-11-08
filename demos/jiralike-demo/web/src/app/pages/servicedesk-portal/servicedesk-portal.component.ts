import { Component, OnInit, inject, signal } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { ActivatedRoute, Router, RouterLink } from '@angular/router';
import { ApiService } from '../../services/api.service';
import {
  CustomerPortalHome,
  CustomerTicketDetail,
  CustomerOrganization,
  RequestType,
  ServiceDesk,
} from '../../models/jira.models';

@Component({
  selector: 'app-servicedesk-portal',
  standalone: true,
  imports: [CommonModule, FormsModule, RouterLink],
  template: `
    <div class="portal-wrapper">
      <!-- Portal Top Header / Hero -->
      <header class="portal-header">
        <div class="portal-header-content">
          <div class="portal-brand">
            <span class="portal-brand-icon">🎧</span>
            <div>
              <h1>{{ portalData()?.instance_name || 'JiraLike Help Center' }}</h1>
              <p>Customer Support Portal &amp; Service Desk</p>
            </div>
          </div>
          <div class="portal-header-actions">
            <button
              type="button"
              class="btn-portal"
              [class.active]="viewMode() === 'catalog'"
              (click)="switchToCatalog()"
            >
              📚 Browse Catalog
            </button>
            <button
              type="button"
              class="btn-portal"
              [class.active]="viewMode() === 'my-requests'"
              (click)="switchToMyRequests()"
            >
              📋 My Requests
              @if (myRequests().length > 0) {
                <span class="portal-counter">{{ myRequests().length }}</span>
              }
            </button>
            <a routerLink="/servicedesk/agent" class="btn-portal btn-portal-agent" title="Switch to Agent Queue View">
              ⚡ Agent Queues &rarr;
            </a>
          </div>
        </div>
      </header>

      <!-- Feedback Alert -->
      @if (feedback()) {
        <div class="portal-alert" [class.alert-success]="feedback()!.type === 'success'" [class.alert-error]="feedback()!.type === 'error'">
          {{ feedback()!.message }}
          <button type="button" class="close-alert" (click)="feedback.set(null)">&times;</button>
        </div>
      }

      <main class="portal-main-container">
        <!-- ----------------------------------------------------------------- -->
        <!-- VIEW 1: TICKET DETAIL / TRACKER VIEW                              -->
        <!-- ----------------------------------------------------------------- -->
        @if (viewMode() === 'ticket-detail' && activeTicket()) {
          <div class="ticket-detail-view">
            <div class="ticket-top-nav">
              <button type="button" class="btn btn-secondary btn-sm" (click)="switchToCatalog()">
                &larr; Back to Help Center
              </button>
              <div class="ticket-status-pills">
                <span class="status-pill" [class.pill-done]="activeTicket()!.status === 'Done'" [class.pill-wait]="activeTicket()!.status.includes('Waiting')">
                  {{ activeTicket()!.status }}
                </span>
                <span class="priority-pill pill-{{ activeTicket()!.priority.toLowerCase() }}">
                  {{ activeTicket()!.priority }}
                </span>
              </div>
            </div>

            <div class="ticket-card">
              <div class="ticket-header-row">
                <div>
                  <span class="ticket-key-badge">{{ activeTicket()!.key }}</span>
                  <span class="ticket-req-type">{{ activeTicket()!.request_type_name }}</span>
                  @if (activeTicket()!.organization_name) {
                    <span class="badge" style="background: #ebecf0; color: #172b4d; border: 1px solid #dfe1e6; font-size: 11px; margin-left: 6px;">🏢 {{ activeTicket()!.organization_name }}</span>
                  }
                  <h2 class="ticket-title">{{ activeTicket()!.title }}</h2>
                </div>
                <div class="ticket-date-info">
                  <span>Created: {{ activeTicket()!.created_at }}</span>
                  <span>Desk: {{ activeTicket()!.service_desk_name }}</span>
                </div>
              </div>

              <!-- Customer SLAs Summary -->
              @if (activeTicket()!.slas && activeTicket()!.slas.length > 0) {
                <div class="portal-sla-summary">
                  <div class="portal-sla-title">Service Level Targets</div>
                  <div class="sla-badges-row">
                    @for (sla of activeTicket()!.slas; track sla.id) {
                      <div class="portal-sla-badge" [style.border-left-color]="sla.badge_color">
                        <span class="sla-metric-label">{{ sla.name }}:</span>
                        <strong [style.color]="sla.badge_color">
                          @if (sla.is_completed) {
                            ✓ Completed
                          } @else if (sla.is_paused) {
                            ⏸ Paused (Waiting for Customer)
                          } @else if (sla.is_breached) {
                            ⚠️ Breached ({{ sla.remaining_formatted }})
                          } @else {
                            ⏱ {{ sla.remaining_formatted }} left
                          }
                        </strong>
                      </div>
                    }
                  </div>
                </div>
              }

              <div class="ticket-body">
                <h3>Description</h3>
                <div class="ticket-desc-box">
                  {{ activeTicket()!.description || 'No additional details provided.' }}
                </div>
              </div>

              <!-- Conversation Timeline -->
              <div class="ticket-comments-section">
                <h3>Activity &amp; Conversation</h3>
                <div class="comments-stream">
                  @for (c of activeTicket()!.comments; track c.id) {
                    <div class="portal-comment-card" [class.agent-comment]="c.author_id && c.author_id > 0">
                      <div class="comment-author-row">
                        <span class="author-name">
                          {{ c.author_name }}
                          @if (c.author_id && c.author_id > 0) {
                            <span class="agent-tag">Support Agent</span>
                          } @else {
                            <span class="customer-tag">Customer</span>
                          }
                        </span>
                        <span class="comment-time">{{ c.created_at }}</span>
                      </div>
                      <div class="comment-body-text">{{ c.body }}</div>
                    </div>
                  } @empty {
                    <div class="empty-stream">No responses yet. An agent will reply shortly!</div>
                  }
                </div>

                <!-- Add Comment Form -->
                <div class="add-reply-card">
                  <h4>Reply to this ticket</h4>
                  <textarea
                    rows="3"
                    [(ngModel)]="replyText"
                    placeholder="Type your message, reply, or additional details here..."
                    class="form-control"
                  ></textarea>
                  <div class="reply-actions">
                    <button
                      type="button"
                      class="btn btn-primary btn-sm"
                      [disabled]="!replyText.trim() || submittingReply()"
                      (click)="submitTicketReply()"
                    >
                      {{ submittingReply() ? 'Sending...' : 'Send Message' }}
                    </button>
                  </div>
                </div>
              </div>
            </div>
          </div>
        }

        <!-- ----------------------------------------------------------------- -->
        <!-- VIEW 2: REQUEST CATALOG                                          -->
        <!-- ----------------------------------------------------------------- -->
        @else if (viewMode() === 'catalog') {
          <div class="catalog-view">
            <!-- Search Input -->
            <div class="portal-search-box">
              <input
                type="text"
                [(ngModel)]="searchFilter"
                placeholder="🔍 Search help topics, request types, or common issues..."
                class="form-control portal-search-input"
              />
            </div>

            <!-- Service Desks & Request Types -->
            @for (desk of filteredDesks(); track desk.id) {
              <div class="desk-section">
                <div class="desk-header">
                  <h3>{{ desk.name }}</h3>
                  <span class="desk-key-pill">{{ desk.portal_key }}</span>
                  @if (desk.description) {
                    <p class="desk-desc">{{ desk.description }}</p>
                  }
                </div>

                <div class="request-types-grid">
                  @for (rt of desk.request_types; track rt.id) {
                    <div class="req-type-card" (click)="openCreateModal(desk, rt)">
                      <div class="rt-icon">{{ getIcon(rt.icon) }}</div>
                      <div class="rt-info">
                        <h4>{{ rt.name }}</h4>
                        <p>{{ rt.description || 'Submit a new request for this category' }}</p>
                      </div>
                      <button type="button" class="btn-raise">Raise &rarr;</button>
                    </div>
                  }
                </div>
              </div>
            } @empty {
              <div class="empty-state">
                No service desks or request types found matching "{{ searchFilter }}".
              </div>
            }
          </div>
        }

        <!-- ----------------------------------------------------------------- -->
        <!-- VIEW 3: MY REQUESTS TRACKER                                      -->
        <!-- ----------------------------------------------------------------- -->
        @else if (viewMode() === 'my-requests') {
          <div class="my-requests-view">
            <div class="my-requests-header">
              <div>
                <h2>My Submitted &amp; Organization Requests</h2>
                <p style="margin: 0; font-size: 13px; color: #5e6c84;">
                  Tickets raised by you or shared with your customer organizations
                </p>
              </div>
              <div class="email-filter-box">
                @if (myOrganizations().length > 0) {
                  <select
                    [ngModel]="selectedOrgFilter()"
                    (ngModelChange)="selectedOrgFilter.set($event)"
                    class="form-control"
                    style="font-size: 12px; height: 32px; width: auto;"
                  >
                    <option value="ALL">All Organizations &amp; Personal</option>
                    @for (org of myOrganizations(); track org.id) {
                      <option [value]="org.id">🏢 {{ org.name }}</option>
                    }
                  </select>
                }
                @if (!api.currentUser()) {
                  <input
                    type="email"
                    [(ngModel)]="customerEmail"
                    placeholder="Filter by customer email..."
                    class="form-control"
                    (keyup.enter)="loadMyRequests()"
                  />
                }
                <button type="button" class="btn btn-secondary btn-sm" (click)="loadMyRequests()">
                  Refresh
                </button>
              </div>
            </div>

            <div class="table-container">
              <table class="portal-table">
                <thead>
                  <tr>
                    <th>Key</th>
                    <th>Summary</th>
                    <th>Request Type</th>
                    <th>Service Desk</th>
                    <th>Organization / Share</th>
                    <th>Reporter</th>
                    <th>Status</th>
                    <th>Last Updated</th>
                    <th>Action</th>
                  </tr>
                </thead>
                <tbody>
                  @for (req of filteredRequests(); track req.id) {
                    <tr class="clickable-row" (click)="viewTicket(req.key)">
                      <td><strong class="ticket-link">{{ req.key }}</strong></td>
                      <td>{{ req.title }}</td>
                      <td><span class="badge badge-info">{{ req.request_type_name }}</span></td>
                      <td>{{ req.service_desk_name }}</td>
                      <td>
                        @if (req.organization_name) {
                          <span class="badge" style="background: #e6fcff; color: #006580; border: 1px solid #b3f5ff; font-size: 11px;">
                            🏢 {{ req.organization_name }}
                          </span>
                        } @else {
                          <span style="color: #8993a4; font-size: 12px;">Private</span>
                        }
                      </td>
                      <td>
                        @if (req.is_my_request) {
                          <span class="badge" style="background: #e3fcef; color: #006644; font-size: 11px;">You</span>
                        } @else {
                          <span style="font-size: 12px;">{{ req.customer_name || 'Organization Member' }}</span>
                        }
                      </td>
                      <td>
                        <span class="status-pill" [class.pill-done]="req.status === 'Done'" [class.pill-wait]="req.status.includes('Waiting')">
                          {{ req.status }}
                        </span>
                      </td>
                      <td>{{ req.updated_at }}</td>
                      <td>
                        <button type="button" class="btn btn-xs btn-primary" (click)="viewTicket(req.key); $event.stopPropagation()">
                          View &rarr;
                        </button>
                      </td>
                    </tr>
                  } @empty {
                    <tr>
                      <td colspan="9" class="text-center" style="padding: 30px; color: #5e6c84;">
                        No requests found for this customer or organization.
                      </td>
                    </tr>
                  }
                </tbody>
              </table>
            </div>
          </div>
        }
      </main>

      <!-- ------------------------------------------------------------------- -->
      <!-- MODAL: RAISE NEW SUPPORT REQUEST                                    -->
      <!-- ------------------------------------------------------------------- -->
      @if (showCreateModal()) {
        <div class="portal-modal-backdrop" (click)="closeCreateModal()">
          <div class="portal-modal" (click)="$event.stopPropagation()">
            <div class="portal-modal-header">
              <h3>
                {{ selectedRequestType() ? selectedRequestType()!.name : 'Raise New Request' }}
              </h3>
              <button type="button" class="close-modal-btn" (click)="closeCreateModal()">&times;</button>
            </div>

            <div class="portal-modal-body">
              @if (selectedRequestType()?.help_text) {
                <div class="help-box">
                  💡 {{ selectedRequestType()!.help_text }}
                </div>
              }

              <div class="form-group">
                <label>Summary / What do you need help with? *</label>
                <input
                  type="text"
                  [(ngModel)]="requestForm.summary"
                  class="form-control"
                  placeholder="e.g., Cannot access email from remote network"
                />
              </div>

              <div class="form-group">
                <label>Description &amp; Details</label>
                <textarea
                  rows="4"
                  [(ngModel)]="requestForm.description"
                  class="form-control"
                  placeholder="Provide step-by-step reproduction, error messages, or affected tools..."
                ></textarea>
              </div>

              <div class="form-row">
                <div class="form-group col">
                  <label>Your Name *</label>
                  <input
                    type="text"
                    [(ngModel)]="requestForm.customer_name"
                    class="form-control"
                    placeholder="Full name"
                  />
                </div>
                <div class="form-group col">
                  <label>Your Email Address *</label>
                  <input
                    type="email"
                    [(ngModel)]="requestForm.customer_email"
                    class="form-control"
                    placeholder="name@example.com"
                  />
                </div>
              </div>

              <div class="form-group">
                <label>Urgency / Priority</label>
                <select [(ngModel)]="requestForm.priority" class="form-control">
                  <option value="Lowest">Lowest - Minor inconvenience</option>
                  <option value="Low">Low - Low impact</option>
                  <option value="Medium" selected>Medium - Normal issue</option>
                  <option value="High">High - Significant work impairment</option>
                  <option value="Highest">Highest - Critical system outage</option>
                </select>
              </div>

              @if (organizationsForSelectedDesk().length > 0) {
                <div class="form-group">
                  <label>Share with Organization (Optional)</label>
                  <select [(ngModel)]="requestForm.customer_organization_id" class="form-control">
                    <option [ngValue]="null">Private (Visible only to me and support)</option>
                    @for (org of organizationsForSelectedDesk(); track org.id) {
                      <option [ngValue]="org.id">🏢 {{ org.name }} (Visible to all organization members)</option>
                    }
                  </select>
                </div>
              }
            </div>

            <div class="portal-modal-footer">
              <button type="button" class="btn btn-secondary" (click)="closeCreateModal()">
                Cancel
              </button>
              <button
                type="button"
                class="btn btn-primary"
                [disabled]="!requestForm.summary.trim() || !requestForm.customer_name.trim() || submittingRequest()"
                (click)="submitRequest()"
              >
                {{ submittingRequest() ? 'Submitting...' : 'Submit Request' }}
              </button>
            </div>
          </div>
        </div>
      }
    </div>
  `,
  styles: [`
    .portal-wrapper {
      min-height: 100vh;
      background: #f4f5f7;
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;
      color: #172b4d;
    }
    .portal-header {
      background: #0052cc;
      color: #ffffff;
      padding: 24px 32px;
      box-shadow: 0 2px 4px rgba(0, 0, 0, 0.1);
    }
    .portal-header-content {
      max-width: 1100px;
      margin: 0 auto;
      display: flex;
      justify-content: space-between;
      align-items: center;
      flex-wrap: wrap;
      gap: 16px;
    }
    .portal-brand {
      display: flex;
      align-items: center;
      gap: 14px;
    }
    .portal-brand-icon {
      font-size: 36px;
    }
    .portal-brand h1 {
      margin: 0;
      font-size: 24px;
      font-weight: 700;
    }
    .portal-brand p {
      margin: 2px 0 0 0;
      font-size: 13px;
      opacity: 0.85;
    }
    .portal-header-actions {
      display: flex;
      gap: 8px;
      align-items: center;
    }
    .btn-portal {
      background: rgba(255, 255, 255, 0.15);
      border: 1px solid rgba(255, 255, 255, 0.3);
      color: #ffffff;
      padding: 8px 16px;
      border-radius: 4px;
      cursor: pointer;
      font-size: 13px;
      font-weight: 600;
      text-decoration: none;
      display: inline-flex;
      align-items: center;
      gap: 6px;
      transition: background 0.15s;
    }
    .btn-portal:hover {
      background: rgba(255, 255, 255, 0.25);
    }
    .btn-portal.active {
      background: #ffffff;
      color: #0052cc;
    }
    .btn-portal-agent {
      background: #ffab00;
      color: #172b4d;
      border: none;
    }
    .btn-portal-agent:hover {
      background: #ff991f;
      color: #172b4d;
    }
    .portal-counter {
      background: #de350b;
      color: #ffffff;
      font-size: 11px;
      padding: 1px 6px;
      border-radius: 10px;
    }
    .portal-main-container {
      max-width: 1100px;
      margin: 28px auto;
      padding: 0 20px;
    }
    .portal-search-box {
      margin-bottom: 24px;
    }
    .portal-search-input {
      font-size: 16px;
      padding: 14px 18px;
      border-radius: 6px;
      box-shadow: 0 1px 3px rgba(9, 30, 66, 0.08);
      border: 1px solid #dfe1e6;
    }
    .desk-section {
      background: #ffffff;
      border-radius: 8px;
      border: 1px solid #dfe1e6;
      padding: 24px;
      margin-bottom: 24px;
      box-shadow: 0 1px 2px rgba(9, 30, 66, 0.05);
    }
    .desk-header {
      margin-bottom: 16px;
      padding-bottom: 12px;
      border-bottom: 1px solid #ebecf0;
      display: flex;
      flex-wrap: wrap;
      align-items: baseline;
      gap: 10px;
    }
    .desk-header h3 {
      margin: 0;
      font-size: 18px;
      color: #172b4d;
    }
    .desk-key-pill {
      background: #ebecf0;
      color: #42526e;
      font-size: 11px;
      font-weight: 700;
      padding: 2px 6px;
      border-radius: 3px;
    }
    .desk-desc {
      width: 100%;
      margin: 4px 0 0 0;
      font-size: 13px;
      color: #5e6c84;
    }
    .request-types-grid {
      display: grid;
      grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
      gap: 16px;
    }
    .req-type-card {
      background: #fafbfc;
      border: 1px solid #dfe1e6;
      border-radius: 6px;
      padding: 16px;
      display: flex;
      align-items: center;
      gap: 14px;
      cursor: pointer;
      transition: all 0.2s ease;
    }
    .req-type-card:hover {
      background: #ffffff;
      border-color: #4c9aff;
      box-shadow: 0 4px 8px rgba(9, 30, 66, 0.08);
      transform: translateY(-1px);
    }
    .rt-icon {
      font-size: 28px;
      flex-shrink: 0;
    }
    .rt-info {
      flex: 1;
    }
    .rt-info h4 {
      margin: 0 0 4px 0;
      font-size: 14px;
      font-weight: 600;
      color: #172b4d;
    }
    .rt-info p {
      margin: 0;
      font-size: 12px;
      color: #5e6c84;
      line-height: 1.3;
    }
    .btn-raise {
      background: transparent;
      border: none;
      color: #0052cc;
      font-weight: 600;
      font-size: 13px;
      cursor: pointer;
    }
    /* Ticket Tracker */
    .ticket-top-nav {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 16px;
    }
    .ticket-card {
      background: #ffffff;
      border: 1px solid #dfe1e6;
      border-radius: 8px;
      padding: 24px;
      box-shadow: 0 1px 3px rgba(9, 30, 66, 0.05);
    }
    .ticket-header-row {
      display: flex;
      justify-content: space-between;
      align-items: flex-start;
      border-bottom: 1px solid #ebecf0;
      padding-bottom: 16px;
      margin-bottom: 20px;
    }
    .ticket-key-badge {
      background: #deebff;
      color: #0747a6;
      font-weight: 700;
      padding: 3px 8px;
      border-radius: 3px;
      font-size: 13px;
      margin-right: 8px;
    }
    .ticket-req-type {
      color: #5e6c84;
      font-size: 13px;
      font-weight: 500;
    }
    .ticket-title {
      margin: 10px 0 0 0;
      font-size: 20px;
      color: #172b4d;
    }
    .ticket-date-info {
      display: flex;
      flex-direction: column;
      align-items: flex-end;
      gap: 4px;
      font-size: 12px;
      color: #5e6c84;
    }
    .portal-sla-summary {
      background: #f4f5f7;
      border: 1px solid #e1e4e8;
      border-radius: 6px;
      padding: 12px 16px;
      margin-bottom: 20px;
    }
    .portal-sla-title {
      font-size: 11px;
      font-weight: 700;
      color: #5e6c84;
      text-transform: uppercase;
      letter-spacing: 0.5px;
      margin-bottom: 8px;
    }
    .sla-badges-row {
      display: flex;
      gap: 12px;
      flex-wrap: wrap;
    }
    .portal-sla-badge {
      background: #ffffff;
      border: 1px solid #dfe1e6;
      border-left-width: 4px;
      border-radius: 4px;
      padding: 6px 12px;
      font-size: 13px;
      display: flex;
      gap: 6px;
      align-items: center;
    }
    .ticket-desc-box {
      background: #fafbfc;
      border: 1px solid #ebecf0;
      border-radius: 6px;
      padding: 14px;
      font-size: 14px;
      line-height: 1.5;
      color: #253858;
      margin-top: 6px;
    }
    .ticket-comments-section {
      margin-top: 28px;
      border-top: 1px solid #ebecf0;
      padding-top: 20px;
    }
    .comments-stream {
      display: flex;
      flex-direction: column;
      gap: 12px;
      margin-bottom: 20px;
    }
    .portal-comment-card {
      background: #f4f5f7;
      border: 1px solid #dfe1e6;
      border-radius: 6px;
      padding: 12px 16px;
    }
    .portal-comment-card.agent-comment {
      background: #f0f7ff;
      border-color: #b3d4ff;
    }
    .comment-author-row {
      display: flex;
      justify-content: space-between;
      margin-bottom: 6px;
      font-size: 12px;
    }
    .author-name {
      font-weight: 700;
      color: #172b4d;
      display: flex;
      gap: 6px;
      align-items: center;
    }
    .agent-tag {
      background: #0052cc;
      color: #ffffff;
      font-size: 10px;
      padding: 1px 5px;
      border-radius: 3px;
    }
    .customer-tag {
      background: #6b778c;
      color: #ffffff;
      font-size: 10px;
      padding: 1px 5px;
      border-radius: 3px;
    }
    .comment-time {
      color: #5e6c84;
    }
    .comment-body-text {
      font-size: 13px;
      line-height: 1.4;
      color: #172b4d;
      white-space: pre-wrap;
    }
    .add-reply-card {
      background: #fafbfc;
      border: 1px solid #dfe1e6;
      border-radius: 6px;
      padding: 16px;
    }
    .add-reply-card h4 {
      margin: 0 0 10px 0;
      font-size: 14px;
    }
    .reply-actions {
      display: flex;
      justify-content: flex-end;
      margin-top: 8px;
    }
    /* My requests */
    .my-requests-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 16px;
      flex-wrap: wrap;
      gap: 12px;
    }
    .email-filter-box {
      display: flex;
      gap: 8px;
      align-items: center;
    }
    .table-container {
      background: #ffffff;
      border: 1px solid #dfe1e6;
      border-radius: 6px;
      overflow: hidden;
    }
    .portal-table {
      width: 100%;
      border-collapse: collapse;
      font-size: 13px;
    }
    .portal-table th {
      background: #f4f5f7;
      text-align: left;
      padding: 10px 14px;
      border-bottom: 2px solid #dfe1e6;
      color: #5e6c84;
      font-size: 12px;
    }
    .portal-table td {
      padding: 10px 14px;
      border-bottom: 1px solid #ebecf0;
    }
    .clickable-row:hover {
      background: #f4f5f7;
      cursor: pointer;
    }
    .status-pill {
      background: #dfe1e6;
      color: #42526e;
      padding: 3px 8px;
      border-radius: 12px;
      font-size: 11px;
      font-weight: 700;
    }
    .status-pill.pill-done {
      background: #e3fcef;
      color: #006644;
    }
    .status-pill.pill-wait {
      background: #fffae6;
      color: #ff8b00;
    }
    /* Modals */
    .portal-modal-backdrop {
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
    .portal-modal {
      background: #ffffff;
      border-radius: 8px;
      width: 100%;
      max-width: 600px;
      box-shadow: 0 10px 30px rgba(9, 30, 66, 0.2);
      overflow: hidden;
    }
    .portal-modal-header {
      padding: 16px 20px;
      background: #f4f5f7;
      border-bottom: 1px solid #dfe1e6;
      display: flex;
      justify-content: space-between;
      align-items: center;
    }
    .portal-modal-header h3 {
      margin: 0;
      font-size: 16px;
      color: #172b4d;
    }
    .close-modal-btn {
      background: transparent;
      border: none;
      font-size: 20px;
      cursor: pointer;
    }
    .portal-modal-body {
      padding: 20px;
    }
    .help-box {
      background: #deebff;
      border-left: 4px solid #0052cc;
      color: #0747a6;
      padding: 10px 14px;
      border-radius: 4px;
      font-size: 13px;
      margin-bottom: 16px;
    }
    .form-group {
      margin-bottom: 14px;
    }
    .form-group label {
      display: block;
      margin-bottom: 6px;
      font-size: 12px;
      font-weight: 600;
      color: #344563;
    }
    .form-row {
      display: flex;
      gap: 12px;
    }
    .form-row .col {
      flex: 1;
    }
    .form-control {
      width: 100%;
      box-sizing: border-box;
      padding: 8px 10px;
      border: 1px solid #dfe1e6;
      border-radius: 4px;
      font-size: 13px;
      font-family: inherit;
    }
    .portal-modal-footer {
      padding: 14px 20px;
      background: #f4f5f7;
      border-top: 1px solid #dfe1e6;
      display: flex;
      justify-content: flex-end;
      gap: 10px;
    }
    .portal-alert {
      max-width: 1100px;
      margin: 12px auto 0;
      padding: 12px 18px;
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
    .close-alert {
      background: transparent;
      border: none;
      font-size: 18px;
      cursor: pointer;
    }
  `],
})
export class ServiceDeskPortalComponent implements OnInit {
  readonly api = inject(ApiService);
  private readonly route = inject(ActivatedRoute);
  private readonly router = inject(Router);

  readonly viewMode = signal<'catalog' | 'my-requests' | 'ticket-detail'>('catalog');
  readonly portalData = signal<CustomerPortalHome | null>(null);
  readonly myRequests = signal<any[]>([]);
  readonly myOrganizations = signal<CustomerOrganization[]>([]);
  readonly selectedOrgFilter = signal<number | 'ALL'>('ALL');
  readonly activeTicket = signal<CustomerTicketDetail | null>(null);

  searchFilter = '';
  customerEmail = '';
  replyText = '';
  readonly feedback = signal<{ type: 'success' | 'error'; message: string } | null>(null);

  readonly showCreateModal = signal<boolean>(false);
  readonly selectedDesk = signal<ServiceDesk | null>(null);
  readonly selectedRequestType = signal<RequestType | null>(null);
  readonly submittingRequest = signal<boolean>(false);
  readonly submittingReply = signal<boolean>(false);

  requestForm = {
    summary: '',
    description: '',
    customer_name: '',
    customer_email: '',
    priority: 'Medium',
    customer_organization_id: null as number | null,
  };

  async ngOnInit(): Promise<void> {
    const user = this.api.currentUser();
    if (user) {
      this.customerEmail = user.email;
      this.requestForm.customer_name = user.display_name;
      this.requestForm.customer_email = user.email;
      await this.loadMyOrganizations();
    }

    await this.loadPortalData();

    // Check route param for ticket key
    this.route.paramMap.subscribe(async (params) => {
      const key = params.get('key');
      if (key) {
        await this.viewTicket(key);
      }
    });
  }

  async loadPortalData(): Promise<void> {
    try {
      const data = await this.api.getPortalHome();
      this.portalData.set(data);
    } catch (err: any) {
      this.feedback.set({ type: 'error', message: 'Failed to load Help Center catalog: ' + (err.message || err) });
    }
  }

  async loadMyOrganizations(): Promise<void> {
    try {
      const orgs = await this.api.getPortalMyOrganizations();
      this.myOrganizations.set(orgs);
    } catch {
      this.myOrganizations.set([]);
    }
  }

  async loadMyRequests(): Promise<void> {
    try {
      const user = this.api.currentUser();
      const list = user ? await this.api.getPortalMyRequests() : await this.api.getPortalRequests(this.customerEmail);
      this.myRequests.set(list);
    } catch (err: any) {
      this.feedback.set({ type: 'error', message: 'Failed to load requests: ' + (err.message || err) });
    }
  }

  filteredRequests(): any[] {
    const list = this.myRequests();
    const filter = this.selectedOrgFilter();
    if (filter === 'ALL') return list;
    return list.filter((r) => r.customer_organization_id === filter);
  }

  filteredDesks(): Array<ServiceDesk & { request_types: RequestType[] }> {
    const data = this.portalData();
    if (!data) return [];
    if (!this.searchFilter.trim()) return data.service_desks;

    const term = this.searchFilter.toLowerCase();
    return data.service_desks
      .map((d) => {
        const matchesDesk = d.name.toLowerCase().includes(term) || d.description.toLowerCase().includes(term);
        const filteredRt = d.request_types.filter(
          (rt) => rt.name.toLowerCase().includes(term) || rt.description.toLowerCase().includes(term)
        );
        return {
          ...d,
          request_types: matchesDesk ? d.request_types : filteredRt,
        };
      })
      .filter((d) => d.request_types.length > 0);
  }

  switchToCatalog(): void {
    this.viewMode.set('catalog');
    this.activeTicket.set(null);
  }

  async switchToMyRequests(): Promise<void> {
    this.viewMode.set('my-requests');
    this.activeTicket.set(null);
    if (this.api.currentUser()) {
      await this.loadMyOrganizations();
    }
    await this.loadMyRequests();
  }

  async viewTicket(key: string): Promise<void> {
    try {
      const detail = await this.api.getPortalRequestDetail(key);
      this.activeTicket.set(detail);
      this.viewMode.set('ticket-detail');
    } catch (err: any) {
      this.feedback.set({ type: 'error', message: 'Failed to load ticket details: ' + (err.message || err) });
    }
  }

  organizationsForSelectedDesk(): CustomerOrganization[] {
    const desk = this.selectedDesk();
    const orgs = this.myOrganizations();
    if (!desk) return orgs;
    return orgs.filter((o) => !o.service_desk_id || o.service_desk_id === desk.id);
  }

  openCreateModal(desk: ServiceDesk, rt: RequestType): void {
    this.selectedDesk.set(desk);
    this.selectedRequestType.set(rt);
    this.requestForm.summary = '';
    this.requestForm.description = '';
    const deskOrgs = this.organizationsForSelectedDesk();
    this.requestForm.customer_organization_id = deskOrgs.length > 0 ? deskOrgs[0].id : null;
    const user = this.api.currentUser();
    if (user) {
      this.requestForm.customer_name = user.display_name;
      this.requestForm.customer_email = user.email;
    }
    this.showCreateModal.set(true);
  }

  closeCreateModal(): void {
    this.showCreateModal.set(false);
    this.selectedDesk.set(null);
    this.selectedRequestType.set(null);
  }

  async submitRequest(): Promise<void> {
    const desk = this.selectedDesk();
    const rt = this.selectedRequestType();
    if (!desk || !rt || !this.requestForm.summary.trim()) return;

    this.submittingRequest.set(true);
    try {
      const res = await this.api.createPortalRequest({
        service_desk_id: desk.id,
        request_type_id: rt.id,
        summary: this.requestForm.summary.trim(),
        description: this.requestForm.description.trim(),
        customer_name: this.requestForm.customer_name.trim(),
        customer_email: this.requestForm.customer_email.trim(),
        priority: this.requestForm.priority,
        customer_organization_id: this.requestForm.customer_organization_id,
      });

      this.closeCreateModal();
      this.feedback.set({
        type: 'success',
        message: `Your request ${res.key} has been submitted successfully! Our support team is on it.`,
      });

      // Jump directly to tracker view
      await this.viewTicket(res.key);
    } catch (err: any) {
      this.feedback.set({ type: 'error', message: 'Failed to submit request: ' + (err.message || err) });
    } finally {
      this.submittingRequest.set(false);
    }
  }

  async submitTicketReply(): Promise<void> {
    const ticket = this.activeTicket();
    if (!ticket || !this.replyText.trim()) return;

    this.submittingReply.set(true);
    try {
      const user = this.api.currentUser();
      await this.api.addPortalRequestComment(ticket.key, {
        body: this.replyText.trim(),
        author_name: user ? user.display_name : ticket.customer_name,
        author_email: user ? user.email : ticket.customer_email,
      });

      this.replyText = '';
      await this.viewTicket(ticket.key);
      this.feedback.set({ type: 'success', message: 'Reply posted successfully!' });
    } catch (err: any) {
      this.feedback.set({ type: 'error', message: 'Failed to post reply: ' + (err.message || err) });
    } finally {
      this.submittingReply.set(false);
    }
  }

  getIcon(icon: string): string {
    const map: Record<string, string> = {
      help_outline: '❓',
      bug_report: '🐛',
      laptop: '💻',
      receipt_long: '🧾',
      vpn_key: '🔑',
      settings: '⚙️',
      security: '🛡️',
    };
    return map[icon] || '🎫';
  }
}
