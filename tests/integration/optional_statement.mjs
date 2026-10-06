import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
const rows=execFileSync(process.argv[2],['--source'],{encoding:'utf8',maxBuffer:8*1024*1024})
  .trim().split('\n').map(line=>JSON.parse(line));
assert.equal(rows.length,80);
function observe(fn,row,kind,middle,point,failure) {
  const trace=[], marker={}, objects=[{}, {}, {}];let sources=0;
  const event=(...parts)=>{trace.push(parts);if(trace.length===point) throw failure;};
  const last=row.computed ? row.chained ? 2 : 1 : row.chained ? 1 : 0;
  const leaf=function(value) {
    assert.equal(this,row.value ? undefined : objects[last]);assert.equal(value,marker);
    event('call');return {get then(){assert.fail('discarded call result must not be assimilated');}};
  };
  for(let i=0;i<=last;++i) Object.defineProperty(objects[i],'field',{get() {
    assert.equal(this,objects[i]);event('get',i);
    Object.defineProperty(this,'field',{get(){assert.fail('callee producer replayed');}});
    return i===0 && middle!=='object' ? middle : i<last ? objects[i+1] : leaf;
  },configurable:true});
  const value=kind==='object' ? row.value ? leaf : objects[0] : kind;
  const key=Object.assign(function() {assert.equal(this,undefined);event('argument');return marker;},
    {[Symbol.toPrimitive](hint){assert.equal(hint,'string');event('key');return 'field';}});
  const source=()=>{assert.equal(++sources,1);event('source');return value;};
  try {assert.equal(fn(value,key,source),undefined);return {trace,sources,outcome:'void'};}
  catch(error) {
    if(error instanceof assert.AssertionError) throw error;
    if(Object.is(error,failure)) return {trace,sources,outcome:'raw'};
    assert.equal(error.name,'TypeError');return {trace,sources,outcome:'TypeError'};
  }
}
let cases=0,mutations=0;
for(const row of rows) {
  const actual=Function(`"use strict";return (${row.source});`)();
  const callee=row.value ? 'value' : `value.field${row.chained ? '.field' : ''}${row.computed ? '[key]' : ''}`;
  const reference=Function(`"use strict";return function(value,key,source) {
    ${row.formed ? 'value=source();' : ''}
    if(value!==null && value!==undefined) ${callee}(key());
  };`)();
  assert.equal((row.source.match(/\?\./g)??[]).length,1);assert.doesNotMatch(row.source,/\bif\s*\(/);
  for(const kind of [undefined,null,false,0,-0,NaN,'',1n,Symbol('value'),'object'])
    for(const middle of ['object',undefined,null,false]) {
      const baseline=observe(reference,row,kind,middle,0,{});
      for(let point=0;point<=baseline.trace.length;++point) for(const failure of [undefined,null,0,Symbol('fault'),{}]) {
        const observed=observe(actual,row,kind,middle,point,failure);
        assert.deepEqual(observed,observe(reference,row,kind,middle,point,failure));
        if(point) assert.equal(observed.outcome,'raw');++cases;
      }
    }
  if(row.guards===1 && !row.formed && !row.computed && row.chained && !row.reverse) {
    for(const changed of [row.source.replace('value?.field.field','value?.field?.field'),
      row.source.replace('value?.field.field','(value?.field).field')]) {
      assert.notEqual(changed,row.source);const mutant=Function(`"use strict";return (${changed});`)();
      const nil=changed.includes('field?.') ? 'object' : null;
      assert.notDeepEqual(observe(mutant,row,nil,null,0,{}),observe(reference,row,nil,null,0,{}));++mutations;
    }
  }
}
assert.equal(mutations,2);console.log(JSON.stringify({fixtures:rows.length,cases,mutations}));
