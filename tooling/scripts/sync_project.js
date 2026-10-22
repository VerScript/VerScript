#!/usr/bin/env node
/**
 * VerScript Organization Project & Roadmap Synchronizer
 * 
 * Fetches all active/closed milestones and issues across all VerScript repositories,
 * aggregates project velocity metrics, updates PROJECT_STATUS.json, and synchronizes
 * the live interactive Project Board at VerScript.github.io/project.
 */

const { execSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const repoRoot = path.resolve(__dirname, '../..');

function runGh(cmd) {
    try {
        return execSync(`gh ${cmd}`, { encoding: 'utf8', stdio: ['pipe', 'pipe', 'pipe'] }).trim();
    } catch (e) {
        return null;
    }
}

const REPOS = [
    { id: 'VerScript', name: 'VerScript (Core Language & Compiler)', repo: 'VerScript/VerScript', color: '#00FFCC' },
    { id: 'desktop', name: 'VerScript Studio Desktop', repo: 'VerScript/verscript-studio-desktop', color: '#0077FE' },
    { id: 'android', name: 'VerScript Studio Android', repo: 'VerScript/verscript-studio-android', color: '#34A853' },
    { id: 'web', name: 'VerScript Website & Academy', repo: 'VerScript/VerScript.github.io', color: '#BD00FF' },
    { id: 'help', name: 'VerScript Help & Docs', repo: 'VerScript/help', color: '#7928CA' }
];

console.log('Synchronizing VerScript Organization Project & Milestones...');
const projectData = {
    generatedAt: new Date().toISOString(),
    organization: 'VerScript',
    repos: REPOS,
    milestones: [],
    issues: []
};

for (const r of REPOS) {
    console.log(`Auditing ${r.repo}...`);
    // 1. Milestones
    const msRaw = runGh(`api /repos/${r.repo}/milestones?state=all`);
    if (msRaw) {
        try {
            const msList = JSON.parse(msRaw);
            for (const ms of msList) {
                projectData.milestones.push({
                    repoId: r.id,
                    repoName: r.name,
                    number: ms.number,
                    title: ms.title,
                    description: ms.description,
                    state: ms.state,
                    openIssues: ms.open_issues,
                    closedIssues: ms.closed_issues,
                    htmlUrl: ms.html_url,
                    dueOn: ms.due_on
                });
            }
        } catch(e) {}
    }

    // 2. Issues
    const issRaw = runGh(`issue list -R ${r.repo} --state all --limit 100 --json number,title,state,body,labels,milestone,createdAt,closedAt,url`);
    if (issRaw) {
        try {
            const issList = JSON.parse(issRaw);
            for (const iss of issList) {
                let status = 'backlog';
                const labelNames = (iss.labels || []).map(l => l.name);
                if (iss.state === 'CLOSED' || labelNames.includes('status:done')) {
                    status = 'done';
                } else if (labelNames.includes('status:in-progress')) {
                    status = 'in-progress';
                } else if (labelNames.includes('status:under-review')) {
                    status = 'under-review';
                } else if (labelNames.includes('status:todo')) {
                    status = 'todo';
                }

                let priority = 'medium';
                if (labelNames.includes('priority:critical')) priority = 'critical';
                else if (labelNames.includes('priority:high')) priority = 'high';
                else if (labelNames.includes('priority:low')) priority = 'low';

                projectData.issues.push({
                    id: `${r.id}-${iss.number}`,
                    number: iss.number,
                    title: iss.title,
                    state: iss.state.toLowerCase(),
                    status: status,
                    priority: priority,
                    repoId: r.id,
                    repoName: r.name,
                    repoFullName: r.repo,
                    repoColor: r.color,
                    milestone: iss.milestone ? iss.milestone.title : null,
                    labels: labelNames,
                    url: iss.url,
                    body: (iss.body || '').slice(0, 300),
                    createdAt: iss.createdAt,
                    closedAt: iss.closedAt
                });
            }
        } catch(e) {}
    }
}

// Write to local project status in VerScript/tooling/PROJECT_STATUS.json
const localStatusPath = path.join(repoRoot, 'tooling/PROJECT_STATUS.json');
fs.writeFileSync(localStatusPath, JSON.stringify(projectData, null, 2), 'utf8');

// Write to web project if exists
const webProjectDir = path.join(repoRoot, '../VerScript.github.io/project');
if (fs.existsSync(webProjectDir)) {
    fs.writeFileSync(path.join(webProjectDir, 'project_data.json'), JSON.stringify(projectData, null, 2), 'utf8');
    fs.writeFileSync(path.join(webProjectDir, 'project_data.js'), `window.VERSCRIPT_PROJECT_DATA = ${JSON.stringify(projectData, null, 2)};\n`, 'utf8');
    console.log(`Updated website project data in: ${webProjectDir}`);
}

const total = projectData.issues.length;
const done = projectData.issues.filter(i => i.status === 'done').length;
const velocity = total > 0 ? Math.round((done / total) * 100) : 0;
console.log(`\nSynchronization Complete!`);
console.log(`- Milestones: ${projectData.milestones.length}`);
console.log(`- Issues: ${projectData.issues.length} (${done} completed, ${velocity}% velocity)`);
