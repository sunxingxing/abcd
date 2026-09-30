import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

// The engine receives only the synthetic ABC fixture. Native ESM linking and
// evaluation independently check declaration timing and the final exported slot.
const source = execFileSync(process.argv[2], ['source'], {encoding: 'utf8'});
const context = vm.createContext({});
const recovered = new vm.SourceTextModule(source, {context});
await recovered.link(() => { throw new Error('unexpected import'); });
assert.throws(() => recovered.namespace.exported, {name: 'ReferenceError'});
await recovered.evaluate();
assert.equal(recovered.namespace.exported, 2);
assert.deepEqual(Object.keys(recovered.namespace), ['exported']);

const reference = new vm.SourceTextModule(
  'export let exported = 0; exported = 1; exported = 2;', {context});
await reference.link(() => { throw new Error('unexpected import'); });
await reference.evaluate();
assert.equal(recovered.namespace.exported, reference.namespace.exported);
