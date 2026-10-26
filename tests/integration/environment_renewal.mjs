import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {pathToFileURL} from 'node:url';
const original = await import('data:text/javascript;base64,' + Buffer.from(await readFile(process.argv[2])).toString('base64'));
const emitted = await import(pathToFileURL(process.argv[3]));
let cases = 0;
const kind = process.argv[4];
const names = Object.keys(original).sort();
assert.deepEqual(Object.keys(emitted).sort(), names);
async function run(fn, name, n, reject, failAt) {
  const log = [], groups = [], identities = new WeakMap();
  const token = {}, failure = new Error('user callback');
  let next = 0, calls = 0;
  const mutable = name === 'body' || name === 'factory';
  const encode = value => {
    if(value === token) return ['token'];
    if(value === failure) return ['user-error', failure.message];
    if(value === undefined) return ['undefined'];
    if(value !== null && (typeof value === 'object' || typeof value === 'function')) {
      if(!identities.has(value)) identities.set(value, ++next);
      return ['object', identities.get(value), value instanceof Number ? Number(value) : null];
    }
    return typeof value === 'number' && Number.isNaN(value) ? ['NaN'] : value;
  };
  const inspect = group => group.map((f,i) => mutable && i === 1 ? ['writer'] : encode(f()));
  const keep = (...group) => {
    ++calls;
    if(mutable && groups.length) {
      const old = groups.at(-1);
      log.push(['old-write', encode(old[1](10)), encode(old[2]())]);
    }
    groups.push(group);
    log.push(['keep', calls, inspect(group)]);
    if(calls === failAt) throw failure;
  };
  const wait = {then(yes,no) {
    log.push(['suspend', groups.map(inspect)]);
    queueMicrotask(() => {
      log.push(['resume', groups.map(inspect)]);
      if(reject) no(failure); else yes(token);
    });
  }};
  let outcome;
  try {
    let value;
    if(kind === 'fresh') {
      const input = name === 'values' ? Array.from({length:n},(_,i)=>i) :
        name === 'keys' ? Object.fromEntries(Array.from({length:n},(_,i)=>['k'+i,i])) : n;
      value = await fn(input, keep);
    } else if(name === 'factory') {
      const create = i => {
        log.push(['create', i, groups.map(inspect)]);
        if(groups.length) log.push(['during-init', encode(groups.at(-1)[1](3))]);
        if(i === 1) return undefined;
        if(i === 3) return null;
        if(i === 4) return false;
        return i === 2 && groups.length ? groups[0][0]() : Object(i);
      };
      value = await fn(n,keep,create,wait);
    } else value = await fn(n,keep,wait);
    outcome = ['return', encode(value)];
  } catch(error) {
    outcome = error === failure ? ['throw-user', error.message] :
      error instanceof Error ? ['throw', error.constructor.name] : ['throw-value', encode(error)];
  }
  log.push(['after',groups.map(inspect)]);
  if(mutable) {
    for(const group of groups) log.push(['late-write', encode(group[1](100)), encode(group[2]())]);
    log.push(['final',groups.map(inspect)]);
  }
  return [outcome, log];
}
for(const name of names) for(const n of [0,1,2,3,5]) for(const reject of [false,true]) for(const failAt of [-1,1,2]) {
  assert.deepEqual(await run(emitted[name],name,n,reject,failAt), await run(original[name],name,n,reject,failAt), `${name} n=${n} reject=${reject} failAt=${failAt}`);
  ++cases;
}
console.log(JSON.stringify({cases}));
