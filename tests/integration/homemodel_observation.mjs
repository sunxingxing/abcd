import assert from 'node:assert/strict';
import vm from 'node:vm';
import {GraphTrace} from './graph_trace.mjs';
import {classLayouts, parseJavaScript} from './class_reference.mjs';

// Exercise the actual generated method and callback with independently supplied
// live imports. Never substitute the method body or read reference text here.
async function observe(source, config = {}) {
  const trace = new GraphTrace(config), context = vm.createContext({});
  const module = new vm.SourceTextModule(source, {context});
  const callbacks = [], items = [], tokens = [];
  let receiver, active;
  const values = Array.from({length: config.count ?? 3}, (_, i) => trace.tag({i}, 'item'+i));
  const push = function(item) {
    assert.equal(this, active); trace.event('push:call', item);
    items.push(item); return items.length;
  };
  const replacementPush = function(item) {
    assert.equal(this, list);trace.event('replacement-push:call',item);items.push(item);return items.length;
  };
  let selectedPush = push;
  const list = new Proxy({}, {get(target, key, owner) {
    assert.equal(owner, list); assert.equal(key, 'push'); trace.event('push:get');
    return selectedPush;
  }});
  trace.tag(list,'list'); active = list;
  receiver = {get tabBarList() {trace.event('list:get');return active;}};
  trace.tag(receiver,'receiver');
  const constants = {};
  const resource = key => new Proxy({}, {get(target,index) {
    trace.event(key+':index',String(index));
    if(config.rebind && key==='icon') {
      dependency.setExport('HomeConstants', replacement);
      selectedPush = replacementPush;
    }
    const token=trace.tag({},key+':'+String(index));tokens.push(token);return token;
  }});
  const icons=resource('icon'),activeIcons=resource('active');
  const text = {get map() {
    trace.event('map:get');return function(callback) {
      assert.equal(this,text);trace.event('map:call');
      assert.equal(callback.name,'');assert.equal(callback.length,2);
      assert.equal(Object.hasOwn(callback,'prototype'),false);
      assert.throws(()=>Reflect.construct(callback,[]),error=>error.name==='TypeError');
      callbacks.push(callback);
      if(!config.deferred) for(let i=0;i<values.length;++i) {
        if(config.sparse && i%2)continue;
        assert.equal(Reflect.apply(callback,{poison:true},[values[i],i]),undefined);
      }
      return {ignored:true};
    };
  }};
  trace.tag(text,'texts');
  for(const [key,value] of [['TAB_BAR_TEXT_LIST',text],['TAB_BAR_ICON_LIST',icons],['TAB_BAR_ACTIVE_ICON_LIST',activeIcons]])
    Object.defineProperty(constants,key,{get(){trace.event('constant:get',key);return value;}});
  const replacement={get TAB_BAR_ACTIVE_ICON_LIST(){trace.event('replacement:active');return activeIcons;},
    get TAB_BAR_ICON_LIST(){trace.event('replacement:icon');return icons;},
    get TAB_BAR_TEXT_LIST(){trace.event('replacement:text');return text;}};
  const dependency=new vm.SyntheticModule(['HomeConstants'],function(){this.setExport('HomeConstants',constants);},{context});
  await module.link(()=>dependency);await module.evaluate();
  const C=module.namespace.HomeModel;
  assert.equal(C.name,'HomeModel');assert.equal(C.length,0);
  const init=C.prototype.initTabBarList,get=C.prototype.getTabBarList;
  for(const name of ['initTabBarList','getTabBarList']) {
    const d=Object.getOwnPropertyDescriptor(C.prototype,name);
    assert.equal(d.enumerable,false);assert.equal(d.writable,true);assert.equal(d.configurable,true);
    assert.equal(d.value.name,name);assert.equal(d.value.length,0);
  }
  trace.call('init',init,receiver);
  if(config.deferred) for(const callback of callbacks) for(let i=values.length-1;i>=0;--i) {
    trace.call('deferred',callback,{poison:true},[values[i],i]);
    if(config.repeat)trace.call('repeat',callback,null,[values[i],i]);
  }
  trace.call('getter',get,receiver);
  assert.equal(trace.failed,config.failAt!==undefined,'requested fault must be reached');
  // Snapshot contents before tagging. Return identity relationships separately:
  // empty objects alone cannot distinguish resource and argument identity.
  const state=items.map(item=>({keys:Reflect.ownKeys(item),values:trace.normalize(item),
    text:values.indexOf(item.text),icon:tokens.indexOf(item.icon),active:tokens.indexOf(item.activeIcon)}));
  assert.equal(new Set(items).size,items.length,'each callback allocates a fresh item');
  return {trace:trace.trace,state,events:trace.events,callbacks:callbacks.length};
}

function style(source, reference) {
  assert.deepEqual(classLayouts(source),classLayouts(reference));
  const nodes=[],pending=[parseJavaScript(source)];
  while(pending.length){const node=pending.pop();nodes.push(node);
    for(const value of Object.values(node))if(Array.isArray(value)) {
      for(const child of value)if(child?.type)pending.push(child);
    } else if(value?.type)pending.push(value);
  }
  for(const name of ['map','push'])assert.ok(nodes.some(node=>node.type==='CallExpression'&&node.callee.type==='MemberExpression'&&node.callee.property.name===name));
  assert.ok(nodes.some(node=>node.type==='ArrowFunctionExpression'&&node.params.length===2));
  assert.ok(nodes.some(node=>node.type==='ObjectExpression'&&node.properties.map(property=>property.key.name).join(',')==='text,icon,activeIcon'));
  assert.ok(nodes.some(node=>node.type==='ArrayExpression'&&node.elements.length===0));
  assert.doesNotMatch(source,/INCOMPLETE|UNRECOVERED/);
}

// Ark instance initializers use PutValue. Node's initialized class field uses
// DefineField, so inherited setters require an independent assignment oracle.
async function constructorAssignments(source) {
  const context=vm.createContext({}),module=new vm.SourceTextModule(source,{context});
  const dependency=new vm.SyntheticModule(['HomeConstants'],function(){this.setExport('HomeConstants',{});},{context});
  await module.link(()=>dependency);await module.evaluate();
  const C=module.namespace.HomeModel;let cases=0;
  for(const failAt of [0,1,2,3,4])for(const failure of [undefined,null,0,Symbol('constructor fault'),{}]) {
    const events=[],state=[];let partial;
    const event=name=>{events.push(name);if(events.length===failAt)throw failure;};
    const proto={set tabBarList(value){partial=this;event('field');assert.ok(Array.isArray(value));assert.equal(value.length,0);state.push(value);},
      get initTabBarList(){assert.equal(this,partial);event('get-init');return function(){assert.equal(this,partial);event('call-init');};}};
    const target=new Proxy(function(){},{get(object,key,receiver){if(key==='prototype'){event('prototype');return proto;}return Reflect.get(object,key,receiver);}});
    let result,error,threw=false;
    try{result=Reflect.construct(C,[],target);}catch(value){threw=true;error=value;}
    assert.equal(threw,failAt!==0);if(threw)assert.ok(Object.is(error,failure));
    else {assert.equal(result,partial);assert.equal(Object.getPrototypeOf(result),proto);assert.deepEqual(Reflect.ownKeys(result),[]);}
    assert.deepEqual(events,['prototype','field','get-init','call-init'].slice(0,failAt||4));
    assert.equal(state.length,failAt===1||failAt===2?0:1);++cases;
  }
  return cases;
}

export async function verifyHomeModel(source,reference) {
  style(source,reference);const constructors=await constructorAssignments(source);let cases=0,faults=0;
  const configs=[];
  for(const count of [0,1,3,200,201])for(const sparse of [false,true])
    for(const deferred of [false,true])configs.push({count,sparse,deferred,repeat:deferred,rebind:true});
  for(const config of configs){assert.deepEqual(await observe(source,config),await observe(reference,config));++cases;}
  const baseline=await observe(reference,{deferred:true,repeat:true,rebind:true});
  for(let failAt=1;failAt<=baseline.events;++failAt)for(const failure of [undefined,null,0,Symbol('fault'),{}]) {
    const config={deferred:true,repeat:true,rebind:true,failAt,failure};
    assert.deepEqual(await observe(source,config),await observe(reference,config));++faults;
  }
  // AST-located mutations alter only generated executable expressions. They
  // must parse, run, and fail behavior comparison, not merely syntax checks.
  const parsed=parseJavaScript(source),nodes=[],work=[parsed];
  while(work.length){const node=work.pop();nodes.push(node);for(const value of Object.values(node)){
    if(Array.isArray(value)){for(const child of value)if(child?.type)work.push(child);}
    else if(value?.type)work.push(value);
  }}
  const object=nodes.find(node=>node.type==='ObjectExpression'&&node.properties.some(property=>property.key.name==='activeIcon'));
  const icon=object.properties.find(property=>property.key.name==='icon').value;
  const active=object.properties.find(property=>property.key.name==='activeIcon').value;
  const callback=nodes.find(node=>node.type==='ArrowFunctionExpression');
  const mutations=[
    [icon.start,icon.end,'undefined'],[active.start,active.end,source.slice(icon.start,icon.end)],
    [callback.body.start,callback.body.end,'{}'],
  ];
  for(const [begin,end,replacement]of mutations){const altered=source.slice(0,begin)+replacement+source.slice(end);parseJavaScript(altered);
    await assert.rejects(async()=>assert.deepEqual(await observe(altered,{deferred:true,repeat:true,rebind:true}),baseline),assert.AssertionError);
  }
  const field=nodes.find(node=>node.type==='AssignmentExpression'&&node.left.type==='MemberExpression'&&node.left.property.name==='tabBarList');
  assert.ok(field);
  const define=source.slice(0,field.start)+'Object.defineProperty(this, "tabBarList", {value: [], writable: true, enumerable: true, configurable: true})'+source.slice(field.end);
  parseJavaScript(define);await assert.rejects(()=>constructorAssignments(define),assert.AssertionError);
  return {homeModelCases:cases,homeModelFaults:faults,homeModelMutations:mutations.length+1,homeModelConstructorAssignments:constructors};
}
