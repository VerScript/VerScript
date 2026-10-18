#!/usr/bin/env node
// ═══════════════════════════════════════════════════════════════════
//  VerScript Toolchain & Metadata Synchronization Script
// ═══════════════════════════════════════════════════════════════════
const fs = require('fs');
const path = require('path');
const cp = require('child_process');

const repoRoot = path.resolve(__dirname, '../../');
const toolingDir = path.join(repoRoot, 'tooling');
const lexerH = path.join(repoRoot, 'include/lexer.h');
const lexerC = path.join(repoRoot, 'src/compiler/lexer.c');
const mainC = path.join(repoRoot, 'src/main.c');
const stdlibDir = path.join(repoRoot, 'stdlib');

console.log('Auditing VerScript compiler headers and sources...');

// 1. Audit stdlib modules
let stdlibModules = [];
if (fs.existsSync(stdlibDir)) {
    stdlibModules = fs.readdirSync(stdlibDir)
        .filter(f => f.endsWith('.lib.vrs'))
        .map(f => f.replace('.lib.vrs', ''));
}

// 2. Audit error names in main.c
let errorNames = [
    'MemoryAllocationError', 'UndefinedVariableError', 'InvalidOperandError',
    'DivisionByZeroError', 'IndentationError', 'LoopIterationError',
    'LoopLimitError', 'LoopDirectionError', 'LoopStepError',
    'ScopeViolationError', 'SyntaxError', 'RuntimeError',
    'InvalidErrorNameError', 'SystemError', 'VisibilityError',
    'ImmutableError', 'IndexOutOfBoundsError', 'EntityError',
    'FileError', 'DomainError', 'AssertError', 'DataTypeError',
    'UnassignedVariableError', 'AsyncError', 'ChannelError', 'MatchError'
];

if (fs.existsSync(mainC)) {
    const mainContent = fs.readFileSync(mainC, 'utf8');
    const errMatches = mainContent.matchAll(/strcmp\(name,\s*"([A-Za-z0-9]+Error)"\)/g);
    for (const m of errMatches) {
        if (!errorNames.includes(m[1])) errorNames.push(m[1]);
    }
}

console.log(`Discovered ${stdlibModules.length} stdlib modules and ${errorNames.length} error types.`);

// 3. Generate updated TextMate grammar
const grammarPath = path.join(toolingDir, 'syntax/verscript.tmLanguage.json');
const vsGrammarPath = path.join(toolingDir, 'vscode/syntaxes/verscript.tmLanguage.json');

const grammar = {
  $schema: "https://raw.githubusercontent.com/martinring/tmlanguage/master/tmlanguage.json",
  name: "VerScript",
  scopeName: "source.verscript",
  fileTypes: ["vrs", "lib.vrs"],
  patterns: [
    { include: "#comments" },
    { include: "#strings" },
    { include: "#attributes" },
    { include: "#numbers" },
    { include: "#booleans" },
    { include: "#keywords-declaration" },
    { include: "#keywords-control" },
    { include: "#keywords-concurrency" },
    { include: "#types" },
    { include: "#built-in-errors" },
    { include: "#stdlib-modules" },
    { include: "#native-math-and-pseudo" },
    { include: "#operators" },
    { include: "#function-calls" },
    { include: "#identifiers" }
  ],
  repository: {
    comments: {
      patterns: [
        { name: "comment.block.verscript", begin: "!!", end: "!!" },
        { name: "comment.line.exclamation.verscript", match: "!(?!=).*$" }
      ]
    },
    strings: {
      patterns: [
        {
          name: "string.quoted.double.verscript",
          begin: "\"",
          end: "\"",
          patterns: [{ name: "constant.character.escape.verscript", match: "\\\\." }]
        },
        {
          name: "string.quoted.single.verscript",
          begin: "'",
          end: "'",
          patterns: [{ name: "constant.character.escape.verscript", match: "\\\\." }]
        }
      ]
    },
    attributes: {
      patterns: [
        {
          match: "(\\?)([a-zA-Z_][a-zA-Z0-9_]*)(?:(=)(\"[^\"]*\"|'[^']*'|#[0-9a-fA-F]{3,8}|[a-zA-Z0-9_.-]+))?",
          captures: {
            "1": { name: "punctuation.definition.annotation.verscript" },
            "2": { name: "entity.other.attribute-name.verscript" },
            "3": { name: "punctuation.separator.key-value.verscript" },
            "4": { name: "string.unquoted.attribute-value.verscript" }
          }
        }
      ]
    },
    numbers: {
      patterns: [
        { name: "constant.numeric.hex.verscript", match: "\\b0x[0-9a-fA-F]+\\b" },
        { name: "constant.numeric.float.verscript", match: "\\b\\d+\\.\\d+\\b" },
        { name: "constant.numeric.integer.verscript", match: "\\b\\d+\\b" }
      ]
    },
    booleans: {
      patterns: [
        { name: "constant.language.boolean.verscript", match: "\\b(true|false|null|nil)\\b" }
      ]
    },
    "keywords-declaration": {
      patterns: [
        { name: "keyword.declaration.verscript", match: "\\b(def|func|method|class|lib|DataType|alias)\\b" },
        { name: "storage.modifier.section.verscript", match: "\\b(static|dynamic|meta|thisstatic|const)\\b" },
        { name: "storage.modifier.visibility.verscript", match: "\\b(public|private)\\b" },
        { name: "storage.modifier.lifecycle.verscript", match: "\\b(inbound|outbound|outscope|temp|inst)\\b" },
        { name: "keyword.control.directive.verscript", match: "\\b(import|from|as|load|inject|del)\\b" }
      ]
    },
    "keywords-control": {
      patterns: [
        { name: "keyword.control.conditional.verscript", match: "\\b(if|then|else|guard|match|with)\\b" },
        { name: "keyword.control.repeat.verscript", match: "\\b(loop|repeat|while|until|cycle|for|by|step)\\b" },
        { name: "keyword.control.watch.verscript", match: "\\b(do|unless|internal|external)\\b" },
        { name: "keyword.control.flow.verscript", match: "\\b(reply|throw)\\b" }
      ]
    },
    "keywords-concurrency": {
      patterns: [
        { name: "keyword.control.concurrency.verscript", match: "\\b(display|prompt|emit|on|when|async|sync|send|recv|channel)\\b" }
      ]
    },
    types: {
      patterns: [
        { name: "support.type.primitive.verscript", match: "\\b(num|bool|str|arr|entity)\\b" }
      ]
    },
    "built-in-errors": {
      patterns: [
        {
          name: "support.type.exception.verscript",
          match: "\\b(" + errorNames.join('|') + ")\\b"
        }
      ]
    },
    "stdlib-modules": {
      patterns: [
        {
          name: "support.class.library.verscript",
          match: "\\b(" + stdlibModules.join('|') + ")\\b"
        }
      ]
    },
    "native-math-and-pseudo": {
      patterns: [
        {
          name: "support.function.math.verscript",
          match: "\\b(sqrt|rt|min|max|clamp|from|to|sign|sgn|divRem|isEven|isOdd|gcd|hcf|lcm|floor|ceil|round|sin|cos|tan|hypot|degToRad|radToDeg|sinDeg|cosDeg|ln|log|log2|exp|lerp)\\b"
        },
        {
          name: "support.function.pseudo.verscript",
          match: "\\b(between|choice|shuffle|sample|uuid|name|email|alphaNumeric|sum|mean|minMax|range)\\b"
        },
        {
          name: "support.function.method.verscript",
          match: "\\b(length|len|isEmpty|indexOf|lastIndexOf|contains|startsWith|endsWith|slice|reverse|join|trim|toUpper|toLower|split|repeat|padLeft|padRight|push|pop|shift|unshift|count)\\b"
        }
      ]
    },
    operators: {
      patterns: [
        { name: "keyword.operator.pipeline.verscript", match: "\\|>" },
        { name: "keyword.operator.arithmetic.verscript", match: "(\\/\\/|\\^|\\+|\\-|\\*|\\/|%|\\|)" },
        { name: "keyword.operator.comparison.verscript", match: "(!=|x=|<=|>=|<|>|=)" },
        { name: "keyword.operator.logical.verscript", match: "(&|\\b(and|or|nor|xor|xnor|xand|x&|not)\\b)" },
        { name: "keyword.operator.assignment.verscript", match: "(?<!\\?):" }
      ]
    },
    "function-calls": {
      patterns: [
        {
          match: "\\b([a-zA-Z_][a-zA-Z0-9_]*)\\s*(?=\\()",
          captures: {
            "1": { name: "entity.name.function.verscript" }
          }
        }
      ]
    },
    identifiers: {
      patterns: [
        { name: "variable.other.verscript", match: "\\b[a-zA-Z_][a-zA-Z0-9_]*\\b" }
      ]
    }
  }
};

fs.writeFileSync(grammarPath, JSON.stringify(grammar, null, 2), 'utf8');
fs.writeFileSync(vsGrammarPath, JSON.stringify(grammar, null, 2), 'utf8');
console.log('Synchronized TextMate grammars.');

// 4. Update Antigravity IDE installed extension
const agyExtDir = path.join(process.env.USERPROFILE, '.antigravity-ide/extensions/verscript-lang-0.1.0');
if (fs.existsSync(agyExtDir)) {
    fs.cpSync(path.join(toolingDir, 'vscode'), agyExtDir, { recursive: true });
    console.log('Synchronized Google Antigravity IDE extension in:', agyExtDir);
}

// 5. Update VS Code installed extension
const vscodeExtDir = path.join(process.env.USERPROFILE, '.vscode/extensions/verscript-lang-0.1.0');
if (fs.existsSync(vscodeExtDir)) {
    fs.cpSync(path.join(toolingDir, 'vscode'), vscodeExtDir, { recursive: true });
    console.log('Synchronized VS Code extension in:', vscodeExtDir);
}

console.log('Metadata and toolchain synchronization complete!');
