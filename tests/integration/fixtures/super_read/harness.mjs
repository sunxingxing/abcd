import {pathToFileURL} from 'node:url';
const results=[];
for(const mode of ['plain','getterthrow','argthrow','callthrow','rewrite','parentchange','receiverchange','proxy','nullparent','staticparentchange','uncallable']) {
 const log=[];const note=x=>log.push(x);
 function getter() {note('get:'+this.tag);if(mode==='getterthrow')throw new RangeError();if(mode==='uncallable')return 17;return function(x){note('call:'+this.tag+':'+x);if(mode==='callthrow')throw new SyntaxError();return this.tag+':'+x;};}
 class Base{}
 Object.defineProperty(Base.prototype,'value',{configurable:true,get:getter});
 Object.defineProperty(Base,'value',{configurable:true,get:getter});
 globalThis.TestBase=Base;
 const {Child}=await import(pathToFileURL(process.argv[2]).href+'?'+mode);
 const instance=new Child();instance.tag='instance';Child.tag='static';
 Object.defineProperty(instance,'value',{get(){throw new EvalError('wrong base');}});
 if(mode==='parentchange')Object.setPrototypeOf(Child.prototype,{get value(){note('newparent');return function(x){note('newcall:'+this.tag);return x+1;};}});
 if(mode==='proxy')Object.setPrototypeOf(Child.prototype,new Proxy(Base.prototype,{get(t,k,r){note('proxy:'+String(k)+':'+r.tag);return Reflect.get(t,k,r);}}));
 if(mode==='staticparentchange')Object.setPrototypeOf(Child,{get value(){note('newstatic');return function(x){note('staticcall:'+this.tag);return x+2;};}});
 if(mode==='nullparent')Object.setPrototypeOf(Child.prototype,null);
 const arg=()=>{note('arg');if(mode==='argthrow')throw new TypeError();if(mode==='rewrite')Object.defineProperty(Base.prototype,'value',{configurable:true,value(){note('WRONG');return 0;}});return 7;};
 const receiver=mode==='receiverchange'?{tag:'other'}:instance;
 const invoke=f=>{try{return f();}catch(error){return error.name;}};
 const value=invoke(()=>Child.prototype.run.call(receiver,arg));
 const staticValue=invoke(()=>Child.run(arg));
 const read=invoke(()=>Child.prototype.read.call(receiver));
 results.push({mode,value,staticValue,read:typeof read==='function'?'function':read,log});
}
delete globalThis.TestBase;console.log(JSON.stringify(results));
