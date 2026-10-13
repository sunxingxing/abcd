export function read() {
  let state = 1;
  try {
    state = 7;
    const result = value;
    state = 9;
    globalThis.trace.push(state);
    return result;
  } catch (error) {
    globalThis.trace.push(state);
    return error.constructor.name;
  } finally {
    globalThis.trace.push('finally');
  }
}
export const early = read();
let value = 42;
export const late = read();
