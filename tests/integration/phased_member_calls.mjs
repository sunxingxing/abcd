// Original source is an independent Node oracle. Only ABC reaches the engine.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const [original, emitted] = process.argv.slice(2);
const load = p => import('data:text/javascript;base64,' + Buffer.from(readFileSync(p)).toString('base64'));
const a = await load(original), b = await load(emitted);
let cases = 0;
const check = async observe => {
  assert.deepEqual(await observe(b), await observe(a));
  cases++;
};
const modes = ['ok', 'base-get', 'callee-get', 'argument-get', 'coercion', 'length-get',
  'call', 'not-callable', 'null', 'undefined', 'null-later', 'outer-null',
  'replace-callee', 'wait-reject'];
for (const name of ['run', 'template', 'base', 'before', 'after', 'nested']) {
  for (const mode of modes) await check(async m => {
    const log = [], sentinel = new Error('user failure');
    let reads = 0, receiver;
    const result = {[Symbol.toPrimitive]() { log.push(['nested-coercion']); return this.length; }, get length() {
      log.push(['nested-length']);
      if (mode === 'length-get') throw sentinel;
      return 8;
    }};
    const camera = {
      [Symbol.toPrimitive]() {
        log.push(['coercion']);
        if (mode === 'coercion') throw sentinel;
        return 'camera';
      },
      get length() {
        log.push(['length']);
        if (mode === 'length-get') throw sentinel;
        return 4;
      },
      get next() {
        log.push(['get-next']);
        return function(tag) {
          log.push(['next', this === camera, tag]);
          return result;
        };
      }
    };
    const target = Object.defineProperty({}, 'info', {configurable: true, get() {
      log.push(['get-info']);
      if (mode === 'callee-get') throw sentinel;
      if (mode === 'not-callable') return 42;
      return function(tag, message) {
        log.push(['call', this === receiver, tag, message]);
        if (mode === 'call') throw sentinel;
        return message;
      };
    }});
    receiver = target;
    const obj = mode === 'outer-null' ? null : {get cameras() {
      log.push(['get-cameras', ++reads]);
      if (mode === 'argument-get') throw sentinel;
      if (mode === 'replace-callee') Object.defineProperty(target, 'info', {
        configurable: true, value() { log.push(['replacement-called']); return 'wrong'; }
      });
      if (mode === 'null' || (mode === 'null-later' && reads === 2)) return null;
      if (mode === 'undefined') return undefined;
      return camera;
    }};
    const wait = x => {
      log.push(['wait', x]);
      return mode === 'wait-reject' ? Promise.reject(sentinel) : x;
    };
    const container = {get log() {
      log.push(['get-base']); if (mode === 'base-get') throw sentinel; return target;
    }};
    const value = await m[name](name === 'base' ? container : target, obj, 'tag', wait);
    const outcome = value instanceof Error ? ['error', value.constructor.name,
      value === sentinel, value === sentinel ? value.message : undefined] : ['return', value];
    return {outcome, log};
  });
}
for (const mode of ['ok', 'next-missing', 'next-throw', 'result-null']) await check(m => {
  const log = [], sentinel = new RangeError('next failure');
  const camera = {
    [Symbol.toPrimitive]() { log.push('coerce'); return 'C'; },
    get next() {
      log.push('next-get');
      if (mode === 'next-missing') return undefined;
      return function(tag) {
        log.push(['next-call', this === camera, tag]);
        if (mode === 'next-throw') throw sentinel;
        return mode === 'result-null' ? null : {length: 3};
      };
    }
  };
  const base = {get info() {
    log.push('info-get');
    return function(...args) { log.push(['info-call', this === base, args]); return 7; };
  }};
  const value = m.nested(base, {get cameras() { log.push('cameras'); return camera; }}, 'T');
  return [value instanceof Error ? [value.constructor.name, value === sentinel] : value, log];
});
for (const bits of [[false,false], [false,true], [true,false], [true,true]]) {
  for (const failure of [0,1,2,3,4]) await check(m => {
    const log = [], sentinel = new TypeError('argument failure');
    let tests = 0;
    const base = {get info() {
      log.push('callee');
      return function(tag, value) { log.push(['call', this === base, tag, value]); return value; };
    }};
    const value = m.choices(base, {}, 'tag', () => {
      log.push(['test', tests]); return bits[tests++];
    }, n => {
      log.push(['arg', n]); if (failure === n) throw sentinel;
      return {[Symbol.toPrimitive]() { log.push(['coerce', n]); return n; }};
    });
    return [value instanceof Error ? [value.constructor.name, value === sentinel] : value, log];
  });
}
for (const mode of ['ok', 'null', 'undefined', 'not-callable', 'argument-throw', 'callee-throw']) await check(m => {
  const log = [], sentinel = new SyntaxError('callback failure');
  const base = {get info() {
    log.push('info-get'); if (mode === 'callee-throw') throw sentinel;
    return function(...args) { log.push(['info-call', this === base, args]); return args[1]; };
  }};
  const cb = mode === 'null' ? null : mode === 'undefined' ? undefined : mode === 'not-callable' ? 3 :
    function(tag) { log.push(['callback', this === undefined, tag]); if (mode === 'argument-throw') throw sentinel; return 6; };
  const value = m.optionalCall(base, {get cameras() { log.push('cameras'); return 'C'; }}, 'T', cb);
  return [value instanceof Error ? [value.constructor.name, value === sentinel] : value, log];
});
assert.ok(cases >= 20);
console.log(JSON.stringify({cases}));
