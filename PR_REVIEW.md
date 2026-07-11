# PR Review

## Summary of Updates
The latest pull request introduces fixes for boolean arithmetic and unary minus support:
- Prevented unary minus from being applied to booleans and strings, stopping invalid operations.
- Checked for undefined variables on the right-hand side of expressions and correctly aborted execution, ensuring consistent behavior with left-hand side checks.
- Enforced that boolean arithmetic operations (+, -, *, /) result in `VAR_INT` rather than retaining the `VAR_BOOL` type.
- Exited the interpreter explicitly via `exit(1)` when undefined variables are accessed, rather than silently propagating `0`.

## Possible Vulnerabilities
While the intentions behind strict error handling are correct, adopting `exit(1)` introduces serious vulnerabilities to the interpreter's stability:
- **Resource Leaks**: `exit(1)` immediately halts the process. Any dynamically allocated memory (tokens, buffers, symbol tables) during parsing and evaluation is bypassed during termination, leaking memory to the OS.
- **Denial-of-Service (DoS) Risk**: If the interpreter is used in a long-running service, embedded context, or as a backend engine, passing a script with a single undefined variable or invalid unary operator allows an attacker to crash the process instantly.

## Updates Needed
- **Implement Graceful Error Propagation**: The `evaluate_expression` function (and the parsing loop) needs to return a designated error code or utilize an error state object instead of invoking `exit(1)`.
- **Ensure Deterministic Cleanup**: The main loop must reliably reach the cleanup section (`free(buffer)`, symbol table deallocation) regardless of runtime or syntax errors.

## Contradictions
- Using `exit(1)` negates the extensive work performed in earlier pull requests (e.g., `#1`), which specifically patched memory leaks by ensuring `freeToken` and variable memory freeing were systematically executed. Bypassing these routines is a regression in our memory management practices.

— The Background Observer
