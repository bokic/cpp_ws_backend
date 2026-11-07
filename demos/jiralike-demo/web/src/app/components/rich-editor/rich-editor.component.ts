import {
  Component,
  ElementRef,
  EventEmitter,
  Input,
  Output,
  ViewChild,
  computed,
  inject,
  signal,
} from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { DomSanitizer, SafeHtml } from '@angular/platform-browser';
import { ApiService } from '../../services/api.service';
import { User } from '../../models/jira.models';

@Component({
  selector: 'app-rich-editor',
  standalone: true,
  imports: [CommonModule, FormsModule],
  templateUrl: './rich-editor.component.html',
  styleUrls: ['./rich-editor.component.css'],
})
export class RichEditorComponent {
  readonly api = inject(ApiService);
  private readonly sanitizer = inject(DomSanitizer);

  @Input() value = '';
  @Output() valueChange = new EventEmitter<string>();

  @Input() placeholder = 'Write details, acceptance criteria, or technical specs...';
  @Input() minRows = 5;
  @Input() workItemId?: number;
  @Input() compact = false;

  @ViewChild('editorTextarea') textareaRef?: ElementRef<HTMLTextAreaElement>;
  @ViewChild('fileInput') fileInputRef?: ElementRef<HTMLInputElement>;

  readonly mode = signal<'write' | 'preview' | 'split'>('write');
  readonly isUploading = signal(false);
  readonly uploadMessage = signal('');

  // Mention autocomplete state
  readonly showMentionPopup = signal(false);
  readonly mentionQuery = signal('');
  readonly mentionSelectedIndex = signal(0);
  readonly mentionPopupX = signal(0);
  readonly mentionPopupY = signal(0);
  private mentionStartPos = -1;

  readonly filteredUsers = computed<User[]>(() => {
    const q = this.mentionQuery().toLowerCase().trim();
    const all = this.api.users();
    if (!q) return all.slice(0, 8);
    return all
      .filter(
        (u) =>
          u.username.toLowerCase().includes(q) ||
          u.display_name.toLowerCase().includes(q) ||
          u.role.toLowerCase().includes(q)
      )
      .slice(0, 8);
  });

  readonly renderedHtml = computed<SafeHtml>(() => {
    return this.sanitizer.bypassSecurityTrustHtml(this.renderMarkdown(this.value));
  });

  onInput(newVal: string): void {
    this.value = newVal;
    this.valueChange.emit(newVal);
    this.checkMentionTrigger();
  }

  setMode(newMode: 'write' | 'preview' | 'split'): void {
    this.mode.set(newMode);
  }

  // Formatting operations
  applyFormat(type: string): void {
    const ta = this.textareaRef?.nativeElement;
    if (!ta) return;

    const start = ta.selectionStart;
    const end = ta.selectionEnd;
    const current = this.value || '';
    const selected = current.substring(start, end);

    let replacement = '';
    let cursorOffset = 0;

    switch (type) {
      case 'bold':
        replacement = `**${selected || 'bold text'}**`;
        cursorOffset = selected ? replacement.length : 2;
        break;
      case 'italic':
        replacement = `*${selected || 'italic text'}*`;
        cursorOffset = selected ? replacement.length : 1;
        break;
      case 'strike':
        replacement = `~~${selected || 'strikethrough'}~~`;
        cursorOffset = selected ? replacement.length : 2;
        break;
      case 'underline':
        replacement = `<u>${selected || 'underlined'}</u>`;
        cursorOffset = selected ? replacement.length : 3;
        break;
      case 'code':
        replacement = '`' + (selected || 'inline code') + '`';
        cursorOffset = selected ? replacement.length : 1;
        break;
      case 'codeblock':
        replacement = `\n\`\`\`ts\n${selected || '// Insert code here'}\n\`\`\`\n`;
        cursorOffset = replacement.length - 5;
        break;
      case 'h1':
        replacement = `\n# ${selected || 'Heading 1'}\n`;
        cursorOffset = replacement.length;
        break;
      case 'h2':
        replacement = `\n## ${selected || 'Heading 2'}\n`;
        cursorOffset = replacement.length;
        break;
      case 'h3':
        replacement = `\n### ${selected || 'Heading 3'}\n`;
        cursorOffset = replacement.length;
        break;
      case 'bullet':
        replacement = `\n- ${selected || 'List item'}\n`;
        cursorOffset = replacement.length;
        break;
      case 'number':
        replacement = `\n1. ${selected || 'First item'}\n`;
        cursorOffset = replacement.length;
        break;
      case 'checklist':
        replacement = `\n- [ ] ${selected || 'Task to complete'}\n`;
        cursorOffset = replacement.length;
        break;
      case 'quote':
        replacement = `\n> ${selected || 'Blockquote note'}\n`;
        cursorOffset = replacement.length;
        break;
      case 'note':
        replacement = `\n> [!NOTE]\n> ${selected || 'Informational notice or context.'}\n`;
        cursorOffset = replacement.length;
        break;
      case 'warning':
        replacement = `\n> [!WARNING]\n> ${selected || 'Caution: please review carefully before proceeding.'}\n`;
        cursorOffset = replacement.length;
        break;
      case 'tip':
        replacement = `\n> [!TIP]\n> ${selected || 'Helpful hint or best practice.'}\n`;
        cursorOffset = replacement.length;
        break;
      case 'table':
        replacement = `\n| Column 1 | Column 2 | Column 3 |\n| --- | --- | --- |\n| Item A | In Progress | High |\n| Item B | Done | Medium |\n`;
        cursorOffset = replacement.length;
        break;
      case 'link':
        replacement = `[${selected || 'Link Title'}](https://)`;
        cursorOffset = replacement.length - 1;
        break;
      default:
        return;
    }

    const updated = current.substring(0, start) + replacement + current.substring(end);
    this.value = updated;
    this.valueChange.emit(updated);

    setTimeout(() => {
      ta.focus();
      const newPos = start + cursorOffset;
      ta.setSelectionRange(newPos, newPos);
    }, 0);
  }

  // Trigger file upload from button
  triggerImageUpload(): void {
    this.fileInputRef?.nativeElement.click();
  }

  async onFileSelected(event: Event): Promise<void> {
    const input = event.target as HTMLInputElement;
    if (!input.files || input.files.length === 0) return;
    const file = input.files[0];
    await this.uploadAndEmbedFile(file);
    input.value = '';
  }

  // Paste Event Handler (Ctrl+V)
  async onPaste(event: ClipboardEvent): Promise<void> {
    const items = event.clipboardData?.items;
    if (!items) return;

    for (let i = 0; i < items.length; i++) {
      const item = items[i];
      if (item.type.indexOf('image') !== -1) {
        event.preventDefault();
        const file = item.getAsFile();
        if (file) {
          await this.uploadAndEmbedFile(file);
          return;
        }
      }
    }
  }

  // Drag and Drop handlers
  onDragOver(event: DragEvent): void {
    event.preventDefault();
    event.stopPropagation();
  }

  async onDrop(event: DragEvent): Promise<void> {
    event.preventDefault();
    event.stopPropagation();
    const files = event.dataTransfer?.files;
    if (files && files.length > 0) {
      const file = files[0];
      if (file.type.startsWith('image/')) {
        await this.uploadAndEmbedFile(file);
      }
    }
  }

  private async uploadAndEmbedFile(file: File): Promise<void> {
    this.isUploading.set(true);
    this.uploadMessage.set(`Uploading ${file.name}...`);
    try {
      const res = await this.api.uploadBlob(file, this.workItemId);
      const markdownImage = `\n![${res.original_filename || 'image'}](${res.url})\n`;
      this.insertAtCursor(markdownImage);
      this.uploadMessage.set('Image attached successfully!');
      setTimeout(() => this.uploadMessage.set(''), 3000);
    } catch (err: any) {
      this.uploadMessage.set(`Upload failed: ${err.message || 'Unknown error'}`);
      setTimeout(() => this.uploadMessage.set(''), 4000);
    } finally {
      this.isUploading.set(false);
    }
  }

  private insertAtCursor(textToInsert: string): void {
    const ta = this.textareaRef?.nativeElement;
    const current = this.value || '';
    if (!ta) {
      this.value = current + textToInsert;
      this.valueChange.emit(this.value);
      return;
    }

    const start = ta.selectionStart;
    const end = ta.selectionEnd;
    const updated = current.substring(0, start) + textToInsert + current.substring(end);
    this.value = updated;
    this.valueChange.emit(updated);

    setTimeout(() => {
      ta.focus();
      const pos = start + textToInsert.length;
      ta.setSelectionRange(pos, pos);
    }, 0);
  }

  // Mentions logic
  private checkMentionTrigger(): void {
    const ta = this.textareaRef?.nativeElement;
    if (!ta) return;

    const cursorPos = ta.selectionStart;
    const textBefore = this.value.substring(0, cursorPos);
    const atIndex = textBefore.lastIndexOf('@');

    if (atIndex !== -1 && (atIndex === 0 || /\s/.test(textBefore[atIndex - 1]))) {
      const query = textBefore.substring(atIndex + 1);
      // Query should not contain spaces or newlines
      if (!/[\s\n]/.test(query)) {
        this.mentionStartPos = atIndex;
        this.mentionQuery.set(query);
        this.mentionSelectedIndex.set(0);
        this.showMentionPopup.set(true);
        return;
      }
    }

    this.showMentionPopup.set(false);
    this.mentionStartPos = -1;
  }

  onKeyDown(event: KeyboardEvent): void {
    if (this.showMentionPopup()) {
      const users = this.filteredUsers();
      if (event.key === 'ArrowDown') {
        event.preventDefault();
        this.mentionSelectedIndex.update((idx) => (idx + 1) % Math.max(users.length, 1));
        return;
      } else if (event.key === 'ArrowUp') {
        event.preventDefault();
        this.mentionSelectedIndex.update((idx) => (idx - 1 + users.length) % Math.max(users.length, 1));
        return;
      } else if (event.key === 'Enter' || event.key === 'Tab') {
        if (users.length > 0) {
          event.preventDefault();
          this.selectMentionUser(users[this.mentionSelectedIndex()]);
          return;
        }
      } else if (event.key === 'Escape') {
        this.showMentionPopup.set(false);
        return;
      }
    }
  }

  selectMentionUser(user: User): void {
    if (!user || this.mentionStartPos === -1) return;

    const ta = this.textareaRef?.nativeElement;
    const cursorPos = ta ? ta.selectionStart : this.value.length;
    const before = this.value.substring(0, this.mentionStartPos);
    const after = this.value.substring(cursorPos);
    const mentionToken = `@${user.username} `;

    this.value = before + mentionToken + after;
    this.valueChange.emit(this.value);
    this.showMentionPopup.set(false);
    this.mentionStartPos = -1;

    setTimeout(() => {
      if (ta) {
        ta.focus();
        const pos = before.length + mentionToken.length;
        ta.setSelectionRange(pos, pos);
      }
    }, 0);
  }

  // Markdown rendering engine
  renderMarkdown(md: string): string {
    if (!md) return '<p class="text-muted-preview">Nothing to preview yet.</p>';

    // Escape HTML entities first to prevent XSS
    let html = md
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');

    // Restore safe tags we support
    html = html.replace(/&lt;u&gt;/gi, '<u>').replace(/&lt;\/u&gt;/gi, '</u>');

    // Code Blocks: ```lang ... ```
    html = html.replace(/```([a-zA-Z0-9_-]*)\n([\s\S]*?)```/g, (_, lang, code) => {
      const badge = lang ? `<span class="code-badge">${lang}</span>` : '';
      return `<div class="md-code-block"><div class="md-code-head">${badge}<span>Code Snippet</span></div><pre><code>${code.trim()}</code></pre></div>`;
    });

    // Callout Panels: > [!NOTE], > [!TIP], > [!WARNING], > [!IMPORTANT]
    html = html.replace(/^&gt; \[!(NOTE|INFO)\]\s*\n((?:&gt; .*(?:\n|$))+)/gim, (_, type, content) => {
      const cleaned = content.replace(/^&gt; ?/gm, '').trim();
      return `<div class="md-callout md-callout-note"><div class="callout-icon">ℹ</div><div class="callout-content"><strong>Note</strong><div>${cleaned}</div></div></div>`;
    });
    html = html.replace(/^&gt; \[!TIP\]\s*\n((?:&gt; .*(?:\n|$))+)/gim, (_, content) => {
      const cleaned = content.replace(/^&gt; ?/gm, '').trim();
      return `<div class="md-callout md-callout-tip"><div class="callout-icon">💡</div><div class="callout-content"><strong>Tip</strong><div>${cleaned}</div></div></div>`;
    });
    html = html.replace(/^&gt; \[!(WARNING|CAUTION)\]\s*\n((?:&gt; .*(?:\n|$))+)/gim, (_, type, content) => {
      const cleaned = content.replace(/^&gt; ?/gm, '').trim();
      return `<div class="md-callout md-callout-warning"><div class="callout-icon">⚠</div><div class="callout-content"><strong>Warning</strong><div>${cleaned}</div></div></div>`;
    });
    html = html.replace(/^&gt; \[!(IMPORTANT|ERROR)\]\s*\n((?:&gt; .*(?:\n|$))+)/gim, (_, type, content) => {
      const cleaned = content.replace(/^&gt; ?/gm, '').trim();
      return `<div class="md-callout md-callout-important"><div class="callout-icon">❗</div><div class="callout-content"><strong>Important</strong><div>${cleaned}</div></div></div>`;
    });

    // Blockquotes: > quote
    html = html.replace(/^&gt; (.*$)/gim, '<blockquote class="md-quote">$1</blockquote>');

    // Headings
    html = html.replace(/^### (.*$)/gim, '<h3 class="md-h3">$1</h3>');
    html = html.replace(/^## (.*$)/gim, '<h2 class="md-h2">$1</h2>');
    html = html.replace(/^# (.*$)/gim, '<h1 class="md-h1">$1</h1>');

    // Task Checklists: - [ ] and - [x]
    html = html.replace(/^- \[x\] (.*$)/gim, '<div class="md-task checked"><input type="checkbox" checked disabled /> <span class="task-label">$1</span></div>');
    html = html.replace(/^- \[ \] (.*$)/gim, '<div class="md-task"><input type="checkbox" disabled /> <span class="task-label">$1</span></div>');

    // Bullet & Number Lists
    html = html.replace(/^- (.*$)/gim, '<li class="md-li">$1</li>');
    html = html.replace(/^(\d+)\. (.*$)/gim, '<li class="md-li-num"><strong>$1.</strong> $2</li>');

    // Tables: | col1 | col2 |
    html = html.replace(/((?:\|.+?\|\s*(?:\r?\n|$))+)/g, (match) => {
      const rows = match.trim().split('\n');
      if (rows.length < 2) return match;
      let out = '<div class="table-responsive"><table class="md-table">';
      rows.forEach((row, idx) => {
        const cells = row.split('|').filter((_, cIdx, arr) => cIdx > 0 && cIdx < arr.length - 1);
        if (cells.length === 0) return;
        if (idx === 1 && cells.some((c) => /^\s*:-+:?\s*$/.test(c) || /^\s*-+\s*$/.test(c))) {
          // Separator row, skip
          return;
        }
        out += '<tr>';
        cells.forEach((cell) => {
          const tag = idx === 0 ? 'th' : 'td';
          out += `<${tag}>${cell.trim()}</${tag}>`;
        });
        out += '</tr>';
      });
      out += '</table></div>';
      return out;
    });

    // Images: ![alt](url)
    html = html.replace(/!\[(.*?)\]\((.*?)\)/g, (_, alt, url) => {
      const safeUrl = /^(https?:\/\/|\/|data:image\/)/i.test(url.trim()) ? url.trim() : '#';
      return `<div class="md-img-card"><img src="${safeUrl}" alt="${alt}" loading="lazy" /><div class="md-img-caption">${alt}</div></div>`;
    });

    // Links: [text](url)
    html = html.replace(/\[(.*?)\]\((.*?)\)/g, (_, text, url) => {
      const safeUrl = /^(https?:\/\/|mailto:|\/|#)/i.test(url.trim()) ? url.trim() : '#';
      return `<a href="${safeUrl}" target="_blank" rel="noopener" class="md-link">${text} ↗</a>`;
    });

    // Bold, Italic, Strikethrough, Inline Code
    html = html.replace(/\*\*(.*?)\*\*/g, '<strong>$1</strong>');
    html = html.replace(/\*(.*?)\*/g, '<em>$1</em>');
    html = html.replace(/~~(.*?)~~/g, '<del>$1</del>');
    html = html.replace(/`([^`]+)`/g, '<code class="inline-code">$1</code>');

    // User Mentions: @username
    html = html.replace(/(?:^|\s)@([a-zA-Z0-9_.-]+)/g, (match, uname) => {
      return ` <span class="mention-badge" title="Mentioned @${uname}">@${uname}</span>`;
    });

    // Issue Key Auto-Links: PROJ-123 / JIRA-123
    html = html.replace(/\b([A-Z][A-Z0-9]+-[0-9]+)\b/g, (match, issueKey) => {
      return `<a href="/items/${issueKey}" class="issue-mention-badge" title="View ${issueKey}">🏷 ${issueKey}</a>`;
    });

    // Paragraph line breaks
    html = html.replace(/\n\n/g, '<p></p>');
    html = html.replace(/\n/g, '<br />');

    return html;
  }
}
