import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {dirname, join} from 'node:path';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';
import {homeStyle,homeEquivalents} from './home_record_style.mjs';
const [tool, abc] = process.argv.slice(2);
assert.equal(createHash('sha256').update(readFileSync(abc)).digest('hex'),
  '854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e');
// Production exits after reading ABC only. The complete paired module enters
// a separate oracle realm, never the engine or any generated function body.
const report = JSON.parse(execFileSync(tool,[abc,'6100','--record-source','--value-source'],{encoding:'utf8'}));
assert.equal(report.record,5521); assert.equal(report.source_complete,true);
assert.equal(report.route,'value_tree'); assert.equal(report.legacy_writer_used, undefined);
const paired = readFileSync(join(dirname(abc),'pasteboard_src/src/main/ets/pages/Home.ts'),'utf8');
const declarations = paired.match(/^    private [A-Za-z_$][\w$]*: [^;=\n]+;$/gm);
assert.equal(declarations?.length,7);
const reference = stripTypeScriptTypes(paired.replace(/^    private [A-Za-z_$][\w$]*: [^;=\n]+;\n/gm,''));
homeStyle(report.source,reference);
let scenarios = 0;const cases=[];
async function exercise(source, options) {
  const {finalize, granted, auth, uri, deferred, failure, promiseFailure, provided, breakpoint, promises, hasData} = options;
  const trace=[], pending=[], settlers=[], clicks=[], dialogs=[], tags=new WeakMap();
  const exception={code:7,message:'same failure'};
  let current, target, factory, registration, element=0;
  const tag=(value,name)=>{tags.set(value,name);return value;};
  const normalize=value=>{
    if(value===exception)return 'exception';
    if(typeof value==='function')return ['function',value.name,value.length];
    if(!value||typeof value!=='object')return value;
    if(tags.has(value))return tags.get(value);
    if(Array.isArray(value))return Array.from(value,normalize);
    const result={}; for(const key of Object.keys(value))result[key]=normalize(value[key]);return result;
  };
  const event=(name,...args)=>{trace.push([name,...args.map(normalize)]);if(name===failure)throw exception;};
  function surface(name, bodies) {
    let owner; owner=new Proxy({}, {get(_,key){
      assert.equal(typeof key,'string');event(`${name}.${key}:get`);
      // Native Promise resolution probes ordinary returned objects for then.
      if(key==='then'&&!Object.hasOwn(bodies,key))return undefined;
      assert.ok(Object.hasOwn(bodies,key),`${name}.${key}`);
      if(typeof bodies[key]!=='function')return bodies[key];
      const fn=function(...args){assert.equal(this,owner);event(`${name}.${key}:call`,...args);return bodies[key](...args);};
      for(const property of ['call','apply','bind'])Object.defineProperty(fn,property,{get(){assert.fail('mutable call adapter');}});
      return fn;
    }});return tag(owner,name);
  }
  class Observed {
    constructor(value,owner,name){assert.equal(owner,current);this.value=value;this.name=name;tag(this,`cell:${name}`);event('observed',name);}
    get(){event(`get:${this.name}`);return this.value;}
    set(value){event(`set:${this.name}`,value);this.value=value;}
    purgeDependencyOnElmtId(id){event('purge',this.name,id);}
    aboutToBeDeleted(){event('delete',this.name);}
  }
  class ViewPU {
    constructor(parent,storage,id,extra){current=this;target=new.target;tag(this,'page');event('super',parent,storage,id,extra);}
    createStorageLink(name,value,key){assert.equal(this,current);event('link',name,value,key);return new Observed(value,this,key);}
    createStorageProp(name,value,key){assert.equal(this,current);event('prop',name,value,key);return new Observed(breakpoint,this,key);}
    observeComponentCreation2(callback,component){assert.equal(this,current);event('observe',component);const id=++element;
      const invoke=()=>Reflect.apply(callback,{wrongReceiver:true},[id,true]);if(deferred)pending.push(invoke);else invoke();}
    id__(){assert.equal(this,current);event('id');return 17;}
    aboutToBeDeletedInternal(){assert.equal(this,current);event('delete.internal');}
    updateDirtyElements(){assert.equal(this,current);event('dirty');}
    getUIContext(){assert.equal(this,current);event('ui');return ui;}
    static create(dialog){assert.equal(this,ViewPU);event('view.create',dialog);}
  }
  if(finalize)ViewPU.prototype.finalizeConstruction=function(){assert.equal(this,current);event('finalize');};
  class Scroller {constructor(){tag(this,'scroller');event('scroller');}}
  class NavPathStack {constructor(){tag(this,'stack');event('stack');}pushPathByName(...args){event('push',...args);}}
  class CustomDialogController {
    constructor(options,owner){assert.equal(owner,current);this.options=options;tag(this,'controller');event('controller',options);}
    open(){event('dialog.open');}close(){event('dialog.close');}
  }
  class TipsDialog {
    constructor(owner,params,storage,id,callback,info){assert.equal(owner,current);this.params=params;tag(this,'dialog');dialogs.push(this);event('dialog',params,storage,id,callback,info);}
    setController(controller){event('dialog.controller',controller);}
  }
  class BreakpointType {
    constructor(...values){this.values=values;event('breakpoint',...values);}
    GetValue(value){event('breakpoint.value',value);return this.values[value==='sm'?0:value==='md'?1:2];}
  }
  const constants=surface('constants',{BREAK_POINT_SM:'sm',FULL_PERCENT:'100%'});
  const logger=surface('logger',{info:()=>undefined,error:()=>undefined});
  const hilog=surface('hilog',{error:()=>undefined});
  const host=tag({},'host');const ui=surface('ui',{getHostContext:()=>host});
  const data=surface('data',{getPrimaryUri:()=>uri});
  function chain(name,value,error=false) {
    if(promises==='native')return error?Promise.reject(exception):Promise.resolve(value);
    if(promises==='deferred')return new Promise((resolve,reject)=>settlers.push(()=>{
      event('settle',name);if(error)reject(exception);else resolve(value);
    }));
    let failed=error;
    return surface(name,{then:callback=>{
      assert.equal(callback.length,1);
      if(!failed){try{Reflect.apply(callback,{wrongReceiver:true},[value]);}catch(raised){assert.equal(raised,exception);failed=true;}}
      return surface(`${name}.result`,{catch:callback=>{
        assert.equal(callback.length,1);if(failed)Reflect.apply(callback,{wrongReceiver:true},[exception]);return undefined;
      }});
    }});
  }
  const system=surface('system',{hasDataType:()=>hasData,clearData:()=>undefined,
    getData:()=>chain('data.promise',data,promiseFailure==='data')});
  const pasteboard=surface('pasteboard',{MIMETYPE_TEXT_URI:'text/uri',getSystemPasteboard:()=>system});
  const manager=surface('manager',{checkAccessTokenSync:()=>granted?0:1,
    requestPermissionsFromUser:(context,permissions)=>{
      assert.equal(context,host);assert.ok(Array.isArray(permissions));assert.equal(permissions.length,1);
      assert.equal(permissions[0],'ohos.permission.READ_PASTEBOARD');
      return chain('permission.promise',{authResults:surface('auth',{toString:()=>auth})},promiseFailure==='permission');
    }});
  const access=surface('access',{createAtManager:()=>manager,GrantStatus:surface('grant',{PERMISSION_GRANTED:0})});
  const bundle=surface('bundle',{getBundleInfoForSelfSync:()=>({appInfo:{accessTokenId:91}}),
    BundleFlag:surface('flags',{GET_BUNDLE_INFO_WITH_APPLICATION:1})});
  const subscriber=surface('subscriber',{delete:()=>undefined});
  const button=surface('Button',{createWithLabel:()=>undefined,fontSize:()=>undefined,width:()=>undefined,height:()=>undefined,
    margin:()=>undefined,backgroundColor:()=>undefined,pop:()=>undefined,onClick:callback=>clicks.push(callback)});
  const column=surface('Column',{create:()=>undefined,height:()=>undefined,width:()=>undefined,justifyContent:()=>undefined,
    padding:()=>undefined,pop:()=>undefined});
  const navigation=surface('Navigation',{create:()=>undefined,titleMode:()=>undefined,hideBackButton:()=>undefined,
    mode:()=>undefined,title:()=>undefined,padding:()=>undefined,height:()=>undefined,width:()=>undefined,
    backgroundColor:()=>undefined,pop:()=>undefined});
  const globals={ViewPU,Scroller,NavPathStack,CustomDialogController,ObservedPropertySimplePU:Observed,
    Navigation:navigation,Column:column,Button:button,NavigationTitleMode:{Mini:'mini'},NavigationMode:{Stack:'stack'},FlexAlign:{End:'end'},
    SubscriberManager:surface('SubscriberManager',{Get:()=>subscriber}),
    registerNamedRoute(callback,name,info){assert.equal(this,undefined);event('register',name,info);factory=callback;registration=normalize(info);}};
  const context=vm.createContext(globals);const module=new vm.SourceTextModule(source,{context});const imports=[];
  await module.link(specifier=>{
    imports.push(specifier);let exports;
    if(specifier==='@ohos:arkui.advanced.Dialog')exports={TipsDialog};
    else if(specifier==='@ohos:pasteboard')exports={default:pasteboard};
    else if(specifier==='@ohos:abilityAccessCtrl')exports={default:access};
    else if(specifier==='@ohos:bundle.bundleManager')exports={default:bundle};
    else if(specifier==='@ohos:hilog')exports={default:hilog};
    else if(specifier.endsWith('/CommonConstants'))exports={CommonConstants:constants,BreakpointType};
    else if(specifier.endsWith('/Logger'))exports={default:logger};else assert.fail(specifier);
    return new vm.SyntheticModule(Object.keys(exports),function(){for(const [key,value]of Object.entries(exports))this.setExport(key,value);},{context});
  });
  try {
    await module.evaluate();assert.deepEqual(Object.keys(module.namespace),[]);assert.equal(factory.length,0);
    const instance=Reflect.apply(factory,{wrongReceiver:true},[]);const Type=instance.constructor;
    assert.equal(Type,target);assert.equal(Type.name,'Index');assert.equal(Type.length,3);assert.equal(Type.getEntryName(),'Index');
    assert.equal(Object.getPrototypeOf(Type),ViewPU);assert.equal(Object.getPrototypeOf(Type.prototype),ViewPU.prototype);
    assert.deepEqual(Reflect.ownKeys(instance),['__bottomRectHeight','__topRectHeight','__curBp','__uri','scroller','pageStack','dialogControllerImage']);
    const params=provided?{uri:'PageABuilder',scroller:new Scroller(),pageStack:new NavPathStack()}:{};
    instance.setInitiallyProvidedValue(params);instance.updateStateVars({});
    instance.onPageShow();instance.requestPermissionsFn();
    instance.dialogControllerImage.options.builder();assert.equal(dialogs.length,1);
    const first=dialogs[0].params;first.primaryButton.action();first.secondaryButton.action();first.onCheckedChange();
    const second=Reflect.apply(dialogs[0].paramsGenerator_,{wrongReceiver:true},[]);
    second.primaryButton.action();second.secondaryButton.action();second.onCheckedChange();
    instance.initialRender();while(pending.length)pending.shift()();assert.equal(clicks.length,3);
    for(const callback of clicks){assert.equal(callback.length,0);Reflect.apply(callback,{wrongReceiver:true},[]);}
    instance.purgeVariableDependenciesOnElmtId(9);instance.rerender();instance.aboutToBeDeleted();
    // Permission -> getData -> catch uses at most six native reaction jobs.
    const microtaskRounds=8;
    for(let round=0;round<microtaskRounds;++round){while(settlers.length)settlers.shift()();await Promise.resolve();}
    assert.equal(settlers.length,0);
    if(promises!=='thenable'&&!promiseFailure&&hasData&&(granted||auth==='0'))
      assert.ok(trace.some(entry=>entry[0]==='data.getPrimaryUri:call'));
    trace.push(['return']);
  }catch(error){assert.equal(error,exception,`unexpected complete Home failure: ${String(error)}`);trace.push(['throw']);}
  return {trace,registration,imports};
}
const defaults={finalize:true,granted:true,auth:'0',uri:'PageABuilder',deferred:false,failure:'',promiseFailure:'',provided:false,breakpoint:'sm',promises:'thenable',hasData:true};
async function compare(options,source=report.source) {
  if(source===report.source)cases.push(options);
  assert.deepEqual(await exercise(source,{...defaults,...options}),await exercise(reference,{...defaults,...options}),JSON.stringify(options));++scenarios;
}
for(const finalize of [false,true])for(const granted of [false,true])for(const auth of ['0','1'])
  for(const uri of ['PageABuilder','PageBBuilder','other'])for(const deferred of [false,true])
    for(const provided of [false,true])for(const breakpoint of ['sm','md','lg'])
      await compare({finalize,granted,auth,uri,deferred,provided,breakpoint});
for(const promises of ['native','deferred'])for(const granted of [false,true])
  for(const promiseFailure of ['','data','permission'])for(const uri of ['PageABuilder','PageBBuilder','other'])
    await compare({promises,granted,promiseFailure,uri});
for(const granted of [false,true])await compare({hasData:false,granted});
for(const promiseFailure of ['data','permission'])for(const granted of [false,true])await compare({promiseFailure,granted});
for(const failure of ['register','super','link','prop','observed','scroller','stack','controller','finalize',
  'pasteboard.getSystemPasteboard:call','system.hasDataType:call','bundle.getBundleInfoForSelfSync:call',
  'manager.checkAccessTokenSync:call','data.getPrimaryUri:call','dialog.open','manager.requestPermissionsFromUser:call',
  'auth.toString:call','logger.info:call','dialog','dialog.controller','view.create','system.clearData:call',
  'observe','Button.onClick:call','breakpoint.value','push','purge','dirty','delete','SubscriberManager.Get:call','id','delete.internal'])
  await compare({failure});
const changed=report.source.replace('"ohos.permission.READ_PASTEBOARD"','"wrong.permission"');assert.notEqual(changed,report.source);
assert.doesNotThrow(()=>new vm.SourceTextModule(changed,{context:vm.createContext({})}));
await assert.rejects(compare({granted:false},changed));
let equivalents=0;
const baselineCases=[...cases];
for(const equivalent of homeEquivalents(report.source)) {
  assert.notEqual(equivalent,report.source);homeStyle(equivalent,reference);
  for(const options of baselineCases)await compare(options,equivalent);
  ++equivalents;
}
const definition=homeStyle(report.source,reference);
const render=definition.body.body.find(method=>method.key.name==='initialRender');
const at=render.value.body.start+1;
const dispatcher=report.source.slice(0,at)+'while(false){}'+report.source.slice(at);
new vm.SourceTextModule(dispatcher,{context:vm.createContext({})});
let styleNeutralCases=0;
for(const options of baselineCases) {
  assert.deepEqual(await exercise(dispatcher,{...defaults,...options}),await exercise(reference,{...defaults,...options}));++styleNeutralCases;
}
assert.throws(()=>homeStyle(dispatcher,reference),assert.AssertionError);
console.log(JSON.stringify({record:5521,pairedScenarios:scenarios,equivalentRepresentations:equivalents,
  styleNeutralCases,semanticMutations:1,representationMutations:1,
  wholeRecordBehavior:'accepted',wholeRecordStyle:'accepted',platformExecution:false}));
