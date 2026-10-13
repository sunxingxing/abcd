import {pathToFileURL} from 'node:url';
const m = await import(pathToFileURL(process.argv[2]));
const output = [];
const run = (name, input, call = m.only) => {
  const events = [];
  try { output.push([name, call(input(events)), events]); }
  catch (e) { output.push([name, e.name, e.message, events]); }
};
run('empty', () => []);
run('unicode', () => 'a\u{1f600}b');
run('sparse', events => {
  const a = [1, , 3];
  const proto = Object.create(Array.prototype);
  Object.defineProperty(proto, '1', {get() { events.push('prototype'); return 9; }});
  Object.setPrototypeOf(a, proto); return a;
});
output.push(['mixed', m.mixed([10, 11], [20])]);
const iterable = (events, fail) => ({
  get [Symbol.iterator]() {
    events.push('iterator'); if (fail === 'iterator') throw Error('iterator');
    return function() {
      let n = 0;
      return {get next() {
        events.push('next-get'); if (fail === 'next-get') throw Error('next-get');
        return function() {
          events.push('next-call'); if (fail === 'next-call') throw Error('next-call');
          const value = ++n;
          return {get done() { events.push('done'); if (fail === 'done') throw Error('done'); return value > 2; },
            get value() { events.push('value'); if (fail === 'value') throw Error('value'); return value; }};
        };
      }};
    };
  }
});
for (const fail of [null, 'iterator', 'next-get', 'next-call', 'done', 'value']) {
  run('outside-' + fail, events => iterable(events, fail));
  run('inside-' + fail, events => iterable(events, fail), m.caught);
}
run('values-ignored', events => {
  const a = [4, 5]; Object.defineProperty(a, 'values', {get() { events.push('values'); throw Error('values'); }}); return a;
});
run('inherited-value', events => ({[Symbol.iterator]() { let done = false; return {next() {
  if (done) return {done: true}; done = true;
  return Object.create({done: false, get value() { events.push('inherited'); return 6; }});
}}; }}));
run('hidden-value', () => ({[Symbol.iterator]() { let done = false; return {next() {
  if (done) return {done: true}; done = true;
  return Object.defineProperty({done: false}, 'value', {value: 7, enumerable: false});
}}; }}));
run('grow', events => {
  const a = [1, 2]; Object.defineProperty(a, '0', {get() { events.push('grow'); a.push(3); return 1; }}); return a;
});
run('shrink', events => {
  const a = [1, 2, 3]; Object.defineProperty(a, '0', {get() { events.push('shrink'); a.length = 1; return 1; }}); return a;
});
// Built-in spread defines own elements; a prototype setter is not called.
let setters = 0, result;
Object.defineProperty(Array.prototype, '0', {configurable: true, set() { ++setters; }});
try { result = m.only('xy'); } finally { delete Array.prototype[0]; }
output.push(['prototype-setter', setters, result]);
for (const name of ['rhs', 'rhscaught']) for (const fail of [null, 'call', 'item', 'method-get', 'method-call']) {
  const events = []; let calls = 0;
  const f = () => { events.push('call' + (++calls)); if (fail === 'call') throw Error('call'); return iterable(events, null); };
  const o = {
    get item() { events.push('item'); if (fail === 'item') throw Error('item'); return 7; },
    get more() {
      events.push('method-get'); if (fail === 'method-get') throw Error('method-get');
      return function() { events.push(['method-call', this === o]); if (fail === 'method-call') throw Error('method-call'); return iterable(events, null); };
    }
  };
  try { output.push([name, fail, m[name](f, o), events]); }
  catch (e) { output.push([name, fail, e.name, e.message, events]); }
}
console.log(JSON.stringify(output));
