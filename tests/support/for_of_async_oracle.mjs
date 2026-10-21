// Independent ordinary-call oracle; never imported by production code.
export async function traceAsyncForOf(fn,values,mode,outcome,throwAt,reject) {
  const log=[],body=new RangeError('body'),close=new URIError('close'),saved=globalThis.bodyError;
  globalThis.bodyError=body;let calls=0;
  const input={[Symbol.iterator](){log.push('acquire');let i=0;const iterator={
    next(){log.push('next');return i<values.length?{done:false,value:values[i++]}:{done:true};}
  };
  if(outcome!=='missing')Object.defineProperty(iterator,'return',{get(){
    log.push('return-get');if(outcome==='getter-throw')throw close;if(outcome==='null')return null;
    return function(){log.push(['return',this===iterator]);if(outcome==='call-throw')throw close;return outcome==='primitive'?1:{};};
  }});return iterator;}};
  try {
    const result=fn(input,x=>{
      if(x instanceof Error){log.push(['catch',x.constructor.name,x===body,x===close]);return;}
      log.push(['visit',x]);if(++calls===throwAt){if(reject)return Promise.reject(body);throw body;}
      return Promise.resolve(x).then(v=>{log.push(['resolved',v]);return v;});
    },mode);
    log.push('after-call');queueMicrotask(()=>log.push('microtask'));log.push(['result',await result]);
  }catch(e){log.push(['escaped',e.constructor.name,e===body,e===close]);}
  finally{globalThis.bodyError=saved;}
  await Promise.resolve();return log;
}

export async function traceAsyncMemberForOf(fn,count,fail,closeKind,member='visit') {
  const trace=[],body=new RangeError('body'),close=new URIError('close');
  const values={[Symbol.iterator](){let i=0;trace.push('acquire');return {
    next(){trace.push('next');if(i===count)return {done:true};const n=i++;
      const value={get [member](){trace.push(['get',n]);if(fail==='getter'&&n===0)throw body;
        return function(){trace.push(['visit',n,this===value]);if(fail==='throw'&&n===0)throw body;
          return fail==='reject'&&n===0?Promise.reject(body):Promise.resolve(n+10).then(x=>{trace.push(['resolved',n]);return x;});};}};
      return {done:false,value};},
    get return(){trace.push('return-get');if(closeKind==='getter')throw close;if(closeKind==='null')return null;
      return function(){trace.push('return');if(closeKind==='call')throw close;return closeKind==='primitive'?1:{};};}
  };}};
  let promise;try{promise=fn(values);trace.push('after-call');queueMicrotask(()=>trace.push('microtask'));trace.push(['result',await promise]);}catch(e){trace.push(['error',e.constructor.name,e===body,e===close]);}
  await Promise.resolve();return trace;
}
