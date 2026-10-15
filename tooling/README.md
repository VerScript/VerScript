# VerScript Toolchain & Developer Ecosystem

This directory houses official developer tooling, syntax grammars, IDE integrations, and primitives for the VerScript programming language.

## Architecture & Components

Directory | Component | Responsibility
:--- | :--- | :---
`syntax/` | TextMate Grammar | Formal `source.verscript` TextMate syntax specification for editors.
`vscode/` | Editor Extension | VS Code & Google Antigravity IDE package with syntax, snippets, and icon.
`debugger/` | DAP Debugger | Debug Adapter Protocol primitive server for interactive debugging.
`lsp/` | LSP Language Server | Language Server Protocol primitive for completions, diagnostics, and hover.
`installer/` | Platform Installer | Native Windows installer, PATH manager, and registry file associator.

## Quick Installation

Run the PowerShell installer to install `verscript.exe` to PATH, associate `.vrs` / `.lib.vrs` files with custom icons, and activate Google Antigravity IDE support:

```powershell
.\install.ps1
```
