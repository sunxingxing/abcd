import assert from 'node:assert/strict';
import vm from 'node:vm';
import {classReference} from './class_reference.mjs';

// Exercise source ownership rather than spellings: interfaces/comments/string
// contents are not class fields; explicit writes and computed/static/private
// effects must survive the independent compiler adapter.
const text = `
interface Shape { icon: string; name?: string; }
const events: string[] = [];
const marker = '𠮷 class Fake { missing: string; }';
const computedKey = () => { events.push('key'); return 'computed'; };
class Parent { set assigned(value: number) { events.push('setter:' + value); } }
class Model extends Parent {
  icon: string;
  bare;
  public name: string;
  private count;
  initialized = (events.push('initializer'), 3);
  static empty;
  static value = (events.push('static'), 4);
  #brand;
  [computedKey()];
  constructor() { super(); this.assigned = 7; }
  read() { return [this.#brand, this.initialized, marker]; }
  nested() { return new (class { erased: string; kept = 9; })(); }
}
globalThis.result = { Model, events, marker };
`;
const adapted = classReference(text);
assert.equal(adapted.erased, 5);
const context = vm.createContext({});
vm.runInContext(adapted.source, context);
const {Model, events, marker} = context.result;
assert.deepEqual(Array.from(events), ['key', 'static']);
const value = new Model();
assert.deepEqual(Array.from(events), ['key', 'static', 'initializer', 'setter:7']);
assert.deepEqual(Reflect.ownKeys(value), ['initialized', 'computed']);
assert.equal(Object.hasOwn(Model, 'empty'), true);
assert.equal(Model.value, 4);
assert.deepEqual(Array.from(value.read()), [undefined, 3, marker]);
assert.throws(() => Reflect.apply(Model.prototype.read, {}, []), error => error.name === 'TypeError');
assert.deepEqual(Reflect.ownKeys(value.nested()), ['kept']);
assert.equal(value.nested().kept, 9);
assert.match(adapted.source, /class Fake \{ missing: string; \}/);
console.log(JSON.stringify({erased: adapted.erased, computedKey: true, privateBrand: true, staticFields: true}));
