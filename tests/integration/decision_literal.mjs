import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,writeFileSync,rmSync} from 'node:fs';
import {join,dirname} from 'node:path';
import {tmpdir} from 'node:os';
import {createHash} from 'node:crypto';
const [engine,fixtures]=process.argv.slice(2);
const temporary=mkdtempSync(join(tmpdir(),'abcd-decision-literal-'));
const run=(exe,args)=>{const p=spawnSync(exe,args,{encoding:'utf8'});assert.equal(p.status,0,p.stderr);return p.stdout;};
try {
  const manifest=JSON.parse(readFileSync(join(fixtures,'manifest.json')));
  for(const [file,hash] of Object.entries(manifest.hashes))assert.equal(createHash('sha256').update(readFileSync(join(fixtures,file))).digest('hex'),hash);
  const original=join(temporary,'original.mjs');writeFileSync(original,readFileSync(join(fixtures,'conditions.js')));
  const runner=join(temporary,'run.mjs');writeFileSync(runner,`
const m=await import(process.argv[2]);const trace=[];let cases=0;
for(const name of ['branch','choose','conjunction','nested'])
for(const mode of ['plain','flag-throw','lookup-throw','value-throw','call-throw','yes-throw','no-throw','replace','inherited-setter'])
for(const flag of [false,true])for(const result of [false,true]) {
  const input={tag:'original'};
  Object.defineProperties(input,{
    flag:{get(){trace.push('flag');if(mode==='flag-throw')throw new ReferenceError();return flag;}},
    test:{configurable:true,get(){trace.push('callee');if(mode==='lookup-throw')throw new RangeError();
      return function(o){trace.push(['call',this===input,Array.isArray(o.params),o.params.length,Object.keys(o),o.tail]);
        trace.push(['descriptor',Object.getOwnPropertyDescriptor(o,'params')]);
        if(mode==='call-throw')throw new URIError();return result;};}},
    value:{get(){trace.push('value');if(mode==='value-throw')throw new TypeError();
      if(mode==='replace')Object.defineProperty(input,'test',{value(){trace.push('replacement');return false;}});return 9;}},
    yes:{get(){trace.push('yes');if(mode==='yes-throw')throw new SyntaxError();return 11;}},
    no:{get(){trace.push('no');if(mode==='no-throw')throw new EvalError();return 22;}}
  });
  if(mode==='inherited-setter')Object.defineProperty(Object.prototype,'params',{configurable:true,set(){trace.push('setter');throw new Error();}});
  try {trace.push(['result',name,flag,result,m[name](input)]);}catch(e){trace.push(['throw',e.constructor.name]);}
  finally{if(mode==='inherited-setter')delete Object.prototype.params;}
  ++cases;
}
console.log(JSON.stringify({cases,trace}));`);
  const expected=run(process.execPath,[runner,original]);assert.equal(JSON.parse(expected).cases,144);
  let cases=0,mutations=0;
  for(const [label,flags] of [['previous',['--language-numeric','--language-constructors']],['source',['--language-source']]]) {
    const out=join(temporary,label);run(engine,[join(fixtures,'conditions.abc'),out,'--value-source',...flags]);
    const record=JSON.parse(readFileSync(join(out,'manifest.json'))).records.find(r=>r.source_status!==2);
    assert.equal(record?.source_status,0,JSON.stringify(record));
    const file=join(out,record.source);run(process.execPath,['--check',file]);
    assert.equal(run(process.execPath,[runner,file]),expected,label);cases+=144;
    const text=readFileSync(file,'utf8'),call=/(\$\w+)\.test\(/.exec(text);assert.ok(call);
    const binding=call[1];
    const variants=[
      text.replace(' || ',' && '),
      text.replace('params: []','params: [1]'),
      text.replace(`${binding}.test(`,`(0, ${binding}.test)(`),
      text.replace(`${binding}.value`, `(${binding}.value, ${binding}.value)`),
      text.replace(`${binding}.value`, `(${binding}.test, ${binding}.value)`),
      text.replace(`${binding}.flag`, `(${binding}.value, ${binding}.flag)`),
    ];
    for(const [i,changed] of variants.entries()) {
      assert.notEqual(changed,text);const path=join(dirname(file),`mutation-${i}.mjs`);writeFileSync(path,changed);
      run(process.execPath,['--check',path]);assert.notEqual(run(process.execPath,[runner,path]),expected,`unobserved mutation ${i}`);++mutations;
    }
  }
  console.log(`${cases} engine-output/original scenarios, ${mutations} observed mutations; compound literal predicates preserve order, exceptions, receiver and DefineOwnProperty`);
} finally {rmSync(temporary,{recursive:true,force:true});}
