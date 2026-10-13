import {pathToFileURL} from 'node:url';
const {run} = await import(pathToFileURL(process.argv[2]));
const cases = ['ok', 'setter', 'rhs', 'readonly', 'callee', 'key', 'rewrite'];
const results = [];
for (const mode of cases) {
  const log = [];
  const marker = new Error(mode);
  let next = 0;
  const target = {};
  for (const name of ['p', 'q']) {
    if (mode === 'readonly') Object.defineProperty(target, name, {value: 0, writable: false});
    else Object.defineProperty(target, name, {set(value) {
      log.push(['set', name, value]);
      if (mode === 'setter') throw marker;
      return -900; // The assignment result must not be the setter return.
    }});
  }
  const obj = {get f() {
    log.push('callee');
    if (mode === 'callee') throw marker;
    return function(...values) {log.push(['call', this === obj, ...values]); return values;};
  }};
  globalThis.obj = obj;
  Object.defineProperty(globalThis, 'target', {configurable: true, get() {log.push('target'); return target;}});
  globalThis.rhs = () => {log.push('rhs'); if (mode === 'rhs') throw marker; if(mode === 'rewrite') Object.defineProperty(obj,'f',{value(){log.push('wrong callee');}}); return ++next;};
  globalThis.key = () => {log.push('key'); if (mode === 'key') throw marker;
    return {[Symbol.toPrimitive]() {log.push('coerce key'); return 'p';}};};
  globalThis.caught = e => {log.push(['caught', e === marker ? mode : e.name]); return 'caught';};
  let result;
  try {result = run();} catch (e) {result = e === marker ? mode : e.name; log.push(['outer', result]);}
  results.push({mode, log, result});
}
console.log(JSON.stringify(results));
