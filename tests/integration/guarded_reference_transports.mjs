// Real compiler fixture -> engine output, with an independently executed oracle.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,writeFileSync,copyFileSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
const [structure, fixtures] = process.argv.slice(2);
const dir=mkdtempSync(join(tmpdir(),'abcd-guarded-transports-'));
const run=(exe,args)=>{const p=spawnSync(exe,args,{encoding:'utf8',timeout:120000});assert.ifError(p.error);return p;};
try {
  const runner=join(dir,'runner.mjs');
  writeFileSync(runner,`
const mode=process.argv[3]; globalThis.trace=[];
const record=x=>trace.push(x), receiver={tag:'original'};
Object.defineProperty(receiver,'method',{configurable:true,get(){
 record('callee');if(mode==='getter')throw new RangeError();
 return function(x){record(['call',this?.tag,x]);if(mode==='call')throw new SyntaxError();return x;};
}});
globalThis.receiver=mode==='null'?null:mode==='undefined'?undefined:receiver;
globalThis.provider=mode==='provider-null'?null:mode==='provider-undefined'?undefined:{};
for(const side of ['left','right'])Object.defineProperty(globalThis,side,{configurable:true,get(){
 record(side);if(mode===side)throw new URIError();
 if(mode==='rewrite')Object.defineProperty(receiver,'method',{configurable:true,value(){record('replacement');return 'wrong';}});
 if(mode==='receiver')globalThis.receiver={tag:'new',method(){record('new');return 'wrong';}};
 return {valueOf(){record('numeric');if(mode==='numeric')throw new TypeError();return mode==='bigint'?1n:4;}};
}});
const m=await import(process.argv[2]);
const results=[false,true,0,1,null,undefined,{},''].map(flag=>m.run(flag));
console.log(JSON.stringify({early:m.early,pure:m.pure(),results,trace}));
`);
  const original=join(dir,'original.mjs');copyFileSync(join(fixtures,'transport.js'),original);
  const modes=['none','getter','call','left','right','numeric','bigint','rewrite','receiver','null','undefined','provider-null','provider-undefined'];
  let compared=0,mutations=0,refused=0;
  for(const abc of ['transport.abc','transport-default.abc'])for(const [label,flags] of [
    ['default',[]],['source',['--language-source']]]){
    const out=join(dir,abc+'-'+label);
    const exported=run(structure,[join(fixtures,abc),out,'--value-source',...flags]);assert.equal(exported.status,0,exported.stderr);
    const records=JSON.parse(readFileSync(join(out,'manifest.json'))).records.filter(r=>r.source_status!==2);
    assert.equal(records.length,1);
    if(abc==='transport-default.abc'){
      // Existing default-optimizer handler layout is not admitted by the raw
      // CatchRegion proof. It is a refusal, never a differential pass.
      assert.equal(records[0].source_status,3);
      assert.deepEqual(records[0].source_failure.causes,[
        {domain:'abcd::source::PhasedMethodError',value:5},
        {domain:'abcd::source::ExceptionPlanError',value:2},
        {domain:'abcd::analysis::CatchRegionError',value:1}]);
      ++refused;continue;
    }
    assert.equal(records[0].source_status,0,JSON.stringify(records[0]));
    const file=join(out,records[0].source),text=readFileSync(file,'utf8');
    assert.equal(run(process.execPath,['--check',file]).status,0);
    assert.match(text,/\.method\(typeof \(1 \+ /);assert.match(text,/\.method\(typeof \$local\d+\)/);
    for(const mode of modes){
      const expected=run(process.execPath,[runner,original,mode]),actual=run(process.execPath,[runner,file,mode]);
      assert.equal(expected.status,0,expected.stderr);assert.equal(actual.status,0,actual.stderr);
      assert.equal(actual.stdout,expected.stdout,abc+'/'+label+'/'+mode);++compared;
    }
    const capture=/typeof (\$local\d+)/.exec(text)[1];
    const variants=[
      text.replaceAll('globalThis.receiver.method(', '(0, globalThis.receiver.method)('),
      text.replaceAll(`typeof ${capture}`, '"undefined"'),
      text.replaceAll('globalThis.left','globalThis.right'),
      `let ${capture};\n`+text.replace(`let ${capture} = globalThis.provider;`, `${capture} = globalThis.provider;`),
    ];
    for(const [i,mutant] of variants.entries()){
      assert.notEqual(mutant,text);const path=join(dir,`mutant-${i}.mjs`);writeFileSync(path,mutant);
      const actual=run(process.execPath,[runner,path,'none']),expected=run(process.execPath,[runner,original,'none']);
      assert.ok(actual.status!==0||actual.stdout!==expected.stdout,'unobserved mutation '+i);++mutations;
    }
  }
  console.log(`guarded_reference_transports: ${compared} Node comparisons, ${mutations} observed mutations, ${refused} optimizer-handler refusals`);
}finally{rmSync(dir,{recursive:true,force:true});}
