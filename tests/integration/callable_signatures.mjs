import assert from 'node:assert/strict';
import {moduleShape, clean, parameter} from './record_style.mjs';
import {memberName} from './expression_edits.mjs';

const isFunction = node => ['FunctionDeclaration', 'FunctionExpression', 'ArrowFunctionExpression'].includes(node.type);
const isClass = node => ['ClassDeclaration', 'ClassExpression'].includes(node.type);
const children = node => Object.values(node).flatMap(value => Array.isArray(value)
  ? value.filter(child => child?.type) : value?.type ? [value] : []);
const ordered = values => values.sort((a, b) => JSON.stringify(a).localeCompare(JSON.stringify(b)));

// Independent signature oracle. Lexical scopes are built before resolving reads;
// parameter defaults cannot see body declarations. Immutable capture copies are
// traced to stable values; placement/initialization timing stays a body obligation.
// This is not a body-equivalence checker or a production recovery analysis.
export function callableSignatures(source) {
  const shape = moduleShape(source), scopes = new WeakMap(), functionScopes = new WeakMap();
  const assignments = [], classes = [];
  const variableOwners = new WeakMap();
  let dynamicScope = false;
  const root = {parent: null, definitions: new Map(), depth: 0, level: 0};
  const scope = (parent, callable = false) => ({parent, definitions: new Map(),
    depth: parent.depth + Number(callable), level: callable ? 0 : parent.level + 1});
  const define = (owner, id, record) => {
    assert.equal(id.type, 'Identifier', 'destructured capture needs an independent binding oracle');
    assert.ok(!owner.definitions.has(id.name), 'duplicate binding needs declaration merging');
    owner.definitions.set(id.name, {...record, name: id.name, owner, writes: 0});
  };
  const visit = (node, parent) => {
    let owner = parent;
    if (isFunction(node)) {
      if (node.type === 'FunctionDeclaration' && node.id) define(parent, node.id, {kind: 'function', init: node});
      owner = scope(parent, true); functionScopes.set(node, owner);
      if (node.type === 'FunctionExpression' && node.id) define(owner, node.id, {kind: 'self'});
      for (const [index, param] of node.params.entries()) define(owner, parameter(param), {kind: 'parameter', index});
    } else if (node.type === 'BlockStatement' || node.type === 'CatchClause' ||
        ['ForStatement', 'ForOfStatement', 'ForInStatement'].includes(node.type)) {
      owner = scope(parent);
      if (node.type === 'CatchClause' && node.param) define(owner, node.param, {kind: 'catch'});
    } else if (node.type === 'ClassExpression') {
      owner = scope(parent);
    }
    scopes.set(node, owner);
    if (isClass(node)) {
      classes.push(node);
      if (node.id) define(owner, node.id, {kind: 'class'});
    }
    if (node.type === 'CallExpression' && node.callee.type === 'Identifier' && node.callee.name === 'eval') dynamicScope = true;
    if (node.type === 'VariableDeclaration') {
      let target = owner;
      if (node.kind === 'var') while (target.parent && (target.depth === 0 || target.level > 1)) target = target.parent;
      for (const declaration of node.declarations) variableOwners.set(declaration, target);
    }
    if (node.type === 'VariableDeclarator') define(variableOwners.get(node) ?? owner, node.id, {kind: 'local', init: node.init});
    if (node.type === 'AssignmentExpression' && node.left.type === 'Identifier') assignments.push([owner, node.left.name]);
    if (node.type === 'UpdateExpression' && node.argument.type === 'Identifier') assignments.push([owner, node.argument.name]);
    for (const child of children(node)) visit(child, owner);
  };
  visit(shape.ast, root);
  const resolve = (owner, name) => {
    for (; owner; owner = owner.parent) if (owner.definitions.has(name)) return owner.definitions.get(name);
  };
  for (const [owner, name] of assignments) {const binding = resolve(owner, name); if (binding) ++binding.writes;}
  const normalize = (node, current, active = new Set()) => {
    if (Array.isArray(node)) return node.map(value => normalize(value, current, active));
    if (node === null || typeof node !== 'object') return node;
    if (node.type === 'Identifier') {
      const binding = resolve(scopes.get(node), node.name);
      if (binding?.kind === 'parameter') {
        if (active.size) assert.equal(binding.writes, 0, 'captured parameter snapshot needs temporal evidence');
        return {type: 'ParameterReference', depth: current.depth - binding.owner.depth, index: binding.index};
      }
      if (active.size && (binding?.owner === root || !binding && node.name !== 'undefined'))
        assert.fail('captured global/module snapshot needs temporal evidence');
      if (shape.imports.has(node.name) && !binding) return {type: 'ImportReference', identity: shape.imports.get(node.name)};
      if (binding?.owner === root || !binding && shape.bindings.has(node.name)) return {type: 'ModuleReference', name: node.name};
      if (!binding && node.name === 'undefined') return {type: 'UndefinedValue'};
      if (!binding) return clean(node);
      assert.equal(binding.kind, 'local', 'unsupported captured default binding');
      assert.equal(dynamicScope, false, 'eval capture requires execution evidence');
      assert.equal(binding.writes, 0, 'mutable captured default requires execution evidence');
      assert.ok(binding.init && !active.has(binding), 'uninitialized/cyclic captured default');
      assert.ok(['Identifier', 'Literal'].includes(binding.init.type), 'effectful captured initializer requires execution evidence');
      const next = new Set(active); next.add(binding);
      const initial = normalize(binding.init, current, next);
      return {type: 'CapturedLocal', initial: initial.type === 'CapturedLocal' ? initial.initial : initial};
    }
    if (node.type === 'UnaryExpression' && node.operator === 'void' && node.argument.type === 'Literal' && node.argument.value === 0)
      return {type: 'UndefinedValue'};
    assert.ok(!isFunction(node) && !isClass(node), 'nested default binding scopes need a separate oracle');
    if (node.type === 'MemberExpression' && !node.computed)
      return {type: node.type, object: normalize(node.object, current, active), property: clean(node.property), optional: node.optional};
    if (node.type === 'Property' && !node.computed)
      return {type: node.type, key: clean(node.key), value: normalize(node.value, current, active), kind: node.kind, method: node.method};
    return Object.fromEntries(Object.entries(node).filter(([key]) => !['start', 'end', 'raw', 'loc'].includes(key))
      .map(([key, value]) => [key, normalize(value, current, active)]));
  };
  const collect = node => {
    if (!node || isClass(node)) return [];
    if (isFunction(node)) return [{placement: placements.get(node) ?? null, kind: node.type === 'ArrowFunctionExpression' ? 'arrow' : 'function',
      async: node.async, generator: node.generator,
      params: normalize(node.params, functionScopes.get(node)), children: ordered(collect(node.body))}];
    return children(node).flatMap(collect);
  };
  const placements = new WeakMap();
  const receiverIsThis = (node, active = new Set()) => {
    if (node.type === 'ThisExpression') return true;
    if (node.type !== 'Identifier') return false;
    const binding = resolve(scopes.get(node), node.name);
    if (!binding?.init || binding.writes || active.has(binding)) return false;
    const next = new Set(active); next.add(binding);
    return receiverIsThis(binding.init, next);
  };
  const place = node => {
    if (node.type === 'PropertyDefinition' && node.value && isFunction(node.value))
      placements.set(node.value, {field: node.computed ? clean(node.key) : String(node.key.name ?? node.key.value), static: node.static});
    if (node.type === 'AssignmentExpression' && isFunction(node.right) &&
        node.left.type === 'MemberExpression' && receiverIsThis(node.left.object))
      placements.set(node.right, {field: memberName(node.left) ?? clean(node.left.property), static: false});
    if (node.type === 'CallExpression' && memberName(node.callee) !== undefined)
      for (const [argument, value] of node.arguments.entries()) if (isFunction(value))
        placements.set(value, {call: memberName(node.callee), argument});
    for (const child of children(node)) place(child);
  };
  place(shape.ast);
  return {module: ordered(collect(shape.ast)), classes: classes.sort((a, b) => a.start - b.start).map(definition => ({
    // Initialized field arrows may be lowered into the native constructor.
    // The body and assignment-vs-DefineField contract remain independently tested.
    initialization: ordered(definition.body.body.flatMap(member => member.kind === 'constructor'
      ? collect(member.value.body) : member.type === 'PropertyDefinition' ? collect(member.value) : [])),
    methods: definition.body.body.filter(member => member.type === 'MethodDefinition' && member.kind !== 'constructor')
      .map(member => ({kind: member.kind, static: member.static, key: member.computed ? clean(member.key) : String(member.key.name ?? member.key.value),
        children: ordered(collect(member.value.body))})),
  }))};
}

export function verifyCallableSignatures(source, reference, label) {
  assert.deepEqual(callableSignatures(source), callableSignatures(reference), label);
}
