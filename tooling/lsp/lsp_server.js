#!/usr/bin/env node
// ═══════════════════════════════════════════════════════════════════
//  VerScript Language Server Protocol (LSP) Primitive
// ═══════════════════════════════════════════════════════════════════
const readline = require('readline');
const cp = require('child_process');
const path = require('path');
const fs = require('fs');

let buffer = Buffer.alloc(0);
process.stdin.on('data', chunk => {
    buffer = Buffer.concat([buffer, chunk]);
    while (true) {
        const headerEnd = buffer.indexOf('\r\n\r\n');
        if (headerEnd === -1) break;
        const headerStr = buffer.slice(0, headerEnd).toString('utf8');
        const match = headerStr.match(/Content-Length:\s*(\d+)/i);
        if (!match) break;
        const len = parseInt(match[1], 10);
        const bodyStart = headerEnd + 4;
        if (buffer.length < bodyStart + len) break;
        const bodyStr = buffer.slice(bodyStart, bodyStart + len).toString('utf8');
        buffer = buffer.slice(bodyStart + len);
        try {
            handleLSP(JSON.parse(bodyStr));
        } catch (e) {
            console.error('LSP Parse error:', e);
        }
    }
});

function sendLSP(msg) {
    const json = JSON.stringify(msg);
    const len = Buffer.byteLength(json, 'utf8');
    process.stdout.write(`Content-Length: ${len}\r\n\r\n${json}`);
}

function handleLSP(req) {
    if (req.method === 'initialize') {
        sendLSP({
            jsonrpc: '2.0',
            id: req.id,
            result: {
                capabilities: {
                    textDocumentSync: 1,
                    completionProvider: { resolveProvider: true, triggerCharacters: ['.', '?', ':', ' '] },
                    hoverProvider: true
                }
            }
        });
    } else if (req.method === 'textDocument/completion') {
        const items = [
            { label: 'def func', kind: 3, detail: 'Pure Function Definition' },
            { label: 'def outbound method', kind: 3, detail: 'Outbound Method' },
            { label: 'class', kind: 7, detail: 'Class Declaration' },
            { label: 'display', kind: 14, detail: 'Output Console Command' },
            { label: 'do ... unless', kind: 15, detail: 'Watchdog Error & Condition Guard' },
            { label: 'guard', kind: 14, detail: 'Pre-condition Guard Clause' },
            { label: 'match', kind: 14, detail: 'Pattern Matching Block' },
            { label: 'channel', kind: 14, detail: 'FIFO Concurrency Channel' },
            { label: 'send', kind: 14, detail: 'Send to Channel' },
            { label: 'recv', kind: 14, detail: 'Receive from Channel' },
            { label: 'Strings', kind: 9, detail: 'Standard Library: Text Manipulation' },
            { label: 'Math', kind: 9, detail: 'Standard Library: High-Performance Math' },
            { label: 'Arrays', kind: 9, detail: 'Standard Library: Dynamic Collections' }
        ];
        sendLSP({ jsonrpc: '2.0', id: req.id, result: items });
    } else if (req.method === 'textDocument/hover') {
        sendLSP({
            jsonrpc: '2.0',
            id: req.id,
            result: {
                contents: {
                    kind: 'markdown',
                    value: '**VerScript Language Primitive**\nHigh-performance deterministic runtime with native attribute ergonomics.'
                }
            }
        });
    } else if (req.id !== undefined) {
        sendLSP({ jsonrpc: '2.0', id: req.id, result: null });
    }
}
