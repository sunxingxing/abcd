import assert from 'node:assert/strict';
import {createRequire, stripTypeScriptTypes} from 'node:module';

// Test-only reference compiler adapter, never an engine input. Node's vendored
// Acorn 8.15.0 (MIT) supplies class ownership and exact UTF-16 source ranges;
// --expose-internals is required, and this dependency identity is pinned.
// The paired compiler erases ordinary uninitialized instance declarations.
// Preserve initializers, computed-key effects, static fields and private brands.
const require = createRequire(import.meta.url);
const acorn = require('internal/deps/acorn/acorn/dist/acorn');
assert.equal(acorn.version, '8.15.0');

export const parseJavaScript = source => acorn.parse(source, {ecmaVersion: 'latest', sourceType: 'module'});

// Compare high-level native class representation separately from execution.
// Constructor assignment lowering is allowed; methods keep order, kind,
// async/generator and formal/default/rest structure without spelling locals.
export function classLayouts(source) {
  const work = [parseJavaScript(source)], classes = [];
  while (work.length) {
    const node = work.pop();
    if (node.type === 'ClassDeclaration' || node.type === 'ClassExpression') classes.push(node);
    for (const value of Object.values(node)) {
      if (Array.isArray(value)) {
        for (const child of value) if (child && typeof child.type === 'string') work.push(child);
      } else if (value && typeof value.type === 'string') work.push(value);
    }
  }
  return classes.sort((a, b) => a.start - b.start).map(node => [node.superClass !== null,
    node.body.body.filter(method => method.type === 'MethodDefinition' && method.kind !== 'constructor')
      .map(method => [method.kind, method.static, method.computed, method.key.name ?? method.key.value,
        method.value.async, method.value.generator, method.value.params.map(param => param.type)])]);
}

export function classReference(text) {
  const source = stripTypeScriptTypes(text, {mode: 'transform'});
  const root = parseJavaScript(source);
  const work = [root], erased = [];
  while (work.length) {
    const node = work.pop();
    if (node.type === 'PropertyDefinition' && node.value === null &&
        !node.static && !node.computed && node.key.type !== 'PrivateIdentifier')
      erased.push([node.start, node.end]);
    for (const value of Object.values(node)) {
      if (Array.isArray(value)) {
        for (const child of value) if (child && typeof child === 'object' && typeof child.type === 'string') work.push(child);
      } else if (value && typeof value === 'object' && typeof value.type === 'string') work.push(value);
    }
  }
  erased.sort((left, right) => left[0] - right[0]);
  const parts = []; let first = 0;
  for (const [begin, end] of erased) {
    assert.ok(first <= begin && begin < end && end <= source.length);
    parts.push(source.slice(first, begin)); first = end;
  }
  parts.push(source.slice(first));
  return {source: parts.join(''), erased: erased.length};
}
