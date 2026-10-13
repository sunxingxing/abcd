import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, writeFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {pathToFileURL} from 'node:url';

const [structure, semantic, fixtures] = process.argv.slice(2);
const root = mkdtempSync(join(tmpdir(), 'abcd-phase-reachability-'));
const run = (tool, args) => {
  const result = spawnSync(tool, args, {encoding: 'utf8'});
  assert.equal(result.status, 0, result.stderr); return result.stdout;
};
const load = async (name, text) => {
  const path = join(root, name + '.mjs'); writeFileSync(path, text);
  return import(pathToFileURL(path).href);
};
function observations(module) {
  const results = [];
  for (const name of ['caught', 'finalized']) {
    for (const failure of ['none', 'lookup', 'call', 'recover', 'finish']) {
      const log = [], raised = {tag: failure};
      const input = {
        get run() {
          log.push('lookup'); if (failure === 'lookup') throw raised;
          return function () { log.push(['run', this === input]); if (failure === 'call' || failure === 'recover') throw raised; return 73; };
        },
        recover(error) { log.push(['recover', this === input, error === raised]); if (failure === 'recover') throw raised; return 29; },
        finish() { log.push(['finish', this === input]); if (failure === 'finish') throw raised; },
      };
      let completion;
      try { completion = ['return', module[name](input)]; }
      catch (error) { completion = ['throw', error === raised]; }
      results.push([name, failure, completion, log]);
    }
  }
  return results;
}
try {
  const abc = join(fixtures, 'reachability.abc'), out = join(root, 'out');
  run(structure, [abc, out, '--value-source', '--language-source']);
  const record = JSON.parse(readFileSync(join(out, 'manifest.json'))).records.find(r => r.source_status !== 2);
  assert.equal(record.source_status, 0, JSON.stringify(record));
  const text = readFileSync(join(out, record.source), 'utf8');
  const metadata = JSON.parse(readFileSync(join(out, record.metadata)));
  const caught = metadata.methods.find(m => m.runtime_name === 'caught');
  const instructions = run(semantic, [abc, String(caught.entity), '--instructions']).trim().split('\n').map(JSON.parse).filter(r => 'pc' in r);
  // The fixture must exercise unreachable input, not merely a live try/catch.
  // es2abc leaves a JMP immediately after RETURN. Its PC remains in the
  // original input; production projection does not inspect this sequence.
  assert.ok(instructions.some((row, i) => row.opcode === 0x64 && instructions[i + 1]?.opcode === 0x4d));
  assert.ok(instructions.some(row => row.exception_edge?.handler_pc !== undefined));
  const original = await load('original', readFileSync(join(fixtures, 'reachability.ts'), 'utf8'));
  const expected = observations(original);
  assert.deepEqual(observations(await load('recovered', text)), expected);
  const noCatch = text.replace(/return (\$\w+)\.recover\((\$\w+)\);/, 'throw $2;');
  assert.notEqual(noCatch, text);
  assert.notDeepEqual(observations(await load('lost-handler', noCatch)), expected);
  const twice = text.replace(/(\$\w+)\.finish\(\);/g, '$1.finish(); $1.finish();');
  assert.notEqual(twice, text);
  assert.notDeepEqual(observations(await load('duplicate-cleanup', twice)), expected);
} finally { rmSync(root, {recursive: true, force: true}); }
