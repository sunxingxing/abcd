import assert from 'node:assert/strict';
import vm from 'node:vm';
import {GraphTrace} from './graph_trace.mjs';
import {classLayouts, parseJavaScript} from './class_reference.mjs';

const Mode=Object.freeze({ordinary:Symbol('ordinary'),sparse:Symbol('sparse')});
const Count=Object.freeze({coercible:Symbol('coercible')});
const Prototype=Object.freeze({class:Symbol('class'),foreign:Symbol('foreign'),fallback:Symbol('fallback'),foreignFallback:Symbol('foreign-fallback')});
const firstConstruction=['target:prototype','initial:trap','initial:prototype','initial:construct','store'];

const views = [
  {record:'HomeViewModel',name:'HomeViewModel',model:'HomeModel',field:'homeModel',methods:[['getTabBarList',false,'tabBarList']]},
  {record:'IconInfoViewModel',name:'IconInfoViewModel',model:'IconInfoModel',field:'iconInfoModel',
    methods:[['getCategoryList',false,'categoryList'],['getWelfareList',true,'welfareList']]},
  {record:'RecommendViewModel',name:'RecommendProductViewModel',model:'RecommendModel',field:'model',methods:[['getRecommendProductList',false,'recommendProductList']]},
  {record:'SectionViewModel',name:'SectionViewModel',model:'SectionModel',field:'model',
    methods:[['getSectionProductList',true,'sectionProductList'],['getFlashSaleProductList',true,'flashSaleProductList']]},
];
const specifier = name => '@normalized:N&&&shopping/src/main/ets/'+name+'&1.0.0';
function clean(node,names=new Map()) {
  if(Array.isArray(node))return node.map(value=>clean(value,names));
  if(node===null||typeof node!=='object')return node;
  return Object.fromEntries(Object.entries(node).filter(([key])=>!['start','end','raw','loc'].includes(key)).map(([key,value])=>{
    if(node.type==='Identifier'&&key==='name')return [key,names.get(value)??value];
    const property=key==='property'&&node.type==='MemberExpression'&&!node.computed;
    return [key,clean(value,property?new Map():names)];
  }));
}
function style(source,paired,config) {
  assert.deepEqual(classLayouts(source),classLayouts(paired));
  const find = text=>parseJavaScript(text).body.flatMap(node=>node.type==='ExportNamedDeclaration'?[node.declaration]:[node]).flatMap(node=>
    node?.type==='VariableDeclaration'?node.declarations.map(binding=>binding.init):[node]).find(node=>node?.type==='ClassDeclaration'||node?.type==='ClassExpression');
  const actual=find(source),expected=find(paired);assert.ok(actual&&expected);
  assert.equal(actual.superClass,null); // Runtime name is checked separately.
  const imports=text=>new Map(parseJavaScript(text).body.filter(node=>node.type==='ImportDeclaration').flatMap(node=>node.specifiers.map(binding=>[binding.local.name,binding.imported?.name??binding.local.name])));
  const methods=actual.body.body.filter(node=>node.type==='MethodDefinition'&&node.kind!=='constructor');
  const original=expected.body.body.filter(node=>node.type==='MethodDefinition');
  assert.deepEqual(methods.map(node=>node.key.name),config.methods.map(([name])=>name));
  for(const method of methods) {
    const counterpart=original.find(node=>node.key.name===method.key.name);assert.ok(counterpart);
    const parameters=node=>new Map(node.params.map((parameter,index)=>{assert.equal(parameter.type,'Identifier');return [parameter.name,'argument'+index];}));
    assert.deepEqual(clean(method.value.body,parameters(method.value)),clean(counterpart.value.body,parameters(counterpart.value)));
  }
  const constructor=actual.body.body.find(node=>node.kind==='constructor');assert.ok(constructor);
  assert.equal(constructor.value.params.length,0);assert.equal(constructor.value.body.body.length,1);
  const assignment=constructor.value.body.body[0].expression;
  assert.equal(assignment.type,'AssignmentExpression');assert.equal(assignment.operator,'=');
  assert.deepEqual(clean(assignment.left),{type:'MemberExpression',object:{type:'ThisExpression'},property:{type:'Identifier',name:config.field},computed:false,optional:false});
  assert.equal(assignment.right.type,'NewExpression');assert.equal(imports(source).get(assignment.right.callee.name),config.model);
  assert.equal(assignment.right.arguments.length,0);
  const field=expected.body.body.find(node=>node.type==='PropertyDefinition'&&node.key.name===config.field);assert.ok(field);
  assert.deepEqual(clean(assignment.right,imports(source)),clean(field.value,imports(paired)));
}

async function exercise(sources,config={}) {
  const trace=new GraphTrace(config),context=vm.createContext({WidthBreakpoint:{SM:'sm',MD:'md',LG:'lg',XL:'xl'}});
  const modules=new Map(Object.entries(sources).map(([name,source])=>[name,new vm.SourceTextModule(source,{context,identifier:name})]));
  // The package barrel is a controlled forwarding boundary. Breakpoint's real
  // constructor and all ten supplied record bodies remain in the module graph.
  const barrel=new vm.SourceTextModule('export {BreakpointType} from "breakpoint";',{context});
  const linker=name=> {
    if(name==='breakpoint')return modules.get('Breakpoint');
    if(name==='@normalized:N&&&commons/Index&1.0.0')return barrel;
    const match=[...modules.keys()].find(key=>name===specifier('model/'+key)||name===specifier('constants/'+key));
    assert.ok(match,name);return modules.get(match);
  };
  await barrel.link(linker);
  for(const module of modules.values())if(module.status==='unlinked')await module.link(linker);
  for(const view of views)assert.throws(()=>modules.get(view.record).namespace[view.name],{name:'ReferenceError'});
  for(const module of modules.values())await module.evaluate();
  const exports=trace.exports(modules),constants=modules.get('HomeConstants').namespace.HomeConstants;
  const partial=[],instances=[];
  for(const view of views) {
    const Model=modules.get(view.model).namespace[view.model],original=Model.prototype.initData;
    Object.defineProperty(Model.prototype,'initData',{configurable:true,get(){
      if(!partial.includes(this)){trace.tag(this,'partial'+partial.length);partial.push(this);}
      trace.event(view.model+'.init:get');return original;
    }});
  }
  for(const key of Reflect.ownKeys(constants)) {
    const descriptor=Object.getOwnPropertyDescriptor(constants,key);
    if(!Array.isArray(descriptor.value))continue;
    const array=descriptor.value;
    if(config.mode===Mode.sparse&&array.length>1)delete array[1];
    const observed=new Proxy(array,{get(target,name,receiver){
      if(name==='map'||name==='length'||/^(0|[1-9][0-9]*)$/.test(String(name)))trace.event('array:get',key,name);
      return Reflect.get(target,name,receiver);
    },has(target,name){if(/^(0|[1-9][0-9]*)$/.test(String(name)))trace.event('array:has',key,name);return Reflect.has(target,name);}});
    trace.tag(observed,'array:'+key);
    Object.defineProperty(constants,key,{configurable:true,get(){trace.event('constants:get',key);return observed;}});
  }
  const count=config.count===Count.coercible?trace.tag({[Symbol.toPrimitive](hint){trace.event('count:coerce',hint);return 2;}},'count'):config.count;
  for(const view of views) {
    const C=modules.get(view.record).namespace[view.name],Model=modules.get(view.model).namespace[view.model];
    assert.equal(C.name,view.name);assert.equal(C.length,0);assert.equal(Object.hasOwn(C,'prototype'),true);
    assert.deepEqual(Reflect.ownKeys(C.prototype),['constructor',...view.methods.map(([name])=>name)]);
    assert.throws(()=>Reflect.apply(C,undefined,[]),{name:'TypeError'});
    const own=[];
    for(let i=0;i<2;++i) {
      let instance;
      try{instance=new C();}catch(error){if(error instanceof assert.AssertionError)throw error;assert.ok(Object.is(error,trace.failure));trace.trace.push(['construct.throw',trace.outcome(error)]);continue;}
      trace.tag(instance,view.name+i);own.push(instance);instances.push(instance);
      assert.equal(Object.getPrototypeOf(instance),C.prototype);
      assert.deepEqual(Reflect.ownKeys(instance),[view.field]);assert.ok(instance[view.field] instanceof Model);
      const descriptor=Object.getOwnPropertyDescriptor(instance,view.field);
      assert.deepEqual([descriptor.writable,descriptor.enumerable,descriptor.configurable],[true,true,true]);
      for(const [name,sliced,field] of view.methods) {
        const method=Object.getOwnPropertyDescriptor(C.prototype,name);
        assert.deepEqual([method.writable,method.enumerable,method.configurable],[true,false,true]);
        assert.equal(method.value.name,name);assert.equal(method.value.length,sliced?1:0);
        const returned=trace.call(view.name+'.'+name,method.value,instance,[count]);
        if(returned!==undefined) {
          assert.ok(Array.isArray(returned));
          if(sliced)assert.notEqual(returned,instance[view.field][field]);else assert.equal(returned,instance[view.field][field]);
        }
      }
    }
    if(own.length===2){assert.notEqual(own[0][view.field],own[1][view.field]);for(const [, ,field] of view.methods)assert.notEqual(own[0][view.field][field],own[1][view.field][field]);}
    await modules.get(view.record).evaluate();assert.equal(modules.get(view.record).namespace[view.name],C);
  }
  if(!config.failAt&&!['symbol','bigint'].includes(typeof count))assert.equal(trace.errors.length,0);
  const observed={status:'evaluated',namespace:Object.fromEntries([...partial,...instances].map((value,index)=>['instance'+index,value]))};
  const state=trace.exports(new Map([...modules,['observed',observed]]));
  return {exports,trace:trace.trace,state,events:trace.events,failed:trace.failed};
}

async function assignment(source,view,kind,failAt, failure) {
  // Pinned Ark instance initialization is PutValue. Native JS class fields use
  // DefineField, so inherited-setter probes have an independent assignment
  // expectation; they never rewrite the paired body to obtain a pass.
  const context=vm.createContext({}),foreign=vm.createContext({}),events=[],models=[];
  let dependency,receiver,stored,at=0,raised=false;
  const event=name=>{events.push(name);if(++at===failAt)throw failure;};
  const peer=label=>{
    let constructor;
    constructor=new Proxy(function(){assert.equal(arguments.length,0);assert.equal(new.target,constructor);event(label+':construct');models.push(this);}, {
      construct(target,args,newTarget){assert.equal(newTarget,constructor);assert.equal(args.length,0);event(label+':trap');return Reflect.construct(target,args,newTarget);},
      get(target,key,actual){if(key==='prototype'){event(label+':prototype');dependency.setExport(view.model,next);}return Reflect.get(target,key,actual);},
    });return constructor;
  };
  const initial=peer('initial'),next=peer('next');
  dependency=new vm.SyntheticModule([view.model],function(){this.setExport(view.model,initial);},{context});
  const module=new vm.SourceTextModule(source,{context});
  await module.link(name=>{assert.equal(name,specifier('model/'+view.model));return dependency;});await module.evaluate();
  const C=module.namespace[view.name],fallback=kind===Prototype.fallback||kind===Prototype.foreignFallback;
  const targetContext=kind===Prototype.foreignFallback?foreign:context;
  const prototype=fallback?vm.runInContext('Object.prototype',targetContext):kind===Prototype.class?C.prototype:{};
  Object.defineProperty(prototype,view.field,{configurable:true,set(value){receiver=this;assert.equal(value,models.at(-1));event('store');stored=value;}});
  const Target=vm.runInContext('(function Target(){})',targetContext);
  const target=new Proxy(Target,{get(object,key,actual){if(key==='prototype'){event('target:prototype');return fallback?undefined:prototype;}return Reflect.get(object,key,actual);}});
  let first;
  try{first=Reflect.construct(C,Array.from({length:201},()=>0),target);}catch(error){if(error instanceof assert.AssertionError)throw error;raised=true;assert.ok(Object.is(error,failure));}
  if(!raised){
    assert.equal(Object.getPrototypeOf(first),prototype);assert.equal(receiver,first);assert.equal(stored,models[0]);
    assert.equal(Object.hasOwn(first,view.field),false);
    const second=Reflect.construct(C,[],target);assert.notEqual(first,second);assert.notEqual(models[0],models[1]);assert.equal(stored,models[1]);
    assert.ok(events.includes('initial:construct'));assert.ok(events.includes('next:construct'));
  }
  if(failAt!==undefined)assert.equal(raised,true);
  assert.deepEqual(events,raised?firstConstruction.slice(0,failAt):[...firstConstruction,'target:prototype','next:trap','next:prototype','next:construct','store']);
  return events;
}

export async function verifyShoppingViewModels(sources,reference) {
  for(const view of views)style(sources[view.record],reference[view.record],view);
  let cases=0,faults=0,mutations=0,assignments=0,equivalents=0;
  for(const mode of [Mode.ordinary,Mode.sparse])for(const count of [undefined,null,-0,-1,2,201,NaN,Infinity,1n,Symbol.for('count'),Count.coercible]) {
    assert.deepEqual(await exercise(sources,{mode,count}),await exercise(reference,{mode,count}));++cases;
  }
  const baseline=await exercise(reference,{mode:Mode.ordinary,count:Count.coercible});
  for(let failAt=1;failAt<=baseline.events;++failAt)for(const failure of [undefined,null,0,Symbol.for('throw'),Object.freeze({})]) {
    const config={mode:Mode.ordinary,count:Count.coercible,failAt,failure};
    const actual=await exercise(sources,config),expected=await exercise(reference,config);
    assert.equal(actual.failed,true);assert.deepEqual(actual,expected);++faults;
  }
  for(const view of views)for(const kind of Object.values(Prototype)) {
    await assignment(sources[view.record],view,kind);++assignments;
    for(let failAt=1;failAt<=firstConstruction.length;++failAt)for(const failure of [undefined,null,0,Symbol.for('throw'),Object.freeze({})]) {
      await assignment(sources[view.record],view,kind,failAt,failure);++assignments;
    }
  }
  const changes=[
    ['HomeViewModel',source=>source.replace('new HomeModel()','HomeModel()')],
    ['HomeViewModel',source=>source.replace('this.homeModel =','this.otherModel =')],
    ['IconInfoViewModel',source=>source.replace('.slice(0, count)','.slice(0, void 0)')],
    ['RecommendViewModel',source=>source.replace('this.model.getRecommendProductList()','(0, this.model.getRecommendProductList)()')],
    ['SectionViewModel',source=>source.replace('.getSectionProductList().slice','.getFlashSaleProductList().slice')],
    ['SectionViewModel',source=>source.replace('new SectionModel()','new RecommendModel()')],
  ];
  for(const [record,change] of changes) {
    const changed={...sources,[record]:change(sources[record])};assert.notEqual(changed[record],sources[record]);parseJavaScript(changed[record]);
    await assert.rejects(async()=>assert.deepEqual(await exercise(changed,{count:2}),await exercise(reference,{count:2})),assert.AssertionError);++mutations;
  }
  for(const view of [views[0],views[2]]) {
    const source=sources[view.record],original=`this.${view.field} = new ${view.model}()`;
    const changed=source.replace(original,`Object.defineProperty(this, "${view.field}", {value: new ${view.model}(), writable: true, enumerable: true, configurable: true})`);
    assert.notEqual(changed,source);parseJavaScript(changed);
    await assert.rejects(()=>assignment(changed,view,Prototype.foreign),assert.AssertionError);++mutations;
  }
  // Acceptance is about native representation, not exact spelling. Independently
  // valid alpha renaming and a let/class expression must pass the SAME style and
  // full connected behavior checks, rather than weakening negative mutations.
  for(const view of views) {
    const source=sources[view.record],ast=parseJavaScript(source),declaration=ast.body.find(node=>node.type==='ClassDeclaration');
    const imported=ast.body.find(node=>node.type==='ImportDeclaration').specifiers[0];
    const constructor=declaration.body.body.find(node=>node.kind==='constructor'),callee=constructor.value.body.body[0].expression.right.callee;
    const body=source.slice(declaration.body.start,declaration.body.end).replace(/\bcount\b/g,'limit');
    const renamedBody=body.slice(0,callee.start-declaration.body.start)+'Peer'+body.slice(callee.end-declaration.body.start);
    let changed=source.slice(0,declaration.start)+`let ${view.name} = class `+renamedBody+';'+source.slice(declaration.end);
    changed=changed.slice(0,imported.start)+`${imported.imported.name} as Peer`+changed.slice(imported.end);
    parseJavaScript(changed);style(changed,reference[view.record],view);
    assert.deepEqual(await exercise({...sources,[view.record]:changed},{count:2}),await exercise(reference,{count:2}));++equivalents;
  }
  return {viewRecords:4,actualRecords:Object.keys(sources).length,cases,faults,assignments,mutations,equivalents,events:baseline.events,platformExecution:false};
}
