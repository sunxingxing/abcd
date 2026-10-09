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
let executions=0,mutations=3;
for(const row of rows) {
  parseJavaScript(row.source);
  if(row.inert_control) {
    const conditions=['flag','produce(1)','+flag','state'];
    const condition=conditions[row.form]??({16:'flag&&produce(1)',17:'flag?produce(1):produce(2)',18:'(produce(1),+flag)',19:'flag=produce(1)',20:'flag.state',21:'flag.state()',22:'produce(1)',23:'produce(1)',24:'(yield produce(1))',25:'(await produce(1))'}[row.form]??'flag');
    const consequents=['flag;','flag;','flag;','flag;','let state=flag;',
      'return 1;','while(true){break;}','try{}finally{produce(2);}',
      'debugger;','throw flag;','state=flag;','while(true){}',
      'while(true){if(flag)continue;else break;}',
      'while(true){while(true){if(flag)break;else break;}break;}',
      'while(true){produce(2);break;}','while(true){try{break;}finally{flag;}}',
      'flag;','flag;','flag;','flag;','flag;','flag;','produce(2);','flag;','flag;','flag;'];
    const alternative=row.form===10?'state=flag;':'flag;';
    const control='if('+condition+'){'+consequents[row.form]+'}else{'+alternative+'}';
    const controls=(row.form===23?'try{'+control+'}catch(state){return flag;}':control).repeat(row.count+1);
    const body=(row.form===10?'let state=flag;':'')+controls+(row.form===3?'let state=flag;':'')+'return '+(row.form===10?'state':'flag')+';';
    if(row.form===24||row.form===25) {
      const actual=Function('return ('+row.source+');')();
      const root=parseJavaScript(row.source);
      const walk=node=>{if(!node||typeof node!=='object')return;assert.notEqual(node.type,'IfStatement');for(const value of Object.values(node))if(Array.isArray(value))value.forEach(walk);else walk(value);};walk(root);
      if(row.form===24) {
        const expected=Function('return function*(produce,flag){"use strict";'+body+'};')();
        const run=(fn,flag,mode,fault)=>{
          const trace=[],steps=[];let thrown;
          const iterator=fn(i=>{trace.push(i);if(fault)throw failure;return values[i];},flag);
          for(let i=0;i<row.count+3;++i) {
            try {const result=i===1&&mode==='return'?iterator.return(values[3]):i===1&&mode==='throw'?iterator.throw(failure):iterator.next(i%2===0);steps.push(result);if(result.done)break;}
            catch(error){thrown=error;break;}
          }
          return {trace,steps,thrown};
        };
        for(const flag of [false,true,values[0]])for(const mode of ['next','return','throw'])for(const fault of [false,true]){assert.deepEqual(run(actual,flag,mode,fault),run(expected,flag,mode,fault));++executions;}
        if(!row.count){const bad=function*(produce,flag){produce(1);return flag;};assert.notDeepEqual(run(bad,true,'next',false),run(expected,true,'next',false));++mutations;}
      }else {
        const expected=Function('return async function(produce,flag){"use strict";'+body+'};')();
        const run=async(fn,flag,fault)=>{
          const trace=[];let value,thrown;
          const produce=i=>{trace.push(i);return {then(resolve,reject){trace.push('then');queueMicrotask(()=>{trace.push('resume');fault?reject(failure):resolve(values[i]);});}};};
          try{value=await fn(produce,flag);}catch(error){thrown=error;}
          return {trace,value,thrown};
        };
        for(const flag of [false,true,values[0]])for(const fault of [false,true]){assert.deepEqual(await run(actual,flag,fault),await run(expected,flag,fault));++executions;}
        if(!row.count){const bad=async(produce,flag)=>{produce(1);return flag;};assert.notDeepEqual(await run(bad,true,false),await run(expected,true,false));++mutations;}
      }
      continue;
    }
    const expected=Function('produce','flag','"use strict";'+body);
    const actual=Function('return ('+row.source+');')();
    if([1,2,3,16,17,18,19,20,21,23].includes(row.form)) {
      const root=parseJavaScript(row.source);
      const walk=node=>{if(!node||typeof node!=='object')return;assert.notEqual(node.type,'IfStatement');for(const value of Object.values(node))if(Array.isArray(value))value.forEach(walk);else walk(value);};walk(root);
    }
    const run=(fn,input,fault)=>{
      const trace=[];const flag=input==='object'?{valueOf(){trace.push('coerce');if(fault==='coerce')throw failure;return 7;},get state(){trace.push('get');if(fault==='get')throw failure;return function(){trace.push(['receiver',this===flag]);if(fault==='method')throw failure;return 7;};}}:input;
      let value,thrown;
      const produce=i=>{trace.push(i);if(fault==='call'||fault==='second'&&i===2)throw failure;return values[i];};
      try{value=[11,12].includes(row.form)?vm.runInNewContext('('+fn.toString()+')(produce,flag)',{produce,flag},{timeout:10}):fn(produce,flag);if(value===flag&&input==='object')value='object';}
      catch(error){thrown=error.code==='ERR_SCRIPT_EXECUTION_TIMEOUT'?'nontermination':error===flag&&input==='object'?'object':error instanceof ReferenceError?'tdz':error instanceof TypeError?'type':error;}
      return {trace,value,thrown};
    };
    for(const input of [undefined,null,false,true,0,-0,7n,Symbol.for('control'),'object'])for(const fault of (row.form===20||row.form===21?['none','call','coerce','get','method']:row.form===22?['none','call','coerce','second']:['none','call','coerce'])) {
      assert.deepEqual(run(actual,input,fault),run(expected,input,fault));++executions;
    }
    if(!row.count&&[1,2,3,5,7,9,11,12,14,16,17,18,19,20,21,22,23].includes(row.form)) {
      const bad=Function('produce','flag','return flag;');
      assert.notDeepEqual(run(bad,'object','none'),run(expected,'object','none'));++mutations;
    }
    if(!row.count&&row.form===21) {
      const wrongReceiver=Function("produce","flag","(0,flag.state)();return flag;");
      assert.notDeepEqual(run(wrongReceiver,"object","none"),run(expected,"object","none"));++mutations;
    }
    continue;
  }
  if(row.inert_statement) {
    const expressions=['flag','state','state','+flag','produce(1)','0','flag?produce:flag','state'];
    const declaration=row.form===7?'let state;':'let state=flag;';
    const statements=(expressions[row.form]+';').repeat(row.count+1);
    const expected=Function('produce','flag','"use strict";'+
      (row.form===2?statements+declaration:declaration+statements)+'return flag;');
    const actual=Function('return ('+row.source+');')();
    const run=(fn,input,fault)=>{
      const trace=[];const flag=input==='object'?{valueOf(){trace.push('coerce');if(fault==='coerce')throw failure;return 7;}}:input;
      let value,thrown;
      try{value=fn(i=>{trace.push(i);if(fault==='call')throw failure;return values[i];},flag);if(value===flag&&input==='object')value='object';}
      catch(error){thrown=error instanceof ReferenceError?'tdz':error instanceof TypeError?'type':error;}
      return {trace,value,thrown};
    };
    for(const input of [undefined,null,false,true,0,-0,7n,Symbol.for('inert'),'object'])
      for(const fault of ['none','call','coerce']) {
        assert.deepEqual(run(actual,input,fault),run(expected,input,fault));++executions;
      }
    if(!row.count&&[2,3,4].includes(row.form)) {
      const bad=Function('produce','flag','"use strict";'+declaration+'return flag;');
      assert.notDeepEqual(run(bad,'object','none'),run(expected,'object','none'));++mutations;
    }
    continue;
  }
  if(row.discarded_result) {
    let expression='(produce(1)'+(','+(row.form===2?'+state':'state')).repeat(row.count+1)+')';
    if(row.form===3)expression='flag?'+expression+':produce(2)';
    if(row.form===4)expression=expression+'||produce(2)';
    if(row.form===5)expression='flag||'+expression;
    const expected=Function('produce','flag','"use strict";let state=flag;'+
      (row.form===1?'return '+expression+';':expression+';return flag;'));
    const actual=Function('return ('+row.source+');')();
    const run=(fn,flag,fail)=>{const result=observe(fn,flag,fail);if(result.thrown instanceof TypeError)result.thrown='type';return result;};
    for(const flag of [undefined,null,false,true,0,-0,7n,Symbol.for('result'),values[0]])for(const fail of [-1,1,2]) {
      assert.deepEqual(run(actual,flag,fail),run(expected,flag,fail));++executions;
    }
    const coercion=(fn,fault)=>{
      const trace=[];const flag={valueOf(){trace.push('coerce');if(fault)throw failure;return 3;}};
      let value,thrown;try {value=fn(i=>{trace.push(i);return values[i];},flag);if(value===flag)value='flag';}catch(error){thrown=error;}
      return {trace,value,thrown};
    };
    for(const fault of [false,true]){assert.deepEqual(coercion(actual,fault),coercion(expected,fault));++executions;}
    if(!row.count) {
      assert.notDeepEqual(run(Function('produce','flag','return flag;'),false,-1),run(expected,false,-1));++mutations;
      if(row.form===1){assert.notDeepEqual(run(Function('produce','flag','return produce(1);'),false,-1),run(expected,false,-1));++mutations;}
      if(row.form===2){assert.notDeepEqual(coercion(Function('produce','flag','produce(1);return flag;'),false),coercion(expected,false));++mutations;}
      if(row.form===4){assert.notDeepEqual(run(Function('produce','flag','produce(1)||produce(2);return flag;'),false,-1),run(expected,false,-1));++mutations;}
    }
    continue;
  }
  if(row.scoped_copy) {
    const actual=Function('return ('+row.source+');')();
    const alternative=row.agreeing?'second':'state';
    const expected=Function('produce','flag','"use strict";let state=produce,second=flag;{let selected=state?second:'+alternative+';let produce=1;let flag=2;return selected;}');
    const run=(fn,produce,flag)=>{
      let value,thrown;try{value=fn(produce,flag);}catch(error){thrown=error instanceof ReferenceError?'tdz':error;}
      return {value,thrown};
    };
    const inputs=[undefined,null,false,true,0,-0,7n,Symbol.for('scope'),values[0]];
    for(const produce of inputs)for(const flag of inputs) {
      assert.deepEqual(run(actual,produce,flag),run(expected,produce,flag));++executions;
    }
    for(const expression of ['produce?second:'+alternative,'state?flag:'+alternative]) {
      const bad=Function('produce','flag','"use strict";let state=produce,second=flag;{let selected='+expression+';let produce=1;let flag=2;return selected;}');
      assert.notDeepEqual(run(bad,values[0],values[1]),run(expected,values[0],values[1]));++mutations;
    }
    continue;
  }
  if(row.copy) {
    let rhs='(produce(1),'+(row.form===7?'origin':'flag')+')';
    if(row.form===1||row.form===5)rhs='flag?'+rhs+':'+(row.form===5?'produce(2)':'(produce(2),flag)');
    if(row.form===2)rhs='flag||'+rhs;
    rhs='('+('produce(0),'.repeat(row.count))+rhs+')';
    const assignment='state='+rhs+';';
    const body='let state;'+(row.form===7?'const origin=flag;':'')+(row.form===4?'produce(state);':'')+
      (row.form===6?'try{'+assignment+'}catch(error){return state;}':assignment)+
      (row.form===3?'flag=produce(3);':'')+'produce(state);return state;';
    const actual=Function('return ('+row.source+');')();
    const expected=Function('produce','flag','"use strict";'+body);
    for(const flag of [undefined,null,false,true,0,-0,7n,Symbol.for('copy'),values[0]])
      for(const fail of [-1,0,1,2,3]) {
        assert.deepEqual(observe(actual,flag,fail),observe(expected,flag,fail));++executions;
      }
    if(!row.count) {
      const bad=Function('produce','flag','"use strict";return flag;');
      assert.notDeepEqual(observe(bad,false,-1),observe(expected,false,-1));++mutations;
    }
    continue;
  }
  if(row.operands) {
    const actual=Function('return ('+row.source+');')();
    const left=row.form?'+state':'state';
    const expected=Function('produce','flag','"use strict";let state=flag;state=produce(1);return ('+
      (left+',').repeat(row.count+1)+'flag);');
    const run=(fn,flag,input,fault)=>{
      const trace=[];let value,thrown;
      try {value=fn(i=>{trace.push(['produce',i]);if(fault==='produce')throw failure;
        return input==='object'?{valueOf(){trace.push(['coerce']);if(fault==='coerce')throw failure;return 7;}}:input;},flag);}
      catch(error){thrown=error instanceof TypeError?'type':error;}
      return {trace,value,thrown};
    };
    for(const flag of [false,true])for(const input of [7,-0,1n,Symbol.for('operand'),'object'])
      for(const fault of ['none','produce','coerce']) {
        assert.deepEqual(run(actual,flag,input,fault),run(expected,flag,input,fault));++executions;
      }
    if(!row.count) {
      const bad=Function('produce','flag','"use strict";'+(row.form?'produce(1);':'')+'return flag;');
      assert.notDeepEqual(run(bad,true,'object','none'),run(expected,true,'object','none'));++mutations;
    }
    continue;
  }
  if(row.initialization) {
    const early=row.form?'state=produce(2);':'state;';
    const tail='produce(0);'.repeat(row.count)+'let state=produce(1);state;return state;';
    const actual=Function('return ('+row.source+');')();
    const expected=Function('produce','flag','"use strict";if(flag){'+early+'}'+tail);
    const run=(fn,flag,fail)=>{const result=observe(fn,flag,fail);if(result.thrown instanceof ReferenceError)result.thrown='tdz';return result;};
    for(const flag of [false,true])for(const fail of [-1,0,1,2,3]) {
      assert.deepEqual(run(actual,flag,fail),run(expected,flag,fail));++executions;
    }
    if(!row.count) {
      const bad=Function('produce','flag','"use strict";'+tail);
      assert.notDeepEqual(run(bad,true,-1),run(expected,true,-1));++mutations;
    }
    continue;
  }
  if(row.binding) {
    const actual=Function('return ('+row.source+');')();
    const tails=['return flag;','produce(state=produce(1));return flag;',
      'produce(state=produce(1));return flag;','++state;return state;',
      '{let state;produce(1);}return state;','return flag;'];
    const expected=Function('produce','flag','"use strict";let state;'+
      (row.form===2?'produce(state);':'')+'produce(0);'.repeat(row.count)+tails[row.form]);
    for(const flag of [false,true])for(const fail of [-1,0,1,2,3]) {
      assert.deepEqual(observe(actual,flag,fail),observe(expected,flag,fail));++executions;
    }
    continue;
  }
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
assert.notDeepEqual(observe(Function('produce','flag','"use strict";return ++state;'),false,-1),
    observe(Function('produce','flag','"use strict";let state;return ++state;'),false,-1));
console.log(`${rows.length} sources, ${executions} execution comparisons, ${mutations} adverse mutants`);
