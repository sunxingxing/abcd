export function makeCaught(state) { return function(sink) { try { return sink.m(++state.p); } catch (error) { return error.name; } }; }
export function makeCaughtCompound(state) { return function(sink, k, g) { try { return sink.m(state[k()] += g()); } catch (error) { return error.name; } }; }
