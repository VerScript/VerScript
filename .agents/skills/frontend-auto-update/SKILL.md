---
name: frontend-auto-update
description: "Automatically synchronizes and updates the VerScript documentation Academy (docs/articles_data.js, docs/docs_app.js, docs/index.html) and homepage interactive playground (index.html) whenever new VerScript language features, syntax, keywords, or error codes are added to the compiler/VM."
---

# VerScript Frontend Auto-Update Skill

Use this skill whenever new VerScript features, syntax, keywords, purity contracts, built-ins, standard libraries, or error mechanisms are designed or implemented in the compiler/VM repository (`VerScript`), ensuring the public documentation website (`VerScript.github.io`) is immediately and accurately synchronized.

---

## Workflow Overview

Whenever a feature is added or modified in the core VerScript engine:
1. **Analyze Language Delta**: Identify changes in lexer tokens, AST parser grammar, runtime bytecode, and error handlers.
2. **Update Academy Chapters (`docs/articles_data.js`)**:
   - Locate the appropriate chapter based on the 8-section pedagogical curriculum.
   - If introducing an entirely new major subsystem, add a new chapter and update total chapter counts across the app.
   - Provide comprehensive conceptual explanations, formal syntax rules, live runnable sandbox code (`codeBlocks`), and real-time validated practice exercises (`exercises`).
3. **Update Error Directory & Criticality (Chapter 16)**:
   - If new errors, exceptions, or directives (`SuppressErrors`, `CriticalErrors`, `ForceErrors`) were added, update the 10-point criticality matrix and suppression tiers in Chapter 16.
4. **Update Homepage Interactive Playground (`index.html`)**:
   - Update `sample.vrs` in the interactive code window to showcase the new feature.
   - If relevant, update or add a dedicated snippet tab (e.g., `functions.vrs`, `watchdog.vrs`).
   - Update the precomputed terminal drawer output to match native C VM output.
   - Update feature highlights and statistics counters if applicable.
5. **Verify Ergonomics & Interactivity**:
   - Ensure PC desktop keyboard navigation (`Ctrl+K`, `ArrowLeft`/`ArrowRight`, `Tab` indentation, `Ctrl+Enter`) works seamlessly.
   - Ensure mobile thumb-first layout (44px+ touch targets, floating bottom nav bar, swipeable tabs, toast alerts) remains functional.
   - Validate JS syntax with `node -c docs/articles_data.js docs/docs_app.js`.
6. **Deploy & Synchronize**:
   - Commit and push changes to `https://github.com/VerScript/VerScript.github.io.git`.
