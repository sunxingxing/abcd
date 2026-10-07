import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {parseJavaScript} from './class_reference.mjs';
const rows = execFileSync(process.argv[2], ['--emit'], {encoding:'utf8', maxBuffer:16_000_000}).trim().split('\n').map(JSON.parse);
const token = {}, second = {}, failure = Symbol('original throw');
function original(row) {
  if (row.decoded) return function(seed, report, flag) {
    const saved = seed(); if (!row.branch || flag !== false && flag !== 0) report(saved); report(saved); return saved;
  };
  if (row.form === 11) return Function('return function(seed,report,again){let saved;while(again()){' +
    'saved=seed();' + 'report(saved);'.repeat(row.count) + '}return;};')();
  if (row.form === 12) return Function('return function(seed,report,again){let saved;let carried=true;while(carried){' +
    'saved=seed();if(report){saved=seed();}' + 'report(saved);'.repeat(row.count) + 'carried=again(saved);}return;};')();
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
async function observe(fn, row, value, failAt, reject = false, request = 'next', flag = true, iterations = 2) {
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
    returned = fn(seed, report, row.form === 11 || row.form === 12 ? again : flag);
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
let comparisons=0, mutations=0, folded=0;
for (const row of rows) {
  const ast = parseJavaScript(row.source), pending=[ast], initialized=[], declarations=[], stores=[];
  while (pending.length) {
    const node=pending.pop(); if(node.type==='VariableDeclaration') {
      declarations.push(node); for(const item of node.declarations) if(item.init) initialized.push(item);
    }
    for(const child of Object.values(node)) {
      if(Array.isArray(child)) pending.push(...child.filter(value=>value?.type)); else if(child?.type) pending.push(child);
    }
    if(node.type==='ExpressionStatement' && node.expression.type==='AssignmentExpression' &&
        node.expression.left.type==='Identifier') stores.push(node);
  }
  if(row.fold || row.branch) {assert.ok(initialized.length); ++folded;}
  const generated=Function('return '+row.source)(), reference=original(row);
  for(const value of [undefined,null,false,0,-0,NaN,1n,Symbol('value'),token])
    for(const flag of row.decoded ? [false,true,0,-0,NaN,undefined,null,1n,Symbol('flag'),{}] : [true])
      for(const iterations of row.form === 11 || row.form === 12 ? [0,1,2,3] : [2])
      for(const fault of [Infinity,1,2,(row.count ?? 2)+1]) {
        try {compare(await observe(generated,row,value,fault,false,'next',flag,iterations),await observe(reference,row,value,fault,false,'next',flag,iterations));}
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
  if((row.form===4 || row.form===10 || row.form===12) && row.count===1) {
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
}
console.log(JSON.stringify({sources:rows.length,folded,comparisons,mutations,platformExecution:false}));
