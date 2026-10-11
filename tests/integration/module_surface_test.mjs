import assert from 'node:assert/strict';
import {verifyModuleSurface} from './module_surface.mjs';

const original = `import D, {x as local} from 'dep'; import * as ns from 'namespace';
  export class First {} class Second {} export {Second as second};
  export {local as value}; export {x as forwarded} from 'dep'; export * from 'star';
  export default new First();`;
const renamed = `import Renamed from 'dep'; import {x as renamed} from 'dep'; import * as n from 'namespace';
  class A {} class B {} export {A as First, B as second, renamed as value};
  export {x as forwarded} from 'dep'; export * from 'star'; export default new A();`;
verifyModuleSurface(renamed, original);
verifyModuleSurface('class Renamed {} export {Renamed as default};', 'export default class Original {}');
verifyModuleSurface('let f; f = function(x) {return x;}; export {f};', 'export function f(x) {return x;}');
verifyModuleSurface('export default new (class A {});', 'export default new (class B {});');
verifyModuleSurface('const internal = class A {}; let publicName = internal; export {publicName as X};',
  'export class X {}');
verifyModuleSurface("export * as ns from 'dep';", "import * as namespace from 'dep'; export {namespace as ns};");
verifyModuleSurface("export {x as value} from 'dep';", "import {x} from 'dep'; export {x as value};");
let rejected = 0;
for (const mutant of [
  original.replace("'dep'", "'wrong'"),
  original.replace('import * as ns', 'import ns'),
  original.replace('{x as local}', '{y as local}'),
  original.replace('Second as second', 'First as second'),
  original.replace('local as value', 'D as value'),
  original.replace('x as forwarded', 'y as forwarded'),
  original.replace("export * from 'star';", ''),
  original.replace('new First()', 'new Second()'),
]) {
  assert.throws(() => verifyModuleSurface(mutant, original), {name: 'AssertionError'}); ++rejected;
}
assert.throws(() => verifyModuleSurface('let x = 1; export {x as default};',
  'let x = 1; export default x;'), {name: 'AssertionError'}); ++rejected;
assert.throws(() => verifyModuleSurface("import 'b'; import 'a';", "import 'a'; import 'b';"), {name: 'AssertionError'}); ++rejected;
assert.throws(() => verifyModuleSurface('class A {} let B = A; export {A as x, B as y};',
  'class A {} export {A as x, A as y};'), {name: 'AssertionError'}); ++rejected;
// Surface equality deliberately does not certify the value/effect of decoration.
verifyModuleSurface('class A {} A = decorate(A); export {A};', 'export class A {}');
const execute = source => import('data:text/javascript,' + encodeURIComponent(source));
const live = await execute('let x = 1; export {x as default}; export function change() {x = 2;}');
const snapshot = await execute('let x = 1; export default x; export function change() {x = 2;}');
live.change(); snapshot.change(); assert.equal(live.default, 2); assert.equal(snapshot.default, 1);
const shared = await execute('class A {} export {A as x, A as y}; export function change() {A = class {}; }');
const copied = await execute('class A {} let B = A; export {A as x, B as y}; export function change() {A = class {}; }');
assert.equal(shared.x, shared.y); assert.equal(copied.x, copied.y);
shared.change(); copied.change(); assert.equal(shared.x, shared.y); assert.notEqual(copied.x, copied.y);
console.log(JSON.stringify({rejected, alphaRenaming: true, splitImports: true, liveDefaultDistinct: true}));
