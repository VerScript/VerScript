# PR Review

## Summary of Updates
The latest pull request brings several updates aimed at improving the interpreter's error handling and mathematical operations:
- Added support for unary minus on the right-hand side (RHS) of expressions.
- Implemented checks to prevent strings and booleans from being modified by unary minus on the RHS.
- Added multiple `exit(1)` sequences to strictly enforce error handling when expected tokens are missing on the LHS or an invalid operation occurs.
- Introduced a variety of new tests covering these new edge cases.

## Possible Vulnerabilities
While the strict error handling is a proactive approach, introducing `exit(1)` mid-execution is a critical anti-pattern for our environment.
- **Resource Leaks**: `exit(1)` forcefully terminates the program without allowing the runtime to unwind and execute its memory-freeing routines (`freeToken`, variable cleanup loops). Any memory dynamically allocated up to the point of the error will be permanently leaked to the OS.
- **Security Context**: In embedded or server contexts where VerScript might run persistently, a hard exit can be exploited as a simple Denial-of-Service (DoS) vector by an attacker feeding intentionally malformed scripts.

## Updates Needed
- **Propagate Errors, Don't Exit**: Replace the `exit(1)` calls with proper error propagation. The parser/evaluator should return a specific error code or state that allows the main loop to handle the failure gracefully.
- **Ensure Cleanup**: The interpreter must always reach the cleanup phase (`free(buffer)`, `freeToken` loops, symbol table cleanup) regardless of syntax or runtime errors encountered during execution.

## Contradictions
- The introduction of `exit(1)` directly contradicts the work done in earlier pull requests (e.g., `#1` patching memory leaks and segfaults). We have invested effort into meticulously using `freeToken` and dynamically allocating variables safely. Hard-exiting bypasses all of this hard work, regressing the robustness of our memory management.

— The Background Observer