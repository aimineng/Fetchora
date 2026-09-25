/*
    tools/check-workflows.js - a dependency-free sanity check for the GitHub
    Actions workflows.

    There is no YAML parser on this machine (no PyYAML, no Ruby, no yq) and
    `npm install js-yaml` would add a dependency to a project that has none, so
    this walks the two workflow files and checks the things that actually break a
    run:

      * tabs anywhere (YAML forbids them for indentation)
      * indentation that does not line up with a parent block
      * `uses:` entries that are not pinned to a version
      * step lists that are not sequences of mappings
      * obvious unbalanced flow sequences/brackets

    It is a structural linter, not a parser: it proves the files are shaped like
    workflow YAML, and it is honest about not proving they are valid YAML.

    Usage:  node tools/check-workflows.js
*/
const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');
const dir = path.join(root, '.github', 'workflows');

function fail(file, line, message) {
  problems.push(`${file}:${line}: ${message}`);
}
const problems = [];
let checked = 0;

for (const name of fs.readdirSync(dir).filter((f) => f.endsWith('.yml') || f.endsWith('.yaml'))) {
  const file = path.join(dir, name);
  const text = fs.readFileSync(file, 'utf8');
  const lines = text.split(/\r?\n/);
  checked++;

  // ---- tabs ---------------------------------------------------------------
  lines.forEach((line, i) => {
    if (/^\s*\t/.test(line) || /\t/.test(line.replace(/#.*$/, ''))) {
      fail(name, i + 1, 'tab character (YAML indentation must use spaces)');
    }
  });

  // ---- indentation must follow a previously seen shallower level ----------
  const seen = new Set([0]);
  const blockKeys = []; // {indent, line}
  // A block scalar (`run: |`, `body: |`, `script: >`) swallows every more
  // indented line verbatim: that content is shell/PowerShell/markdown, so its
  // own indentation, braces and fences must not be linted as YAML.
  let blockScalarIndent = -1;

  lines.forEach((raw, i) => {
    if (!raw.trim() || raw.trim().startsWith('#')) return;
    if (raw.trim() === '---' || raw.trim() === '...') return;
    const indent = raw.length - raw.trimStart().length;
    const body = raw.trim();

    if (blockScalarIndent >= 0) {
      if (indent > blockScalarIndent) return; // still inside the scalar
      blockScalarIndent = -1;
    }
    if (/[|>][-+]?\d*\s*(#.*)?$/.test(body) && /^[\w."'${{}}[\]-]+:\s/.test(body)) {
      blockScalarIndent = indent;
      seen.add(indent);
      return;
    }

    // A list item may be indented one level deeper than its key's column+2.
    if (!seen.has(indent) && !body.startsWith('- ')) {
      const parent = Math.max(...[...seen].filter((n) => n < indent));
      if (indent !== parent + 2 && indent !== parent + 4) {
        fail(name, i + 1, `indentation ${indent} does not line up with any open block`);
      }
    }
    seen.add(indent);

    // ---- unbalanced flow collections -------------------------------------
    const stripped = body.replace(/#.*$/, '').replace(/'[^']*'/g, "''").replace(/"[^"]*"/g, '""');
    for (const [open, close] of [['[', ']'], ['{', '}']]) {
      const o = (stripped.match(new RegExp('\\' + open, 'g')) || []).length;
      const c = (stripped.match(new RegExp('\\' + close, 'g')) || []).length;
      if (o !== c) fail(name, i + 1, `unbalanced '${open}${close}' on one line`);
    }

    // ---- `uses:` must be pinned ------------------------------------------
    const uses = /^uses:\s*(\S+)/.exec(body) || /^-\s+uses:\s*(\S+)/.exec(body);
    if (uses) {
      const ref = uses[1];
      if (!ref.includes('@')) fail(name, i + 1, `unpinned action '${ref}'`);
      else if (/@(master|main|HEAD)$/.test(ref)) fail(name, i + 1, `floating ref '${ref}'`);
    }

    // ---- `- name:` / `- uses:` means a step sequence ----------------------
    if (body.startsWith('- ') && /^-\s+(name|uses|run|id|if|with|env|shell|working-directory):/.test(body)) {
      if (indent % 2 !== 0) fail(name, i + 1, 'step sequence indented by an odd number of spaces');
    }

    blockKeys.push({ indent, line: i + 1, key: /^([A-Za-z_][\w.-]*):/.exec(body)?.[1] });
  });

  // ---- required top-level keys -------------------------------------------
  for (const key of ['name:', 'on:', 'jobs:']) {
    if (!lines.some((l) => l.startsWith(key))) fail(name, 1, `missing top-level '${key}'`);
  }
}

if (problems.length) {
  console.error(`✗ ${problems.length} problem(s) in ${checked} workflow file(s):`);
  for (const p of problems) console.error('  ' + p);
  process.exit(1);
}
console.log(`✓ ${checked} workflow file(s) passed the structural check`);
console.log('  (indentation, tabs, pinned actions, flow brackets - not a full YAML parse)');
