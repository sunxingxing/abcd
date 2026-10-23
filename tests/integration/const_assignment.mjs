// Differential: assignment to a const evaluates the right-hand side, then
// throws a realm TypeError (THROW_CONSTASSIGNMENT).
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const [original, emitted] = process.argv.slice(2);
const load = p => import('data:text/javascript;base64,' + Buffer.from(readFileSync(p)).toString('base64'));
const a = await load(original), b = await load(emitted);
let cases = 0;
const run = (m, f) => { try { return ['ok', f(m)]; } catch (e) { return ['threw', e.constructor === TypeError, e instanceof Error]; } };
const check = f => { assert.deepEqual(run(b, f), run(a, f)); cases++; };
check(m => { const log = []; const r = (() => { try { return m.f(log); } catch (e) { return e.constructor.name; } })(); return [r, log]; });
check(m => m.g());
for (const o of [{v: 2}, {get v() { throw new RangeError('v'); }}]) check(m => { try { return m.h(o); } catch (e) { return e.constructor.name; } });
console.log(JSON.stringify({cases}));
