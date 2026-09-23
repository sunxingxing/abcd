import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';

const context = vm.createContext({});
const source = readFileSync(process.argv[2], 'utf8');
const module = new vm.SourceTextModule(source, {context});
await module.link(() => { throw new Error('unexpected import'); });
await module.evaluate();

const {CommonConstants, BreakpointType} = module.namespace;
assert.equal(CommonConstants.name, 'CommonConstants');
assert.equal(CommonConstants.FULL_PERCENT, '100%');
assert.equal(CommonConstants.TEXTAREA_HEIGHT, '35%');
assert.equal(CommonConstants.BREAK_POINT_SM, 'sm');
for (const key of ['FULL_PERCENT', 'TEXTAREA_HEIGHT', 'BREAK_POINT_SM']) {
  assert.deepEqual(Object.getOwnPropertyDescriptor(CommonConstants, key), {
    value: CommonConstants[key], writable: true, enumerable: true,
    configurable: true,
  });
}
assert.equal(BreakpointType.name, 'BreakpointType');
const first = {}, second = {}, third = {};
const breakpoint = new BreakpointType(first, second, third);
assert.equal(breakpoint.GetValue('sm'), first);
assert.equal(breakpoint.GetValue('md'), second);
assert.equal(breakpoint.GetValue('lg'), third);
assert.equal(breakpoint.GetValue('other'), undefined);
