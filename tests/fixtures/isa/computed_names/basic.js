export function make(k, trace) {
  return {before: trace.first, [k]: function () {return 17;}, after: trace.last};
}
export function arrow(k) {return {[k]: () => 23};}
