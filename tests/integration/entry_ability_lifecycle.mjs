import assert from 'node:assert/strict';
import vm from 'node:vm';

const methods = [['onCreate',2],['onDestroy',0],['onWindowStageCreate',1],
  ['onWindowStageDestroy',0],['onForeground',0],['onBackground',0],['updateBreakpoint',1]];

async function load(source,context,exports) {
  const root = new vm.SourceTextModule(source,{context}), dependencies = new Map();
  await root.link(name => {
    assert.ok(exports.has(name),name);
    if (!dependencies.has(name)) {
      const values = exports.get(name);
      dependencies.set(name,new vm.SyntheticModule(Object.keys(values),function() {
        for (const [key,value] of Object.entries(values)) this.setExport(key,value);
      },{context}));
    }
    return dependencies.get(name);
  });
  assert.deepEqual(Reflect.ownKeys(root.namespace),['default',Symbol.toStringTag]);
  assert.throws(() => root.namespace.default,{name:'ReferenceError'});
  await root.evaluate(); const C = root.namespace.default;
  assert.equal(C.name,'EntryAbility'); assert.equal(C.length,0);
  assert.throws(() => Reflect.apply(C,null,[]),{name:'TypeError'});
  for (const [name,length] of methods) {
    const d = Object.getOwnPropertyDescriptor(C.prototype,name);
    assert.ok(d); assert.equal(d.enumerable,false); assert.equal(d.configurable,true); assert.equal(d.writable,true);
    assert.equal(d.value.name,name); assert.equal(d.value.length,length);
    assert.equal(Object.hasOwn(d.value,'prototype'),false);
    assert.throws(() => Reflect.construct(d.value,[]),{name:'TypeError'});
  }
  assert.equal(Reflect.set(root.namespace,'default',null),false);
  await root.evaluate(); assert.equal(root.namespace.default,C);
  return {C,dependencies};
}

// Run the original complete class and every callback, including after a failed
// registration retains it. Imports and scheduling are controlled, never bodies.
async function observe(source,app,options = {}) {
  const {mainCode = 0,reverse = false,concurrent = false,
    failAt = 0,failEvent = '',swapAt = 0,errorGetter = '',json = 'normal'} = options;
  const option = (key,fallback) => Object.hasOwn(options,key) ? options[key] : fallback;
  const width = option('width',700), density = option('density',1), code = option('code',0), device = option('device','phone');
  const trace = [], queued = [], listeners = [], storageState = new Map(), tags = new WeakMap();
  let steps = 0, failed = false, dependencies;
  const tag = (object,name) => {tags.set(object,name); return object;};
  const encode = value => (typeof value === 'object' && value !== null || typeof value === 'function') ?
    tags.get(value) ?? 'opaque' : typeof value === 'symbol' ? 'symbol' : value;
  const original = tag({},'original-error'), secondary = tag({},'error-getter-error');
  const event = (label,...values) => {
    trace.push([label,...values.map(encode)]);
    if (++steps === swapAt) for (const [name,values] of replacementExports)
      for (const [key,value] of Object.entries(values)) dependencies.get(name)?.setExport(key,value);
    if (steps === failAt || !failed && label === failEvent) {failed = true; throw original;}
  };
  Object.defineProperties(original,{
    code:{get() {event('failure.code'); if (errorGetter === 'code') throw secondary; return 17;}},
    message:{get() {event('failure.message'); if (errorGetter === 'message') throw secondary; return 'original failure';}},
  });
  const poison = {get then() {assert.fail('void host return was assimilated');}};
  const callable = (owner,label,body = () => poison) => function(...args) {
    assert.equal(this,owner,`${label} receiver`); event(`${label}:call`,...args); return body(...args);
  };
  const logger = label => {
    const owner = {};
    for (const name of ['info','error']) Object.defineProperty(owner,name,{get() {
      event(`${label}.${name}:get`); return callable(owner,`${label}.${name}`);
    }});
    return owner;
  };
  const scalar = (value,name) => value === 'coerce' ? tag({[Symbol.toPrimitive](hint) {
    event(`${name}:coerce`,hint); return name === 'density' ? 1 : 700;
  }},name) : value;
  const display = label => {
    const owner = {};
    Object.defineProperty(owner,'getDefaultDisplaySync',{get() {
      event(`${label}:get`); return callable(owner,label,() => ({get densityPixels() {
        event(`${label}.density`); return scalar(density,'density');
      }}));
    }});
    return owner;
  };
  const constants = label => ({
    get BREAKPOINT_RANGES() {event(`${label}.ranges`); return new Proxy([0,600,840],{get(target,key) {
      if (key === '1' || key === '2') event(`${label}.range`,key); return Reflect.get(target,key);
    }});},
    get BREAKPOINT_SM() {event(`${label}.sm`); return 'sm';},
    get BREAKPOINT_MD() {event(`${label}.md`); return 'md';},
    get BREAKPOINT_LG() {event(`${label}.lg`); return 'lg';},
  });
  const windowImport = label => ({get AvoidAreaType() {
    event(`${label}.avoid`); return {get TYPE_SYSTEM() {event(`${label}.system`); return 1;},
      get TYPE_NAVIGATION_INDICATOR() {event(`${label}.navigation`); return 2;}};
  }});
  const deviceInfo = label => ({get deviceType() {event(`${label}.type`); return device;}});
  const baseCommon = label => ({get DEVICE_2IN1() {event(`${label}.2in1`); return '2in1';}});
  const exports = new Map([
    ['@ohos:hilog',{default:logger('hilog')}],['@ohos:display',{default:display('display')}],
    ['@ohos:window',{default:windowImport('window')}],['@ohos:deviceInfo',{default:deviceInfo('device')}],
    ['@bundle:com.example.pasteboard/entry/ets/common/Logger',{default:logger('logger')}],
    ['@normalized:N&&&base/Index&1.0.0',{CommonConstants:baseCommon('common'),BreakpointConstants:constants('breakpoints')}],
  ]);
  const replacementExports = new Map([
    ['@ohos:hilog',{default:logger('new-hilog')}],['@ohos:display',{default:display('new-display')}],
    ['@ohos:window',{default:windowImport('new-window')}],['@ohos:deviceInfo',{default:deviceInfo('new-device')}],
    ['@bundle:com.example.pasteboard/entry/ets/common/Logger',{default:logger('new-logger')}],
    ['@normalized:N&&&base/Index&1.0.0',{CommonConstants:baseCommon('new-common'),BreakpointConstants:constants('new-breakpoints')}],
  ]);
  function Base() {}
  exports.set('@ohos:app.ability.UIAbility',{default:Base});
  const storage = {};
  Object.defineProperty(storage,'setOrCreate',{get() {
    event('storage:get'); return callable(storage,'storage',(key,value) => {storageState.set(key,value); return poison;});
  }});
  const context = vm.createContext({AppStorage:storage});
  const nativeStringify = vm.runInContext('JSON.stringify',context), jsonOwner = {};
  Object.defineProperty(jsonOwner,'stringify',{get() {
    event('json:get'); return callable(jsonOwner,'json',value => {
      if (json === 'undefined') return undefined;
      if (json === 'null') return null;
      if (json === 'throw') throw original;
      return nativeStringify(value);
    });
  }});
  context.JSON = jsonOwner;
  const loaded = await load(source,context,exports); dependencies = loaded.dependencies;
  const C = loaded.C; assert.equal(Object.getPrototypeOf(C),Base);
  dependencies.get('@ohos:app.ability.UIAbility').setExport('default',function Wrong() {assert.fail('heritage was re-read');});
  const fieldState = [];
  const outcome = (label,fn) => {
    try {assert.equal(fn(),undefined); trace.push([label,'returned']);}
    catch (error) {
      if (error instanceof assert.AssertionError) throw error;
      trace.push([label,error === original ? 'original' : error === secondary ? 'secondary' : error.name]);
    }
  };
  const stage = number => {
    const label = `request${number}`, owner = {}, win = tag({},`${label}:window`), ui = tag({},`${label}:ui`);
    Object.defineProperty(ui,'px2vp',{get() {event(`${label}.px:get`); return callable(ui,`${label}.px`,n => n / 2);}});
    const getter = (key,body) => Object.defineProperty(win,key,{get() {
      event(`${label}.${key}:get`); return callable(win,`${label}.${key}`,body);
    }});
    getter('getUIContext',() => ui);
    getter('setWindowLayoutFullScreen');
    getter('getWindowProperties',() => ({get windowRect() {event(`${label}.rect`); return {get width() {
      event(`${label}.width`); return scalar(width,'width');
    }};}}));
    getter('getWindowAvoidArea',type => ({get topRect() {event(`${label}.top`); return {get height() {event(`${label}.top.height`); return 48;}};},
      get bottomRect() {event(`${label}.bottom`); return {get height() {event(`${label}.bottom.height`); return 24;}};}}));
    getter('on',(name,callback) => {listeners.push({label,callback}); event(`${label}.listener:retained`,name); return poison;});
    Object.defineProperty(owner,'getMainWindowSync',{get() {
      event(`${label}.sync:get`); return callable(owner,`${label}.sync`,() => win);
    }});
    for (const kind of ['getMainWindow','loadContent']) Object.defineProperty(owner,kind,{get() {
      event(`${label}.${kind}:get`); return callable(owner,`${label}.${kind}`,(...args) => {
        const callback = args.at(-1); assert.equal(typeof callback,'function');
        if (kind === 'loadContent') assert.equal(args.length,2); else assert.equal(args.length,1);
        queued.push({label,kind,callback,win}); event(`${label}.${kind}:retained`); return poison;
      });
    }});
    return owner;
  };
  for (let number = 0; number < (concurrent ? 2 : 1); ++number) {
    const owner = new C();
    assert.deepEqual(Reflect.ownKeys(owner),app === 'pasteboard' ? ['curBp'] : []);
    if (app === 'pasteboard') {
      const d = Object.getOwnPropertyDescriptor(owner,'curBp');
      assert.deepEqual([d.value,d.writable,d.enumerable,d.configurable],['',true,true,true]);
      fieldState[number] = '';
      Object.defineProperty(owner,'curBp',{enumerable:true,configurable:true,get() {
        event(`owner${number}.curBp:get`); return fieldState[number];
      },set(value) {event(`owner${number}.curBp:set`,value); fieldState[number] = value;}});
    }
    const denied = new Proxy({}, {get() {assert.fail('unused lifecycle parameter read');}});
    for (const [name] of methods.filter(([name]) => name !== 'updateBreakpoint' && name !== 'onWindowStageCreate'))
      outcome(`${number}.${name}`,() => Reflect.apply(C.prototype[name],owner,[denied,denied]));
    outcome(`${number}.createStage`,() => owner.onWindowStageCreate(stage(number)));
  }
  while (queued.length) {
    const {label,kind,callback,win} = reverse ? queued.pop() : queued.shift();
    const error = {get code() {event(`${label}.${kind}.code`); return kind === 'loadContent' ? code : mainCode;}};
    const data = kind === 'loadContent' ? {toJSON() {event(`${label}.data:toJSON`); return {loaded:true};}} : win;
    outcome(`${label}.${kind}:callback`,() => Reflect.apply(callback,{foreign:true},[error,data]));
  }
  // Replay real retained listener functions after lifecycle completion/failure;
  // foreign this must not replace the captured activation's actual receiver.
  for (const {label,callback} of (reverse ? listeners.toReversed() : listeners))
    for (const value of [599,840]) outcome(`${label}.listener`,() => Reflect.apply(callback,null,[{get width() {
      event(`${label}.listener.width`); return value;
    }}]));
  return {trace,steps,listeners:listeners.length,fields:fieldState,
    storage:[...storageState].map(([key,value]) => [key,encode(value)])};
}

// Pinned STOBJBYNAME initializes curBp by assignment. Native TS DefineField is
// not an oracle when an inherited setter exists; test this boundary explicitly.
async function assignments(source,app,fail,count = 201) {
  const trace = [], original = Symbol('setter exception'); let receiver;
  const args = Array.from({length:count},(_,i) => ({i}));
  function Base(...received) {
    assert.equal(received.length,args.length);
    received.forEach((value,i) => assert.equal(value,args[i]));
    receiver = this; trace.push('base');
  }
  if (app === 'pasteboard') Object.defineProperty(Base.prototype,'curBp',{set(value) {
    assert.equal(this,receiver); assert.equal(value,''); trace.push('setter'); if (fail) throw original;
  }});
  const exports = new Map([
    ['@ohos:app.ability.UIAbility',{default:Base}],['@ohos:hilog',{default:{}}],
    ['@ohos:display',{default:{}}],['@ohos:window',{default:{}}],['@ohos:deviceInfo',{default:{}}],
    ['@bundle:com.example.pasteboard/entry/ets/common/Logger',{default:{}}],
    ['@normalized:N&&&base/Index&1.0.0',{CommonConstants:{},BreakpointConstants:{}}],
  ]);
  const {C} = await load(source,vm.createContext({}),exports);
  if (app === 'pasteboard' && fail) assert.throws(() => Reflect.construct(C,args),error => error === original);
  else {const result = Reflect.construct(C,args); assert.equal(result,receiver); assert.deepEqual(Reflect.ownKeys(result),[]);}
  assert.deepEqual(trace,app === 'pasteboard' ? ['base','setter'] : ['base']);
}

export async function verifyEntryAbilities(records) {
  let pairedCases = 0, assignmentCases = 0, mutations = 0;
  for (const {app,source,reference} of records) {
    assert.match(source,/class extends/); assert.match(source,/super\(\.\.\./);
    for (const [name] of methods) assert.match(source,new RegExp(`\\b${name}\\(`));
    assert.match(source,/\.getMainWindow\(/); assert.match(source,/\.loadContent\(/);
    assert.match(source,/\.on\("windowSizeChange"/); assert.match(source,/AppStorage\.setOrCreate\(/);
    assert.doesNotMatch(source,/INCOMPLETE|UNRECOVERED|Reflect\.construct|\.call\(|\bswitch\s*\(|\$acc|\$reg/);
    const compare = async options => {
      assert.deepEqual(await observe(source,app,options),await observe(reference,app,options),`${app} ${JSON.stringify(options,(_,v) => typeof v === 'bigint' ? String(v) : v)}`);
      ++pairedCases;
    };
    for (const width of [undefined,null,false,0,-0,599,600,839,840,NaN,Infinity,'600',1n,Symbol('width'),'coerce'])
      for (const density of [undefined,0,1,2,NaN,1n,Symbol('density'),'coerce']) await compare({width,density});
    for (const code of [undefined,null,false,0,1,true,NaN,{},Symbol('code')])
      for (const mainCode of [0,1]) for (const reverse of [false,true])
        for (const json of ['normal','undefined','null','throw']) await compare({code,mainCode,reverse,json,concurrent:true});
    for (const device of ['phone','2in1',undefined]) for (const reverse of [false,true]) {
      const options = {device,reverse,concurrent:true};
      const baseline = await observe(source,app,options); assert.equal(baseline.listeners,2);
      await compare(options);
      // Fault every event in the completed schedule, rather than silently
      // limiting injection to an early prefix of one callback arrangement.
      for (let failAt = 1; failAt <= baseline.steps; ++failAt) await compare({...options,failAt});
    }
    const baseline = await observe(source,app,{concurrent:true,reverse:true});
    for (let swapAt = 1; swapAt <= baseline.steps; ++swapAt) await compare({swapAt,concurrent:true,reverse:true});
    for (const errorGetter of ['code','message']) for (const failEvent of ['display:call','display.density']) {
      const options = {errorGetter,failEvent,concurrent:true};
      const result = await observe(source,app,options);
      // The failure must reach an actual catch and its accessor, not merely
      // abort before registration and accidentally satisfy an equal trace.
      assert.ok(result.trace.some(([label]) => label === `failure.${errorGetter}`));
      await compare(options);
    }
    for (const count of [0,1,200,201]) for (const fail of [false,true]) {
      await assignments(source,app,fail,count); ++assignmentCases;
    }
    for (const [before,after,options] of [
      ['"windowSizeChange"','"wrongEvent"',{}],
      ['data.getWindowProperties().windowRect.width','void 0',{}],
      ['AppStorage.setOrCreate("currentBreakpoint"','AppStorage.setOrCreate("wrongBreakpoint"',{}],
      [app === 'pasteboard' ? '"pages/Home"' : '"pages/MainPage"','"pages/Wrong"',{}],
      ['hilog.info(0, "testTag", "%{public}s", "Ability onCreate");',
        'return hilog.info(0, "testTag", "%{public}s", "Ability onCreate");',{}],
    ]) {
      assert.ok(source.includes(before),before); const altered = source.replace(before,after);
      new vm.SourceTextModule(altered);
      await assert.rejects(async () => assert.deepEqual(await observe(altered,app,options),
        await observe(reference,app,options)),error => error instanceof assert.AssertionError); ++mutations;
    }
    const callbackReceiver = source.replace('((windowSize) => {','(function(windowSize) {')
      .replace(/\$local\d+\.updateBreakpoint\(windowSize\.width\)/,'this.updateBreakpoint(windowSize.width)');
    assert.notEqual(callbackReceiver,source); new vm.SourceTextModule(callbackReceiver);
    await assert.rejects(async () => assert.deepEqual(await observe(callbackReceiver,app,{concurrent:true,reverse:true}),
      await observe(reference,app,{concurrent:true,reverse:true})),error => error instanceof assert.AssertionError); ++mutations;
    if (app === 'pasteboard') {
      const altered = source.replace('(this.curBp = "");',
        'Object.defineProperty(this,"curBp",{value:"",writable:true,enumerable:true,configurable:true});');
      assert.notEqual(altered,source); new vm.SourceTextModule(altered);
      await assert.rejects(assignments(altered,app,false),error => error instanceof assert.AssertionError); ++mutations;
    }
  }
  return {pairedCases,assignmentCases,mutations,wholeRecordStyle:'accepted',platformExecution:false};
}
