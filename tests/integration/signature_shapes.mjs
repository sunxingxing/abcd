import assert from 'node:assert/strict';
import {moduleShape, nodes, clean, parameter} from './record_style.mjs';

function normalize(node, names) {
  if (Array.isArray(node)) return node.map(value => normalize(value, names));
  if (node === null || typeof node !== 'object') return node;
  if (node.type === 'Identifier') {
    if (names.has(node.name)) return names.get(node.name);
    if (node.name === 'undefined') return {type: 'UndefinedValue'};
  }
  if (node.type === 'UnaryExpression' && node.operator === 'void' && node.argument.type === 'Literal' && node.argument.value === 0)
    return {type: 'UndefinedValue'};
  // Defaults in this fixed inventory are expressions without nested binding
  // scopes. Do not silently alpha-rename across a scope the oracle cannot model.
  assert.ok(!['FunctionExpression', 'ArrowFunctionExpression', 'ClassExpression'].includes(node.type),
    'nested default binding scopes need an independent oracle');
  if (node.type === 'MemberExpression' && !node.computed)
    return {type: node.type, object: normalize(node.object, names), property: clean(node.property), optional: node.optional};
  if (node.type === 'Property' && !node.computed)
    return {type: node.type, key: clean(node.key), value: normalize(node.value, names), kind: node.kind, method: node.method};
  return Object.fromEntries(Object.entries(node).filter(([key]) => !['start', 'end', 'raw', 'loc'].includes(key))
    .map(([key, value]) => [key, normalize(value, names)]));
}

// Independent representation oracle, not a recovery rule. Names may change,
// but positional bindings, default expressions, rest, and callable kinds may
// not. Body/closure semantics require the separate execution oracles.
function signature(fn, imports, bindings) {
  const names = new Map([...imports].map(([name, identity]) => [name, {type: 'ImportReference', identity}]));
  for (const name of bindings.keys()) names.set(name, {type: 'ModuleReference', name});
  for (const [index, param] of fn.params.entries()) {
    const binding = parameter(param);
    assert.equal(binding.type, 'Identifier', 'extend the oracle for destructuring');
    names.set(binding.name, {type: 'ParameterReference', index});
  }
  return {async: fn.async, generator: fn.generator, params: normalize(fn.params, names)};
}

export function signatureShapes(source) {
  const shape = moduleShape(source);
  // A module binding may be initialized by a later assignment, as happens for
  // mutually referring helpers. Follow AST ownership, never printed spelling.
  for (const statement of shape.ast.body) {
    const expression = statement.type === 'ExpressionStatement' && statement.expression;
    if (expression?.type === 'AssignmentExpression' && expression.operator === '=' && expression.left.type === 'Identifier')
      shape.bindings.set(expression.left.name, expression.right);
  }
  const classes = nodes(shape.ast).filter(node => ['ClassDeclaration', 'ClassExpression'].includes(node.type))
    .sort((a, b) => a.start - b.start).map(definition => {
      const constructor = definition.body.body.find(method => method.kind === 'constructor');
      // Omitted base/derived constructors have () / (...args) signatures.
      // This normalization proves no claim about the explicit constructor body.
      const implicit = definition.superClass ? [{type: 'RestElement', argument: {type: 'ParameterReference', index: 0}}] : [];
      return {derived: definition.superClass !== null,
        constructor: constructor ? signature(constructor.value, shape.imports, shape.bindings).params : implicit,
        methods: definition.body.body.filter(method => method.type === 'MethodDefinition' && method.kind !== 'constructor')
          .map(method => ({kind: method.kind, static: method.static, computed: method.computed,
            key: method.computed ? clean(method.key) : String(method.key.name ?? method.key.value),
            ...signature(method.value, shape.imports, shape.bindings)}))};
    });
  const functions = [...shape.bindings].filter(([, value]) => ['FunctionDeclaration', 'FunctionExpression', 'ArrowFunctionExpression'].includes(value?.type))
    .sort((a, b) => a[1].start - b[1].start)
    .map(([name, value]) => ({exports: [...shape.exported].filter(([, local]) => local === name).map(([name]) => name).sort(),
      ...signature(value, shape.imports, shape.bindings)}));
  return {classes, functions};
}

export function verifySignatures(source, reference, label) {
  assert.deepEqual(signatureShapes(source), signatureShapes(reference), label);
}
