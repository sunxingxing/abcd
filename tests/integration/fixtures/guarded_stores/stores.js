export function makeWriter() {
  return function write(next) {
    try {
      state = globalThis.prepare(next);
      return state;
    } catch (error) {
      globalThis.trace.push(['catch', error.constructor.name]);
      return error.constructor.name;
    }
  };
}
export function observe() {
  try {
    return state !== undefined && state.tag;
  } catch (error) {
    return error.constructor.name;
  }
}
const writer = makeWriter();
export const early = writer({tag: 'early'});
export const earlyRead = observe();
let state = {tag: 'initial'};
export const late = writer({tag: 'late'});
export { writer };
