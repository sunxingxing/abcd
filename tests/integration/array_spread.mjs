// Source-language oracle, not Ark runtime equivalence; see fixture README.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, writeFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
const [structure, semantic, fixtures] = process.argv.slice(2);
const root = mkdtempSync(join(tmpdir(), 'abcd-array-spread-'));
const run = (tool, args) => {
  const result = spawnSync(tool, args, {encoding: 'utf8'});
  assert.equal(result.status, 0, result.stderr); return result.stdout;
};
try {
  const input = join(fixtures, 'spread.abc');
  const out = join(root, 'source');
  run(structure, [input, out, '--value-source', '--language-source']);
  const manifest = JSON.parse(readFileSync(join(out, 'manifest.json')));
  const record = manifest.records.find(r => r.source_status !== 2);
  assert.equal(record.source_status, 0, JSON.stringify(record));
  const text = readFileSync(join(out, record.source), 'utf8');
  assert.match(text, /\.\.\./);
  const metadata = JSON.parse(readFileSync(join(out, record.metadata)));
  let spreads = 0, caught = 0, computed = 0;
  for (const method of metadata.methods) {
    for (const row of run(semantic, [input, String(method.entity), '--instructions']).trim().split('\n').map(JSON.parse)) {
      if (row.opcode === 0xc6) { ++spreads; caught += !!row.exception_edge?.handler_pc; assert.equal(row.exceptional_completion, 1); }
      // Pinned ISA STOWNBYVALUE; validate target bytecode independently of source text.
      if (row.opcode === 0x78 || row.opcode === 0xc8) ++computed;
    }
  }
  assert.equal(spreads, 8); assert.ok(caught > 0); assert.ok(computed >= 2);
  for (const [i, flags] of [[], ['--language-numeric', '--language-constructors']].entries()) {
    const old = join(root, 'old' + i); run(structure, [input, old, '--value-source', ...flags]);
    const rejected = JSON.parse(readFileSync(join(old, 'manifest.json'))).records.find(r => r.source_status !== 2);
    assert.equal(rejected.source_status, 3);
    // Default arithmetic can fail earlier on the mixed cursor increment.
    if (flags.length) assert.ok((rejected.source_failure?.causes ?? rejected.source_causes).some(c => c.domain.endsWith('ArrayInitializationError') && c.value === 0), JSON.stringify(rejected));
  }
  const evaluate = (name, code) => {
    const path = join(root, name + '.mjs'); writeFileSync(path, code);
    return run(process.execPath, [join(fixtures, 'harness.mjs'), path]).trim();
  };
  const expected = evaluate('original', readFileSync(join(fixtures, 'spread.ts'), 'utf8'));
  assert.equal(evaluate('recovered', text), expected);
  // Loss of spread semantics and repeated iteration must each change observations.
  const mutation = text.replace(/\.\.\.(\$\w+)/, '$1');
  assert.notEqual(mutation, text); assert.notEqual(evaluate('no-spread', mutation), expected);
  const repeated = text.replace(/\.\.\.(\$\w+)/, '...$1, ...$1');
  assert.notEqual(repeated, text); assert.notEqual(evaluate('twice', repeated), expected);
  // Moving a getter before the preceding iterator or evaluating it twice is observable.
  const reordered = text.replace(/(\.\.\.\$\w+\(\)), (\$\w+\.item)/, '$2, $1');
  assert.notEqual(reordered, text); assert.notEqual(evaluate('rhs-order', reordered), expected);
  const getterTwice = text.replace(/(\$\w+\.item)/, '($1, $1)');
  assert.notEqual(getterTwice, text); assert.notEqual(evaluate('getter-twice', getterTwice), expected);
} finally { rmSync(root, {recursive: true, force: true}); }
