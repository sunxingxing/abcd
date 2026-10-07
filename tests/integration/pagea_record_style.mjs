import assert from 'node:assert/strict';
import {classLayouts} from './class_reference.mjs';
import {moduleShape,clean,profile,parameter,renameConstructorParameters} from './record_style.mjs';

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
  const renamed=renameConstructorParameters(source,definition);
  const expression=source.slice(0,definition.start)+'let PageA = class '+source.slice(definition.id.end,definition.end)+';'+source.slice(definition.end);
  return [renamed,expression];
}
