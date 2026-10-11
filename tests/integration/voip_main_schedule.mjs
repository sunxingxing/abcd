import assert from 'node:assert/strict';
import vm from 'node:vm';
import {editExpression} from './expression_edits.mjs';

// Drive promises returned by real methods and retained callbacks. Host controls
// releases; it never supplies a replacement body or infers completion by time.
async function observe(source,scenario) {
  const trace = [], requests = [], listeners = [], infoCallbacks = [], runs = [], outcomes = [];
  const abort = Symbol('original error getter failure');
  const failure = {};
  for (const key of ['code','message']) Object.defineProperty(failure,key,{get() {
    trace.push(['failure:get',key]);
    if (scenario.errorGetter === key) throw abort;
    return key === 'code' ? 17 : 'failure';
  }});
  const event = (...parts) => trace.push(parts);
  const request = (kind,id,value) => {
    const number = requests.length; event('request',number,kind,id);
    return new Promise((resolve,reject) => requests.push({number,kind,id,value,resolve,reject,settled:false}));
  };
  let finished = 0, service, logModule;
  const monitor = promise => {
    const number = runs.length;
    const nativeThen = vm.runInContext('Promise.prototype.then',context);
    assert.equal(Object.getPrototypeOf(promise),vm.runInContext('Promise.prototype',context));
    runs.push(Reflect.apply(nativeThen,promise,[value => {
      assert.equal(value,undefined); outcomes[number] = 'fulfilled'; ++finished;
    },error => {
      assert.ok(error === failure || error === abort); outcomes[number] = error === abort ? 'getter-failure' : 'original-failure'; ++finished;
    }]));
  };
  const enums = (name,values) => new Proxy(values,{get(target,key) {event(name,key); return target[key];}});
  const ui = enums('ui',{VOIP_CALL_EVENT_VOICE_ANSWER:1,VOIP_CALL_EVENT_VIDEO_ANSWER:2,
    VOIP_CALL_EVENT_REJECT:3,VOIP_CALL_EVENT_HANGUP:4});
  const state = enums('state',{VOIP_CALL_STATE_RINGING:10,VOIP_CALL_STATE_ANSWERED:11,
    VOIP_CALL_STATE_ACTIVE:12,VOIP_CALL_STATE_DISCONNECTED:13});
  const logger = label => {
    const result = {};
    for (const level of ['info','error']) Object.defineProperty(result,level,{get() {
      event('log:get',label,level);
      return function(...args) {
        assert.equal(this,result); event('log:call',label,level,...args);
        return {get then() {assert.fail('logging return value must be discarded');}};
      };
    }});
    return result;
  };
  const initialLog = logger('initial'), replacementLog = logger('replacement');
  const buffers = new Map();
  const pixels = new Map();
  for (let id = 0; id < 2; ++id) {
    const raw = new Uint8Array([id,7]); buffers.set(raw.buffer,id);
    const pixel = {get getImageInfo() {
      event('info:get',id);
      return function(callback) {
        assert.equal(this,pixel); event('info:register',id); infoCallbacks.push({id,callback});
      };
    }};
    pixels.set(id,{raw,pixel});
  }
  const image = {get createImageSource() {
    event('image:get');
    return function(buffer) {
      assert.equal(this,image);
      const id = buffers.get(buffer) ?? -1; event('image:call',id,buffer.byteLength);
      if (id === -1) assert.equal(buffer.byteLength,0);
      const imageSource = {get createPixelMap() {
        event('pixel:get',id);
        return function() {assert.equal(this,imageSource); return request('pixel',id,scenario.noPixel ? null : pixels.get(id === -1 ? 0 : id).pixel);};
      }};
      return imageSource;
    };
  }};
  const voip = {
    VoipCallUiEvent:ui,VoipCallState:state,VoipCallType:enums('type',{VOIP_CALL_VOICE:20,VOIP_CALL_VIDEO:21}),
    get on() {event('on:get'); return function(name,callback) {
      assert.equal(this,voip); assert.equal(name,'voipCallUiEvent'); event('on:call',listeners.length);
      assert.equal(callback.length,1); assert.equal(Object.hasOwn(callback,'prototype'),false);
      listeners.push(callback); if (scenario.registrationFailure) throw failure;
    };},
    get off() {event('off:get'); return function(name) {
      assert.equal(this,voip); event('off:call',name); if (scenario.offFailure) throw failure;
    };},
    get reportIncomingCall() {event('incoming:get'); return function(call) {
      assert.equal(this,voip);
      assert.deepEqual(Reflect.ownKeys(call),['callId','voipCallType','userName','userProfile','abilityName','voipCallState']);
      for (const key of Reflect.ownKeys(call)) {
        const d = Object.getOwnPropertyDescriptor(call,key);
        assert.ok('value' in d && d.writable && d.enumerable && d.configurable);
      }
      const id = call.callId === 'call:0' ? 0 : 1;
      assert.equal(call.userProfile,scenario.noPixel ? null : pixels.get(scenario.reject === 'raw' ? 0 : id).pixel);
      event('incoming:call',call.callId,call.voipCallType,call.userName,call.abilityName,call.voipCallState);
      return request('incoming',id,0);
    };},
    get reportCallStateChange() {event('change:get'); return function(id,value) {
      assert.equal(this,voip); event('change:call',id,value); return request('change',id,undefined);
    };},
  };
  const context = vm.createContext({Uint8Array});
  const module = new vm.SourceTextModule(source,{context});
  const hosts = new Map([['@hms:telephony.voipCall',voip],['@ohos:hilog',initialLog],['@ohos:multimedia.image',image]]);
  await module.link(name => {
    assert.ok(hosts.has(name));
    const dependency = new vm.SyntheticModule(['default'],function() {this.setExport('default',hosts.get(name));},{context});
    if (name === '@ohos:hilog') logModule = dependency;
    return dependency;
  });
  assert.throws(() => module.namespace.VoipCallService,{name:'ReferenceError'});
  await module.evaluate(); service = module.namespace.VoipCallService;
  const identity = service; await module.evaluate(); assert.equal(module.namespace.VoipCallService,identity);
  assert.deepEqual(Reflect.ownKeys(new service()),[]);
  assert.throws(() => Reflect.apply(service,null,[]),{name:'TypeError'});
  for (let id = 0; id < (scenario.concurrent ? 2 : 1); ++id) {
    const resource = {get getRawFileContent() {event('raw:get',id); return function(name) {
      assert.equal(this,resource); assert.equal(name,'example.png'); return request('raw',id,pixels.get(id).raw);
    };}};
    const owner = {get resourceManager() {event('resource:get',id); return resource;}};
    const data = JSON.stringify({data:JSON.stringify(id ? {scene:'video'} : scenario.scene),callId:`call:${id}`});
    monitor(Reflect.apply(service.processVoIPMainMsg,{wrongReceiver:true},[data,owner]));
  }
  const first = scenario.concurrent ? 2 : 1;
  const initialFinished = scenario.registrationFailure && scenario.errorGetter !== 'none' ? first : 0;
  assert.equal(requests.length,first - initialFinished,'main method must suspend after its first resource request');
  const advance = async predicate => {
    const maxMicrotasks = 48; // Test deadlock bound, not source scheduling policy.
    for (let i = 0; i < maxMicrotasks && !predicate(); ++i) await Promise.resolve();
    assert.ok(predicate(),'released operation must resume or finish');
  };
  const drain = async () => {
    await advance(() => finished + requests.filter(item => !item.settled).length >= runs.length);
    while (requests.some(item => !item.settled)) {
      const available = requests.filter(item => !item.settled);
      const item = scenario.reverse ? available.at(-1) : available[0];
      const priorRequests = requests.length, priorFinished = finished;
      item.settled = true; event('release',item.number,item.kind,item.id);
      service.callId = `resume:${item.number}`;
      if (scenario.replaceLogger) logModule.setExport('default',replacementLog);
      if (scenario.reject === item.kind) item.reject(failure); else item.resolve(item.value);
      await advance(() => requests.length > priorRequests || finished > priorFinished);
    }
    await advance(() => finished === runs.length); await Promise.all(runs);
  };
  await drain();
  // Native image-info callbacks are synchronous and can outlive the method.
  // Keep their real captured logger reads, input getters and original throws.
  for (const {id,callback} of scenario.reverse ? [...infoCallbacks].reverse() : infoCallbacks) {
    const info = {get size() {event('size:get',id); return {get width() {
      event('width:get',id); if (scenario.callbackFailure) throw abort; return 13;
    },get height() {event('height:get',id); return 7;}};}};
    try {assert.equal(Reflect.apply(callback,{wrongReceiver:true},[failure,scenario.noInfo ? null : info]),undefined);}
    catch (error) {assert.equal(error,abort); event('info:throw',id);}
  }
  for (const listener of scenario.reverse ? [...listeners].reverse() : listeners) {
    service.callId = 'listener-call';
    const input = {get voipCallUiEvent() {event('listener:event'); return scenario.input;}};
    monitor(Reflect.apply(listener,{wrongReceiver:true},[input])); await drain();
  }
  // close intentionally discards its started promise. Capture that exact
  // invocation's return to settle it; never replace its implementation.
  const original = service.processVoipCallEvent;
  let closing;
  service.processVoipCallEvent = function(...args) {
    assert.equal(this,service); closing = Reflect.apply(original,this,args); return closing;
  };
  service.callId = 'closing-call';
  try {assert.equal(Reflect.apply(service.close,{wrongReceiver:true},[]),undefined);}
  catch (error) {assert.equal(error,abort); event('close:throw');}
  assert.ok(closing); monitor(closing); await drain();
  return {trace,outcomes,callId:service.callId,listeners:listeners.length,infoCallbacks:infoCallbacks.length};
}

export async function verifyVoipMainSchedules(source,reference) {
  let schedules = 0, mutations = 0;
  for (const concurrent of [false,true]) for (const reverse of [false,true])
    for (const reject of ['none','raw','pixel','incoming','change']) for (const replaceLogger of [false,true])
      for (const errorGetter of ['none','code','message']) {
        const scenario = {concurrent,reverse,reject,replaceLogger,errorGetter,scene:{scene:'voice'},input:1};
        assert.deepEqual(await observe(source,scenario),await observe(reference,scenario),JSON.stringify(scenario)); ++schedules;
      }
  for (const scene of [null,{}, {scene:'video'}]) for (const input of [1,2,3,4,5])
    for (const registrationFailure of [false,true]) for (const offFailure of [false,true])
      for (const noPixel of [false,true]) for (const callbackFailure of [false,true]) {
        const scenario = {scene,input,registrationFailure,offFailure,noPixel,callbackFailure,
          concurrent:true,reverse:true,replaceLogger:true,reject:'none',errorGetter:'none'};
        assert.deepEqual(await observe(source,scenario),await observe(reference,scenario),JSON.stringify(scenario)); ++schedules;
      }
  // A host may retain the listener before registration throws. Replay that
  // actual callback after the main promise rejected; do not repair/restart it.
  for (const errorGetter of ['code','message'])
    for (const [registrationFailure,offFailure] of [[true,false],[false,true],[true,true]]) {
      const scenario = {scene:{},input:2,concurrent:true,reverse:false,replaceLogger:true,
        reject:'none',errorGetter,registrationFailure,offFailure};
      assert.deepEqual(await observe(source,scenario),await observe(reference,scenario),JSON.stringify(scenario)); ++schedules;
    }
  const baseline = {scene:{scene:'video'},input:1,concurrent:true,reverse:true,replaceLogger:true,reject:'none',errorGetter:'none'};
  const expected = await observe(reference,baseline);
  for (const [from,to] of [[source => editExpression(source,
      node => node.type === 'Property' && !node.computed && node.key.name === 'userProfile',
      (node, text) => text(node.key) + ': null'), null],
    ['$local1.processVoipCallEvent(event.voipCallUiEvent)','$local1.processVoipCallEvent(3)'],
    ['imageInfo.size.height','imageInfo.size.width'],['voipCall.off("voipCallUiEvent")','voipCall.off("wrong-event")']]) {
    if (typeof from !== 'function') assert.ok(source.includes(from));
    const altered = typeof from === 'function' ? from(source) : source.replace(from,to);
    assert.notEqual(altered, source); new vm.SourceTextModule(altered);
    let detected = false;
    try {assert.deepEqual(await observe(altered,baseline),expected);}
    catch (error) {if (!(error instanceof assert.AssertionError)) throw error; detected = true;}
    assert.equal(detected,true,from); ++mutations;
  }
  return {mainSchedules:schedules,mainMutations:mutations,platformExecution:false};
}
