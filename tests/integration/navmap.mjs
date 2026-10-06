import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import {dirname, join} from 'node:path';
import vm from 'node:vm';
import {verifyNavMap} from './navmap_state.mjs';

const generated = execFileSync(process.argv[2], [process.argv[3], '--navmap'], {encoding: 'utf8'});
// Paired source belongs only to this independent test process, never the engine.
const paired = readFileSync(join(dirname(process.argv[3]),
  'Immersive_src/default/src/main/ets/pages/Index.ts'), 'utf8');
const begin = paired.indexOf('    NavPageMap(name: string, parent = null) {');
const end = paired.indexOf('    initialRender()', begin);
assert.ok(begin >= 0 && end > begin);
const reference = paired.slice(begin, end).replace('name: string', 'name');
const scenarios = await verifyNavMap(
  context => vm.runInContext(`(class { ${generated} }).prototype.NavPageMap`, context), reference);
console.log(`NavPageMap: ${scenarios} paired-source behavior scenarios passed`);
