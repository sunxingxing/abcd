import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';
import {parseJavaScript} from './class_reference.mjs';
const rows=execFileSync(process.argv[2],['--emit'],{encoding:'utf8',maxBuffer:16_000_000}).trim().split('\n').map(JSON.parse);
const programs=[
  'state=produce(1);state=produce(2);return state;',
  'state=produce(1);try{state=produce(2);}catch(error){return state;}return state;',
  'state=produce(1);if(flag)state=produce(2);return state;',
  'state=produce(1);if(flag)state=produce(2);else state=produce(3);return state;',
  'return state=produce(1);',
  'state=produce(1);state=produce(2);return state;',
  'state=produce(1);state=produce(2);return state;',
];
const values=[{tag:0},{tag:1},{tag:2},{tag:3}],failure=Symbol('throw');
function observe(fn,flag,fail) {
  const trace=[];let value,thrown;
  try {value=fn(i=>{trace.push(i);if(i===fail)throw failure;return values[i];},flag);}
  catch(error){thrown=error;}
  return {trace,value,thrown};
}
let executions=0;
for(const row of rows) {
  parseJavaScript(row.source);
  if(row.chain) {
    for(const input of [undefined,null,false,0,-0,7n,Symbol('input'),values[0]])for(const flag of [false,true]) {
      const got=vm.runInNewContext('('+row.source+')(input,flag)',{input,flag},{timeout:1000});
      // Independent array transport: distinct initial values, then ordered
      // copies until the loop exits. No generated source enters the oracle.
      const slots=Array.from({length:row.size},(_,i)=>i);slots[row.size-1]=input;
      let running=true,condition=flag;
      while(running) {
        slots[0]=row.cycle?slots.at(-1):input;
        for(let i=1;i<slots.length;++i)slots[i]=slots[i-1];
        if(!row.cycle||condition)running=false;else condition=true;
      }
      assert.equal(got,row.observed?slots.at(-1):0);++executions;
    }
    continue;
  }
  const actual=Function('return ('+row.source+');')();
  const expected=Function('produce','flag','"use strict";'+(row.form>=5?'let state;':'let state=flag;')+
      (row.form===6?'produce(state);':'')+
      'produce(0);'.repeat(row.count)+programs[row.form]);
  for(const flag of [false,true])for(const fail of [-1,0,1,2,3]) {
    const got=observe(actual,flag,fail),want=observe(expected,flag,fail);
    assert.deepEqual(got,want);assert.equal(got.value,want.value);assert.equal(got.thrown,want.thrown);++executions;
  }
}
// Adverse mutants: deleting RHS evaluation or losing a value observed by catch
// must be distinguished by this same independent observer.
assert.notDeepEqual(observe(Function('produce','flag','return produce(2);'),true,-1),
    observe(Function('produce','flag',programs[0].replace('state=produce(1);','let state=produce(1);')),true,-1));
assert.notDeepEqual(observe(Function('produce','flag','let state;try{state=produce(2);}catch(e){return state;}'),true,2),
    observe(Function('produce','flag','let state;'+programs[1]),true,2));
console.log(`${rows.length} sources, ${executions} execution comparisons, 2 adverse mutants`);
