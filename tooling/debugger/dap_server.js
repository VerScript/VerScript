#!/usr/bin/env node
// ═══════════════════════════════════════════════════════════════════
//  VerScript Debug Adapter Protocol (DAP) Server Primitive
// ═══════════════════════════════════════════════════════════════════
const readline = require('readline');
const cp = require('child_process');
const path = require('path');
const fs = require('fs');

let seq = 1;
function sendResponse(request, body = {}, success = true, message = '') {
    const res = {
        seq: seq++,
        type: 'response',
        request_seq: request.seq,
        command: request.command,
        success,
        body
    };
    if (message) res.message = message;
    sendPayload(res);
}

function sendEvent(event, body = {}) {
    sendPayload({
        seq: seq++,
        type: 'event',
        event,
        body
    });
}

function sendPayload(obj) {
    const json = JSON.stringify(obj);
    const len = Buffer.byteLength(json, 'utf8');
    process.stdout.write(`Content-Length: ${len}\r\n\r\n${json}`);
}

let buffer = Buffer.alloc(0);
process.stdin.on('data', chunk => {
    buffer = Buffer.concat([buffer, chunk]);
    while (true) {
        const headerEnd = buffer.indexOf('\r\n\r\n');
        if (headerEnd === -1) break;
        const headerStr = buffer.slice(0, headerEnd).toString('utf8');
        const match = headerStr.match(/Content-Length:\s*(\d+)/i);
        if (!match) break;
        const contentLength = parseInt(match[1], 10);
        const bodyStart = headerEnd + 4;
        if (buffer.length < bodyStart + contentLength) break;
        const bodyStr = buffer.slice(bodyStart, bodyStart + contentLength).toString('utf8');
        buffer = buffer.slice(bodyStart + contentLength);
        try {
            handleRequest(JSON.parse(bodyStr));
        } catch (e) {
            console.error('DAP Parse error:', e);
        }
    }
});

let childProcess = null;

function handleRequest(req) {
    const cmd = req.command;
    if (cmd === 'initialize') {
        sendResponse(req, {
            supportsConfigurationDoneRequest: true,
            supportsFunctionBreakpoints: false,
            supportsConditionalBreakpoints: false,
            supportsStepBack: false,
            supportsSetVariable: true
        });
        sendEvent('initialized');
    } else if (cmd === 'launch') {
        const program = req.arguments.program;
        sendResponse(req, {});
        // Execute VerScript binary
        const exe = process.env.VERSCRIPT_PATH || path.join(__dirname, '../../verscript.exe');
        childProcess = cp.spawn(exe, [program], { stdio: ['pipe', 'pipe', 'pipe'] });
        childProcess.stdout.on('data', d => {
            sendEvent('output', { category: 'stdout', output: d.toString() });
        });
        childProcess.stderr.on('data', d => {
            sendEvent('output', { category: 'stderr', output: d.toString() });
        });
        childProcess.on('exit', code => {
            sendEvent('output', { category: 'console', output: `Program exited with code ${code}\n` });
            sendEvent('terminated');
            sendEvent('exited', { exitCode: code });
        });
    } else if (cmd === 'setBreakpoints') {
        sendResponse(req, { breakpoints: (req.arguments.breakpoints || []).map(b => ({ verified: true, line: b.line })) });
    } else if (cmd === 'configurationDone') {
        sendResponse(req, {});
    } else if (cmd === 'threads') {
        sendResponse(req, { threads: [{ id: 1, name: 'Main Thread' }] });
    } else if (cmd === 'stackTrace') {
        sendResponse(req, { stackFrames: [{ id: 1, name: 'main', line: 1, column: 1, source: { name: 'script.vrs' } }], totalFrames: 1 });
    } else if (cmd === 'scopes') {
        sendResponse(req, { scopes: [{ name: 'Global', variablesReference: 1, expensive: false }] });
    } else if (cmd === 'variables') {
        sendResponse(req, { variables: [] });
    } else if (cmd === 'disconnect') {
        if (childProcess) childProcess.kill();
        sendResponse(req, {});
        process.exit(0);
    } else {
        sendResponse(req, {});
    }
}
