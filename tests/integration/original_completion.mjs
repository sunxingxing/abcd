import assert from 'node:assert/strict';
import {Session} from 'node:inspector';
import {generatedSource} from '../support/source/generated_fixture.mjs';
import {parseJavaScript} from './class_reference.mjs';

const rows=generatedSource(process.argv[2],['--source'],{encoding:'utf8',maxBuffer:16_000_000}).trim().split('\n').map(JSON.parse);
const session=new Session();session.connect();let active;
session.on('Debugger.paused',()=>{active?.push('debugger');session.post('Debugger.resume');});
session.post('Debugger.enable');session.post('Debugger.setPauseOnExceptions',{state:'none'});
const failure=Symbol('input failure');
const values=[undefined,null,false,true,0,-0,1,7,NaN,Infinity,0n,Symbol('input'),{},[]];
function outcome(fn,value,throws=false) {
  const trace=[];active=trace;
  const input=()=>{trace.push('input');if(throws)throw failure;return value;};
  try {
    const result=fn(input,Symbol('scratch'));
    // Arrays must be fresh, not the callback's input or a reused global object.
    return {trace,result:Array.isArray(result)?{kind:'array',length:result.length,input:result===value}:result};
  } catch(error) {return {trace,error:error===failure?failure:error.name};}
  finally {active=undefined;}
}
function nodes(root) {
  const result=[],pending=[root];
  while(pending.length) {
    const node=pending.pop();if(!node||typeof node!=='object')continue;
    if(node.type)result.push(node);
    for(const value of Object.values(node))if(value&&typeof value==='object')
      Array.isArray(value)?pending.push(...value):pending.push(value);
  }
  return result;
}
let comparisons=0,mutants=0;
try {
  for(const row of rows) {
    const source='('+row.source+')',actual=Function('return '+source)();
    const reference=row.positive?function(input){debugger;return input()===0?7:[];}:
      function(input){debugger;return input()===0?[]:7;};
    for(const value of values)for(const throws of [false,true]) {
      const expected=outcome(reference,value,throws),got=outcome(actual,value,throws);
      assert.deepEqual(got,expected);assert.deepEqual(got.trace,['debugger','input']);++comparisons;
    }
    const object={[Symbol.toPrimitive](){throw new Error('strict comparison coerced an object');}};
    assert.deepEqual(outcome(actual,object),outcome(reference,object));++comparisons;
    const arrayInput=row.positive?1:0;
    assert.notEqual(actual(()=>arrayInput),actual(()=>arrayInput),'each executed allocation creates a new array');
    const ast=nodes(parseJavaScript(source)),debuggerNode=ast.find(node=>node.type==='DebuggerStatement');
    assert.ok(debuggerNode);
    const skipped=Function('return '+source.slice(0,debuggerNode.start)+';'+source.slice(debuggerNode.end))();
    assert.notDeepEqual(outcome(skipped,0),outcome(reference,0));++mutants;
    const equality=ast.find(node=>node.type==='BinaryExpression'&&node.operator==='===');assert.ok(equality);
    const wrong=Function('return '+source.slice(0,equality.start)+'false'+source.slice(equality.end))();
    assert.notDeepEqual(outcome(wrong,0),outcome(reference,0));++mutants;
  }
  assert.equal(rows.length,208);
} finally {session.post('Debugger.disable');session.disconnect();}
console.log(`${rows.length} decoded completion sources / ${comparisons} execution and debugger-trace comparisons / ${mutants} adverse mutants`);
