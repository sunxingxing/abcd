// Differential: native instance fields define own data properties in order at
// construction, before the constructor body, without invoking setters.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const [original, emitted] = process.argv.slice(2);
const load = p => import('data:text/javascript;base64,' + Buffer.from(readFileSync(p)).toString('base64'));
const a = await load(original), b = await load(emitted);
const own = o => Reflect.ownKeys(o).map(k => [k, Object.getOwnPropertyDescriptor(o, k)]);
let cases = 0;
const check = observe => { const run = m => { try { return observe(m); } catch (e) { return ['threw', e.constructor.name, e.message]; } };
  assert.deepEqual(run(b), run(a)); cases++; };
for (const v of [0, 5, 'q']) check(m => { const o = new m.A(v); return [own(o), o.w, o.m(), m.A.length, m.A.name]; });
check(m => own(new m.Plain()));
check(m => { const o = new m.Shadow(); return [own(o), o.x]; });
check(m => { const o = new m.Counter(); return [o.inc(), o.inc(), own(o)]; });
check(m => { class Sub extends m.A { constructor() { super(3); this.q = this.x; } } return own(new Sub()); });
check(m => m.A());
console.log(JSON.stringify({cases}));
