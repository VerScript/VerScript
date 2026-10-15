---
name: VerScript-update-meta
description: >-
  Maintains and synchronizes VerScript developer tooling, TextMate grammar,
  VS Code and Google Antigravity extensions, language configuration, code snippets,
  Windows registry associations, and installer when new language features, keywords,
  operators, or standard libraries are added to the compiler.
---

# VerScript Toolchain & Metadata Synchronization Skill

This skill documents and automates the synchronization between the VerScript native compiler engine (`src/main.c`, `src/compiler/lexer.c`, `include/lexer.h`, `stdlib/`) and the language's external ecosystem, editors, grammars, and installer.

## When to Run This Skill
Execute this workflow whenever:
1. A new keyword, modifier, or statement is introduced to `include/lexer.h` or `src/compiler/lexer.c`.
2. A new standard library module (`stdlib/<Module>.lib.vrs`) is added to the standard library ecosystem.
3. A new error type or exception is registered in `src/main.c` (`is_error_name`).
4. New native math functions, pseudo-random generators, or string/array pseudo-methods are added.
5. The language installer (`install.ps1`) or editor extensions need to be updated.

---

## 🛠️ Step-by-Step Synchronization Procedure

### Step 1: Run the Automated Sync Script
The repository includes a dedicated sync runner:
```bash
node tooling/scripts/sync_meta.js
```
This script audits:
- `include/lexer.h` and `src/compiler/lexer.c` for tokens.
- `src/main.c` for registered error names (`is_error_name`).
- `stdlib/*.lib.vrs` for newly introduced libraries.

It updates:
- `tooling/syntax/verscript.tmLanguage.json`
- `tooling/vscode/syntaxes/verscript.tmLanguage.json`
- `C:\Users\<user>\.antigravity-ide\extensions\verscript-lang-0.1.0`
- `C:\Users\<user>\.vscode\extensions\verscript-lang-0.1.0` (if installed)

### Step 2: Update Language Configuration & Snippets (If Needed)
If the feature introduces a new block structure (e.g. indentation-scoped blocks):
1. Open `tooling/vscode/language-configuration.json`.
2. Ensure `increaseIndentPattern` matches the new block opening keyword:
   ```json
   "increaseIndentPattern": "^\\s*(def|class|lib|DataType|alias|static:|dynamic:|meta:|thisstatic:|do|unless|if|match|with|loop|repeat|while|until|cycle|for|<NEW_KEYWORD>).*$"
   ```
3. If relevant, add an ergonomic snippet to `tooling/vscode/snippets/verscript.json`.

### Step 3: Rebuild the Native Binary & Re-run Installer
Rebuild `verscript.exe` with GCC and run `install.ps1`:
```powershell
gcc -O2 -Iinclude src/main.c src/compiler/lexer.c -o verscript.exe
.\install.ps1
```
This:
- Replaces the binary in `%LOCALAPPDATA%\VerScript\verscript.exe`.
- Updates `%LOCALAPPDATA%\VerScript\stdlib\`.
- Verifies `.vrs` and `.lib.vrs` file associations and icon links in the Windows Registry.
- Re-syncs the extension in `.antigravity-ide/extensions/verscript-lang-0.1.0`.

### Step 4: Synchronize Documentation Academy (If Needed)
If the new language feature affects articles or the interactive sandbox:
- Use the `frontend-auto-update` skill to update `docs/articles_data.js`.
- Run the batch validator to ensure all documentation blocks and test exercises pass:
  ```bash
  node brain/.../scratch/test_runner.js
  ```

---

## 📁 Key File Locations & Artifacts

File / Directory | Purpose
:--- | :---
`tooling/syntax/verscript.tmLanguage.json` | Master TextMate syntax grammar.
`tooling/vscode/package.json` | Extension manifest specifying language ID, extensions (`.vrs`, `.lib.vrs`), grammars, and debuggers.
`tooling/vscode/language-configuration.json` | Indentation rules, bracket pairs, comment styles (`!`, `!!`).
`tooling/debugger/dap_server.js` | Debug Adapter Protocol (DAP) primitive server.
`tooling/lsp/lsp_server.js` | Language Server Protocol (LSP) primitive server.
`tooling/installer/install.ps1` | Native Windows installer (copies binary, updates PATH, sets registry, installs IDE extension).
`tooling/installer/register_filetypes.ps1` | Windows Registry association script for `.vrs` and `.lib.vrs`.
`C:\Users\alekhyo\.antigravity-ide\extensions\` | Installed location for Google Antigravity IDE language support.
