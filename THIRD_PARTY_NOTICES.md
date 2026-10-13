# Third-party components

filM bundles the following components. Their license texts are included next to the files.

| Component | Version | License | Location |
|---|---|---|---|
| Microsoft Edge WebView2 SDK (header, `WebView2Loader.dll`) | 1.0.3485.44 | BSD-3-Clause style (see file) | `third_party/webview2/` (`LICENSE.txt`, `NOTICE.txt`) |
| marked | 12.0.2 | MIT | `viewer/lib/marked.min.js`, `viewer/lib/LICENSE-marked.txt` |
| KaTeX (JS, CSS, fonts) | 0.16.11 | MIT | `viewer/lib/katex.min.js`, `katex.min.css`, `auto-render.min.js`, `fonts/`, `LICENSE-katex.txt` |
| mermaid | 10.9.1 | MIT | `viewer/lib/mermaid.min.js`, `viewer/lib/LICENSE-mermaid.txt` |

The Markdown viewer uses the Microsoft Edge WebView2 Runtime installed on the user's PC; the runtime is not bundled.
