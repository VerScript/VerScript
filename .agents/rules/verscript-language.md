# VerScript Language Specification & AI Agent Guidelines

VerScript is an ultra-fast, minimalist scripting language engineered to blend the deterministic execution and speed of native C with modern dynamic programming ergonomics.

## File Extensions
- `.vrs`: VerScript script / source code files.
- `.lib.vrs`: VerScript library modules (auto-loaded or imported).

## Language Primitives & Syntax Reference

### 1. Variables & Lifecycles
- Standard variable: `variable: expression` or `set variable: expression`
- Temporary variable: `temp variable: expression` (freed with `del variable`)
- Stack-isolated variable: `inst variable: expression` (isolated to current call stack)
- Outscope mutation: `outscope variable: expression` (permits modifying outer variable)

### 2. Output & Formatting
- Output: `display "message"` or `display expr ?color="green"`
- Colors: named strings (`"green"`, `"cyan"`, `"red"`, `"yellow"`, `"purple"`, `"blue"`) or hex (`#22c55e`, `#38bdf8`)
- Attributes: `?key=value` (e.g. `?color=tint`, `?msg="Error message"`)

### 3. Native Math & Logic Gates
- Arithmetic: `+`, `-`, `*`, `/`, `//` (integer division), `^` (power), `|val|` (absolute value)
- Native math functions: `sqrt`, `rt`, `min`, `max`, `clamp val from low to high`, `floor`, `ceil`, `round`, `sin`, `cos`, `tan`
- Native pseudo functions: `between min, max`, `choice list`, `shuffle list`, `uuid()`, `name()`, `email()`, `alphaNumeric len`
- Boolean gates:
  - `&`: Normal AND (true only if both are true)
  - `and`: Negated AND / NAND (true unless both are true)
  - `or`: Normal OR
  - `nor`: Negated OR
  - `xor`: Exclusive OR
  - `xnor`, `xand`, `x&`: Equivalence
  - `not`: Logical inversion (`not false` -> `true`)

### 4. Control Flow
- Single line guard clause: `guard condition else statement` (e.g. `guard n > 0 else reply -1`)
- Pattern match:
  ```verscript
  match val
    with 1:
      display "one"
    with _:
      display "wildcard"
  ```
- Pipeline operator: `value |> func1 |> func2`
- Loops: `loop count`, `repeat count`, `while cond`, `until cond`, `cycle count`, `for start to end by step`
- Try-Unless Watchdog:
  ```verscript
  temperature: 80
  do
    display "Checking..."
    temperature: temperature + 40
  unless internal temperature > 100
    display "Watchdog alert!" ?color="yellow"
  ```
- Error catching:
  ```verscript
  do
    bad_val: 100 / 0
  unless DivisionByZeroError
    display "Handled cleanly"
  ```

### 5. Routines, Entities & Classes
- Function (pure): `def func name param` -> `reply value`
- Outbound Method: `def outbound method name(param)`
- Class definition:
  ```verscript
  class BankAccount(owner, initialBalance)
    static:
      public currency: "USD"
      public minDeposit: 10
    dynamic:
      set balance: initialBalance
      def outbound method deposit(amount)
        if amount >= BankAccount.minDeposit then
          balance: balance + amount
  ```
- Class instantiation: `set acct: BankAccount("Alice", 500)`
- Static property access: `BankAccount.minDeposit`

### 6. Reactive Events & Channels
- Reactive events: `on event_name:`, `when condition:`, `emit "event_name", payload`
- High-throughput FIFO channels: `channel queue_name`, `send queue_name, item`, `set item: recv queue_name`

### 7. Comments
- Canonical single-line comment: `! comment text`
- Canonical multiline comment: `!! multiline comment !!`
- Secondary backward-compatible comments: `// ...` and `# ...`

### 8. Interactive Shell (REPL)
- Running `verscript` without arguments opens the interactive REPL shell.
- Features: auto-expression evaluation, persistent variable/routine state, error recovery with `setjmp`/`longjmp`, built-in commands (`vars`, `help`, `clear`, `exit`).

### 9. Windows Binary Synchronization Guardrail
- When building `verscript.exe` on Windows, synchronize to:
  1. `C:\Users\alekhyo\mingw64\bin\verscript.exe` (MinGW bin directory)
  2. `%LOCALAPPDATA%\VerScript\verscript.exe` (Local install)
  3. `VerScript/bin/verscript.exe`
  4. `verscript-studio-desktop/resources/bin/verscript.exe`

