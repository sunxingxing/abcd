import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';

const {Derived, HistoricalDerived, GlobalDerived, HistoricalGlobalDerived,
       PositionalDerived, SpreadFieldDerived} =
  await import(pathToFileURL(process.argv[2]));
const instance = new Derived(3, 'x');
assert.deepEqual(instance.values, [3, 'x']);
assert.equal(Derived.length, 0);
assert.equal(Derived.prototype.constructor, Derived);
assert.equal(Object.getPrototypeOf(Derived.prototype),
             Object.getPrototypeOf(Derived).prototype);
assert.deepEqual(new HistoricalDerived(3, 'x').values, [3, 'x']);
assert.equal(HistoricalDerived.length, 0);
const globalInstance = new GlobalDerived(4, 'y');
assert.deepEqual(globalInstance.values, [4, 'y']);
assert.equal(GlobalDerived.length, 0);
assert.equal(GlobalDerived.prototype.constructor, GlobalDerived);
assert.equal(Object.getPrototypeOf(GlobalDerived.prototype),
             Object.getPrototypeOf(GlobalDerived).prototype);
assert.deepEqual(new HistoricalGlobalDerived(4, 'y').values, [4, 'y']);
assert.equal(HistoricalGlobalDerived.length, 0);
assert.equal(PositionalDerived.length, 2);
assert.deepEqual(new PositionalDerived(3, 'x', 'ignored').values, [3, 'x']);
assert.deepEqual(new PositionalDerived(3).values, [3, undefined]);
function Alternate() {}
const alternate = Reflect.construct(PositionalDerived, [9, 'z'], Alternate);
assert.deepEqual(alternate.values, [9, 'z']);
assert.equal(Object.getPrototypeOf(alternate), Alternate.prototype);
assert.equal(SpreadFieldDerived.length, 0);
const spread = new SpreadFieldDerived(3, 'x');
assert.deepEqual(spread.values, [3, 'x']);
assert.equal(spread.curBp, '');
const writes = [];
function AlternateSpread() {}
Object.defineProperty(AlternateSpread.prototype, 'curBp', {
  set(value) { writes.push({receiver: this, value}); },
});
const alternateSpread = Reflect.construct(
  SpreadFieldDerived, [5, 'z'], AlternateSpread);
assert.deepEqual(alternateSpread.values, [5, 'z']);
assert.deepEqual(writes, [{receiver: alternateSpread, value: ''}]);
assert.equal(Object.hasOwn(alternateSpread, 'curBp'), false);
// Native super spread accepts this parent; pinned v7 SuperCallSpread rejects
// its non-JSFunction brand, so the source writer must require brand evidence.
let proxyCalls = 0;
const ProxyParent = new Proxy(class {}, {
  construct(target, args, newTarget) {
    proxyCalls++;
    return Reflect.construct(target, args, newTarget);
  },
});
class NativeProxyDerived extends ProxyParent {
  constructor(...rest) { super(...rest); this.curBp = ''; }
}
new NativeProxyDerived(1);
assert.equal(proxyCalls, 1);
class MutableParentDerived extends (class {}) {
  constructor(...rest) { super(...rest); }
}
Object.setPrototypeOf(MutableParentDerived, ProxyParent);
new MutableParentDerived(2);
assert.equal(proxyCalls, 2);
