import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';

const context = vm.createContext({
  WidthBreakpoint: {WIDTH_SM: 'small', WIDTH_MD: 'medium', WIDTH_LG: 'large'},
});
const source = readFileSync(process.argv[2], 'utf8');
const module = new vm.SourceTextModule(source, {context});
await module.link(() => { throw new Error('unexpected import'); });
await module.evaluate();
const Type = module.namespace.BreakpointType;
assert.equal(Type.name, 'BreakpointType');
assert.equal(Type.length, 4);
const instance = new Type(1, 2, 3, 4);
assert.deepEqual(Object.keys(instance), ['sm', 'md', 'lg', 'xl']);
assert.equal(instance.getValue('small'), 1);
assert.equal(instance.getValue('medium'), 2);
assert.equal(instance.getValue('large'), 3);
assert.equal(instance.getValue('unknown'), 4);
