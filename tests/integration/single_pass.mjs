import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
const load=async path=>import('data:text/javascript;base64,'+Buffer.from(await readFile(path)).toString('base64'));
const original=await load(process.argv[2]),emitted=await load(process.argv[3]);
assert.deepEqual(Object.keys(emitted),Object.keys(original));
const policies=['missing','null','object','primitive','noncallable','getterThrow','callThrow'];
let cases=0;
async function observe(module,name,options) {
  const log=[],ids=new WeakMap(),errors=new Map();let identity=0,outerChanged=false;
  const error=key=>{const e=new Error(key);errors.set(e,key);return e;};
  const bodyError=error('body'),failures={outer:error('outer'),inner:error('inner'),pattern:error('pattern')};
  const encode=x=>x===undefined?['undefined']:errors.has(x)?['user',errors.get(x),x.message]:
    x!==null&&(typeof x==='object'||typeof x==='function')?(ids.has(x)||ids.set(x,++identity),x instanceof Error?['error-value',x.constructor.name,ids.get(x)]:['identity',ids.get(x)]):x;
  const payload=which=>options.payload==='undefined'?undefined:options.payload==='null'?null:options.payload==='primitive'?13:failures[which];
  function iterable(which,values,policy) {
    let index=0;const receiver={};
    Object.defineProperty(receiver,'next',{get(){
      log.push([which,'next-get']);if(options.fail===which+':next-get')throw failures[which];
      return function(){assert.equal(this,receiver);log.push([which,'next',index]);
        if(options.fail===which+':next-call')throw failures[which];
        const done=index>=values.length,value=values[index++];
        return {get done(){log.push([which,'done',done]);if(options.fail===which+':done')throw failures[which];return done;},
          get value(){log.push([which,'value',encode(value)]);if(options.fail===which+':value')throw failures[which];return value;}};
      };
    }});
    Object.defineProperty(receiver,'return',{get(){
      const p=which==='outer'&&outerChanged?'getterThrow':policy;log.push([which,'return-get',p]);
      if(p==='getterThrow')throw payload(which);if(p==='missing')return undefined;if(p==='null')return null;if(p==='noncallable')return 7;
      return function(){assert.equal(this,receiver);log.push([which,'return-call']);if(which==='inner'&&options.mutate)outerChanged=true;
        if(p==='callThrow')throw payload(which);return p==='primitive'?0:{};
      };
    }});
    return {get [Symbol.iterator](){log.push([which,'iterator-get']);if(options.fail===which+':iterator-get')throw failures[which];const source=this;
      return function(){assert.equal(this,source);log.push([which,'acquire']);return receiver;};}};
  }
  const xs=iterable('outer',Array.from({length:options.outerSize??3},()=>({})),options.outer);
  const rows=x=>{log.push(['rows',encode(x)]);return iterable('inner',Array.from({length:options.innerSize??2},()=>name==='destructured'?iterable('pattern',[{},{}],options.pattern??'object'):{}),options.inner);};
  const value={};
  const visit=(...args)=>{
    log.push(['visit:before',...args.map(encode)]);if(options.bodyThrow)throw bodyError;
    const result=options.decision===false?false:value;
    if(['onceSync','allReturn','allThrow','nestedTerminal','returnInWhile'].includes(name))return result;
    return {then(resolve,reject){log.push(['suspend']);queueMicrotask(()=>{log.push(['visit:after',Boolean(options.reject)]);if(options.reject)reject(bodyError);else resolve(result);});}};
  };
  try {return {outcome:['return',encode(await module[name](xs,rows,visit))],log};}
  catch(e){return {outcome:errors.has(e)?['throw',encode(e)]:e instanceof Error?['error',e.constructor.name]:['throw-value',encode(e)],log};}
}
const scenarios=[];
for(const inner of policies)for(const outer of policies)for(const outerSize of [0,1,3])for(const innerSize of [0,1,3])scenarios.push({inner,outer,outerSize,innerSize});
for(const inner of policies)for(const outer of policies)for(const extra of [{bodyThrow:true},{reject:true},{decision:false}])scenarios.push({inner,outer,...extra});
for(const pattern of policies)for(const inner of ['missing','object','getterThrow','primitive'])for(const outer of ['missing','object','callThrow'])scenarios.push({pattern,inner,outer});
for(const which of ['inner','outer','pattern'])for(const point of ['iterator-get','next-get','next-call','done','value'])scenarios.push({inner:'object',outer:'object',fail:which+':'+point});
for(const payload of ['undefined','null','primitive'])for(const inner of ['getterThrow','callThrow'])scenarios.push({inner,outer:'callThrow',payload,bodyThrow:true});
scenarios.push({inner:'object',outer:'object',mutate:true});
for(const name of Object.keys(original))for(const scenario of scenarios) {
  assert.deepEqual(await observe(emitted,name,scenario),await observe(original,name,scenario),JSON.stringify({name,scenario}));++cases;
}
console.log(JSON.stringify({cases}));
