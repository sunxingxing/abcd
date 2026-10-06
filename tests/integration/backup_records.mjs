import assert from 'node:assert/strict';
import vm from 'node:vm';

// Run whole generated modules. Paired bodies stay independent; controlled imports
// and native promises expose identities, evaluation order and suspension behavior.
async function load(source, context, parent, logger) {
  const module = new vm.SourceTextModule(source,{context});
  const log = new vm.SyntheticModule(['default'],function() {this.setExport('default',logger);},{context});
  const base = new vm.SyntheticModule(['default'],function() {this.setExport('default',parent);},{context});
  const dependencies = new Map([['@ohos:hilog',log],['@ohos:application.BackupExtensionAbility',base]]);
  const imports = [];
  await module.link(name => {imports.push(name); assert.ok(dependencies.has(name)); return dependencies.get(name);});
  assert.deepEqual(imports,['@ohos:hilog','@ohos:application.BackupExtensionAbility']);
  assert.deepEqual(Reflect.ownKeys(module.namespace),['default',Symbol.toStringTag]);
  assert.throws(() => module.namespace.default,{name:'ReferenceError'});
  await module.evaluate();
  const C = module.namespace.default;
  assert.equal(C.name,'EntryBackupAbility'); assert.equal(C.length,0);
  assert.equal(Object.getPrototypeOf(C),parent);
  assert.deepEqual(Reflect.ownKeys(C.prototype),['constructor','onBackup','onRestore']);
  for (const [name,length] of [['onBackup',0],['onRestore',1]]) {
    const descriptor = Object.getOwnPropertyDescriptor(C.prototype,name), method = descriptor.value;
    assert.equal(descriptor.enumerable,false); assert.equal(descriptor.writable,true); assert.equal(descriptor.configurable,true);
    assert.equal(method.name,name); assert.equal(method.length,length);
    assert.equal(Object.hasOwn(method,'prototype'),false);
    assert.throws(() => Reflect.construct(method,[]),{name:'TypeError'});
  }
  assert.throws(() => Reflect.apply(C,undefined,[]),{name:'TypeError'});
  assert.throws(() => {module.namespace.default = parent;},{name:'TypeError'});
  await module.evaluate(); assert.equal(module.namespace.default,C);
  return {C,log,base};
}

async function constructor(source,args,mode,behavior,alternate,realm = 'host') {
  const events = [], failure = Symbol('constructor failure'), token = {};
  let expected;
  function dispatch(received,newTarget) {
    events.push(['base',received.length]); assert.equal(newTarget,expected);
    const offset = mode === 'bound' ? 1 : 0;
    assert.equal(received.length,args.length + offset);
    for (let i = 0; i < args.length; ++i) assert.ok(Object.is(received[i + offset],args[i]));
    if (behavior === 'throw') throw failure;
    return behavior === 'object' ? token : 7;
  }
  function HostBase(...received) {return dispatch(received,new.target);}
  const context = vm.createContext({dispatch});
  const Base = realm === 'host' ? HostBase : vm.runInContext('(function Base(...args) {return dispatch(args,new.target);})',context);
  const {C,base} = await load(source,context,Base,{});
  // Import replacement does not retroactively change the evaluated class parent.
  base.setExport('default',function Wrong() {assert.fail('class re-read its import');});
  assert.equal(Object.getPrototypeOf(C),Base);
  let parent = mode === 'bound' ? Base.bind(null,'prefix') : Base;
  if (mode === 'proxy') parent = new Proxy(Base,{construct(target,list,newTarget) {
    events.push(['proxy',list.length]); return Reflect.construct(target,list,newTarget);
  }});
  if (mode === 'arrow') parent = () => {};
  Object.setPrototypeOf(C,parent);
  const Target = realm === 'host' ? function Target() {} : vm.runInContext('(function Target() {})',context);
  if (alternate === 'null') Target.prototype = null;
  expected = alternate === 'class' ? C : alternate === 'throw-prototype' ? new Proxy(Target,{get(target,key,receiver) {
    if (key === 'prototype') {events.push(['prototype:get']); throw failure;}
    return Reflect.get(target,key,receiver);
  }}) : Target;
  try {
    const result = Reflect.construct(C,args,expected);
    assert.notEqual(mode,'arrow'); assert.notEqual(behavior,'throw');
    assert.equal(result === token,behavior === 'object');
    if (result !== token) {
      // GetPrototypeFromConstructor obtains the fallback from new.target's
      // realm, which may differ from the realm owning the derived class.
      const fallback = realm === 'host' ? Object.prototype : vm.runInContext('Object.prototype',context);
      assert.equal(Object.getPrototypeOf(result),alternate === 'null' ? fallback : expected.prototype);
      assert.deepEqual(Reflect.ownKeys(result),[]);
    }
    return {events,returnedObject:result === token};
  } catch (error) {
    if (error === failure) return {events,originalException:true};
    if (error instanceof assert.AssertionError) throw error;
    assert.equal(mode,'arrow'); assert.equal(error.name,'TypeError'); return {events,typeError:true};
  }
}

async function methods(source,{method,input,promise,fail = '',mutate = '',receiver = 'instance',concurrent = false}) {
  const events = [], failure = Symbol('method failure');
  const event = label => {events.push(label); if (label === fail) throw failure;};
  const context = vm.createContext({event,failure,promise,mutate,queueMicrotask});
  // Save realm intrinsics before replacing global bindings. Async functions must
  // return their own intrinsic promise and must not await logger return values.
  vm.runInContext(`
    globalThis.NativePromise = Promise;
    const stringify = JSON.stringify;
    globalThis.logger = {get info() {
      event('info:get');
      if (mutate === 'import') rebindLogger();
      if (mutate === 'callee') Object.defineProperty(logger,'info',{value(){event('wrong:replacement');},configurable:true});
      return function(...args) {
        if (this !== logger) throw new Error('logger receiver');
        event('info:call'); globalThis.logged = args;
        return {get then() {throw new Error('logger result was assimilated');}};
      };
    }};
    globalThis.otherLogger = {get info() {event('other:get'); return function(...args) {
      if (this !== otherLogger) throw new Error('live logger receiver');
      event('other:call'); globalThis.logged = args;
    };}};
    globalThis.JSON = {get stringify() {
      event('json:get'); const owner = JSON;
      if (mutate === 'json') globalThis.JSON = {stringify(){event('wrong:json');}};
      return function(value) {
        if (this !== owner) throw new Error('JSON receiver');
        event('json:call'); return stringify(value);
      };
    }};
    globalThis.Promise = {get resolve() {
      event('resolve:get'); const owner = Promise;
      if (mutate === 'promise') globalThis.Promise = {resolve(){event('wrong:resolve');}};
      return function(...args) {
        if (this !== owner || args.length !== 0) throw new Error('Promise receiver/arguments');
        event('resolve:call');
        if (promise === 0) return 7;
        if (promise === 1) return NativePromise.resolve(7);
        if (promise === 2) return NativePromise.reject(failure);
        if (promise === 7) return {get then() {event('then:get'); return 9;}};
        return {get then() {event('then:get'); return function(resolve,reject) {
          event('then:call');
          if (promise === 4) reject(failure);
          else if (promise === 8) resolve({get then() {event('nested:get'); return function(next) {
            event('nested:call'); next(7);
          };}});
          else {resolve(7); if (promise === 5) reject(failure); if (promise === 6) throw failure;}
        };}};
      };
    }};
  `,context);
  let value;
  switch (input) {
    case 0: value = undefined; break;
    case 1: value = null; break;
    case 2: value = {version:3,name:'backup'}; break;
    case 3: value = 1n; break;
    case 4: value = {}; value.self = value; break;
    case 5: value = {toJSON(key) {event(`toJSON:${key}`); return {version:7};}}; break;
    default: assert.fail('unknown input');
  }
  function Base() {event('base');}
  const {C,log} = await load(source,context,Base,context.logger);
  context.rebindLogger = () => log.setExport('default',context.otherLogger);
  const instance = new C(); assert.equal(Object.getPrototypeOf(instance),C.prototype);
  events.length = 0;
  const denied = new Proxy({}, {get() {throw new Error('unexpected receiver read');}});
  const target = receiver === 'instance' ? instance : receiver === 'null' ? null : denied;
  const invoke = ordinal => {
    let result;
    assert.doesNotThrow(() => {result = Reflect.apply(C.prototype[method],target,[value]);});
    assert.equal(Object.getPrototypeOf(result),context.NativePromise.prototype);
    events.push(`invoked:${ordinal}`);
    return Reflect.apply(context.NativePromise.prototype.then,result,[
      returned => {assert.equal(returned,undefined); events.push(`fulfilled:${ordinal}`);},
      error => {
        if (error === failure) events.push(`rejected:sentinel:${ordinal}`);
        else {assert.equal(error.name,'TypeError'); events.push(`rejected:TypeError:${ordinal}`);}
      },
    ]);
  };
  const pending = [invoke(0)];
  queueMicrotask(() => {events.push('checkpoint:1'); queueMicrotask(() => events.push('checkpoint:2'));});
  if (concurrent) {
    log.setExport('default',context.otherLogger);
    pending.push(invoke(1));
  }
  await Promise.all(pending);
  // Drain the scheduled independent checkpoints even for immediate rejection.
  await Promise.resolve(); await Promise.resolve();
  return {events,logged:context.logged ? Array.from(context.logged) : null};
}

export async function verifyBackupRecords(records) {
  let constructorCases = 0, methodCases = 0, mutations = 0;
  const payloads = [undefined,null,false,0,-0,NaN,1n,Symbol('argument'),{},[]];
  for (const {app,source,reference} of records) {
    assert.match(source,/class extends/);
    assert.match(source,/async onBackup\(\)/); assert.match(source,/async onRestore\(bundleVersion\)/);
    assert.match(source,/hilog\.info\(/); assert.match(source,/JSON\.stringify\(bundleVersion\)/);
    assert.doesNotMatch(source,/INCOMPLETE|UNRECOVERED|Reflect\.construct|\.call\(|\bswitch\s*\(|\$acc|\$reg/);
    // Temporary await results and mutable module bindings are permitted local
    // representation differences; constructor/method bodies remain native JS.
    for (const value of payloads) for (const mode of ['direct','bound','proxy','arrow'])
      for (const behavior of ['primitive','object','throw']) for (const alternate of ['class','foreign','null']) {
        const args = [value,{},value];
        assert.deepEqual(await constructor(source,args,mode,behavior,alternate),
          await constructor(reference,args,mode,behavior,alternate),app); ++constructorCases;
      }
    for (const size of [0,1,200,201]) {
      const args = Array.from({length:size},(_,i) => ({i}));
      assert.deepEqual(await constructor(source,args,'proxy','primitive','foreign'),
        await constructor(reference,args,'proxy','primitive','foreign')); ++constructorCases;
    }
    for (const mode of ['direct','bound','proxy','arrow']) for (const behavior of ['primitive','object','throw'])
      for (const alternate of ['class','foreign','null']) {
        const args = [{},Symbol('arg')];
        assert.deepEqual(await constructor(source,args,mode,behavior,alternate,'module'),
          await constructor(reference,args,mode,behavior,alternate,'module')); ++constructorCases;
      }
    for (const realm of ['host','module']) for (const mode of ['direct','bound','proxy','arrow']) {
      assert.deepEqual(await constructor(source,[] ,mode,'primitive','throw-prototype',realm),
        await constructor(reference,[],mode,'primitive','throw-prototype',realm)); ++constructorCases;
    }
    for (const method of ['onBackup','onRestore']) for (let input = 0; input < 6; ++input)
      for (let promise = 0; promise < 9; ++promise)
        for (const fail of ['', 'info:get','info:call','json:get','json:call','resolve:get','resolve:call',
          'then:get','then:call','nested:get','nested:call','toJSON:']) {
          const scenario = {method,input,promise,fail};
          assert.deepEqual(await methods(source,scenario),await methods(reference,scenario),
            `${app}/${method}/${input}/${promise}/${fail}`); ++methodCases;
        }
    for (const mutate of ['', 'callee','json','promise','import']) for (const receiver of ['instance','null','denied'])
      for (const concurrent of [false,true]) for (const method of ['onBackup','onRestore']) {
        const scenario = {method,input:5,promise:8,mutate,receiver,concurrent};
        assert.deepEqual(await methods(source,scenario),await methods(reference,scenario)); ++methodCases;
      }
    const variants = [
      [source.includes('DOMAIN = 0') ? 'DOMAIN = 0' : 'hilog.info(0,', source.includes('DOMAIN = 0') ? 'DOMAIN = 1' : 'hilog.info(1,'],
      ['JSON.stringify(bundleVersion)','JSON.stringify(undefined)'],
      ['async onBackup() {','async onBackup() { this.spurious;'],
    ];
    if (source.includes('super(...args)')) variants.push(['super(...args)','super()']);
    if (source.includes('(await $value0)')) variants.push(['(await $value0)','($value0)'],['(await $value0)','return (await $value0)']);
    for (const [from,to] of variants) {
      assert.ok(source.includes(from)); const altered = source.replace(from,to);
      new vm.SourceTextModule(altered); // Mutation must remain valid JavaScript.
      let detected = false;
      try {
        if (from === 'super(...args)') assert.deepEqual(await constructor(altered,[1,2],'proxy','primitive','class'),
          await constructor(reference,[1,2],'proxy','primitive','class'));
        else {
          const scenario = {method:from.startsWith('JSON') ? 'onRestore' : 'onBackup',input:2,promise:3,receiver:'denied'};
          assert.deepEqual(await methods(altered,scenario),await methods(reference,scenario));
        }
      } catch (error) {if (!(error instanceof assert.AssertionError)) throw error; detected = true;}
      assert.equal(detected,true,from); ++mutations;
    }
  }
  return {completeRecords:records.length,constructorCases,methodCases,mutations,
    wholeRecordControlledAcceptance:true,pairedIteratorContamination:false,platformExecution:false};
}
