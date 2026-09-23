import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';

const context = vm.createContext({});
const source = readFileSync(process.argv[2], 'utf8');
const module = new vm.SourceTextModule(source, {context});
await module.link(() => { throw new Error('unexpected import'); });
await module.evaluate();
const Type = module.namespace.CommonConstants;
assert.equal(Type.name, 'CommonConstants');
assert.equal(Type.FULL_PERCENT, '100%');
assert.equal(Type.FONT_WEIGHT_500, 500);
assert.deepEqual(Array.from(Type.TEXT_OPACITY), [1, 0.4, 0.6, 0.05]);
for (const key of ['FULL_PERCENT', 'FONT_WEIGHT_500', 'TEXT_OPACITY']) {
  const descriptor = Object.getOwnPropertyDescriptor(Type, key);
  assert.equal(descriptor.writable, true);
  assert.equal(descriptor.enumerable, true);
  assert.equal(descriptor.configurable, true);
}
