import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {pathToFileURL} from 'node:url';

const original = await import('data:text/javascript;base64,' + Buffer.from(await readFile(process.argv[2])).toString('base64'));
const emitted = await import(pathToFileURL(process.argv[3]));
const kind = process.argv[4];
let cases = 0;
const scenarios = [
  {}, {empty:true}, {object:true}, {fail:0}, {fail:1}, {fail:2},
  {close:true}, {reject:1}, {reject:2}, {null:true}, {object:true,reject:1},
];
for (const name of Object.keys(original)) {
 for (const options of scenarios) {
  for (const action of (kind.includes('generators') ? ['next','return','throw'] : ['next'])) {
   const run = async module => {
    const log = [], token = {}, failure = new Error('user failure'), terminal = {};
    const encode = value => {
      if (value === token) return 'input identity';
      if (value === failure) return 'error identity';
      if (value === terminal) return 'completion identity';
      if (value === undefined) return 'undefined';
      if (value instanceof Error) return ['error',value.constructor.name];
      if (Array.isArray(value)) return value.map(encode);
      if (value && typeof value === 'object') return Object.fromEntries(Object.entries(value).map(([k,v])=>[k,encode(v)]));
      return value;
    };
    const iterable = () => options.null ? null : {
      get [Symbol.iterator]() {
        log.push('iterator getter');
        return function () {
          log.push('iterator call'); let index = 0;
          const values = options.empty ? [] : [options.object ? token : 3, 7, 9];
          return {
            next() {
              const i = index++; log.push(['next',i]);
              if (options.fail === i) throw failure;
              return {get done(){log.push(['done',i]);return i >= values.length;},
                      get value(){log.push(['value',i]);return values[i];}};
            },
            return() { log.push('close'); if (options.close) throw failure; return {}; },
          };
        };
      },
    };
    let waits = 0;
    const wait = async value => {
      log.push(['wait',++waits,encode(value)]);
      if (options.reject === waits) throw failure;
      return value;
    };
    try {
      let result;
      if (kind.includes('generators')) {
        const iterator = module[name](iterable(),wait); const steps = [];
        for (let i = 0; i < 5; ++i) {
          const step = i === 1 && action === 'return' ? await iterator.return(terminal)
            : i === 1 && action === 'throw' ? await iterator.throw(failure)
            : await iterator.next(token);
          steps.push(encode(step));
          if (step.done) break;
        }
        result = steps;
      } else if (kind === 'concurrent') result = await module[name](iterable());
      else if (name === 'multiple') result = await module[name](iterable(),iterable(),wait);
      else result = await module[name](iterable(),wait);
      return [['ok',encode(result)],log];
    } catch (error) {
      return [['throw',error === failure ? ['Error','user failure','same identity'] : [error.constructor.name]],log];
    }
   };
   assert.deepEqual(await run(emitted),await run(original),`${kind}.${name} ${JSON.stringify(options)} ${action}`);
   ++cases;
  }
 }
}
console.log(JSON.stringify({cases}));
