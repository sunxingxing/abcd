import {pathToFileURL} from 'node:url';
const {run}=await import(pathToFileURL(process.argv[2]));
const results=[];
for(const mode of ['plain','setter','readonly','firstthrow','secondthrow','getterthrow']) {
 let log='';const note=x=>{log+=x+';';};let value,error;
 const source={get first(){note('get:first');if(mode==='getterthrow')throw new RangeError();return ()=>{note('first');if(mode==='firstthrow')throw new TypeError();return 13;};},second(){note('second');if(mode==='secondthrow')throw new SyntaxError();return 29;}};
 const previous=Object.getOwnPropertyDescriptor(Object.prototype,'7');
 if(mode==='setter')Object.defineProperty(Object.prototype,'7',{configurable:true,set(){note('setter');throw new Error();}});
 if(mode==='readonly')Object.defineProperty(Object.prototype,'7',{configurable:true,writable:false,value:99});
 try{value=run(source);}catch(e){error=e.name;}finally{if(previous)Object.defineProperty(Object.prototype,'7',previous);else delete Object.prototype['7'];}
 const descriptors=typeof value==='object'&&value!==null?Object.getOwnPropertyDescriptors(value):null;
 results.push({mode,log,value,error,descriptors});
}
console.log(JSON.stringify(results));
