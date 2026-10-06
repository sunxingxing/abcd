import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { stripTypeScriptTypes } from 'node:module';
import { createHash } from 'node:crypto';
import { verifyStaticConstantsRecord } from './constant_record_state.mjs';
const [tool, abc, paired] = process.argv.slice(2);
assert.equal(createHash('sha256').update(readFileSync(abc)).digest('hex'),
  '8de95a4c6579b38133367422d43ffac146cba5e8222a874df4c3d7942380dc6a');
const run = spawnSync(tool, [abc, '7312', '--record-source', '--value-source'],
  { encoding: 'utf8', timeout: 60_000 });
assert.equal(run.status, 0, run.stderr + run.stdout);
const report = JSON.parse(run.stdout);
assert.equal(report.source_complete, true);
assert.equal(report.route, 'value_tree');
assert.equal(report.legacy_writer_used, false);
assert.equal(report.record, 7167);
assert.doesNotMatch(report.source, /INCOMPLETE|\$value|\.call\(|Object\.defineProperty/);
assert.match(report.source, /static TEXT_OPACITY = \[/);
const reference = stripTypeScriptTypes(readFileSync(paired, 'utf8'));
console.log(JSON.stringify(await verifyStaticConstantsRecord(report.source, reference)));
