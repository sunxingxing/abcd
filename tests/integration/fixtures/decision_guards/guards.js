export function make(initial) {
  let limits = initial;
  function replace(value) { limits = value; }
  function run(target) {
    try {
      if (target.state.valueOf() !== limits.left &&
          target.state.valueOf() !== limits.right) {
        globalThis.trace.push('miss');
        return 'miss';
      }
      globalThis.trace.push('hit');
      return 'hit';
    } catch (error) {
      globalThis.trace.push(error.constructor.name);
      return 'caught';
    } finally {
      globalThis.trace.push('finally');
    }
  }
  return {run, replace};
}
