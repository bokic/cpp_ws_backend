import { Component, Input, computed, inject } from '@angular/core';
import { CommonModule } from '@angular/common';
import { DomSanitizer, SafeHtml } from '@angular/platform-browser';

@Component({
  selector: 'app-markdown-viewer',
  standalone: true,
  imports: [CommonModule],
  template: `
    <div class="markdown-rendered-view" [innerHTML]="renderedHtml()"></div>
  `,
  styles: [`
    .markdown-rendered-view {
      font-size: 13.5px;
      line-height: 1.6;
      color: #172b4d;
      word-break: break-word;
    }
    :host ::ng-deep h1.md-h1 {
      font-size: 1.4rem;
      font-weight: 700;
      margin: 12px 0 6px;
      color: #172b4d;
      border-bottom: 1px solid #ebecf0;
      padding-bottom: 4px;
    }
    :host ::ng-deep h2.md-h2 {
      font-size: 1.2rem;
      font-weight: 600;
      margin: 10px 0 4px;
      color: #172b4d;
    }
    :host ::ng-deep h3.md-h3 {
      font-size: 1.05rem;
      font-weight: 600;
      margin: 8px 0 4px;
      color: #253858;
    }
    :host ::ng-deep .md-code-block {
      background: #091e42;
      border-radius: 6px;
      margin: 8px 0;
      overflow: hidden;
    }
    :host ::ng-deep .md-code-head {
      display: flex;
      align-items: center;
      justify-content: space-between;
      padding: 4px 10px;
      background: #172b4d;
      color: #97a0af;
      font-size: 11px;
      font-family: monospace;
    }
    :host ::ng-deep .code-badge {
      background: #0052cc;
      color: #ffffff;
      padding: 1px 5px;
      border-radius: 3px;
      font-weight: bold;
    }
    :host ::ng-deep pre {
      margin: 0;
      padding: 10px 12px;
      color: #e6edf3;
      font-family: 'Fira Code', Consolas, Monaco, monospace;
      font-size: 12.5px;
      overflow-x: auto;
    }
    :host ::ng-deep code.inline-code {
      background: #f4f5f7;
      color: #bf2600;
      padding: 1px 5px;
      border-radius: 3px;
      font-family: Consolas, monospace;
      font-size: 12px;
    }
    :host ::ng-deep .md-callout {
      display: flex;
      gap: 10px;
      padding: 10px 14px;
      border-radius: 6px;
      margin: 8px 0;
      font-size: 13px;
      border-left: 4px solid;
    }
    :host ::ng-deep .md-callout-note {
      background: #deebff;
      border-color: #0052cc;
      color: #0747a6;
    }
    :host ::ng-deep .md-callout-tip {
      background: #e3fcef;
      border-color: #00875a;
      color: #006644;
    }
    :host ::ng-deep .md-callout-warning {
      background: #fff0b3;
      border-color: #ff991f;
      color: #172b4d;
    }
    :host ::ng-deep .md-callout-important {
      background: #ffebe6;
      border-color: #de350b;
      color: #bf2600;
    }
    :host ::ng-deep .callout-icon {
      font-size: 16px;
      flex-shrink: 0;
      margin-top: 1px;
    }
    :host ::ng-deep .md-quote {
      border-left: 3px solid #dfe1e6;
      margin: 6px 0;
      padding-left: 10px;
      color: #5e6c84;
      font-style: italic;
    }
    :host ::ng-deep .md-task {
      display: flex;
      align-items: center;
      gap: 8px;
      margin: 3px 0;
    }
    :host ::ng-deep .md-task.checked .task-label {
      text-decoration: line-through;
      color: #6b778c;
    }
    :host ::ng-deep .md-li {
      margin: 2px 0 2px 18px;
    }
    :host ::ng-deep .md-li-num {
      margin: 2px 0 2px 18px;
      list-style-type: none;
    }
    :host ::ng-deep .md-table {
      width: 100%;
      border-collapse: collapse;
      margin: 8px 0;
      font-size: 12.5px;
    }
    :host ::ng-deep .md-table th {
      background: #f4f5f7;
      padding: 6px 10px;
      border: 1px solid #dfe1e6;
      font-weight: 600;
      text-align: left;
    }
    :host ::ng-deep .md-table td {
      padding: 6px 10px;
      border: 1px solid #dfe1e6;
    }
    :host ::ng-deep .md-img-card {
      margin: 8px 0;
      max-width: 100%;
    }
    :host ::ng-deep .md-img-card img {
      max-width: 100%;
      border-radius: 4px;
      border: 1px solid #dfe1e6;
      box-shadow: 0 1px 3px rgba(0,0,0,0.1);
    }
    :host ::ng-deep .md-img-caption {
      font-size: 11px;
      color: #6b778c;
      margin-top: 2px;
    }
    :host ::ng-deep .mention-badge {
      display: inline-flex;
      align-items: center;
      background: #e9f2ff;
      color: #0052cc;
      padding: 1px 6px;
      border-radius: 12px;
      font-size: 12px;
      font-weight: 600;
      margin: 0 2px;
    }
    :host ::ng-deep .issue-mention-badge {
      display: inline-flex;
      align-items: center;
      background: #f4f5f7;
      color: #0052cc;
      border: 1px solid #dfe1e6;
      border-radius: 3px;
      padding: 1px 5px;
      font-size: 11.5px;
      font-weight: 600;
      text-decoration: none;
      margin: 0 2px;
    }
    :host ::ng-deep .issue-mention-badge:hover {
      background: #deebff;
      border-color: #0052cc;
    }
  `]
})
export class MarkdownViewerComponent {
  private readonly sanitizer = inject(DomSanitizer);

  @Input() content = '';

  readonly renderedHtml = computed<SafeHtml>(() => {
    return this.sanitizer.bypassSecurityTrustHtml(this.renderMarkdown(this.content));
  });

  private renderMarkdown(md: string): string {
    if (!md) return '';

    let html = md
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;');

    html = html.replace(/&lt;u&gt;/gi, '<u>').replace(/&lt;\/u&gt;/gi, '</u>');

    // Code blocks
    html = html.replace(/```([a-zA-Z0-9_-]*)\n([\s\S]*?)```/g, (_, lang, code) => {
      const badge = lang ? `<span class="code-badge">${lang}</span>` : '';
      return `<div class="md-code-block"><div class="md-code-head">${badge}<span>Code Snippet</span></div><pre><code>${code.trim()}</code></pre></div>`;
    });

    // Callout Panels
    html = html.replace(/^&gt; \[!(NOTE|INFO)\]\s*\n((?:&gt; .*(?:\n|$))+)/gim, (_, type, c) => {
      const cl = c.replace(/^&gt; ?/gm, '').trim();
      return `<div class="md-callout md-callout-note"><div class="callout-icon">ℹ</div><div class="callout-content"><strong>Note</strong><div>${cl}</div></div></div>`;
    });
    html = html.replace(/^&gt; \[!TIP\]\s*\n((?:&gt; .*(?:\n|$))+)/gim, (_, c) => {
      const cl = c.replace(/^&gt; ?/gm, '').trim();
      return `<div class="md-callout md-callout-tip"><div class="callout-icon">💡</div><div class="callout-content"><strong>Tip</strong><div>${cl}</div></div></div>`;
    });
    html = html.replace(/^&gt; \[!(WARNING|CAUTION)\]\s*\n((?:&gt; .*(?:\n|$))+)/gim, (_, type, c) => {
      const cl = c.replace(/^&gt; ?/gm, '').trim();
      return `<div class="md-callout md-callout-warning"><div class="callout-icon">⚠</div><div class="callout-content"><strong>Warning</strong><div>${cl}</div></div></div>`;
    });
    html = html.replace(/^&gt; \[!(IMPORTANT|ERROR)\]\s*\n((?:&gt; .*(?:\n|$))+)/gim, (_, type, c) => {
      const cl = c.replace(/^&gt; ?/gm, '').trim();
      return `<div class="md-callout md-callout-important"><div class="callout-icon">❗</div><div class="callout-content"><strong>Important</strong><div>${cl}</div></div></div>`;
    });

    // Blockquotes
    html = html.replace(/^&gt; (.*$)/gim, '<blockquote class="md-quote">$1</blockquote>');

    // Headings
    html = html.replace(/^### (.*$)/gim, '<h3 class="md-h3">$1</h3>');
    html = html.replace(/^## (.*$)/gim, '<h2 class="md-h2">$1</h2>');
    html = html.replace(/^# (.*$)/gim, '<h1 class="md-h1">$1</h1>');

    // Task Checklists
    html = html.replace(/^- \[x\] (.*$)/gim, '<div class="md-task checked"><input type="checkbox" checked disabled /> <span class="task-label">$1</span></div>');
    html = html.replace(/^- \[ \] (.*$)/gim, '<div class="md-task"><input type="checkbox" disabled /> <span class="task-label">$1</span></div>');

    // Bullet & Number Lists
    html = html.replace(/^- (.*$)/gim, '<li class="md-li">$1</li>');
    html = html.replace(/^(\d+)\. (.*$)/gim, '<li class="md-li-num"><strong>$1.</strong> $2</li>');

    // Tables
    html = html.replace(/((?:\|.+?\|\s*(?:\r?\n|$))+)/g, (match) => {
      const rows = match.trim().split('\n');
      if (rows.length < 2) return match;
      let out = '<div class="table-responsive"><table class="md-table">';
      rows.forEach((row, idx) => {
        const cells = row.split('|').filter((_, cIdx, arr) => cIdx > 0 && cIdx < arr.length - 1);
        if (cells.length === 0) return;
        if (idx === 1 && cells.some((c) => /^\s*:-+:?\s*$/.test(c) || /^\s*-+\s*$/.test(c))) return;
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

    // Images
    html = html.replace(/!\[(.*?)\]\((.*?)\)/g, '<div class="md-img-card"><img src="$2" alt="$1" loading="lazy" /><div class="md-img-caption">$1</div></div>');

    // Links
    html = html.replace(/\[(.*?)\]\((.*?)\)/g, '<a href="$2" target="_blank" rel="noopener" class="md-link">$1 ↗</a>');

    // Inlines
    html = html.replace(/\*\*(.*?)\*\*/g, '<strong>$1</strong>');
    html = html.replace(/\*(.*?)\*/g, '<em>$1</em>');
    html = html.replace(/~~(.*?)~~/g, '<del>$1</del>');
    html = html.replace(/`([^`]+)`/g, '<code class="inline-code">$1</code>');

    // Mentions
    html = html.replace(/(?:^|\s)@([a-zA-Z0-9_.-]+)/g, ' <span class="mention-badge">@$1</span>');

    // Issue key badges
    html = html.replace(/\b([A-Z][A-Z0-9]+-[0-9]+)\b/g, '<a href="/board?selected=$1" class="issue-mention-badge" title="View $1">🏷 $1</a>');

    html = html.replace(/\n\n/g, '<p></p>');
    html = html.replace(/\n/g, '<br />');

    return html;
  }
}
