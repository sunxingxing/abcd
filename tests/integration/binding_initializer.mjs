import assert from 'node:assert/strict';
import {generatedSource as execFileSync} from '../support/source/generated_fixture.mjs';
import {parseJavaScript} from './class_reference.mjs';
import {editExpression} from './expression_edits.mjs';
const rows = execFileSync(process.argv[2], ['--emit'], {encoding:'utf8', maxBuffer:32_000_000}).trim().split('\n').map(JSON.parse);
const token = {}, second = {}, failure = Symbol('original throw'), tdzFailure=Symbol('TDZ');
function original(row) {
  if(row.decoded_cell&&row.field)return function(seed,report,flag,target,key){const saved=seed();target[key]=saved;if(!row.branch||flag!==false&&flag!==0)report(saved);report(saved);return saved;};
  if (row.decoded_loop) return function(again,seed,report,choice,stop) {
    while(again()) {const saved=seed();if(choice!==false && choice!==0) report(saved);report(saved);if(stop!==false && stop!==0)return saved;}
    return;
  };
  if (row.decoded) return function(seed, report, flag) {
    const saved = seed(); if (!row.branch || flag !== false && flag !== 0) report(saved); report(saved); return saved;
  };
  if (row.form === 11) return Function('return function(seed,report,again){let saved;while(again()){' +
    'saved=seed();' + 'report(saved);'.repeat(row.count) + '}return;};')();
  if (row.form === 12) return Function('return function(seed,report,again){let saved;let carried=true;while(carried){' +
    'saved=seed();if(report){saved=seed();}' + 'report(saved);'.repeat(row.count) + 'carried=again(saved);}return;};')();
  if (row.form === 13 || row.form === 14) return Function('return function(seed,report,again){let saved;while(again()){' +
    'let fence;saved=seed();' + 'report(saved);'.repeat(row.count) + '}return' + (row.form === 14 ? ' saved' : '') + ';};')();
  if (row.form >= 8 && row.form <= 10) {
    const body = 'saved=seed();' + 'report(saved);'.repeat(row.count) + 'return saved;';
    return Function('return function(seed,report){let saved;' +
      (row.form === 8 ? 'if(seed){' + body + '}return;' :
        'try{' + body + '}finally{report(' + (row.form === 10 ? 'saved' : '') + ');}') + '};')();
  }
  if(row.form>=22 && row.form<=24)return Function('return function(seed,report){let saved;'+(row.form===22?'saved;':row.form===23?'saved=0;':'report(saved);')+'{saved=seed();'+'report(saved);'.repeat(row.count)+'return saved;}}')();
  const value = row.form === 3 ? 'saved' : row.form === 5 ? 'await seed()' : row.form === 6 ? 'yield seed()' : 'seed()';
  return Function('return (' + (row.form === 5 ? 'async ' : '') + 'function' + (row.form === 6 ? '*' : '') + '(seed,report){let saved;' +
    (row.form === 2 ? 'report(saved);' : row.form === 15 ? 'saved;' : row.form === 16 ? '(saved,report());' : row.form === 17 ? '(report(saved),0);' : row.form === 18 ? 'saved=0;' : row.form === 19 ? 'saved=report();' : row.form === 20 ? '{saved=0;}' : '') + (row.form === 4 ? 'if(seed){saved=seed();}' : row.form === 21 ? 'let unused;unused=(saved='+value+');' : 'saved='+value+';') +
    'report(saved);'.repeat(row.count) + (row.form === 1 ? 'saved=seed();' : '') + 'return saved;})')();
}
async function observe(fn, row, value, failAt, reject = false, request = 'next', flag = true, iterations = 2, stop = false) {
  const trace = [], requests = []; let at = 0, seeds = 0, returned, thrown;
  const event = (kind, value) => {trace.push([kind, value]); if (++at === failAt) throw failure;};
  const seed = function() {
    assert.equal(this, undefined); const result = seeds++ ? second : value; event('seed', result);
    if (row.form === 5) {
      queueMicrotask(() => trace.push(['job'])); return reject ? Promise.reject(failure) : Promise.resolve(result);
    }
    return result;
  };
  const report = function(value) {assert.equal(this, undefined); event('report', value);};
  const storage={};let target;
  if(row.field)target=new Proxy(storage,{set(object,key,value,receiver){assert.equal(receiver,target);event('set',value);return Reflect.set(object,key,value,receiver);}});
  const key={[Symbol.toPrimitive](hint){assert.equal(hint,'string');event('key',hint);return 'saved';}};
  let checks = 0;
  const again = function(value) {assert.equal(this, undefined); event('again', row.form === 12 ? value : checks); return checks++ < iterations;};
  try {
    returned = row.decoded_loop ? fn(again,seed,report,flag,stop) : fn(seed, report, row.form >= 11 ? again : flag, target, key);
    if (row.form === 6) {
      const generator = returned; const first = generator.next(); requests.push(first.value);
      const end = request === 'throw' ? generator.throw(failure) : request === 'return' ? generator.return(second) : generator.next(value);
      assert.equal(end.done, true); returned = end.value;
    }
    if (row.form === 5) returned = await returned;
  } catch (error) {thrown = error; returned = undefined;}
  return {trace, requests, returned, thrown,...(row.field?{stored:storage.saved,hasStore:Object.hasOwn(storage,'saved')}:{})};
}
function compare(actual, expected) {
  if(Object.hasOwn(actual,'stored')){assert.ok(Object.is(actual.stored,expected.stored));assert.equal(actual.hasStore,expected.hasStore);}
  assert.ok(Object.is(actual.returned, expected.returned)); assert.ok(Object.is(actual.thrown, expected.thrown));
  assert.deepEqual(actual.requests, expected.requests); assert.equal(actual.trace.length, expected.trace.length);
  for (let i=0; i<actual.trace.length; ++i) {
    assert.equal(actual.trace[i][0], expected.trace[i][0]); assert.ok(Object.is(actual.trace[i][1], expected.trace[i][1]));
  }
}
function constantReference(row) {
  const value=row.constant===14?'{}':row.constant===15?'[]':row.constant===13?'-2':['void 0','null','false','true','42','-0','NaN','Infinity','-Infinity',JSON.stringify('Ā\n𝒳')][row.literal];
  const reports='report(saved);'.repeat(row.count);
  let body=(row.constant===2?'report(saved);':'')+(row.constant===4?'if(seed){saved='+value+';}':'saved='+(row.constant===6?'saved':value)+';')+reports;
  if(row.constant>=17&&row.constant<=19)body=(row.constant===19?'report(saved);':'')+'saved='+value+';saved='+(row.constant===18?'(report(saved),saved)':'saved')+';'+reports;
  if(row.constant===20)body='saved=saved;'+body;
  if(row.constant===3)body+='saved=seed();';
  if(row.constant===16)body='if(seed){saved='+value+';}else{return;}'+reports;
  if(row.constant===7)body='try{seed();saved='+value+';'+reports+'}finally{report(saved);}';
  if(row.constant===8 || row.constant===9)body='while(seed()){saved='+value+';'+reports+'}';
  if(row.constant===10 || row.constant===11)body='saved='+value+';'+(row.constant===10?'await':'yield')+' seed();'+reports;
  body+='return'+(row.constant===9?'':row.constant===12?' saved.toString()':row.constant===13?' saved ** 2':' saved')+';';
  return Function('return ('+(row.constant===10?'async function':row.constant===11?'function*':'function')+'(seed,report){'+(row.constant===21?'':'let saved;')+body+(row.constant===21?'let saved;':'')+'})')();
}
async function observeConstant(fn,row,value,fault,flag,iterations,request='next',reject=false) {
  const trace=[],requests=[];let at=0,checks=0,returned,thrown,allocation;
  const event=(kind,value)=>{trace.push([kind,value]);if(++at===fault)throw failure;};
  const seed=function(){assert.equal(this,undefined);const result=row.constant===8 || row.constant===9?checks++<iterations:value;event('seed',result);
    if(row.constant===10){queueMicrotask(()=>trace.push(['job']));return reject?Promise.reject(failure):Promise.resolve(result);}return result;};
  const report=function(value){assert.equal(this,undefined);
    if([14,15].includes(row.constant)){allocation??=value;assert.equal(value,allocation);value.visits=(value.visits??0)+1;event('report',value.visits);}
    else event('report',value);};
  const descriptor=Object.getOwnPropertyDescriptor(Number.prototype,'toString');
  if(row.constant===12)Object.defineProperty(Number.prototype,'toString',{configurable:true,get(){event('get',this);return function(){event('member',this);return typeof this+':'+String(this);};}});
  try {
    returned=fn([4,16].includes(row.constant)?flag:seed,report);
    if(row.constant===11){const generator=returned,first=generator.next();requests.push(first.value);const end=request==='throw'?generator.throw(failure):request==='return'?generator.return(second):generator.next(value);assert.equal(end.done,true);returned=end.value;}
    if(row.constant===10)returned=await returned;
    if([14,15].includes(row.constant)){assert.equal(returned,allocation??returned);assert.equal(Array.isArray(returned),row.constant===15);assert.equal(returned.visits??0,row.count);returned=token;}
  }catch(error){thrown=error;returned=undefined;}
  finally{if(row.constant===12)Object.defineProperty(Number.prototype,'toString',descriptor);}
  if(row.constant===21){assert.ok(thrown instanceof ReferenceError);thrown=failure;}
  return {trace,requests,returned,thrown};
}
let comparisons=0, mutations=0, folded=0;
function aliasReference(row) {
  const root=row.alias===9?'{}':row.alias===10?'[]':'seed()';
  const declarations=Array.from({length:row.count},(_,i)=>'let copy'+i+';').join('');
  const copies=Array.from({length:row.count},(_,i)=>{
    const name='copy'+i,source=i?'copy'+(i-1):'saved';
    return ([27,28].includes(row.alias)?name+'='+name+';':'')+name+'='+(row.alias===28?'(report('+source+'),'+source+')':source)+';';
  }).join('');
  const last=row.count?'copy'+(row.count-1):'saved';
  const reports=('report('+last+');').repeat(row.alias===15?0:row.count);
  let body=declarations+'let saved;'+(row.alias===26?'saved=saved;':'')+(row.alias===2&&row.count?'report('+last+');':'')+'saved='+root+';'+
    (row.alias===24?'saved=saved;':row.alias===25?'saved=(report(saved),saved);':'')+
    (row.alias===3?'if(flag){'+copies+'}':row.alias===21&&row.count?'if(flag){'+copies+'}else{return;}':copies)+
    (row.alias===7?'await seed();':row.alias===8?'yield seed();':'');
  let tail=reports+(row.alias===1?'saved=seed();':row.alias===11&&row.count?last+'=seed();':'')+'return '+last+(row.alias===13?'()':'')+';';
  if(row.alias===4)tail='{'+tail+'}';
  if(row.alias===5)tail='{let saved=123;'+tail+'}';
  if(row.alias===6)tail=reports+'{let saved=123;}return '+last+';';
  if(row.alias===20)tail='{'.repeat(row.count+1)+tail+'}'.repeat(row.count+1);
  if(row.alias===22||row.alias===23)tail=reports+(row.alias===23?'{let saved=123;':'')+'return typeof '+last+';'+(row.alias===23?'}':'');
  if(row.alias>=14&&row.alias<=16) {
    const first=row.count?'copy0=saved;':'';
    const rest=Array.from({length:Math.max(0,row.count-1)},(_,i)=>'copy'+(i+1)+'=copy'+i+';').join('');
    body=declarations+'let saved;saved='+root+';'+first;
    tail='{let saved=123;'+rest+reports+(row.alias===16&&row.count?last+'=seed();':'')+
      (row.alias===15?'':'return '+last+';')+'}'+(row.alias===15?'return;':'');
  }
  if(row.alias===17){body=declarations+'let saved;try{saved='+root+';'+copies+reports+'}finally{report('+last+');}';tail='return '+last+';';}
  if(row.alias===18||row.alias===19){body=declarations+'let saved;while(flag()){saved='+root+';'+copies+reports+'}';tail='return'+(row.alias===18?'':' '+last)+';';}
  if(row.alias===29){body=body.slice(declarations.length);tail+=declarations;}
  return Function('return ('+(row.alias===7?'async function':row.alias===8?'function*':'function')+'(seed,report,flag){'+body+tail+'})')();
}
function entryReference(row) {
  const caught=[2,3,7,11].includes(row.entry),source=caught?'caught':row.entry===4?'owner':row.entry===16||row.entry>=18?'state':'input';
  const last=row.count?'copy'+(row.count-1):source;
  const declarations=Array.from({length:row.count},(_,i)=>'let copy'+i+';').join('');
  const copies=Array.from({length:row.count},(_,i)=>'copy'+i+'='+(i?'copy'+(i-1):source)+';').join('');
  const reports=('report('+last+');').repeat(row.count);
  let body=(row.entry===11?'if(flag){'+copies+'}':copies)+
    (row.entry===13?'await other();':row.entry===14?'yield other();':row.entry===16?'(()=>{state=replacement;})();':'')+reports+
    (row.entry===1||row.entry===3?source+'=other;':'')+'return '+last+(row.entry===15?'()':'')+';';
  if(row.entry===10)body=copies+'{let input=123;'+reports+'return '+last+';}';
  if(caught)body='try{throw input;}catch(caught){'+(row.entry===7?copies+reports:body)+'}'+(row.entry===7?'finally{report('+last+');}return '+last+';':'');
  if(row.entry>=18) {
    const initial=row.entry===23?'': '='+([22,25,26,27].includes(row.entry)?(row.entry===26?'await ':row.entry===27?'yield ':'')+'other()':row.entry===28?'state':row.entry===29?'{}':'input');
    const declared=(row.entry===19?'const':'let')+' state'+initial+';';
    body=declared+copies+reports+(row.entry===20?'state=other;':'')+'return '+last+';';
    if(row.entry===21)body=copies+declared+reports+'return '+last+';';
    if(row.entry===24)body='{'+declared+copies+'}'+reports+'return '+last+';';
    if(row.entry===25)body='while(flag()){'+declared+copies+reports+'}return;';
    if(row.entry>=30)body='let state;'+(row.entry===32?'if(flag){state=input;}':'state=input;')+copies+reports+(row.entry===31?'state=other;':'')+'return '+last+';';
    if(row.entry===33)body='let state;while(flag()){state=other();'+copies+reports+'}return;';
  }
  const parameters=(row.entry===8?'input=42':'input')+',report,other,'+(row.entry===17?'...flag':'flag');
  const head=row.entry===9?'('+parameters+')=>':(row.entry===13||row.entry===26?'async function':row.entry===14||row.entry===27?'function*':'function')+(row.entry===4?' owner':'')+'('+parameters+')';
  return Function((row.strict?'"use strict";':'')+'return ('+head+'{'+declarations+(row.entry===16?'let state=input;let replacement=other;':'')+body+'})')();
}
async function observeEntry(fn,row,value,fault,flag=true,request='next',reject=false,iterations=2) {
  const trace=[],requests=[];let at=0,returned,thrown,didThrow=false,checks=0,seeds=0,allocation;
  const event=(kind,value)=>{trace.push([kind,value]);if(++at===fault)throw failure;};
  const callable=function(){assert.equal(this,undefined);event('callee',value);return value;};
  const report=function(value){assert.equal(this,undefined);if(row.entry===4){assert.equal(value,fn);value=token;}if(row.entry===15)value='callable';if(row.entry===29){allocation??=value;assert.equal(value,allocation);assert.equal(typeof value,'object');value='allocation';}event('report',value);};
  const pause=function(){assert.equal(this,undefined);const seed=row.entry===25||row.entry===33?(seeds++?second:value):second;event('pause',seed);if(row.entry===13||row.entry===26){queueMicrotask(()=>trace.push(['job']));return reject?Promise.reject(failure):Promise.resolve(seed);}return seed;};
  const again=function(){assert.equal(this,undefined);event('again',checks);return checks++<iterations;};
  try {
    returned=fn(row.entry===15?callable:value,report,[13,14,22,25,26,27,33].includes(row.entry)?pause:second,row.entry===25||row.entry===33?again:flag);
    if(row.entry===14||row.entry===27){const generator=returned,first=generator.next();requests.push(first.value);const end=request==='throw'?generator.throw(failure):request==='return'?generator.return(second):generator.next(value);assert.equal(end.done,true);returned=end.value;}
    if(row.entry===13||row.entry===26)returned=await returned;
    if(row.entry===4){assert.equal(returned,fn);returned=token;}
    if(row.entry===29){allocation??=returned;assert.equal(returned,allocation);assert.equal(Object.getPrototypeOf(returned),Object.prototype);returned='allocation';}
  }catch(error){returned=undefined;thrown=([21,28].includes(row.entry)&&error instanceof ReferenceError)?'tdz':error;didThrow=true;}
  return {trace,requests,returned,thrown,didThrow};
}
function entryCompare(actual,expected) {compare(actual,expected);assert.equal(actual.didThrow,expected.didThrow);}
function argumentMutation(source,row) {
  const ast=parseJavaScript('('+source+')'),body=ast.body[0].expression.body;
  // Test-only coupled-view probe. The native arguments spelling is outside the
  // current Tree AST's snapshot domain; neither this probe nor reference text
  // enters the engine. Insert the same action after copies in both programs.
  const returned=body.body.findLast(node=>node.type==='ReturnStatement');assert.ok(returned);
  const position=returned.start-1;
  const changed=source.slice(0,position)+'arguments[0]=other;'+source.slice(position);parseJavaScript('('+changed+')');
  return Function((row.strict?'"use strict";':'')+'return ('+changed+')')();
}
async function observeAlias(fn,row,value,fault,flag=true,request='next',reject=false,iterations=2) {
  const trace=[],requests=[];let at=0,seeds=0,allocation,returned,thrown,checks=0;
  const event=(kind,value)=>{trace.push([kind,value]);if(++at===fault)throw failure;};
  const callable=function(){assert.equal(this,undefined);event('callee',value);return value;};
  const seed=function(){assert.equal(this,undefined);const result=seeds++?second:row.alias===13?callable:value;event('seed',row.alias===13&&seeds===1?'callable':result);
    if(row.alias===7&&seeds>1){queueMicrotask(()=>trace.push(['job']));return reject?Promise.reject(failure):Promise.resolve(result);}return result;};
  const report=function(value){assert.equal(this,undefined);
    if(row.alias===9||row.alias===10){allocation??=value;assert.equal(value,allocation);value.visits=(value.visits??0)+1;event('report',value.visits);}
    else event('report',row.alias===13?'callable':value);};
  const again=function(){assert.equal(this,undefined);event('again',checks);return checks++<iterations;};
  try {
    returned=fn(seed,report,row.alias===18||row.alias===19?again:flag);
    if(row.alias===8){const generator=returned,first=generator.next();requests.push(first.value);const end=request==='throw'?generator.throw(failure):request==='return'?generator.return(second):generator.next(value);assert.equal(end.done,true);returned=end.value;}
    if(row.alias===7)returned=await returned;
    if(row.alias===9||row.alias===10){assert.equal(returned,allocation??returned);assert.equal(Array.isArray(returned),row.alias===10);assert.equal(returned.visits??0,row.count);returned=token;}
  }catch(error){returned=undefined;thrown=error;}
  if(row.alias===29&&thrown instanceof ReferenceError)thrown=tdzFailure;
  return {trace,requests,returned,thrown};
}
function expressionReference(row) {
  if(row.expression===20)return function(input,report,other){const saved=other(),transport=saved;let carried=saved;while(carried)carried=false;report(transport);return transport;};
  const root=row.expression===17?'other':row.expression>=10&&row.expression<=13?'state':'input';
  const last=row.count?'copy'+(row.count-1):root;
  const declarations=Array.from({length:row.count},(_,i)=>'let copy'+i+';').join('');
  const assignments=Array.from({length:row.count},(_,i)=>'copy'+i+'='+(!i&&row.expression===16?'0':!i&&row.expression===8?'copy0':i?'copy'+(i-1):root));
  const nested='('+[...assignments,last].join(',')+')';let effect='report('+nested+')';
  if(row.expression===1)effect='report('+last+','+nested+')';
  if(row.expression===2)effect='report('+nested+','+last+')';
  if(row.expression===3)effect='flag&&'+nested;
  if(row.expression===4)effect='flag&&report('+nested+','+last+')';
  if(row.expression===21||row.expression===22)effect='flag'+(row.expression===21?'||':'??')+'report('+nested+','+last+')';
  if(row.expression===5)effect='flag?'+nested+':undefined';
  if(row.expression===6)effect='flag?report('+nested+','+last+'):undefined';
  if(row.expression===7)effect='report(('+last+','+nested+'))';
  if(row.expression===14)effect='if(flag){'+effect+';}';
  if(row.expression===19)effect='{'+effect+';}';
  if(row.expression===15)effect='return '+nested;
  if(row.expression===17)effect='report((0,'+nested+')())';
  const initial=row.expression>=10&&row.expression<=13?'let state='+(row.expression===11?'await ':row.expression===12?'yield ':'')+(row.expression===13?'{}':'other()')+';':'';
  const tail=row.expression===15?'':(row.expression===9?last+'=other;':'')+([4,6,17,21,22].includes(row.expression)?'return;':'report('+last+');return '+last+';');
  if(row.expression===18)return Function('"use strict";return function(input,report,other,flag){'+declarations+'copy0=input;{let input=123;report(('+[...assignments.slice(1),last].join(',')+'));report('+last+');return '+last+';}}')();
  return Function('"use strict";return ('+(row.expression===11?'async function':row.expression===12?'function*':'function')+'(input,report,other,flag){'+initial+declarations+effect+';'+tail+'})')();
}
async function observeExpression(fn,row,value,fault,flag,request='next',reject=false) {
  const trace=[],requests=[];let at=0,returned,thrown,allocation;
  const event=(kind,...values)=>{trace.push([kind,...values]);if(++at===fault)throw failure;};
  const other=function(){assert.equal(this,undefined);event('other',value);if(row.expression===11){queueMicrotask(()=>trace.push(['job']));return reject?Promise.reject(failure):Promise.resolve(value);}return value;};
  const report=function(...values){assert.equal(this,undefined);if(row.expression===13)values=values.map(value=>{allocation??=value;assert.equal(value,allocation);return 'allocation';});if(row.expression===9)values=values.map(value=>value===other?'other':value);event('report',...values);};
  try {
    returned=fn(value,report,other,flag);
    if(row.expression===11)returned=await returned;
    if(row.expression===12){const first=returned.next();requests.push(first.value);const end=request==='throw'?returned.throw(failure):request==='return'?returned.return(second):returned.next(value);assert.equal(end.done,true);returned=end.value;}
    if(row.expression===13){allocation??=returned;assert.equal(returned,allocation);assert.equal(Object.getPrototypeOf(returned),Object.prototype);returned='allocation';}
    if(row.expression===9){assert.equal(returned,other);returned='other';}
  }catch(error){returned=undefined;thrown=error;}
  return {trace,requests,returned,thrown};
}
function sequenceReference(row) {
  const left=['0','input','!input','typeof input','input===flag','input==flag','-input','input.value',
    '0','this','input&&flag','flag?input:other','state','state','await other()','yield other()','{}','other()','__sequenceMissing'][row.sequence];
  let expression='input';
  for(let i=0;i<row.count;++i)expression='('+left+','+expression+')';
  if(row.sequence===8)expression='typeof ('+expression+',__sequenceMissing)';
  const before=row.sequence===12?'let state=input;':'',after=row.sequence===13?'let state=input;':'';
  return Function('"use strict";return ('+(row.sequence===14?'async function':row.sequence===15?'function*':'function')+
    '(input,report,other,flag){'+before+'report('+expression+');'+after+'return input;})')();
}
async function observeSequence(fn,row,value,flag,fault,resolved,request='next',reject=false) {
  const trace=[],requests=[];let at=0,returned,thrown;
  const event=(kind,...values)=>{trace.push([kind,...values]);if(++at===fault)throw failure;};
  const input=value==='object'?Object.defineProperties({}, {
    value:{get(){event('getter');return 3;}},
    [Symbol.toPrimitive]:{value(hint){event('coerce',hint);return 1;}},
  }):value;
  const normalized=value=>value===input?'input':value;
  const report=function(value){assert.equal(this,undefined);event('report',normalized(value));};
  const other=function(){assert.equal(this,undefined);event('other');
    if(row.sequence===14){queueMicrotask(()=>trace.push(['job']));return reject?Promise.reject(failure):Promise.resolve(input);}return input;};
  const prior=Object.getOwnPropertyDescriptor(globalThis,'__sequenceMissing');
  if(resolved)Object.defineProperty(globalThis,'__sequenceMissing',{configurable:true,get(){event('global');return input;}});
  else delete globalThis.__sequenceMissing;
  try {
    returned=fn(input,report,other,flag);
    if(row.sequence===14)returned=await returned;
    if(row.sequence===15){const generator=returned;let result;
      for(let i=0;i<row.count;++i){result=generator.next(input);requests.push(normalized(result.value));assert.equal(result.done,false);if(request!=='next')break;}
      result=request==='throw'?generator.throw(failure):request==='return'?generator.return(second):generator.next(input);
      assert.equal(result.done,true);returned=result.value;}
    returned=normalized(returned);
  }catch(error){if(error instanceof assert.AssertionError)throw error;thrown=error===failure?failure:error.name;returned=undefined;}
  finally{if(prior)Object.defineProperty(globalThis,'__sequenceMissing',prior);else delete globalThis.__sequenceMissing;}
  return {trace,requests,returned,thrown};
}
for (const row of rows) {
  const ast = parseJavaScript(row.source), pending=[ast], initialized=[], declarations=[], stores=[], loops=[];
  while (pending.length) {
    const node=pending.pop(); if(node.type==='VariableDeclaration') {
      declarations.push(node); for(const item of node.declarations) if(item.init) initialized.push(item);
    }
    for(const child of Object.values(node)) {
      if(Array.isArray(child)) pending.push(...child.filter(value=>value?.type)); else if(child?.type) pending.push(child);
    }
    if(node.type==='ExpressionStatement' && node.expression.type==='AssignmentExpression' &&
        node.expression.left.type==='Identifier') stores.push(node);
    if(node.type==='WhileStatement' || node.type==='ForStatement') loops.push(node);
  }
  if(row.sequence!==undefined) {
    const generated=Function('"use strict";return '+row.source)(),reference=sequenceReference(row);
    const run=(fn,value,flag,fault,resolved,request='next',reject=false)=>observeSequence(fn,row,value,flag,fault,resolved,request,reject);
    for(const value of [undefined,null,-0,NaN,1n,Symbol.for('sequence-input'),'object'])for(const flag of [false,true,1,null])
      for(const fault of [Infinity,1,2])for(const resolved of row.sequence===8||row.sequence===18?[false,true]:[false]) {
        const actual=await run(generated,value,flag,fault,resolved),expected=await run(reference,value,flag,fault,resolved);
        try{assert.deepEqual(actual,expected);}catch(error){error.message+=`; sequence=${row.sequence}, count=${row.count}`;throw error;}++comparisons;
      }
    if(row.sequence===14&&row.count){assert.deepEqual(await run(generated,'object',true,Infinity,false,'next',true),await run(reference,'object',true,Infinity,false,'next',true));++comparisons;}
    if(row.sequence===15&&row.count)for(const request of ['throw','return']){assert.deepEqual(await run(generated,'object',true,Infinity,false,request),await run(reference,'object',true,Infinity,false,request));++comparisons;}
    if(row.inert&&row.sequence!==8) {
      const pending=[ast];while(pending.length){const node=pending.pop();if(node.type==='CallExpression')for(const argument of node.arguments)assert.notEqual(argument.type,'SequenceExpression');
        for(const value of Object.values(node)){if(Array.isArray(value)){for(const child of value)if(child?.type)pending.push(child);}else if(value?.type)pending.push(value);}}
    }
    if(row.count===1&&(!row.inert&&row.sequence!==16||row.sequence===8)) {
      const pending=[ast];let sequence;while(pending.length){const node=pending.pop();if(!sequence&&node.type==='SequenceExpression')sequence=node;
        for(const value of Object.values(node)){if(Array.isArray(value)){for(const child of value)if(child?.type)pending.push(child);}else if(value?.type)pending.push(value);}}
      assert.ok(sequence);const right=sequence.expressions.at(-1),changed=row.source.slice(0,sequence.start)+'('+row.source.slice(right.start,right.end)+')'+row.source.slice(sequence.end);
      parseJavaScript(changed);const bad=Function('"use strict";return '+changed)();
      await assert.rejects(async()=>{
        const actual=await run(bad,'object',true,Infinity,false),expected=await run(reference,'object',true,Infinity,false);
        assert.deepEqual(actual,expected);
      },assert.AssertionError,`sequence mutation=${row.sequence}`);++mutations;
    }
    continue;
  }
  if(row.expression!==undefined) {
    const generated=Function('"use strict";return '+row.source)(),reference=expressionReference(row);
    if(row.expression===20)assert.ok(!declarations.some(node=>node.declarations.some(item=>item.id.name==='transport')),
      'outlined prefix value remains available after the loop without a copied slot');
    if(row.expression===8&&row.count)assert.deepEqual(declarations.flatMap(node=>node.declarations.map(item=>item.id.name)).filter(name=>name.startsWith('copy')),[]);
    if(row.folded){assert.ok(!declarations.some(node=>node.declarations.some(item=>item.id.name.startsWith('copy'))));++folded;}
    if(row.folded) {
      const pending=[ast];
      while(pending.length) {
        const node=pending.pop();
        if(node.type==='CallExpression')for(const argument of node.arguments)
          assert.notEqual(argument.type,'SequenceExpression','completed pure copies must project to their value');
        for(const value of Object.values(node)) {
          if(Array.isArray(value)){for(const child of value)if(child?.type)pending.push(child);}
          else if(value?.type)pending.push(value);
        }
      }
    }
    const run=(fn,value,fault,flag,request='next',reject=false)=>observeExpression(fn,row,value,fault,flag,request,reject);
    for(const value of [undefined,null,-0,NaN,1n,Symbol('input'),token])for(const flag of [false,true,0,NaN,undefined,null,1n,token])for(const fault of [Infinity,1,2,3]) {
      const actual=await run(generated,value,fault,flag),expected=await run(reference,value,fault,flag);
      try{assert.deepEqual(actual.trace,expected.trace);compare(actual,expected);}catch(error){error.message+=`; expression=${row.expression}, count=${row.count}, flag=${String(flag)}, fault=${fault}`;throw error;}++comparisons;
    }
    if(row.expression===11){compare(await run(generated,token,Infinity,true,'next',true),await run(reference,token,Infinity,true,'next',true));++comparisons;}
    if(row.expression===12)for(const request of ['throw','return']){compare(await run(generated,token,Infinity,true,request),await run(reference,token,Infinity,true,request));++comparisons;}
    if(row.expression===13)assert.notEqual(generated(token,()=>{},()=>token,true),generated(token,()=>{},()=>token,true));
    if(row.folded&&row.count===1) {
      const pending=[ast];let operand;
      while(pending.length){const node=pending.pop();if(node.type==='ReturnStatement'&&node.argument)operand=node.argument;if(node.type==='CallExpression'&&node.arguments.length)operand=node.arguments[0];for(const value of Object.values(node)){if(Array.isArray(value)){for(const child of value)if(child?.type)pending.push(child);}else if(value?.type)pending.push(value);}}
      assert.ok(operand);const changed=row.source.slice(0,operand.start)+'123'+row.source.slice(operand.end);parseJavaScript(changed);
      const bad=Function('"use strict";return '+changed)(),flag=row.expression===21||row.expression===22?undefined:true;
      const actual=await run(bad,token,Infinity,flag),expected=await run(reference,token,Infinity,flag);
      assert.throws(()=>compare(actual,expected),assert.AssertionError);++mutations;
    }
    continue;
  }
  if(row.entry!==undefined) {
    const generated=Function((row.strict?'"use strict";':'')+'return '+row.source)(),reference=entryReference(row);
    if(row.folded){assert.ok(!declarations.some(node=>node.declarations.some(item=>item.id.name.startsWith('copy'))));++folded;}
    const run=(fn,value,fault,flag=true,request='next',reject=false,iterations=2)=>observeEntry(fn,row,value,fault,flag,request,reject,iterations);
    for(const value of [undefined,null,-0,NaN,1n,Symbol('input'),token])for(const flag of row.entry===11||row.entry===32?[false,true,undefined,0,1n,token]:[true])for(const fault of [Infinity,1,2,row.count+1])for(const iterations of row.entry===25||row.entry===33?[0,1,2,3]:[2]) {
      try{entryCompare(await run(generated,value,fault,flag,'next',false,iterations),await run(reference,value,fault,flag,'next',false,iterations));}
      catch(error){error.message+=`; entry=${row.entry}, count=${row.count}, fault=${fault}, flag=${String(flag)}`;throw error;}++comparisons;
    }
    if(row.entry===13||row.entry===26){entryCompare(await run(generated,token,Infinity,true,'next',true),await run(reference,token,Infinity,true,'next',true));++comparisons;}
    if(row.entry===14||row.entry===27)for(const request of ['throw','return']){entryCompare(await run(generated,token,Infinity,true,request),await run(reference,token,Infinity,true,request));++comparisons;}
    if([5,6,8,9,17].includes(row.entry)&&row.count) {
      const actual=argumentMutation(generated.toString(),row),expected=argumentMutation(reference.toString(),row);
      for(const value of [undefined,-0,NaN,1n,token]){entryCompare(await run(actual,value,Infinity),await run(expected,value,Infinity));++comparisons;}
    }
    if(row.entry>=18){const root=declarations.find(node=>node.declarations.some(item=>item.id.name==='state'));assert.ok(root);assert.equal(root.kind,row.entry===19?'const':'let');}
    if(row.entry===29){const values=[generated(token,()=>{},second,true),generated(token,()=>{},second,true)];assert.notEqual(values[0],values[1]);}
    if(row.folded&&row.count===1) {
      const work=[ast];let returned;while(work.length){const node=work.pop();if(node.type==='ReturnStatement')returned=node;for(const value of Object.values(node)){if(Array.isArray(value)){for(const child of value)if(child?.type)work.push(child);}else if(value?.type)work.push(value);}}
      const initializer=row.entry===33||row.entry===25?declarations.find(node=>node.declarations.some(item=>item.id.name==='state')).declarations[0].init:null;
      const operand=initializer??returned.argument,replacement=initializer?'('+row.source.slice(operand.start,operand.end)+', '+row.source.slice(operand.start,operand.end)+')':'123';
      const changed=row.source.slice(0,operand.start)+replacement+row.source.slice(operand.end);parseJavaScript(changed);
      const bad=Function((row.strict?'"use strict";':'')+'return '+changed)(),actual=await run(bad,token,Infinity),expected=await run(reference,token,Infinity);
      assert.throws(()=>entryCompare(actual,expected),assert.AssertionError);++mutations;
    }
    continue;
  }
  if(row.alias!==undefined) {
    const generated=Function('return '+row.source)(),reference=aliasReference(row);
    if((row.alias===24&&row.count)||row.alias===25) {
      const declaration=declarations.find(node=>node.declarations.some(item=>item.id.name==='saved'));
      assert.equal(declaration?.kind,'const');
    }
    if(row.alias===12){const declaration=declarations.find(node=>node.declarations.some(item=>item.id.name==='saved'));assert.equal(declaration.kind,'let');assert.ok(declaration.declarations.find(item=>item.id.name==='saved').init);assert.equal(stores.filter(node=>node.expression.left.name==='saved').length,0);}
    if(row.folded){assert.ok(!declarations.some(node=>node.declarations.some(item=>item.id.name.startsWith('copy'))));++folded;}
    if(row.alias===14&&row.count)assert.ok(declarations.flatMap(node=>node.declarations).filter(item=>item.id.name.startsWith('copy')).length<=1);
    if(row.alias===9||row.alias===10)assert.notEqual(generated(()=>{},()=>{}),generated(()=>{},()=>{}));
    const run=(fn,value,fault,flag=true,request='next',reject=false,iterations=2)=>observeAlias(fn,row,value,fault,flag,request,reject,iterations);
    for(const value of [undefined,null,-0,NaN,1n,Symbol('input'),token])for(const flag of [3,21].includes(row.alias)?[false,true,undefined,0,1n,token]:[true])for(const iterations of row.alias===18||row.alias===19?[0,1,2,3]:[2])for(const fault of [Infinity,1,2,row.count+1]) {
      try{compare(await run(generated,value,fault,flag,'next',false,iterations),await run(reference,value,fault,flag,'next',false,iterations));}
      catch(error){error.message+=`; alias=${row.alias}, count=${row.count}, fault=${fault}, flag=${String(flag)}`;throw error;}++comparisons;
    }
    if(row.alias===7){compare(await run(generated,token,Infinity,true,'next',true),await run(reference,token,Infinity,true,'next',true));++comparisons;}
    if(row.alias===8)for(const request of ['throw','return']){compare(await run(generated,token,Infinity,true,request),await run(reference,token,Infinity,true,request));++comparisons;}
    if((row.alias===25||row.alias===28)&&row.count===1) {
      // Removing an identity store must retain its RHS observation and throw.
      const changed=editExpression(row.source,(node,dependsOn)=>node.type==='CallExpression'&&
        dependsOn(node.callee,value=>value.type==='Identifier'&&value.name==='report'),()=> '0',{first:true});
      parseJavaScript(changed);const bad=Function('return '+changed)();
      const actual=await run(bad,token,Infinity),expected=await run(reference,token,Infinity);
      assert.throws(()=>compare(actual,expected),assert.AssertionError);++mutations;
    }
    if(row.alias===23&&row.count===1) {
      const pending=[ast];let returned;
      while(pending.length){const node=pending.pop();if(node.type==='ReturnStatement')returned=node;for(const value of Object.values(node)){if(Array.isArray(value)){for(const child of value)if(child?.type)pending.push(child);}else if(value?.type)pending.push(value);}}
      assert.ok(returned?.argument);const operand=returned.argument;
      // Force an otherwise value-compatible alias through a different lexical
      // binding. Execution must distinguish the inner number from the object.
      const changed=row.source.slice(0,operand.start)+'typeof saved'+row.source.slice(operand.end);
      parseJavaScript(changed);const bad=Function('return '+changed)();
      const actual=await run(bad,token,Infinity),expected=await run(reference,token,Infinity);
      assert.throws(()=>compare(actual,expected),assert.AssertionError);++mutations;
    }
    if(row.folded&&row.count===1) {
      const pending=[ast];let returned,call;
      while(pending.length){const node=pending.pop();if(node.type==='ReturnStatement')returned=node;if(node.type==='CallExpression')call??=node;for(const value of Object.values(node)){if(Array.isArray(value)){for(const child of value)if(child?.type)pending.push(child);}else if(value?.type)pending.push(value);}}
      // Dead copies may leave only the required call, with no value to mutate.
      const operand=returned?.argument??initialized.find(item=>item.id.name==='saved'&&item.init?.type==='CallExpression')?.init??call;
      assert.ok(operand);const changed=row.source.slice(0,operand.start)+'123'+row.source.slice(operand.end);parseJavaScript(changed);
      const bad=Function('return '+changed)(),actual=await run(bad,token,Infinity),expected=await run(reference,token,Infinity);
      assert.throws(()=>compare(actual,expected),assert.AssertionError);++mutations;
    }
    continue;
  }
  if(row.constant!==undefined) {
    const generated=Function('return '+row.source)(),reference=constantReference(row);
    assert.equal(declarations.some(node=>node.declarations.some(item=>item.id.name==='saved')),!row.folded);
    if(row.folded)++folded;
    if([14,15].includes(row.constant)){const objects=[generated(()=>{},()=>{}),generated(()=>{},()=>{})];assert.notEqual(objects[0],objects[1]);}
    const run=(fn,value,fault,flag=true,iterations=2,request='next',reject=false)=>observeConstant(fn,row,value,fault,flag,iterations,request,reject);
    for(const value of [undefined,null,-0,NaN,1n,Symbol('input'),token])for(const flag of [4,16].includes(row.constant)?[false,true,undefined,0,1n,token]:[true])
      for(const iterations of row.constant===8 || row.constant===9?[0,1,2,3]:[2])for(const fault of [Infinity,1,2,row.count+1]) {
        try{compare(await run(generated,value,fault,flag,iterations),await run(reference,value,fault,flag,iterations));}
        catch(error){error.message+=`; constant=${row.constant}, literal=${row.literal}, count=${row.count}, fault=${fault}`;throw error;}++comparisons;
      }
    if(row.constant===10){compare(await run(generated,token,Infinity,true,2,'next',true),await run(reference,token,Infinity,true,2,'next',true));++comparisons;}
    if(row.constant===11)for(const request of ['throw','return']){compare(await run(generated,token,Infinity,true,2,request),await run(reference,token,Infinity,true,2,request));++comparisons;}
    if(row.constant===16 && row.count===0) {
      // A successful constant proof does not authorize removing the other
      // branch's abrupt completion. A valid-JS mutant must expose that error.
      const branch=ast.body[0].expression.body.body.find(node=>node.type==='IfStatement');
      assert.ok(branch?.alternate);
      const changed=row.source.slice(0,branch.alternate.start)+'{}'+row.source.slice(branch.alternate.end);
      parseJavaScript(changed);const bad=Function('return '+changed)();
      const actual=await run(bad,token,Infinity,false),expected=await run(reference,token,Infinity,false);
      assert.throws(()=>compare(actual,expected),assert.AssertionError);++mutations;
    }
    if(row.folded && row.count===1) {
      const returned=ast.body[0].expression.body.body.findLast(node=>node.type==='ReturnStatement');
      if(returned?.argument && row.constant!==12){
        const operand=returned.argument,changed=row.source.slice(0,operand.start)+'123'+row.source.slice(operand.end);parseJavaScript(changed);
        const bad=Function('return '+changed)();const actual=await run(bad,token,Infinity),expected=await run(reference,token,Infinity);
        assert.throws(()=>compare(actual,expected),assert.AssertionError);++mutations;
      }
      if(row.constant===12){
        const callee=returned.argument.callee,changed=row.source.slice(0,callee.start)+'(0, '+row.source.slice(callee.start,callee.end)+')'+row.source.slice(callee.end);parseJavaScript(changed);
        const bad=Function('return '+changed)();const actual=await run(bad,token,Infinity),expected=await run(reference,token,Infinity);
        assert.throws(()=>compare(actual,expected),assert.AssertionError);++mutations;
      }
    }
    continue;
  }
  if(row.decoded_cell){const name=String.fromCharCode(...row.cell_name),declaration=declarations.find(node=>node.declarations.some(item=>item.id.name===name));assert.equal(declaration.kind,'let');assert.ok(declaration.declarations.find(item=>item.id.name===name).init);assert.equal(stores.filter(node=>node.expression.left.name===name).length,0);}
  if(row.form===21)assert.ok(!declarations.some(node=>node.declarations.some(item=>item.id.name==='unused')));
  if(row.forwarded||row.elided) {assert.ok(!declarations.some(node=>node.declarations.some(item=>item.id.name==='saved')));++folded;}
  else if(row.fold || row.branch&&!row.decoded_cell) {assert.ok(initialized.length); ++folded;}
  const generated=Function('return '+row.source)(), reference=original(row);
  if(row.decoded_cell&&row.field) {
    assert.equal(row.shared_cell,true);
    const work=[ast];let assignment;
    while(work.length){const node=work.pop();if(node.type==='AssignmentExpression'&&node.left.type==='MemberExpression')assignment=node;for(const value of Object.values(node)){if(Array.isArray(value)){for(const child of value)if(child?.type)work.push(child);}else if(value?.type)work.push(value);}}
    assert.ok(assignment);const right=assignment.right,changed=row.source.slice(0,right.start)+'123'+row.source.slice(right.end);parseJavaScript(changed);
    const bad=Function('return '+changed)();await assert.rejects(async()=>compare(await observe(bad,row,token,Infinity),await observe(reference,row,token,Infinity)),assert.AssertionError);++mutations;
  }
  let seedInitializer;
  if(row.decoded_loop) {
    assert.equal(loops.length,1); assert.notEqual(loops[0].test.type,'Literal');
    const body=[loops[0].body];
    while(body.length) {
      const node=body.pop();
      if(node.type==='VariableDeclaration' && node.kind==='const') for(const item of node.declarations) {
        const callee=item.init?.type==='CallExpression' ? item.init.callee : null;
        const identifier=callee?.type==='SequenceExpression' ? callee.expressions.at(-1) : callee;
        if(identifier?.type==='Identifier' && identifier.name==='seed') seedInitializer=item.init;
      }
      for(const child of Object.values(node)) {
        if(Array.isArray(child)) body.push(...child.filter(value=>value?.type)); else if(child?.type) body.push(child);
      }
    }
    assert.ok(seedInitializer,'native private seed initialization in the decoded loop body');
  }
  for(const value of [undefined,null,false,0,-0,NaN,1n,Symbol('value'),token])
    for(const flag of row.decoded || row.decoded_loop ? [false,true,0,-0,NaN,undefined,null,1n,Symbol('flag'),{}] : [true])
      for(const stop of row.decoded_loop ? [false,true,0,undefined] : [false])
      for(const iterations of row.form >= 11 || row.decoded_loop ? [0,1,2,3] : [2])
      for(const fault of row.field?[Infinity,1,2,3,4,5]:[Infinity,1,2,(row.count ?? 2)+1]) {
        try {compare(await observe(generated,row,value,fault,false,'next',flag,iterations,stop),await observe(reference,row,value,fault,false,'next',flag,iterations,stop));}
        catch(error) {error.message += `; form=${row.form}, count=${row.count}, decoded=${row.decoded}, flag=${flag}, fault=${fault}, value=${String(value)}`; throw error;}
        ++comparisons;
      }
  if(row.form===5) {
    compare(await observe(generated,row,token,Infinity,true),await observe(reference,row,token,Infinity,true)); ++comparisons;
  }
  if(row.form===6) for(const request of ['throw','return']) {
    compare(await observe(generated,row,token,Infinity,false,request),await observe(reference,row,token,Infinity,false,request)); ++comparisons;
  }
  if(row.form===4 || row.form===8) for(const seed of [undefined,null,false,0]) {
    const run=fn=>{const trace=[];const returned=fn(seed,value=>trace.push(value));return {trace,returned};};
    assert.deepEqual(run(generated),run(reference));++comparisons;
  }
  if(initialized.length && !row.decoded && row.count===1) {
    const init=initialized[0].init;
    for(const replacement of ['void 0','(seed(), seed())']) {
      const changed=row.source.slice(0,init.start)+replacement+row.source.slice(init.end); parseJavaScript(changed);
      const bad=Function('return '+changed)();
      await assert.rejects(async()=>compare(await observe(bad,row,token,Infinity),await observe(reference,row,token,Infinity)),assert.AssertionError); ++mutations;
    }
    if(row.form===1) {
      const declaration=declarations.find(node=>node.kind==='let');assert.ok(declaration);
      const changed=row.source.slice(0,declaration.start)+'const'+row.source.slice(declaration.start+declaration.kind.length);
      parseJavaScript(changed);const bad=Function('return '+changed)();
      await assert.rejects(async()=>compare(await observe(bad,row,token,Infinity),await observe(reference,row,token,Infinity)),assert.AssertionError);++mutations;
    }
  }
  if((row.form===4 || row.form===10 || row.form===12 || row.form===14) && row.count===1) {
    const declaration=declarations.find(node=>node.declarations.some(item=>item.id.name==='saved'));
    const store=stores.filter(node=>node.expression.left.name==='saved').sort((a,b)=>a.start-b.start)[0]; assert.ok(declaration && store);
    // Valid JS with an incorrectly shortened lifetime must be detected when
    // a sibling/finally observation sees old storage, including a thrown seed.
    const replacement=(row.form===12 ? 'let' : 'const')+' saved = '+row.source.slice(store.expression.right.start,store.expression.right.end)+';';
    const changed=row.source.slice(0,declaration.start)+row.source.slice(declaration.end,store.start)+
      replacement+row.source.slice(store.end);
    parseJavaScript(changed); const bad=Function('return '+changed)();
    await assert.rejects(async()=>compare(await observe(bad,row,token,row.form===10 ? 1 : Infinity),
      await observe(reference,row,token,row.form===10 ? 1 : Infinity)),assert.AssertionError,`lifetime mutation ${row.form}`); ++mutations;
  }
  if(row.form===17 && row.count===1) {
    // Its early read is now undefined, so shortening a later declaration's
    // lifetime alone is equivalent. Prematurely evaluating the later producer
    // at that initial read must still change value/effect order.
    const changed=editExpression(row.source,node=>node.type==='UnaryExpression'&&node.operator==='void',()=> 'seed()',{first:true});
    parseJavaScript(changed);const bad=Function('return '+changed)();
    await assert.rejects(async()=>compare(await observe(bad,row,token,Infinity),
      await observe(reference,row,token,Infinity)),assert.AssertionError);++mutations;
  }
  if(row.decoded_loop && row.mutate) {
    const call=row.source.slice(seedInitializer.start,seedInitializer.end);
    for(const replacement of ['void 0','('+call+','+call+')']) {
      const changed=row.source.slice(0,seedInitializer.start)+replacement+row.source.slice(seedInitializer.end);
      parseJavaScript(changed); const bad=Function('return '+changed)();
      await assert.rejects(async()=>compare(await observe(bad,row,token,Infinity),await observe(reference,row,token,Infinity)),assert.AssertionError);++mutations;
    }
  }
}
console.log(JSON.stringify({sources:rows.length,folded,comparisons,mutations,platformExecution:false}));
