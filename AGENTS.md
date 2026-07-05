# AGENTS.md — VerScript C Interpreter

Instructions and context for autonomous AI coding agents (such as Google Jules).

## 🚀 Dev Environment & Commands
- **Compiler**: Uses GCC compiler.
- **Build Interpreter**: Run `make` (produces `verscript` executable or `verscript.exe` on Windows).
- **Clean Object Files**: Run `make clean`.

## 🏗️ Language Grammar & Rules
VerScript is a custom, lightweight, C-based scripting language. Its key specifications are:
- **Variable Assignment**: Spaced colon syntax: `variable_name : expression` (e.g. `x : 42` or `name : "World"`).
- **Output Statements**: `display "message"` or `display variable`.
- **Input Statements**: `prompt variable` (stores keyboard input to variables).
- **Comments**: Any text on a line following `!` is ignored.
- **Arithmetic**: Supports simple operations (+, -, *, /).

## 🛡️ Coding Guidelines & Rules
- **Lexer & Tokens**: Tokenizer logic lives in `src/compiler/lexer.c`. If an invalid character is encountered, return `TOKEN_ERROR` with a 2-byte allocated `value` containing that single character.
- **Memory Management**: Free any dynamically allocated identifiers/string memory correctly. Avoid memory leaks inside the token loop or expression evaluation.
- **Rebuilds**: Always clean previous compilation artifacts (`make clean`) before rebuilding to ensure changes in headers (`include/lexer.h`) propagate correctly across all object files.
