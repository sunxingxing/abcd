import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const [original, emitted] = process.argv.slice(2);
const load = p => import('data:text/javascript;base64,' + Buffer.from(readFileSync(p)).toString('base64'));
const a = await load(original), b = await load(emitted);
let cases = 0;
function observe(module, name, config) {
  const log = [];
  const user = label => Object.assign(new Error(label), {user: true});
  const value = {
    get value() { log.push('payload:get'); if (config.fail === 'payload') throw user('payload'); return 7; },
    [Symbol.toPrimitive](hint) { log.push(['coerce', hint]); if (config.fail === 'coerce') throw user('coerce'); return 7; },
  };
  const fallback = {fallback: true};
  const encode = x => x === value ? 'value-identity' : x === fallback ? 'fallback-identity' : x;
  const xs = {
    get [Symbol.iterator]() {
      log.push('iterator:get');
      return function() {
        log.push('iterator:call'); let i = 0;
        return {
          get next() { log.push('next:get'); return function() {
            log.push('next:call');
            if (config.fail === 'next') throw user('next');
            return {get done() { log.push('done'); return i >= config.items.length; },
              get value() { log.push('value'); return config.items[i++]; }};
          }; },
          get return() {
            log.push('return:get');
            if (config.close === 'get-throw') throw user('close-get');
            if (config.close === 'missing') return undefined;
            if (config.close === 'null') return null;
            return function() {
              log.push('return:call');
              if (config.close === 'call-throw') throw user('close-call');
              if (config.close === 'primitive') return 1;
              return {};
            };
          },
        };
      };
    },
  };
  const select = x => { log.push(['select', x]); if (config.fail === 'select') throw user('select'); return config.action; };
  const read = x => { log.push(['read', x]); if (config.fail === 'read') throw user('read'); return config.identity ? value : config.n; };
  const valid = n => { log.push(['valid', encode(n)]); if (config.fail === 'valid') throw user('valid'); return config.valid; };
  const finish = n => { log.push(['finish', encode(n)]); if (config.fail === 'finish') throw user('finish'); return n; };
  try {
    const fn = name === 'captured' ? module[name](fallback) : module[name];
    return {kind: 'return', value: encode(fn(xs, select, read, valid, finish)), log};
  } catch (error) {
    return {kind: 'throw', error: error.constructor.name, ...(error.user ? {message: error.message} : {}), log};
  }
}
for (const name of Object.keys(a)) {
  assert.equal(typeof b[name], 'function');
  for (const items of [[], [3], [3, 4]])
    for (const action of [0, 1, 2])
      for (const valid of [false, true])
        for (const n of [0, 7, null, undefined, false])
          for (const close of ['object', 'missing', 'null', 'get-throw', 'call-throw', 'primitive']) {
            const config = {items, action, valid, n, close};
            assert.deepEqual(observe(b, name, config), observe(a, name, config), JSON.stringify([name, config])); cases++;
          }
  for (const fail of ['select', 'read', 'valid', 'finish', 'next', 'payload', 'coerce'])
    for (const close of ['object', 'get-throw', 'call-throw', 'primitive']) {
      const config = {items: [3], action: 1, valid: true, n: 7, close, fail, identity: fail === 'payload' || fail === 'coerce'};
      assert.deepEqual(observe(b, name, config), observe(a, name, config), JSON.stringify([name, config])); cases++;
    }
  for (const action of [0, 1, 2]) {
    const config = {items: [3], action, valid: true, n: 7, close: 'object', identity: true};
    assert.deepEqual(observe(b, name, config), observe(a, name, config)); cases++;
  }
}
console.log(JSON.stringify({cases}));
