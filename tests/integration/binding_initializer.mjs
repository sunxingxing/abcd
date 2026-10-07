import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {parseJavaScript} from './class_reference.mjs';
const rows = execFileSync(process.argv[2], ['--emit'], {encoding:'utf8', maxBuffer:32_000_000}).trim().split('\n').map(JSON.parse);
const token = {}, second = {}, failure = Symbol('original throw');
function original(row) {
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
  let checks = 0;
  const again = function(value) {assert.equal(this, undefined); event('again', row.form === 12 ? value : checks); return checks++ < iterations;};
  try {
    returned = row.decoded_loop ? fn(again,seed,report,flag,stop) : fn(seed, report, row.form >= 11 ? again : flag);
    if (row.form === 6) {
      const generator = returned; const first = generator.next(); requests.push(first.value);
      const end = request === 'throw' ? generator.throw(failure) : request === 'return' ? generator.return(second) : generator.next(value);
      assert.equal(end.done, true); returned = end.value;
    }
    if (row.form === 5) returned = await returned;
  } catch (error) {thrown = error; returned = undefined;}
  return {trace, requests, returned, thrown};
}
function compare(actual, expected) {
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
  if(row.fold || row.branch) {assert.ok(initialized.length); ++folded;}
  const generated=Function('return '+row.source)(), reference=original(row);
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
      for(const fault of [Infinity,1,2,(row.count ?? 2)+1]) {
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
