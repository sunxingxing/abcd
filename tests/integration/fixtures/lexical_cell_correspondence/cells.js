export function make(seed) {
  function write(rhs) {
    function descendant() { return extra; }
    state = rhs();
    return descendant;
  }
  function read() { return state; }
  let extra = seed.extra;
  let state = seed.initial;
  return {write, read};
}
export function permuted(seed) {
  function read() { return state; }
  function write(rhs) {
    function second() { return other; }
    function first() { return extra; }
    state = rhs();
    return [first, second];
  }
  let state = seed.initial;
  let other = seed.other;
  let extra = seed.extra;
  return {write, read};
}
export function early(seed) {
  function write(rhs) {
    function descendant() { return extra; }
    state = rhs();
    return descendant;
  }
  seed.before(write);
  let extra = seed.extra;
  let state = seed.initial;
  return write;
}
