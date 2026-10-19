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
const {checkValueCallRemovals: calls}=await import('../tools/grouping_tokens.mjs');
assert.equal(calls('(0, f)(x);','f(x);'),1);
assert.equal(calls('(0, renamed)((f = other));','renamed((f = other));'),1);
for(const [a,b] of [
 ['(0, eval)(x);','eval(x);'],['(0, obj.f)(x);','obj.f(x);'],
 ['new (0, f)();','new f();'],['(0, f)?.();','f?.();'],
 ['(0, f)`x`;','f`x`;'],['typeof (0, f);','typeof f;'],
 ['let x=(0, function(){});','let x=function(){};'],
 ['(0, f)(x);','f(y);'],['(0, f)(x);','f(\nx);']])assert.throws(()=>calls(a,b));
const moduleAlias='import {f as eval} from "x";(0, eval)();';
assert.throws(()=>calls(moduleAlias,moduleAlias.replace('(0, eval)','eval')));
console.log('value-call token checker: remove and forbidden-context mutations passed');
const {checkBraceRemovals: braces}=await import('../tools/grouping_tokens.mjs');
assert.equal(braces('function f(){ { x(); } }','function f(){ x(); }'),1);
assert.equal(braces('if(a){}else{if(b){}else{c();}}','if(a){}else if(b){}else{c();}'),1);
assert.equal(braces('function f(){ { { x(); } } }','function f(){ x(); }'),2);
for(const [a,b] of [
 ['function f(){{let x=1;x;}}','function f(){let x=1;x;}'],
 ['function f(){{const x=1;x;}}','function f(){const x=1;x;}'],
 ['function f(){{class X{}}}','function f(){class X{}}'],
 ['function f(){{function g(){}}}','function f(){function g(){}}'],
 ['function f(){{"use strict";x();}}','function f(){"use strict";x();}'],
 ['label:{x();}','label:x();'], ['while(a){x();}','while(a)x();'],
 ['if(a){if(b){} }else {}','if(a)if(b){}else {}'],
 ['try{x();}catch(e){y();}','try{x();}catch(e)y();'],
 ['function f(){{/*keep*/x();}}','function f(){/*keep*/x();}']])assert.throws(()=>braces(a,b));
console.log('brace token checker: pair, scope, directive, dangling-else and required-body checks passed');
