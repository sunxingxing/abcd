import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {parseJavaScript} from './class_reference.mjs';
const rows = execFileSync(process.argv[2], ['--emit'], {encoding:'utf8', maxBuffer:32_000_000}).trim().split('\n').map(JSON.parse);
const token = {}, second = {}, failure = Symbol('original throw');
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
  if (row.form >= 8) {
    const body = 'saved=seed();' + 'report(saved);'.repeat(row.count) + 'return saved;';
    return Function('return function(seed,report){let saved;' +
      (row.form === 8 ? 'if(seed){' + body + '}return;' :
        'try{' + body + '}finally{report(' + (row.form === 10 ? 'saved' : '') + ');}') + '};')();
  }
  const value = row.form === 3 ? 'saved' : row.form === 5 ? 'await seed()' : row.form === 6 ? 'yield seed()' : 'seed()';
  return Function('return (' + (row.form === 5 ? 'async ' : '') + 'function' + (row.form === 6 ? '*' : '') + '(seed,report){let saved;' +
    (row.form === 2 ? 'report(saved);' : '') + (row.form === 4 ? 'if(seed){saved=seed();}' : 'saved='+value+';') +
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
  if(row.constant===3)body+='saved=seed();';
  if(row.constant===7)body='try{seed();saved='+value+';'+reports+'}finally{report(saved);}';
  if(row.constant===8 || row.constant===9)body='while(seed()){saved='+value+';'+reports+'}';
  if(row.constant===10 || row.constant===11)body='saved='+value+';'+(row.constant===10?'await':'yield')+' seed();'+reports;
  body+='return'+(row.constant===9?'':row.constant===12?' saved.toString()':row.constant===13?' saved ** 2':' saved')+';';
  return Function('return ('+(row.constant===10?'async function':row.constant===11?'function*':'function')+'(seed,report){let saved;'+body+'})')();
}
async function observeConstant(fn,row,value,fault,flag,iterations,request='next',reject=false) {
  const trace=[],requests=[];let at=0,checks=0,returned,thrown,allocation;
  const event=(kind,value)=>{trace.push([kind,value]);if(++at===fault)throw failure;};
  const seed=function(){assert.equal(this,undefined);const result=row.constant===8 || row.constant===9?checks++<iterations:value;event('seed',result);
    if(row.constant===10){queueMicrotask(()=>trace.push(['job']));return reject?Promise.reject(failure):Promise.resolve(result);}return result;};
  const report=function(value){assert.equal(this,undefined);
    if(row.constant>=14){allocation??=value;assert.equal(value,allocation);value.visits=(value.visits??0)+1;event('report',value.visits);}
    else event('report',value);};
  const descriptor=Object.getOwnPropertyDescriptor(Number.prototype,'toString');
  if(row.constant===12)Object.defineProperty(Number.prototype,'toString',{configurable:true,get(){event('get',this);return function(){event('member',this);return typeof this+':'+String(this);};}});
  try {
    returned=fn(row.constant===4?flag:seed,report);
    if(row.constant===11){const generator=returned,first=generator.next();requests.push(first.value);const end=request==='throw'?generator.throw(failure):request==='return'?generator.return(second):generator.next(value);assert.equal(end.done,true);returned=end.value;}
    if(row.constant===10)returned=await returned;
    if(row.constant>=14){assert.equal(returned,allocation??returned);assert.equal(Array.isArray(returned),row.constant===15);assert.equal(returned.visits??0,row.count);returned=token;}
  }catch(error){thrown=error;returned=undefined;}
  finally{if(row.constant===12)Object.defineProperty(Number.prototype,'toString',descriptor);}
  return {trace,requests,returned,thrown};
}
let comparisons=0, mutations=0, folded=0;
function aliasReference(row) {
  const root=row.alias===9?'{}':row.alias===10?'[]':'seed()';
  const declarations=Array.from({length:row.count},(_,i)=>'let copy'+i+';').join('');
  const copies=Array.from({length:row.count},(_,i)=>'copy'+i+'='+(i?'copy'+(i-1):'saved')+';').join('');
  const last=row.count?'copy'+(row.count-1):'saved';
  const reports=('report('+last+');').repeat(row.alias===15?0:row.count);
  let body=declarations+'let saved;'+(row.alias===2&&row.count?'report('+last+');':'')+'saved='+root+';'+
    (row.alias===3?'if(flag){'+copies+'}':copies)+
    (row.alias===7?'await seed();':row.alias===8?'yield seed();':'');
  let tail=reports+(row.alias===1?'saved=seed();':row.alias===11&&row.count?last+'=seed();':'')+'return '+last+(row.alias===13?'()':'')+';';
  if(row.alias===4)tail='{'+tail+'}';
  if(row.alias===5)tail='{let saved=123;'+tail+'}';
  if(row.alias===6)tail=reports+'{let saved=123;}return '+last+';';
  if(row.alias===20)tail='{'.repeat(row.count+1)+tail+'}'.repeat(row.count+1);
  if(row.alias>=14&&row.alias<=16) {
    const first=row.count?'copy0=saved;':'';
    const rest=Array.from({length:Math.max(0,row.count-1)},(_,i)=>'copy'+(i+1)+'=copy'+i+';').join('');
    body=declarations+'let saved;saved='+root+';'+first;
    tail='{let saved=123;'+rest+reports+(row.alias===16&&row.count?last+'=seed();':'')+
      (row.alias===15?'':'return '+last+';')+'}'+(row.alias===15?'return;':'');
  }
  if(row.alias===17){body=declarations+'let saved;try{saved='+root+';'+copies+reports+'}finally{report('+last+');}';tail='return '+last+';';}
  if(row.alias===18||row.alias===19){body=declarations+'let saved;while(flag()){saved='+root+';'+copies+reports+'}';tail='return'+(row.alias===18?'':' '+last)+';';}
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
      const initializer=row.entry===33?stores.find(node=>node.expression.left.name==='state').expression.right:row.entry===25?declarations.find(node=>node.declarations.some(item=>item.id.name==='state')).declarations[0].init:null;
      const operand=initializer??returned.argument,replacement=initializer?'('+row.source.slice(operand.start,operand.end)+', '+row.source.slice(operand.start,operand.end)+')':'123';
      const changed=row.source.slice(0,operand.start)+replacement+row.source.slice(operand.end);parseJavaScript(changed);
      const bad=Function((row.strict?'"use strict";':'')+'return '+changed)(),actual=await run(bad,token,Infinity),expected=await run(reference,token,Infinity);
      assert.throws(()=>entryCompare(actual,expected),assert.AssertionError);++mutations;
    }
    continue;
  }
  if(row.alias!==undefined) {
    const generated=Function('return '+row.source)(),reference=aliasReference(row);
    if(row.alias===12){assert.equal(declarations.find(node=>node.declarations.some(item=>item.id.name==='saved')).kind,'let');assert.equal(stores.filter(node=>node.expression.left.name==='saved').length,1);}
    if(row.folded){assert.ok(!declarations.some(node=>node.declarations.some(item=>item.id.name.startsWith('copy'))));++folded;}
    if(row.alias===9||row.alias===10)assert.notEqual(generated(()=>{},()=>{}),generated(()=>{},()=>{}));
    const run=(fn,value,fault,flag=true,request='next',reject=false,iterations=2)=>observeAlias(fn,row,value,fault,flag,request,reject,iterations);
    for(const value of [undefined,null,-0,NaN,1n,Symbol('input'),token])for(const flag of row.alias===3?[false,true,undefined,0,1n,token]:[true])for(const iterations of row.alias===18||row.alias===19?[0,1,2,3]:[2])for(const fault of [Infinity,1,2,row.count+1]) {
      try{compare(await run(generated,value,fault,flag,'next',false,iterations),await run(reference,value,fault,flag,'next',false,iterations));}
      catch(error){error.message+=`; alias=${row.alias}, count=${row.count}, fault=${fault}, flag=${String(flag)}`;throw error;}++comparisons;
    }
    if(row.alias===7){compare(await run(generated,token,Infinity,true,'next',true),await run(reference,token,Infinity,true,'next',true));++comparisons;}
    if(row.alias===8)for(const request of ['throw','return']){compare(await run(generated,token,Infinity,true,request),await run(reference,token,Infinity,true,request));++comparisons;}
    if(row.folded&&row.count===1) {
      const pending=[ast];let returned;
      while(pending.length){const node=pending.pop();if(node.type==='ReturnStatement')returned=node;for(const value of Object.values(node)){if(Array.isArray(value)){for(const child of value)if(child?.type)pending.push(child);}else if(value?.type)pending.push(value);}}
      const operand=returned?.argument??initialized.find(item=>item.id.name==='saved'&&item.init?.type==='CallExpression')?.init;
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
    if(row.constant>=14){const objects=[generated(()=>{},()=>{}),generated(()=>{},()=>{})];assert.notEqual(objects[0],objects[1]);}
    const run=(fn,value,fault,flag=true,iterations=2,request='next',reject=false)=>observeConstant(fn,row,value,fault,flag,iterations,request,reject);
    for(const value of [undefined,null,-0,NaN,1n,Symbol('input'),token])for(const flag of row.constant===4?[false,true,undefined,0,1n,token]:[true])
      for(const iterations of row.constant===8 || row.constant===9?[0,1,2,3]:[2])for(const fault of [Infinity,1,2,row.count+1]) {
        try{compare(await run(generated,value,fault,flag,iterations),await run(reference,value,fault,flag,iterations));}
        catch(error){error.message+=`; constant=${row.constant}, literal=${row.literal}, count=${row.count}, fault=${fault}`;throw error;}++comparisons;
      }
    if(row.constant===10){compare(await run(generated,token,Infinity,true,2,'next',true),await run(reference,token,Infinity,true,2,'next',true));++comparisons;}
    if(row.constant===11)for(const request of ['throw','return']){compare(await run(generated,token,Infinity,true,2,request),await run(reference,token,Infinity,true,2,request));++comparisons;}
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
  if(row.decoded_cell){const name=String.fromCharCode(...row.cell_name);assert.equal(declarations.find(node=>node.declarations.some(item=>item.id.name===name)).kind,'let');assert.equal(stores.filter(node=>node.expression.left.name===name).length,1);}
  if(row.fold || row.branch&&!row.decoded_cell) {assert.ok(initialized.length); ++folded;}
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
      await observe(reference,row,token,row.form===10 ? 1 : Infinity)),assert.AssertionError); ++mutations;
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
