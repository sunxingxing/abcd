import assert from 'node:assert/strict';
import {GraphTrace} from './graph_trace.mjs';

const intrinsics = new Map([[Object.prototype, 'object'], [Array.prototype, 'array'], [Function.prototype, 'function']]);
const snapshot = (root, prototypes = intrinsics) => new GraphTrace().exports(
  new Map([['fixture', {status: 'evaluated', namespace: {root}}]]), prototypes);
const make = () => {
  const first = [], second = [];
  const root = {first, second, alias: first, absent: undefined, nan: NaN, zero: -0};
  root.self = root; return root;
};
assert.deepEqual(snapshot(make()), snapshot(make()));
assert.deepEqual(snapshot(make(), null), snapshot(make(), null));
const reference = snapshot(make());
for (const change of [
  root => {root.second = root.first;},
  root => {root.alias = [];},
  root => {Object.setPrototypeOf(root.first, null);},
  root => {Object.preventExtensions(root.first);},
  root => {delete root.absent;},
  root => {root.zero = 0;},
  root => {root.first.length = 1;},
  root => {root.first[0] = undefined;},
  root => {Object.defineProperty(root, 'first', {writable: false});},
]) {
  const root = make(); change(root); assert.notDeepEqual(snapshot(root), reference);
}
let reads = 0;
const accessor = {}; Object.defineProperty(accessor, 'value', {get() {++reads; return 7;}});
snapshot(accessor); assert.equal(reads, 0, 'descriptor traversal does not execute getters');
class Base {}
const first = new Base(), second = new Base();
const original = snapshot({Base, first, second});
Object.setPrototypeOf(second, Object.create(Base.prototype));
assert.notDeepEqual(snapshot({Base, first, second}), original, 'exact imported prototype identity matters');
console.log(JSON.stringify({graphMutations: 10, cycles: true, gettersNotExecuted: true, legacyModeRetained: true}));
