# AGENTS.md — VerScript Compiler, Runtime & Developer Ecosystem

Instructions and context for autonomous AI coding agents (such as Google Antigravity & Jules).

## 🚀 Dev Environment & Commands
- **Compiler**: Uses GCC C99 compiler.
- **Build Interpreter**: Run `gcc -O2 -Iinclude src/main.c src/compiler/lexer.c -o verscript.exe` (or `make`).
- **Run Tests**: Execute `Get-ChildItem tests\*.vrs | ForEach-Object { .\verscript.exe $_.FullName }` to validate the full 53-suite test matrix.
- **Run Sync Toolchain**: Execute `node tooling/scripts/sync_meta.js` to propagate new keywords, tokens, error names, and libraries to TextMate grammar, language config, snippets, and Antigravity extensions.
- **Run Official Installer**: Run `powershell -ExecutionPolicy Bypass -File install.ps1` to install `verscript.exe` to User PATH, register Windows file associations (`.vrs`, `.lib.vrs`) with logo icons, and install the Antigravity IDE extension.

## 🏗️ Language Architecture
- **Lexer**: `src/compiler/lexer.c` tokenizes source code, strips comments (`!`, `!!`), computes indentation depth, and processes command attributes (`?key=val`).
- **VM Engine**: `src/main.c` manages symbol tables, call stacks, expressions, do-unless watchdogs, classes, reactive events, and FIFO channels.
- **Standard Library**: `stdlib/*.lib.vrs` (20 auto-loaded enterprise libraries: `Strings`, `Math`, `Arrays`, `Json`, etc.).
- **Developer Tooling**: `tooling/` contains TextMate grammar, VS Code / Antigravity IDE extension, DAP debugger, LSP server, and native Windows installer.

## 🛡️ Coding Guidelines & Rules
- **Memory Management**: Track dynamic allocations with `track_alloc` and `untrack_alloc`. Free string/entity memory cleanly.
- **Purity Contracts**: Inbound functions cannot mutate outer scope without `outscope`.
- **Ecosystem Sync**: When adding keywords, update `include/lexer.h`, `src/compiler/lexer.c`, and run `node tooling/scripts/sync_meta.js`.
