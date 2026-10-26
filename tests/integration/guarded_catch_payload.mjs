import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {pathToFileURL} from 'node:url';
const [sourcePath, emittedPath] = process.argv.slice(2);
const original = await import('data:text/javascript;base64,' + readFileSync(sourcePath).toString('base64'));
const emitted = await import(pathToFileURL(emittedPath).href);
let cases = 0;
async function observe(module, name, mode, closeMode) {
  const log = [];
  const user = new WeakSet();
  const error = message => {
    const value = new RangeError(message);
    user.add(value);
    return value;
  };
  const failure = {
    get message() {
      log.push('message:get');
      if (mode === 'message-throws') throw error('message failed');
      return {
        [Symbol.toPrimitive](hint) {
          log.push(['message:coerce', hint]);
          if (mode === 'coercion-throws') throw error('coercion failed');
          return 'detail';
        }
      };
    }
  };
  const thrown = mode === 'undefined' ? undefined : mode === 'null' ? null :
    mode === 'primitive' ? 13 : failure;
  const iterable = (values, label, close) => ({
    [Symbol.iterator]() {
      log.push(label + ':acquire');
      let index = 0;
      return {
        next() {
          log.push(label + ':next');
          if (closeMode === 'next-throws' && label === 'outer') throw error('next failed');
          return index < values.length ? {value: values[index++], done: false} : {done: true};
        },
        get return() {
          log.push(label + ':return:get');
          if (close && closeMode === 'getter-throws') throw error('return getter failed');
          if (close && closeMode === 'missing') return undefined;
          return function() {
            log.push([label + ':return:call', this !== undefined]);
            if (close && closeMode === 'call-throws') throw error('return call failed');
            return close && closeMode === 'primitive-result' ? 7 : {};
          };
        }
      };
    }
  });
  let rows;
  if (name === 'pattern') {
    rows = [iterable([1, 2], 'inner0', true), iterable([3, 4], 'inner1', true)];
  } else if (name === 'asyncPattern') {
    rows = iterable([1, 2], 'inner', true);
  } else {
    rows = iterable([1, 2], 'outer', true);
  }
  const visit = value => {
    log.push(['visit', value]);
    if (mode === 'continue') return false;
    if (mode === 'break') return true;
    if (mode === 'reject' && name.startsWith('async')) return Promise.reject(thrown);
    throw thrown;
  };
  const report = message => log.push(['report', message]);
  let outcome;
  try {
    const value = await module[name](rows, visit, report);
    outcome = ['return', value === undefined ? 'undefined' : value];
  } catch (caught) {
    if (caught === undefined || caught === null || typeof caught !== 'object')
      outcome = ['throw-value', caught === undefined ? 'undefined' : caught];
    else outcome = ['throw', caught.constructor.name, user.has(caught) ? caught.message : null];
  }
  return {log, outcome};
}
for (const name of ['sync', 'pattern', 'asyncBody', 'asyncPattern']) {
  for (const mode of ['continue', 'break', 'object', 'undefined', 'null', 'primitive',
                       'message-throws', 'coercion-throws', 'reject']) {
    for (const close of ['object', 'missing', 'getter-throws', 'call-throws', 'primitive-result', 'next-throws']) {
      assert.deepEqual(await observe(emitted, name, mode, close),
                       await observe(original, name, mode, close), `${name}/${mode}/${close}`);
      cases++;
    }
  }
}
console.log(JSON.stringify({cases}));
