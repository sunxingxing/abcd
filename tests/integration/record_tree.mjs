import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

const source = execFileSync(process.argv[2], ['source'], {encoding: 'utf8'});
const module = new vm.SourceTextModule(source);
await module.link(() => { throw new Error('unexpected import'); });
assert.throws(() => module.namespace.answer, {name: 'ReferenceError'});
await module.evaluate();
assert.equal(module.namespace.answer, 42);
assert.deepEqual(Object.keys(module.namespace), ['answer']);
