export function run() {
  let saved = globalThis.state;
  try {
    return globalThis.receiver.method(globalThis.before(), value, globalThis.after());
  } catch (error) {
    globalThis.trace.push(saved, error.constructor.name);
    return 'caught';
  } finally {
    globalThis.trace.push('finally');
  }
}
export const early = run();
let value = 42;
export const late = run();
