export function makePrefix(state) { return function(sink) { return sink.m(++state.p); }; }
export function makePostfix(state) { return function(sink) { return sink.m(state.p++); }; }
export function makeCompound(state) { return function(sink, k, g) { return sink.m(state[k()] += g()); }; }
export function makeMultiply(state) { return function(sink) { return sink.m(state.a.b *= 2); }; }
export function makeCaptured(state) { return () => ++state.count; }
export function prefix(f, o) { return f(++o.p); }
export function postfix(f, o) { return f(o.p++); }
export function compound(f, o, k, g) { return f(o[k()] += g()); }
export function statement(o) { o.p -= 1; return o.p; }
export function multiply(o) { let x; x = o.a.b *= 2; return x; }
export function makeMutable(state) { return [sink => sink.m(++state.p), next => state = next]; }
export function makeDecrement(state) { return function(sink) { return sink.m(state.p--); }; }
export function makeSubtract(state) { return function(sink, k, g) { return sink.m(state.p -= g()); }; }
