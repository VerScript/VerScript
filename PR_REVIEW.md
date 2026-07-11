# PR Review

## Summary of Updates
The latest pull request introduces fixes for boolean arithmetic, unary minus support, and error handling for missing/undefined variables in both RHS and LHS of expressions.
- Prevented unary minus from being applied to booleans and strings, stopping invalid operations.
- Checked for undefined variables on the right-hand side of expressions and aborted execution, ensuring consistent behavior with left-hand side checks.
- Forced boolean arithmetic operations (+, -, *, /) to result in `VAR_INT` instead of keeping the type as `VAR_BOOL`.
- Added multiple `exit(1)` statements explicitly when expected tokens are missing or invalid operations occur (e.g., division by zero, invalid unary minus on booleans, and undefined variables) rather than silently propagating `0`.

## Possible Vulnerabilities
While the intentions behind strict error handling are correct, adopting `exit(1)` introduces serious vulnerabilities to the interpreter's stability:
- **Resource Leaks**: `exit(1)` forcefully and immediately halts the process. Any dynamically allocated memory (like dynamically allocated strings, parsing buffers, or token structures) up to the point of execution is skipped for cleanup during termination, severely leaking memory to the OS.
- **Denial-of-Service (DoS) Risk**: If this interpreter runs continuously (like as a background engine, in long-running services, or embedded context), submitting a script with a single undefined variable or invalid unary operator will instantly crash the process. An attacker can exploit this for a trivial DoS.

## Updates Needed
- **Implement Graceful Error Propagation**: The `evaluate_expression` function, as well as the main parsing loop, should be updated to return a designated error code or utilize an error state object instead of resorting to `exit(1)`.
- **Ensure Deterministic Cleanup**: It is imperative that the main loop handles error codes by breaking the execution flow and jumping to the existing memory cleanup section (`free(buffer)`, `freeToken`, symbol table deallocation loops) regardless of runtime or syntax errors.

## Contradictions
- Relying on `exit(1)` directly negates the extensive work performed in earlier pull requests (such as PR `#1` and recent lexer refactoring), which specifically aimed at patching memory leaks by ensuring `freeToken` and variable freeing routines were systematically and properly executed. By aborting instantly, we bypass these hard-earned memory management safety nets, causing a regression in stability.

— The Background Observer
