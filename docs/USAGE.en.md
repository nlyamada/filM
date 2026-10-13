# filM usage (English)  /  [日本語](USAGE.md)

The **key list** on the right shows the current key bindings. **While you hold Shift it switches to the Shift+key bindings.**
Clicking a row runs that command (`F1` shows / hides the list). Key bindings can be changed in the **Settings dialog (`Ctrl+,` or the ⚙ at the right end of any bar → Settings)**; you can also edit `keymap.json` directly.

## Basics
- `Up` / `Down` move, `Enter` opens a folder / file, `Backspace` goes to the parent folder, `Shift+Backspace` to the drive root.
- Single-key commands work **while the file list has focus**. `Esc` always returns to the file list.
- IME is disabled in the list (for single-key commands); it works in the rename box.

## Single-key commands (plain / Shift)
| Key | Plain | Shift |
|---|---|---|
| A | Attributes | |
| B | Duplicate | |
| C | Copy to other tab | Copy to folder... |
| D | Move to Recycle Bin | Delete permanently (asks first) |
| E | Edit in the text editor (not for binary files) | Edit aliases file |
| F | Copy full path | |
| G | Go to folder... | |
| H | Mark all / clear | |
| I | Properties | |
| J | Focus alias list | Register as alias |
| K | New folder | |
| L | Switch tab set (menu) | Add tab set |
| M | Move to other tab | Move to folder... |
| N | Rename | |
| O | Open new tab at folder... | |
| P | Zip | |
| Q | Close tab | |
| R | Refresh | |
| S | Create shortcut | |
| T | New tab in current folder | |
| U | Unzip (extracts into the current folder; no folder named after the zip) | |
| Shift+U | Unzip into a new folder named after the zip | |
| V | Show / hide the viewer | Enlarge / restore the viewer |
| X | Open PowerShell | Open Command Prompt |
| Z | Undo | |
| Del | Move to Recycle Bin | Delete permanently (asks first) |
| `,` / `.` | Previous / next tab | Move tab left / right |
| Space | Mark and move down | |
| Enter | Open / run | Open with... |
| Tab | Move focus to the viewer (inside the viewer: back to the list) | |
| Esc | Focus file list (inside the viewer: close the viewer) | |

Also: `F2` rename, `F5` / `F6` copy / move to the other tab, `Ctrl+C/X/V` clipboard (works with Explorer), `Ctrl+Shift+C` copy path,
`Ctrl+1..4` sort (name, type, size, date), `Ctrl+5` view mode, `Ctrl+A` mark all, `Ctrl+Shift+A` clear marks, `Ctrl+T/W` tabs,
`Ctrl+Tab` switch tab, `Ctrl+D` register alias, `Ctrl+B` show / hide the alias pane, `Ctrl+Arrow` list <-> alias pane, `Ctrl+,` settings.

## Marks (multi-select)
`Space` marks an item (a check badge on the icon and a tinted row). Copy, move, delete, zip and so on act on **all marked items, or on the item under the cursor if nothing is marked**.

## Aliases (folder shortcuts)
- `Shift+J` (or `Ctrl+D`) registers the current folder (name, call key, folder). Aliases appear in the left pane, sorted by name.
- In folder prompts (`G`, `O`, `Shift+C`, `Shift+M`) press **TAB to complete folder and alias names**, e.g. with alias `HOME` = `C:\home`, type `HOME\l` and press TAB to get `HOME\lib\`.
- Call keys are shown only **while Shift is held**. With focus in the alias pane (`J`): `Enter` goes there, `Shift+Enter` or **Shift+call key** opens it in a new tab and returns focus to the file list, `F2` edits, `Delete` deletes. A plain letter jumps to the item starting with it.
- `Shift+E` opens `favorites.json` in your editor; changes are loaded when you return to the window.

## Undo (Z)
Undoes **copy, duplicate, move, rename, new folder, zip, unzip, create shortcut, delete to Recycle Bin and attribute changes**, newest first (up to 50; cleared when the app closes). **Permanent deletion cannot be undone.**

## Viewer (preview pane) (V)
`V` shows / hides a viewer **below the file list**. While it is shown, it follows the cursor: images, Markdown and text files are displayed there (view only - no editing; a binary file shows a note). Drag the border to resize it (the height is saved).
- **Scrolling**: the mouse wheel scrolls the viewer when the mouse is over it (even if the list has focus). `Tab` moves focus into it, so arrow keys / PageUp / PageDown / `Home` / `End` scroll it. Inside the viewer, `Tab` returns to the list (the viewer stays open) and **`Esc` closes the viewer and returns to the list**. `Shift+V` enlarges it over the list (and back). The `x` at the right of its header closes it too.
- **Images**: `Left` / `Right` (PageUp, PageDown, Space, Backspace) previous / next image, `+` `-` or the wheel zoom, `0` fit, `1` 100%, drag to pan, double-click toggles fit and 100%. EXIF orientation is applied. JPEG, PNG, BMP, GIF and TIFF always work; HEIC, WebP and others work if Windows has the codec. Huge images are shrunk when loaded.
- **Markdown**: headings, tables, code blocks, task lists, images, `$...$` / `$$...$$` math (KaTeX) and ` ```mermaid ` diagrams. Images work with `![](img/a.png)` and also with paths that go up, like `../images/a.png`, as long as they are on the same drive. `F5` reloads. Links to other `.md` files open in the viewer, `http(s)` links in your default browser. Other local files open only when you click and, for types other than images / text / PDF / audio / video, after a confirmation; programs and scripts never open from a link. **For safety, scripts in the document do not run, and images from the Internet are not loaded by default** (turn on "Show images from the Internet in Markdown" in Settings to show them; each image then connects to its server). It needs the Microsoft Edge WebView2 Runtime (included in Windows 11) plus `WebView2Loader.dll` and the `viewer\` folder next to the exe; without them the Markdown source is shown as text.
- **Text**: shown with its encoding (UTF-8 / UTF-8 with BOM / UTF-16 / ANSI) and line ending detected, up to 10 MB. `Alt+Z` toggles word wrap. To edit, press `E`: it opens the text editor chosen in Settings (the Windows default, Notepad or any program).
## Tab sets (like virtual desktops)
- `L` opens a menu (switch, rename, delete); `Shift+L` adds a new set (starting with one tab at the current folder). The ⚙ at the right end of the tab bar offers the same.
- Tabs and sets are saved to `session.json` whenever they change and restored at the next start.

## Adjusting the window
- Drag the borders between panes. Pane widths and the window size are saved to `session.json`.
- ⚙ at the right end of each bar: aliases (register / edit / delete), tabs (tabs and sets), keys (Settings, edit `keymap.json`, reset).

## Settings (Ctrl+,)
- **Language**: click `JA | EN` at the bottom right, or use the Settings dialog. The first run follows the OS language.
- **Theme**: follow system / light / dark.
- **Text editor**: the Windows default / Notepad / any program (by path). Used by `E` and when editing config files.
- **Key bindings**: select a command, then "Change..." (press the key you want), "Add..." (add another key to the same command), "Remove" or "Reset to defaults...". If the key is already used you are asked whether to replace it.

## Where the config files are
`keymap.json`, `favorites.json`, `session.json` and `settings.json` are created **next to the exe** (only if that folder is not writable, `%APPDATA%\filM` is used). Settings from the old version (`%APPDATA%\SpringMClone`) are imported once.
File type names such as "TXT file" come from Windows and follow the OS language.
