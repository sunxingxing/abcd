import assert from 'node:assert/strict';
import {parseJavaScript} from './class_reference.mjs';
import {nodes} from './record_style.mjs';

const name = node => node.name ?? node.value;
const sorted = rows => rows.sort((a, b) => JSON.stringify(a).localeCompare(JSON.stringify(b)));

// Test-only module interface oracle. Compare actual request order, imported
// members and export binding origins, independently of private local spelling.
// It does not prove initializer/body behavior; connected execution does that.
export function moduleSurface(source) {
  const ast = parseJavaScript(source), requests = [], requested = new Set();
  const bindings = new Map(), origins = new Map(), declarations = [], imports = [], exports = [], stars = [];
  const request = specifier => {
    if (!requested.has(specifier)) {requested.add(specifier); requests.push(specifier);}
  };
  const bind = (id, value) => {
    assert.equal(id.type, 'Identifier', 'module oracle needs destructuring support');
    assert.ok(!bindings.has(id.name), 'duplicate module binding');
    bindings.set(id.name, value);
  };
  for (const statement of ast.body) {
    if (statement.source) request(statement.source.value);
    if (statement.type === 'ImportDeclaration') {
      for (const specifier of statement.specifiers) {
        const member = specifier.type === 'ImportNamespaceSpecifier' ? {namespace: true}
          : {member: specifier.type === 'ImportDefaultSpecifier' ? 'default' : name(specifier.imported)};
        const origin = {import: statement.source.value, ...member};
        origins.set(specifier.local.name, origin); imports.push(origin);
      }
    }
    const node = ['ExportNamedDeclaration', 'ExportDefaultDeclaration'].includes(statement.type)
      ? statement.declaration : statement;
    if (node?.type === 'VariableDeclaration') for (const declaration of node.declarations) bind(declaration.id, declaration.init);
    else if (['ClassDeclaration', 'FunctionDeclaration'].includes(node?.type)) {
      if (node.id) bind(node.id, node);
      declarations.push(node);
    }
    const assignment = node?.type === 'ExpressionStatement' && node.expression;
    if (assignment?.type === 'AssignmentExpression' && assignment.operator === '=' && assignment.left.type === 'Identifier') {
      const local = assignment.left.name;
      // Follow a split initial definition only. Later writes change the value,
      // not the exported cell's identity; execution tests own those semantics.
      if (bindings.has(local) && bindings.get(local) === null) bindings.set(local, assignment.right);
    }
  }
  const identities = new Map(), serials = {class: 0, function: 0, value: 0};
  const classes = nodes(ast).filter(node => ['ClassDeclaration', 'ClassExpression'].includes(node.type));
  const definitions = [...new Set([...classes, ...declarations, ...bindings.values()].filter(Boolean))].sort((a, b) => a.start - b.start);
  for (const node of definitions) {
    const kind = ['ClassDeclaration', 'ClassExpression'].includes(node.type) ? 'class'
      : ['FunctionDeclaration', 'FunctionExpression', 'ArrowFunctionExpression'].includes(node.type) ? 'function' : 'value';
    identities.set(node, {kind, index: serials[kind]++});
  }
  const publicLocals = new Set();
  for (const statement of ast.body) if (statement.type === 'ExportNamedDeclaration' && !statement.source) {
    for (const specifier of statement.specifiers) publicLocals.add(name(specifier.local));
    if (statement.declaration?.type === 'VariableDeclaration')
      for (const declaration of statement.declaration.declarations) publicLocals.add(declaration.id.name);
    else if (statement.declaration?.id) publicLocals.add(statement.declaration.id.name);
  }
  const initialValues = new Map();
  const initialValue = local => {
    const seen = new Set();
    let result;
    while (bindings.has(local) && !initialValues.has(local)) {
      assert.ok(!seen.has(local), 'cyclic module initializer aliases'); seen.add(local);
      const value = bindings.get(local);
      if (value?.type !== 'Identifier' || !bindings.has(value.name)) {result = value; break;}
      local = value.name;
    }
    if (initialValues.has(local)) result = initialValues.get(local);
    for (const visited of seen) initialValues.set(visited, result);
    return result;
  };
  let valueCell = 0;
  for (const local of bindings.keys()) {
    const value = initialValue(local);
    if (publicLocals.has(local) && (!value || identities.get(value)?.kind === 'value'))
      origins.set(local, {kind: 'binding', index: valueCell++});
  }
  const origin = local => {
    if (origins.has(local)) return origins.get(local);
    assert.ok(bindings.has(local), 'unresolved exported binding ' + local);
    const value = initialValue(local);
    assert.ok(value && identities.has(value), 'uninitialized exported binding needs an execution oracle');
    return identities.get(value);
  };
  function expression(node) {
    if (node.type === 'Identifier') return origin(node.name);
    if (identities.has(node)) return identities.get(node);
    if (node.type === 'Literal') return {literal: node.value};
    if (node.type === 'NewExpression') return {construct: expression(node.callee), arguments: node.arguments.map(expression)};
    assert.fail('unsupported default-export expression ' + node.type);
  }
  for (const statement of ast.body) {
    if (statement.type === 'ExportAllDeclaration') {
      if (statement.exported) exports.push([name(statement.exported), {live: {import: statement.source.value, namespace: true}}]);
      else stars.push(statement.source.value);
    } else if (statement.type === 'ExportNamedDeclaration') {
      const node = statement.declaration;
      if (node?.type === 'VariableDeclaration') for (const declaration of node.declarations)
        exports.push([declaration.id.name, {live: origin(declaration.id.name)}]);
      else if (node) exports.push([node.id.name, {live: origin(node.id.name)}]);
      for (const specifier of statement.specifiers) exports.push([name(specifier.exported), {live: statement.source
        ? {import: statement.source.value, member: name(specifier.local)} : origin(name(specifier.local))}]);
    } else if (statement.type === 'ExportDefaultDeclaration') {
      const node = statement.declaration;
      exports.push(['default', ['ClassDeclaration', 'FunctionDeclaration'].includes(node.type)
        ? {live: identities.get(node)} : {snapshot: expression(node)}]);
    }
  }
  // Two public names can alias one live local cell. Two copies initialized
  // from the same class/function are distinct cells even if values coincide.
  const localExports = new Map();
  const exportedLocal = (local, exported) => {
    if (origins.get(local)?.import) return; // re-exported imported cell already has external identity
    if (!localExports.has(local)) localExports.set(local, []);
    localExports.get(local).push(exported);
  };
  for (const statement of ast.body) {
    if (statement.type === 'ExportNamedDeclaration' && !statement.source) {
      for (const specifier of statement.specifiers) exportedLocal(name(specifier.local), name(specifier.exported));
      if (statement.declaration?.type === 'VariableDeclaration')
        for (const declaration of statement.declaration.declarations) exportedLocal(declaration.id.name, declaration.id.name);
      else if (statement.declaration?.id) exportedLocal(statement.declaration.id.name, statement.declaration.id.name);
    } else if (statement.type === 'ExportDefaultDeclaration' &&
      ['ClassDeclaration', 'FunctionDeclaration'].includes(statement.declaration.type))
      exportedLocal(statement.declaration.id?.name, 'default');
  }
  const dependencies = [...imports, ...exports.map(([, value]) => value.live).filter(value => value?.import !== undefined)];
  const uniqueDependencies = new Map(dependencies.map(value => [JSON.stringify(value), value]));
  return {requests, imports: sorted([...uniqueDependencies.values()]), exports: sorted(exports), stars,
    localCells: sorted([...localExports.values()].map(aliases => aliases.sort()))};
}

export function verifyModuleSurface(source, reference, label) {
  assert.deepEqual(moduleSurface(source), moduleSurface(reference), label);
}
