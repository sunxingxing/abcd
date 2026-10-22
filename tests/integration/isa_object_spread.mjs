import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
const load = async p => import('data:text/javascript;base64,'+Buffer.from(await readFile(p,'utf8')).toString('base64'));
const [old, now] = await Promise.all(process.argv.slice(2).map(load));
let cases=0;
if ('f' in old) {
  for(const value of [null,undefined,3,'abc',{x:1},Object.create({p:2})]) assert.deepEqual(now.f(value),old.f(value));
  const symbol=Symbol.for('x');assert.deepEqual(now.f({[symbol]:8}),old.f({[symbol]:8}));
  console.log(JSON.stringify({cases:7}));process.exit(0);
}

function exercise(m) {
  const trace=[];
  const symbol=Symbol.for('spread');
  const inputs=[null,undefined,0,false,'abc',{}, {a:3,b:4}, {[symbol]:5},Object.create({inherited:1})];
  const results=[];
  for(const x of inputs) for(const name of ['copy','empty','nested','overwrite']) {
    const object=m[name](x);
    results.push([name,Reflect.ownKeys(object).map(k=>[String(k),Object.getOwnPropertyDescriptor(object,k)] )]);
  }
  results.push(m.multiple({a:1,fixed:2},{b:3,fixed:4}));
  const source={get p(){trace.push('get:p');return 8;},get q(){trace.push('get:q');return 9;}};
  results.push(m.ordered(()=>{trace.push('first');return 1;},()=>{trace.push('source');return source;},()=>{trace.push('last');return 2;}));
  for(const failure of ['first','source','get','last']) {
    try {m.ordered(()=>{trace.push('first');if(failure==='first')throw new RangeError('first');return 1;},()=>{trace.push('source');if(failure==='source')throw new TypeError('source');return {get p(){trace.push('get');if(failure==='get')throw new SyntaxError('get');return 3;}};},()=>{trace.push('last');if(failure==='last')throw new ReferenceError('last');return 2;});}
    catch(e) {results.push([e.constructor.name,e.message]);}
  }
  // Native spread must define own data properties, never invoke an inherited setter.
  Object.defineProperty(Object.prototype,'trap',{configurable:true,set(){trace.push('BAD:setter');}});
  try {const out=m.empty({trap:11});assert.equal(Object.getOwnPropertyDescriptor(out,'trap').value,11);}
  finally {delete Object.prototype.trap;}
  assert(!trace.includes('BAD:setter'));
  assert.equal(m.discarded({get discarded(){trace.push('discarded:get');return 3;}}),4);
  assert(trace.includes('discarded:get'));
  return {results,trace};
}
assert.deepEqual(exercise(now),exercise(old));cases+=44;
const ordinary=now.copy({q:4});
assert.deepEqual(Object.getOwnPropertyDescriptor(ordinary,'q'),{value:4,writable:true,enumerable:true,configurable:true});cases++;
console.log(JSON.stringify({cases}));
