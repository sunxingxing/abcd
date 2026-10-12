import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {stripTypeScriptTypes} from 'node:module';
import {verifyRoutesRecord} from './constant_record_state.mjs';

const [tool, abc, paired] = process.argv.slice(2);
assert.equal(createHash('sha256').update(readFileSync(abc)).digest('hex'),
  '8de95a4c6579b38133367422d43ffac146cba5e8222a874df4c3d7942380dc6a');
// Paired source stays exclusively in this oracle. The engine receives ABC only.
const run = spawnSync(tool, [abc, '8889', '--record-source', '--value-source'],
  {encoding: 'utf8', timeout: 60_000});
assert.equal(run.status, 0, run.stderr + run.stdout);
const report = JSON.parse(run.stdout);
assert.equal(report.source_complete, true);
assert.equal(report.legacy_writer_used, undefined);
assert.equal(report.route, 'value_tree');
assert.equal(report.record, 8755);
const reference = stripTypeScriptTypes(readFileSync(paired, 'utf8'));
// Buffer placeholders must disappear independently of behavioral agreement.
// Nested literals use the common AST layout, rather than one opaque source line.
assert.match(report.source, /let ROUTES = \[\n  \{\n    title: \{/);
assert.doesNotMatch(report.source, /\(\{/);
assert.match(report.source, /export \{ ROUTES/);
assert.ok(!report.source.includes('$acc'));
assert.match(report.source, /let ROUTES = \[/);
assert.ok(!report.source.includes('$value'), 'single-use initialization should not retain a root snapshot');
assert.match(report.source, /title: /);
assert.ok(!report.source.includes('["title"]:'), 'ordinary object key should use native syntax');
const retainedPlaceholders = /(?:\["(?:title|name|children|params)"\]|\b(?:title|name|children|params)): null/.test(report.source);
assert.equal(retainedPlaceholders, false, 'overwritten buffer placeholders remain');
console.log(JSON.stringify({semantic: 'passed', retainedBufferPlaceholders: retainedPlaceholders,
  ...(await verifyRoutesRecord(report.source, reference))}));
