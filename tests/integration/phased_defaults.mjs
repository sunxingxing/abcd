import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {pathToFileURL} from 'node:url';
const original = await import('data:text/javascript;base64,' + Buffer.from(await readFile(process.argv[2])).toString('base64'));
const emitted = await import(pathToFileURL(process.argv[3]));
const generators = process.argv[4] === 'generators';
let cases = 0;
const inputs = [[], [undefined], [null], [false], [0], [19], ['text'], [{token:1}], [undefined, true, 23, 5], [0, null, false, 0]];
const encode = (value, failure, token, input) => input && typeof input === 'object' && value === input ? ['argument', 'identity'] : value === failure ? ['user error', 'identity'] : value === token ? ['token', 'identity'] :
  value === undefined ? ['undefined'] : Array.isArray(value) ? value.map(x=>encode(x,failure,token,input)) : value;
const functions = module => {
 const list = Object.entries(module).filter(([name]) => name !== 'Methods').map(([name, fn]) => [name, fn]);
 if(module.Methods) list.push(['method',new module.Methods().run],['static',module.Methods.run]);
 return list;
};
const old = functions(original), fresh = functions(emitted);
assert.deepEqual(fresh.map(([n,f])=>[n,f.length]),old.map(([n,f])=>[n,f.length]));
for (const [name, fn] of old) {
 const recovered = fresh.find(([n])=>n === name)[1];
 assert.equal(fn.length, 2, name + '.length'); ++cases;
 for(const tail of inputs) for(const reject of [false,true]) {
  const run = async f => {
   const failure = new Error('user rejection'), token = {token:1}, log = [];
   const wait = {then(resolve, no){log.push('await'); if(reject)no(failure);else resolve(token);}};
   const args = [generators ? token : wait, token, ...tail];
   try {
    let value;
    if(generators) {
     const iterator = f(...args); value = [];
     log.push('created');
     for(let i=0;i<6;++i){const step=iterator.next(token);value.push([encode(step.value,failure,token,tail[0]),step.done]);if(step.done)break;}
    } else value = encode(await f(...args),failure,token,tail[0]);
    return [['return', value], log];
   } catch(error) {return [['throw',error === failure ? ['Error',error.message,'identity'] : [error.constructor.name]],log];}
  };
  assert.deepEqual(await run(recovered),await run(fn),`${name} ${JSON.stringify(tail)} reject=${reject}`);++cases;
 }
}
console.log(JSON.stringify({cases}));
