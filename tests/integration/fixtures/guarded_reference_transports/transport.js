export function run(flag) {
  try {
    return globalThis.receiver.method(typeof (1 + (flag ? globalThis.left : globalThis.right)));
  } catch (error) {
    globalThis.trace.push(['catch', error.constructor.name]);
    return 'caught';
  }
}
export function pure() {
  try {
    return globalThis.receiver.method(typeof provider);
  } catch (error) {
    globalThis.trace.push(['purecatch', error.constructor.name]);
    return error.constructor.name;
  }
}
export const early = pure();
let provider = globalThis.provider;
