import assert from 'node:assert/strict';
import {checkGroupingTokens, inferGroupingRemovals} from '../tools/grouping_tokens.mjs';
const old = 'f(({p: "((中文))", r: /[(]/, t: `(()${x}` }));';
const open=old.indexOf('(')+1, close=old.lastIndexOf(')')-1;
const next=old.slice(0,open)+old.slice(open+1,close)+old.slice(close+1);
assert.equal(checkGroupingTokens(old,next,[[open,close]]).removedPairs,1);
assert.throws(()=>checkGroupingTokens(old,next.replace('中文','文中'),[[open,close]]));
assert.throws(()=>checkGroupingTokens(old,next,[]));
assert.throws(()=>checkGroupingTokens(old,next,[[open,close],[open,close]]));
assert.throws(()=>checkGroupingTokens('f((x));','f(y);',[[2,4]]));
assert.throws(()=>checkGroupingTokens('f((x));','f(x);',[[1,5]])); // call delimiters are not the planned pair
assert.throws(()=>checkGroupingTokens('/*a*/ f(x);','f(/*a*/ x);',[]));
assert.throws(()=>checkGroupingTokens('function f(){return\n(x);}', 'function f(){return x;}', [[20,22]]));
assert.equal(inferGroupingRemovals('f(((x)));','f(x);').length,2);
assert.throws(()=>inferGroupingRemovals('(0, eval)(x);','eval(x);'));
assert.throws(()=>inferGroupingRemovals('(0, obj.f)(x);','obj.f(x);'));
assert.throws(()=>inferGroupingRemovals('f(x);','f((x));'));
assert.throws(()=>inferGroupingRemovals('(x + y) * z;','x + (y * z);'));
console.log('grouping token checker: 13 checks passed');
