import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, writeFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {createHash} from 'node:crypto';
const [engine, existing, fixtures] = process.argv.slice(2);
const temp = mkdtempSync(join(tmpdir(), 'abcd-nullish-call-'));
const run = (exe, args) => {
  const p = spawnSync(exe, args, {encoding: 'utf8', maxBuffer: 32 * 1024 * 1024});
  assert.equal(p.status, 0, p.stderr); return p.stdout;
};
try {
  const runner = join(temp, 'run.mjs');
  writeFileSync(runner, `
const m = await import(process.argv[2]); const name = process.argv[3];
const all = []; let count = 0;
for (const base of ['null', 'undefined', 'object'])
for (const result of ['null','undefined','false','zero','minusZero','nan','empty','one','text','bigint','symbol','object'])
for (const fallback of ['null','undefined','false','zero','minusZero','nan','empty','one','text','bigint','symbol','object'])
for (const mode of ['normal','callee-throw','noncallable','get-throw','call-throw','inner-get-throw','inner-call-throw','replace']) {
  const trace = []; const token = {}; const argument = {}; const sym = Symbol('value');
  const values = {null:null, undefined:undefined, false:false, zero:0, minusZero:-0, nan:NaN,
    empty:'', one:1, text:'x', bigint:2n, symbol:sym, object:token};
  token[Symbol.toPrimitive] = () => {trace.push('coercion'); throw new URIError();};
  const encode = x => x===token?'object':x===undefined?'undefined':x===sym?'symbol':
    typeof x==='bigint'?'bigint':Object.is(x,-0)?'minusZero':typeof x==='number'&&Number.isNaN(x)?'nan':x;
  const object = {}; let version = 0;
  for (const key of ['invoke', 'inner']) Object.defineProperty(object, key, {configurable:true,get() {
    trace.push(['callee',key,version]);
    if (mode===(key==='invoke'?'callee-throw':'inner-get-throw')) throw new RangeError();
    if (key==='invoke' && mode==='noncallable') return 1;
    const saved = version;
    return function(value) {
      trace.push(['invoke',key,saved,this===object,encode(value)]);
      if(key==='inner' && mode==='inner-call-throw') throw new EvalError();
      return value;
    };
  }});
  const a = base==='object'?{}:base==='null'?null:undefined;
  if(a) for(const key of ['m','b']) Object.defineProperty(a,key,{get() {
    trace.push(['get',key]); if(mode==='get-throw')throw new SyntaxError();
    if(mode==='replace') {++version;Object.defineProperty(object,'invoke',{configurable:true,value() {trace.push('wrong-callee');}});}
    if(key==='b')return values[result];
    return function(...args) {
      trace.push(['method',this===a,args.length,args[0]===argument]);
      if(mode==='call-throw')throw new TypeError(); return values[result];
    };
  }});
  try {
    let value;
    if(name==='optionalArgument')value=m[name](object,a,argument,values[fallback]);
    else if(name==='nullishChain')value=m[name](object,values[result],values[fallback],argument);
    else value=m[name](object,a,values[fallback]);
    trace.push(['return',value===argument?'argument':encode(value)]);
  } catch(e) {trace.push(['throw',e.constructor.name]);}
  all.push(trace); ++count;
}
console.log(JSON.stringify({count,all}));
`);
  let scenarios = 0, published = 0, refused = 0, mutations = 0;
  for (const root of [existing, fixtures]) {
    const manifest = JSON.parse(readFileSync(join(root, 'manifest.json')));
    for (const [file, sha] of Object.entries(manifest.hashes))
      assert.equal(createHash('sha256').update(readFileSync(join(root,file))).digest('hex'),sha);
    for (const e of manifest.fixtures) {
      if (!['optionalMethod','optionalProperty','nullishChain','nestedNullish','optionalArgument'].includes(e.name)) continue;
      const output = join(temp, e.abc); run(engine, [join(root,e.abc),output,'--value-source','--language-source']);
      const record = JSON.parse(readFileSync(join(output,'manifest.json'))).records.find(r=>r.source_status!==2);
      assert.equal(record.source_status,0,JSON.stringify(record)); ++published;
      const source = join(output,record.source); run(process.execPath,['--check',source]);
      const original = join(temp,e.name+'.mjs');writeFileSync(original,readFileSync(join(root,e.source)));
      const expected = run(process.execPath,[runner,original,e.name]);
      assert.equal(run(process.execPath,[runner,source,e.name]),expected,e.name); scenarios+=JSON.parse(expected).count;
      const text=readFileSync(source,'utf8'); assert.ok(text.includes(' ?? '));
      const wrong=join(temp,'wrong.mjs');
      for(const modified of [text.replaceAll(' ?? ',' || '),text.replace('$arg0.invoke(','(0, $arg0.invoke)(')]) {
        assert.notEqual(modified,text);writeFileSync(wrong,modified);
        assert.notEqual(run(process.execPath,[runner,wrong,e.name]),expected);++mutations;
      }
    }
  }
  assert.equal(published,10);assert.equal(refused,0);assert.equal(mutations,20);
  console.log(JSON.stringify({published,refused,scenarios,mutations}));
} finally {rmSync(temp,{recursive:true,force:true});}
