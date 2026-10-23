import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const [original, emitted] = process.argv.slice(2);
const load = p => import('data:text/javascript;base64,'+Buffer.from(readFileSync(p,'utf8')).toString('base64'));
const a=await load(original), b=await load(emitted);
let cases=0;
for(const kind of ['string','number','negative-zero','symbol','object','throw-key','first','last']) {
  const run = m => {
    const trace=[]; let setter=0;
    const key = kind==='number'?7:kind==='negative-zero'?-0:kind==='symbol'?Symbol('key'):
      ['object','throw-key'].includes(kind)?{[Symbol.toPrimitive](hint){trace.push(['key',hint]);if(kind==='throw-key')throw new RangeError('key');return 'slot';}}:'slot';
    const input = new Proxy({}, {get(_,p){trace.push(String(p));if(p===kind)throw new TypeError(kind);return String(p);}});
    Object.defineProperty(Object.prototype,'slot',{set(){setter++;},configurable:true});
    try {
      const value=m.make(key,input);const stored=Reflect.ownKeys(value).map(k=>{
        const d=Object.getOwnPropertyDescriptor(value,k);return [typeof k==='symbol'?'symbol':k,typeof d.value==='function'?[d.value(),d.value.name,d.value.length]:d.value,d.writable,d.enumerable,d.configurable];
      });
      const arrow=m.arrow?.('arrow');return {stored,trace,setter,arrow:arrow&&[arrow.arrow(),arrow.arrow.name]};
    } catch(e) {return {error:[e.name,e.message],trace,setter};}
    finally {delete Object.prototype.slot;}
  };
  assert.deepEqual(run(b),run(a),kind);cases++;
}
console.log(JSON.stringify({cases,exclusions:['ark-numeric-computed-function-name']}));
