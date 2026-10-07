import {parseJavaScript} from './class_reference.mjs';
import {memberName} from './expression_edits.mjs';

const children = node => Object.values(node).flatMap(value => Array.isArray(value)
  ? value.filter(child => child?.type) : value?.type ? [value] : []);
export function nodes(root) {
  const result=[], pending=[root];
  while(pending.length){const node=pending.pop();result.push(node);pending.push(...children(node));}
  return result;
}
export const parameter = node => node.type==='AssignmentPattern'?node.left:node.type==='RestElement'?node.argument:node;
export function moduleShape(source) {
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
export function clean(node,names=new Map()) {
  if(Array.isArray(node))return node.map(value=>clean(value,names));
  if(node===null||typeof node!=='object')return node;
  if((node.type==='Identifier'&&node.name==='undefined') || (node.type==='UnaryExpression'&&node.operator==='void'&&node.argument.type==='Literal'&&node.argument.value===0))
    return {type:'UndefinedValue'};
  if(node.type==='MemberExpression')return {type:node.type,object:clean(node.object,names),
    key:node.computed&&node.property.type!=='Literal'?clean(node.property,names):memberName(node),optional:node.optional};
  return Object.fromEntries(Object.entries(node).filter(([key])=>!['start','end','raw','loc'].includes(key)).map(([key,value])=>
    [key,node.type==='Identifier'&&key==='name'?(names.get(value)??value):clean(value,names)]));
}
export function profile(root,imports) {
  const all=nodes(root), count=type=>all.filter(node=>node.type===type).length;
  return {
    calls:all.filter(node=>node.type==='CallExpression'&&memberName(node.callee)!==undefined).map(node=>memberName(node.callee)).sort(),
    constructions:all.filter(node=>node.type==='NewExpression').map(node=>clean(node.callee,imports)).sort((a,b)=>JSON.stringify(a).localeCompare(JSON.stringify(b))),
    writes:all.filter(node=>node.type==='AssignmentExpression'&&memberName(node.left)!==undefined).map(node=>memberName(node.left)).sort(),
    awaits:count('AwaitExpression'),async:all.filter(node=>node.async).length,
    arrows:count('ArrowFunctionExpression'),
    // A native page must not be flattened into a dispatcher or interpreter.
    dispatchers:all.filter(node=>['SwitchStatement','WhileStatement','DoWhileStatement','ForStatement','ForInStatement','ForOfStatement'].includes(node.type)).length,
  };
}
// Equivalence fixture for these paired constructors. The complete behavior
// matrix is rerun after renaming; this is not a general-purpose JS refactoring.
export function renameConstructorParameters(source,definition) {
  const constructor=definition.body.body.find(node=>node.kind==='constructor');
  const names=new Map(constructor.value.params.map((param,index)=>[parameter(param).name,'argument'+index]));
  const edits=[];
  function rename(node,parent,key) {
    const memberKey=parent?.type==='MemberExpression'&&key==='property'&&!parent.computed;
    const propertyKey=['Property','MethodDefinition','PropertyDefinition'].includes(parent?.type)&&key==='key'&&!parent.computed;
    if(node.type==='Identifier'&&names.has(node.name)&&!memberKey&&!propertyKey) {
      const name=names.get(node.name);
      edits.push([node.start,node.end,parent?.type==='Property'&&parent.shorthand&&key==='value'?node.name+': '+name:name]);
    }
    for(const [key,value] of Object.entries(node)) {
      if(Array.isArray(value)) {
        for(const child of value)if(child?.type)rename(child,node,key);
      } else if(value?.type)rename(value,node,key);
    }
  }
  rename(constructor.value);let renamed=source;
  for(const [begin,end,value] of edits.sort((a,b)=>b[0]-a[0]))renamed=renamed.slice(0,begin)+value+renamed.slice(end);
  return renamed;
}
