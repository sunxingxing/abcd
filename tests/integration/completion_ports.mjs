import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {pathToFileURL} from 'node:url';
const original=await import('data:text/javascript;base64,'+Buffer.from(await readFile(process.argv[2])).toString('base64'));
const emitted=await import(pathToFileURL(process.argv[3]));
assert.deepEqual(Object.keys(emitted).sort(),Object.keys(original).sort());
const policies=['missing','null','object','primitive','noncallable','getterThrow','callThrow'];
let cases=0;
async function run(module,name,options) {
  const log=[],known=new Map(),identities=new WeakMap();let nextIdentity=0,changed=false;
  const bodyError=new Error('body failure'),innerError=new Error('inner failure'),outerError=new Error('outer failure');
  known.set(bodyError,'body');known.set(innerError,'inner');known.set(outerError,'outer');
  const encode=value=>{
    if(value===undefined)return ['undefined'];
    if(known.has(value))return ['user',known.get(value),value.message];
    if(value!==null&&(typeof value==='object'||typeof value==='function')) {
      if(!identities.has(value))identities.set(value,++nextIdentity);
      return ['identity',identities.get(value)];
    }
    return value;
  };
  const failure=which=>options.payload==='undefined'?undefined:options.payload==='null'?null:
    options.payload==='number'?19:which==='outer'?outerError:innerError;
  function iterable(label,values,policy,which) {
    const receiver={};let cursor=0;
    Object.defineProperty(receiver,'next',{get(){
      log.push([label,'next-get']);
      return function(){
        assert.equal(this,receiver);log.push([label,'next',cursor]);
        const done=cursor>=values.length,value=values[cursor++];
        return {get done(){log.push([label,'done',done]);return done;},
          get value(){log.push([label,'value',encode(value)]);return value;}};
      };
    }});
    Object.defineProperty(receiver,'return',{get(){
      const actual=which==='outer'&&changed?'getterThrow':policy;
      log.push([label,'return-get',actual]);
      if(actual==='getterThrow')throw failure(which);
      if(actual==='missing')return undefined;
      if(actual==='null')return null;
      if(actual==='noncallable')return 17;
      return function(){
        assert.equal(this,receiver);log.push([label,'return-call']);
        if(which==='inner'&&options.mutate)changed=true;
        if(actual==='callThrow')throw failure(which);
        return actual==='primitive'?0:{};
      };
    }});
    return {get [Symbol.iterator](){
      log.push([label,'iterator-get']);const self=this;
      return function(){assert.equal(this,self);log.push([label,'acquire']);return receiver;};
    }};
  }
  const outerValues=Array.from({length:options.size??2},()=>({}));
  const row=()=>iterable('pattern',[{},{}],options.inner,'inner');
  const xs=iterable('outer',name==='pattern'?outerValues.map(row):outerValues,options.outer,'outer');
  const ys=x=>{
    log.push(['ys',encode(x)]);
    const values=name==='breakAndPattern'?[row(),row()]:[{},{}];
    return iterable('inner',values,options.inner,'inner');
  };
  const visit=(...args)=>{
    log.push(['visit',...args.map(encode)]);
    if(options.bodyThrow)throw bodyError;
    if(name==='suspended'||name==='caughtSuspended'||name==='singleIteration'||(name==='breakAndPattern'&&args.length===2))return {then(yes,no){
      log.push(['suspend']);queueMicrotask(()=>{
        log.push(['resume',Boolean(options.reject)]);if(options.reject)no(bodyError);else yes(!options.exhaust);
      });
    }};
    return !options.exhaust;
  };
  const choose=(...args)=>{
    log.push(['choose',...args.map(encode)]);
    if(options.bodyThrow)throw bodyError;
    return args.length===1?options.action:options.action>=6?-1:options.action;
  };
  try {
    let value;
    if(name==='pattern')value=await module[name](xs,visit);
    else if(name==='transfers')value=await module[name](xs,ys,choose);
    else if(name==='breakAndPattern')value=await module[name](xs,ys,x=>{
      log.push(['stop',encode(x)]);return Boolean(options.stop);
    },visit);
    else value=await module[name](xs,ys,visit);
    return {outcome:['return',encode(value)],log};
  } catch(error) {
    const outcome=known.has(error)?['throw',encode(error)]:error instanceof Error?
      ['error',error.constructor.name]:['throw-value',encode(error)];
    return {outcome,log};
  }
}
const scenarios=[];
for(const inner of policies)for(const outer of policies) {
  for(const action of [0,1,2,3,4,5,6,7,8,9])scenarios.push({inner,outer,action});
  scenarios.push({inner,outer,action:4,bodyThrow:true},{inner,outer,action:4,reject:true});
}
for(const payload of ['undefined','null','number'])for(const inner of ['getterThrow','callThrow'])
  scenarios.push({inner,outer:'callThrow',action:4,payload});
scenarios.push({inner:'object',outer:'object',action:4,mutate:true},
  {inner:'object',outer:'object',action:4,size:0},
  {inner:'object',outer:'object',action:4,exhaust:true},
  {inner:'object',outer:'object',action:4,stop:true});
for(const name of Object.keys(original).sort())for(const options of scenarios) {
  assert.deepEqual(await run(emitted,name,options),await run(original,name,options),JSON.stringify({name,options}));
  ++cases;
}
console.log(JSON.stringify({cases}));
