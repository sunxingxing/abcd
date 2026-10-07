import assert from 'node:assert/strict';
import {classLayouts} from './class_reference.mjs';
import {moduleShape,clean,profile,parameter,nodes,renameConstructorParameters} from './record_style.mjs';

// Paired-record representation acceptance, independent of the runtime oracle.
// Complex callbacks retain native calls, constructions, field writes and arrows;
// their inventories do not prove ordering or arbitrary semantic equivalence.
export function homeStyle(source,paired) {
  const actual=moduleShape(source),expected=moduleShape(paired);
  assert.deepEqual([...actual.imports.values()].sort(),[...expected.imports.values()].sort());
  assert.deepEqual([...actual.exported.keys()].sort(),[...expected.exported.keys()].sort());
  assert.deepEqual(classLayouts(source),classLayouts(paired));
  const definition=actual.bindings.get('Index'),reference=expected.bindings.get('Index');
  assert.ok(['ClassDeclaration','ClassExpression'].includes(definition?.type));
  assert.deepEqual(clean(definition.superClass,actual.imports),clean(reference.superClass,expected.imports));
  for(const method of definition.body.body) {
    const counterpart=reference.body.body.find(other=>other.kind===method.kind&&other.key.name===method.key.name);
    assert.ok(counterpart);
    const names=(value,imports)=>new Map([...imports,...value.params.map((param,index)=>[parameter(param).name,'argument'+index])]);
    assert.deepEqual(clean(method.value.params,names(method.value,actual.imports)),clean(counterpart.value.params,names(counterpart.value,expected.imports)));
    assert.deepEqual(profile(method.value.body,actual.imports),profile(counterpart.value.body,expected.imports));
    const complex=nodes(counterpart.value.body).some(node=>['VariableDeclaration','ArrowFunctionExpression','FunctionExpression'].includes(node.type));
    if(method.kind!=='constructor'&&!complex)
      assert.deepEqual(clean(method.value.body,names(method.value,actual.imports)),clean(counterpart.value.body,names(counterpart.value,expected.imports)));
  }
  assert.deepEqual(profile(actual.ast,actual.imports),profile(expected.ast,expected.imports));
  return definition;
}

export function homeEquivalents(source) {
  const shape=moduleShape(source),definition=shape.bindings.get('Index');
  const declaration=shape.ast.body.find(node=>node.type==='VariableDeclaration'&&node.declarations.some(binding=>binding.init===definition));
  assert.equal(declaration.declarations.length,1);assert.equal(definition.type,'ClassExpression');
  assert.equal(definition.id,null);
  const nativeClass=source.slice(0,declaration.start)+'class Index'+source.slice(definition.start+'class'.length,definition.end)+source.slice(declaration.end);
  return [renameConstructorParameters(source,definition),nativeClass];
}
