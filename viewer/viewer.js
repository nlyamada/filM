// filM の Markdown ビューアー。ホスト (C++) が render(markdown, options) を呼ぶ。
// 数式は marked に壊されないよう、先にプレースホルダーに置き換えてから KaTeX で描く。

function extractCodeAndMath(md) {
  const store = [];
  const keep = (s) => { store.push(s); return '\u0000' + (store.length - 1) + '\u0000'; };
  const math = [];
  const mk = (tex, display) => { math.push({ tex: tex, display: display }); return 'MATHPH' + (math.length - 1) + 'PH'; };

  md = md.replace(/(^|\n)(```|~~~)[^\n]*\n[\s\S]*?\n\2[^\n]*(?=\n|$)/g, (m) => keep(m));
  md = md.replace(/`[^`\n]+`/g, (m) => keep(m));
  md = md.replace(/\$\$([\s\S]+?)\$\$/g, (m, t) => mk(t, true));
  md = md.replace(/\\\[([\s\S]+?)\\\]/g, (m, t) => mk(t, true));
  md = md.replace(/\\\((.+?)\\\)/g, (m, t) => mk(t, false));
  md = md.replace(/(^|[^\\$])\$(?!\s)([^$\n]+?)(?<!\s)\$(?!\d)/g, (m, p, t) => p + mk(t, false));
  md = md.replace(/\u0000(\d+)\u0000/g, (m, i) => store[Number(i)]);
  return { md: md, math: math };
}

function restoreMath(html, math) {
  return html.replace(/MATHPH(\d+)PH/g, (m, i) => {
    const item = math[Number(i)];
    try {
      return katex.renderToString(item.tex, { displayMode: item.display, throwOnError: false, output: 'html' });
    } catch (e) {
      return '<span class="math-error">' + String(item.tex).replace(/</g, '&lt;') + '</span>';
    }
  });
}

// 文書に書かれた HTML から、スクリプト・フォーム・埋め込み・外部への接続を起こす要素や属性を取り除く。
// (CSP でも同じものを防いでいる。念のため二重に守る。KaTeX が出す span / svg はそのまま残る。)
function sanitizeHtml(html) {
  const tpl = document.createElement('template');
  tpl.innerHTML = html;  // template の中は不活性 (画像の読み込みやスクリプトの実行は起きない)
  tpl.content.querySelectorAll('script,iframe,frame,frameset,object,embed,applet,form,button,select,textarea,link,meta,base,style,noscript,portal,dialog').forEach((n) => n.remove());
  tpl.content.querySelectorAll('input').forEach((n) => { if ((n.getAttribute('type') || '').toLowerCase() !== 'checkbox') n.remove(); });
  tpl.content.querySelectorAll('*').forEach((el) => {
    Array.from(el.attributes).forEach((attr) => {
      const name = attr.name.toLowerCase();
      const value = attr.value.replace(/[\u0000- ]/g, '').toLowerCase();
      if (name.startsWith('on') || name === 'srcdoc' || name === 'formaction' || name === 'ping' || name === 'background') {
        el.removeAttribute(attr.name);
      } else if (['href', 'src', 'xlink:href', 'action', 'data'].includes(name) && /^(javascript:|vbscript:|data:text\/html|data:application)/.test(value)) {
        el.removeAttribute(attr.name);
      } else if (name === 'srcset' && /https?:/.test(value)) {
        el.removeAttribute(attr.name);
      }
    });
  });
  return tpl.innerHTML;
}

async function render(markdown, options) {
  options = options || {};
  document.documentElement.setAttribute('data-theme', options.dark ? 'dark' : 'light');
  if (options.base) {
    let base = document.querySelector('base');
    if (!base) { base = document.createElement('base'); document.head.appendChild(base); }
    base.href = options.base;
  }
  const parsed = extractCodeAndMath(markdown);
  marked.setOptions({ gfm: true, breaks: false });
  const el = document.getElementById('content');
  el.innerHTML = sanitizeHtml(restoreMath(marked.parse(parsed.md), parsed.math));

  // 見出しに GitHub 風の id を付ける (#見出し のリンク用)。
  const used = {};
  el.querySelectorAll('h1,h2,h3,h4,h5,h6').forEach((h) => {
    let slug = h.textContent.trim().toLowerCase().replace(/[^\p{L}\p{N}\s_-]/gu, '').replace(/\s/g, '-');
    if (!slug) return;
    if (used[slug] !== undefined) { used[slug] += 1; slug += '-' + used[slug]; } else { used[slug] = 0; }
    h.id = slug;
  });
  // ```mermaid のブロックを図にする。
  const blocks = el.querySelectorAll('pre > code.language-mermaid');
  blocks.forEach((code) => {
    const div = document.createElement('div');
    div.className = 'mermaid';
    div.textContent = code.textContent;
    code.parentElement.replaceWith(div);
  });
  if (blocks.length > 0) {
    try {
      mermaid.initialize({ startOnLoad: false, theme: options.dark ? 'dark' : 'default', securityLevel: 'strict' });
      await mermaid.run({ querySelector: '.mermaid' });
    } catch (e) {
      console.error(e);
    }
  }
  if (options.title) document.title = options.title;
  if (options.hash) {
    const target = document.getElementById(decodeURIComponent(options.hash));
    if (target) target.scrollIntoView();
  }
  return 'ok';
}
