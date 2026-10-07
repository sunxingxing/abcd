import assert from 'node:assert/strict';
import {classLayouts, parseJavaScript} from './class_reference.mjs';
import {memberName} from './expression_edits.mjs';

const children = node => Object.values(node).flatMap(value => Array.isArray(value)
  ? value.filter(child => child?.type) : value?.type ? [value] : []);
function nodes(root) {
  const result=[], pending=[root];
  while(pending.length){const node=pending.pop();result.push(node);pending.push(...children(node));}
  return result;
}
const parameter = node => node.type==='AssignmentPattern'?node.left:node.type==='RestElement'?node.argument:node;
function moduleShape(source) {
  const ast=parseJavaScript(source), bindings=new Map(), imports=new Map(), exported=new Map();
  for(const statement of ast.body) {
    if(statement.type==='ImportDeclaration')for(const specifier of statement.specifiers)
      imports.set(specifier.local.name,statement.source.value+'#'+(specifier.imported?.name??'default'));
    const node=statement.type==='ExportNamedDeclaration'?statement.declaration:statement;
    if(node?.type==='VariableDeclaration')for(const binding of node.declarations)bindings.set(binding.id.name,binding.init);
    else if(node?.id)bindings.set(node.id.name,node);
    if(statement.type==='ExportNamedDeclaration') {
      if(node?.id)exported.set(node.id.name,node.id.name);
      for(const specifier of statement.specifiers)exported.set(specifier.exported.name??specifier.exported.value,specifier.local.name??specifier.local.value);
    }
  }
  return {ast,bindings,imports,exported};
}
function clean(node,names=new Map()) {
  if(Array.isArray(node))return node.map(value=>clean(value,names));
  if(node===null||typeof node!=='object')return node;
  if((node.type==='Identifier'&&node.name==='undefined') || (node.type==='UnaryExpression'&&node.operator==='void'&&node.argument.type==='Literal'&&node.argument.value===0))
    return {type:'UndefinedValue'};
  if(node.type==='MemberExpression')return {type:node.type,object:clean(node.object,names),
    key:node.computed&&node.property.type!=='Literal'?clean(node.property,names):memberName(node),optional:node.optional};
  return Object.fromEntries(Object.entries(node).filter(([key])=>!['start','end','raw','loc'].includes(key)).map(([key,value])=>
    [key,node.type==='Identifier'&&key==='name'?(names.get(value)??value):clean(value,names)]));
}
function profile(root,imports) {
  const all=nodes(root), count=type=>all.filter(node=>node.type===type).length;
  return {
    calls:all.filter(node=>node.type==='CallExpression'&&memberName(node.callee)!==undefined).map(node=>memberName(node.callee)).sort(),
    constructions:all.filter(node=>node.type==='NewExpression').map(node=>clean(node.callee,imports)).sort((a,b)=>JSON.stringify(a).localeCompare(JSON.stringify(b))),
    awaits:count('AwaitExpression'),async:all.filter(node=>node.async).length,
    arrows:count('ArrowFunctionExpression'),
    // A native page must not be flattened into a dispatcher or interpreter.
    dispatchers:all.filter(node=>['SwitchStatement','WhileStatement','DoWhileStatement','ForStatement','ForInStatement','ForOfStatement'].includes(node.type)).length,
  };
}
// Test-only acceptance for this paired record, not general recovery admission.
// Call inventories supplement the runtime oracle; they do not prove ordering
// or arbitrary behavioral equivalence. Runtime behavior is checked separately.
// Normalize import grouping, parameter spelling, undefined syntax and static
// member access; permit native class expressions and necessary receiver aliases.
export function pageAStyle(source,paired) {
  const actual=moduleShape(source),expected=moduleShape(paired);
  assert.deepEqual([...actual.imports.values()].sort(),[...expected.imports.values()].sort());
  assert.deepEqual([...actual.exported.keys()].sort(),[...expected.exported.keys()].sort());
  assert.deepEqual(classLayouts(source),classLayouts(paired));
  const actualClass=actual.bindings.get(actual.exported.get('PageA'));
  const expectedClass=expected.bindings.get(expected.exported.get('PageA'));
  assert.ok(['ClassDeclaration','ClassExpression'].includes(actualClass?.type));
  assert.deepEqual(clean(actualClass.superClass,actual.imports),clean(expectedClass.superClass,expected.imports));
  for(const method of actualClass.body.body) {
    assert.equal(method.type,'MethodDefinition');
    const counterpart=expectedClass.body.body.find(other=>other.kind===method.kind&&other.key.name===method.key.name);
    assert.ok(counterpart);
    const names=(value,imports)=>new Map([...imports,...value.params.map((param,index)=>[parameter(param).name,'argument'+index])]);
    assert.deepEqual(clean(method.value.params,names(method.value,actual.imports)),clean(counterpart.value.params,names(counterpart.value,expected.imports)));
    if(method.key.name==='initialRender')assert.deepEqual(profile(method.value.body,actual.imports),profile(counterpart.value.body,expected.imports));
    else assert.deepEqual(clean(method.value.body,names(method.value,actual.imports)),clean(counterpart.value.body,names(counterpart.value,expected.imports)));
  }
  const builder=actual.bindings.get(actual.exported.get('PageABuilder'));
  assert.ok(['FunctionDeclaration','FunctionExpression'].includes(builder?.type));
  assert.equal(builder.params.length,1);assert.equal(builder.params[0].type,'AssignmentPattern');
  assert.equal(builder.params[0].right.value,null);
  assert.deepEqual(profile(actual.ast,actual.imports),profile(expected.ast,expected.imports));
  return actualClass;
}

export function pageAEquivalents(source) {
  const shape=moduleShape(source),definition=shape.bindings.get(shape.exported.get('PageA'));
  const constructor=definition.body.body.find(node=>node.kind==='constructor');
  const names=new Map(constructor.value.params.map((param,index)=>[parameter(param).name,'argument'+index]));
  const edits=[];
  function rename(node,parent,key) {
    if(node.type==='Identifier'&&names.has(node.name)&&!(parent?.type==='MemberExpression'&&key==='property'&&!parent.computed))
      edits.push([node.start,node.end,names.get(node.name)]);
    for(const [key,value] of Object.entries(node)) {
      if(Array.isArray(value)) {
        for(const child of value)if(child?.type)rename(child,node,key);
      } else if(value?.type)rename(value,node,key);
    }
  }
  rename(constructor.value);let renamed=source;
  for(const [begin,end,value] of edits.sort((a,b)=>b[0]-a[0]))renamed=renamed.slice(0,begin)+value+renamed.slice(end);
  const expression=source.slice(0,definition.start)+'let PageA = class '+source.slice(definition.id.end,definition.end)+';'+source.slice(definition.end);
  return [renamed,expression];
}
