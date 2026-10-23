import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,writeFileSync,rmSync} from 'node:fs';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';
const [structure,fixtures]=process.argv.slice(2);
const run=(exe,args)=>{const r=spawnSync(exe,args,{encoding:'utf8',timeout:120000,maxBuffer:16*1024*1024});assert.ifError(r.error);assert.equal(r.status,0,r.stderr+'\n'+r.stdout);return r.stdout;};
const registry=JSON.parse(readFileSync(join(fixtures,'build.json')));
assert.equal(registry.fixtures.length,6);
for(const fixture of registry.fixtures){
 const hash=file=>createHash('sha256').update(readFileSync(join(fixtures,file))).digest('hex');
 assert.equal(hash(fixture.source),fixture.sha256,'source identity');
 assert.equal(hash(fixture.abc),fixture.abc_sha256,'ABC identity');
}
const dir=mkdtempSync(join(tmpdir(),'abcd-capture-scope-'));
const harness=`
const log=[], readers=[];let serial=0;
const observe=async cb=>{try{log.push(['read',await cb()]);}catch(e){log.push(['read-error',e.constructor.name]);}};
globalThis.__bind=async(mode,cb,out,trace)=>{
 out.push(cb);trace.push('bind');await observe(cb);trace.push('await');
 await Promise.resolve();if(mode==='reject')throw new TypeError('reject');return ++serial;
};
globalThis.__escape=(cb,out,trace)=>{out.push(cb);try{trace.push(['early',cb()]);}catch(e){trace.push(['early-error',e.constructor.name]);}};
globalThis.__initialize=(mode,trace)=>{trace.push('init');if(mode==='reject')throw new TypeError('reject');return ++serial;};
const m=await import(process.argv[2]);
for(const mode of ['skip','success','reject','after','other']){
 const out=[];log.push(['mode',mode]);
 try{log.push(['result',await m.bind.call({id:23},mode,log,out)]);}catch(e){log.push(['error',e.constructor.name]);}
 await Promise.resolve();for(const r of out)await observe(r);readers.push(...out);
}
log.push('retained');for(const r of readers)await observe(r);
console.log(JSON.stringify(log));
`;
try {
 const runner=join(dir,'runner.mjs');writeFileSync(runner,harness);
 const evaluate=p=>run(process.execPath,[runner,pathToFileURL(resolve(p)).href]);
 let comparisons=0,mutations=0,published=0,refused=0;
 for(const [contract,flags] of [['previous',['--language-numeric','--language-constructors']],['source',['--language-source']]]){
 for(const name of ['async','sync','nested']){
  const oracle=join(dir,name+'-oracle.mjs');writeFileSync(oracle,readFileSync(join(fixtures,name+'.js')));
  for(const mode of ['O0','default']){
   const out=join(dir,contract+'-'+name+'-'+mode);run(structure,[join(fixtures,name+(mode==='O0'?'':'.default')+'.abc'),out,'--value-source',...flags]);
   const records=JSON.parse(readFileSync(join(out,'manifest.json'))).records.filter(r=>r.source_status!==2);
   assert.equal(records.length,1);const record=records[0];
   if(mode==='default'){
    assert.equal(record.source_status,3,JSON.stringify(record));
    const domains=record.source_failure.causes.map(c=>c.domain);
    // Since the optimizer's `sta c; lda c` completion catch and code_size-0
    // completion handler are accepted (2026-10-23), async/nested default
    // builds reach the same zero-length user handler shape as sync.
    assert.ok(domains.includes('abcd::analysis::CatchRegionError'),JSON.stringify(record));
    assert.equal(record.source_failure.causes.at(-1).value,1);++refused;continue;
   }
   if(contract==='previous' && name==='async'){
    // Scope/activation is admitted; this fixture's guarded array-return value
    // reaches a separate existing evaluation-ownership obligation.
    assert.equal(record.source_status,3,JSON.stringify(record));
    assert.deepEqual(record.source_failure.causes,[
      {domain:'abcd::source::EvaluationError',value:5},
      {domain:'abcd::source::EvaluationObligation',value:0}]);++refused;continue;
   }
   assert.equal(record.source_status,0,JSON.stringify(record));++published;
   const output=join(out,record.source);run(process.execPath,['--check',output]);
   const expected=evaluate(oracle);assert.equal(evaluate(output),expected,name);++comparisons;
   const text=readFileSync(output,'utf8');
   const declaration=/let ([$\w]+) = (?:await |globalThis\.__initialize)/.exec(text);
   assert.ok(declaration,name+' initialized declaration');const symbol=declaration[1];
   const replace=(label,changed)=>{
    assert.notEqual(changed,text,label+' must mutate');const path=join(out,label+'.mjs');writeFileSync(path,changed);
    run(process.execPath,['--check',path]);assert.notEqual(evaluate(path),expected,name+' '+label);++mutations;
   };
   // Empty declaration at allocation loses early TDZ and escaped-rejection TDZ.
   replace('hoisted','let '+symbol+';\n'+text.replace('let '+symbol+' =',symbol+' ='));
   replace('wrong-cell',text.replaceAll('return '+symbol,'return 99').replaceAll('return ['+symbol,'return [99'));
   if(name!=='sync'){
    assert.ok(text.includes('async () =>'));
    replace('wrong-catch',text.replace(/([$\w]+)\.push\(([$\w]+)\.constructor\.name\);/,'$1.push("wrong-catch");'));
   }
  }
 }
 }
 console.log(JSON.stringify({published,refused,comparisons,mutations}));
}finally{rmSync(dir,{recursive:true,force:true});}
