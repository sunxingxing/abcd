import assert from 'node:assert/strict';
import {execFileSync, spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
const [engine, abc, semantic] = process.argv.slice(2);
const dir = mkdtempSync(join(tmpdir(), 'abcd-iteration-context-'));
try {
  execFileSync(engine, [abc, dir + '/out', '--value-source', '--language-source']);
  const manifest = JSON.parse(readFileSync(dir + '/out/manifest.json'));
  const record = manifest.records.find(r => r.entity === 299);
  assert.equal(record.source_status, 3);
  const failure = record.source_failure;
  assert.equal(failure.evaluation.obligation, 20); // kProtocolContext
  assert.equal(failure.evaluation.protocol_context, 0); // kMissingInput
  assert.equal(failure.evaluation.location.method, 395);
  const instructions = execFileSync(semantic, [abc, '395', '--instructions'], {encoding:'utf8'}).trim().split('\n').map(JSON.parse);
  const acquire = instructions.find(i => i.opcode === 103); // getiterator imm8
  assert.ok(acquire);
  assert.equal(failure.evaluation.location.pc, acquire.pc);
  assert.equal(failure.evaluation.location.working_pc, acquire.pc);
  assert.equal(typeof failure.evaluation.location.operation, 'number');
  assert.equal(typeof failure.evaluation.value, 'number');
  assert.equal(failure.evaluation.use, undefined); // no fabricated operand use
  const inspection = spawnSync(semantic, [abc, '395', '--record-source', '--language-source'], {encoding:'utf8'});
  assert.equal(inspection.status, 1); // a typed refusal is an unsuccessful source request
  const inspected = JSON.parse(inspection.stdout);
  assert.deepEqual(inspected.source_failure, failure);
} finally { rmSync(dir, {recursive:true, force:true}); }
