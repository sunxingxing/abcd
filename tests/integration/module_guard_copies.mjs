import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
// Differential oracle for module reads copied to a register before their
// Hole guard (`ldlocalmodulevar; sta vN; throw.undefinedifholewithname`) in
// functions created after the binding is initialized.
const [originalPath, emittedPath] = process.argv.slice(2);
let fresh = 0;
const load = path => import('data:text/javascript;base64,' +
    Buffer.from(`${readFileSync(path, 'utf8')}\n// ${++fresh}`).toString('base64'));
const observe = async path => {
  const module = await load(path);
  const made = module.make();
  const passed = module.pass();
  return {made: [made.constructor === module.X, made.a, made.b], call: module.call(3),
          pass: [passed[0] === module.X, passed[1] === module.helper]};
};
const [original, emitted] = [await observe(originalPath), await observe(emittedPath)];
assert.deepEqual(emitted, original);
console.log(JSON.stringify({cases: 3}));
