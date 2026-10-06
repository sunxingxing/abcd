import assert from 'node:assert/strict';
import {parseJavaScript} from './class_reference.mjs';

const isFunction = node => ['FunctionDeclaration', 'FunctionExpression', 'ArrowFunctionExpression'].includes(node.type);
const children = node => Object.values(node).flatMap(value => Array.isArray(value)
  ? value.filter(child => child?.type) : value?.type ? [value] : []);
export const memberName = node => node?.type === 'MemberExpression'
  ? node.computed ? node.property.value : node.property.name : undefined;

// Test-only mutation selection: follow unique expression definitions within the
// owning function, so eliminating or renaming private snapshots does not change
// the behavioral oracle. Never resolve ambiguous writes or nested function bodies.
// Generated source alone supplies these ranges; paired source is not consulted.
export function editExpression(source, select, edit, {first = false} = {}) {
  const entries = [], pending = [{node: parseJavaScript(source), scope: {definitions: new Map()}}];
  while (pending.length) {
    const {node, scope: parent} = pending.pop();
    const scope = isFunction(node) ? {definitions: new Map()} : parent;
    entries.push({node, scope});
    let name, expression;
    if (node.type === 'AssignmentExpression' && node.left.type === 'Identifier') {
      name = node.left.name; expression = node.operator === '=' ? node.right : null;
    } else if (node.type === 'VariableDeclarator' && node.id.type === 'Identifier' && node.init) {
      name = node.id.name; expression = node.init;
    } else if (node.type === 'UpdateExpression' && node.argument.type === 'Identifier') {
      name = node.argument.name; expression = null;
    }
    if (name) scope.definitions.set(name, scope.definitions.has(name) ? null : expression);
    for (const child of children(node)) pending.push({node: child, scope});
  }
  const dependsOn = (node, scope, predicate) => {
    const work = [node], seen = new Set();
    while (work.length) {
      const expression = work.pop();
      if (!expression || seen.has(expression)) continue;
      seen.add(expression);
      if (predicate(expression)) return true;
      if (isFunction(expression)) continue;
      if (expression.type === 'Identifier') {
        const definition = scope.definitions.get(expression.name);
        if (definition) work.push(definition);
      } else if (expression.type === 'MemberExpression') {
        work.push(expression.object);
        if (expression.computed) work.push(expression.property);
      } else if (expression.type === 'Property') {
        work.push(expression.value);
        if (expression.computed) work.push(expression.key);
      } else work.push(...children(expression));
    }
    return false;
  };
  const matches = entries.filter(({node, scope}) => select(node,
    (expression, predicate) => dependsOn(expression, scope, predicate)));
  matches.sort((a, b) => a.node.start - b.node.start);
  assert.ok(matches.length > 0, 'mutation expression exists');
  if (!first) assert.equal(matches.length, 1, 'mutation expression is unambiguous');
  const node = matches[0].node, text = expression => source.slice(expression.start, expression.end);
  const changed = source.slice(0, node.start) + edit(node, text) + source.slice(node.end);
  parseJavaScript(changed);
  return changed;
}

export const editAwait = (source, match, edit) => editExpression(source,
  (node, dependsOn) => node.type === 'AwaitExpression' && dependsOn(node.argument, match), edit);
export const callsMember = (node, name) => node.type === 'CallExpression' && memberName(node.callee) === name;
