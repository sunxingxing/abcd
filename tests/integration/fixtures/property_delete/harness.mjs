const {run}=await import(process.argv[2]);
const results=[];
for(const mode of ['plain','missing','getter','readonly','proxy','proxy-false','proxy-throw','key','key-throw','null','primitive']){
 const log=[];let object={p:7},key='p';
 if(mode==='missing')key='q';
 if(mode==='getter')Object.defineProperty(object,'p',{configurable:true,get(){log.push('GET');return 9;}});
 if(mode==='readonly')Object.defineProperty(object,'p',{configurable:false,value:7});
 if(mode.startsWith('proxy'))object=new Proxy(object,{get(t,k,r){log.push('get:'+String(k));return Reflect.get(t,k,r);},deleteProperty(t,k){log.push('delete:'+String(k));if(mode==='proxy-throw')throw new RangeError();if(mode==='proxy-false')return false;return Reflect.deleteProperty(t,k);}});
 if(mode.startsWith('key'))key={[Symbol.toPrimitive](hint){log.push('key:'+hint);if(mode==='key-throw')throw new SyntaxError();return 'p';}};
 if(mode==='null')object=null;
 if(mode==='primitive')object=5;
 let value;try{value=run(object,key);}catch(error){value=error.name;}
 results.push({mode,value,log,has:object===null?null:Object.hasOwn(Object(object),'p')});
}
console.log(JSON.stringify(results));
