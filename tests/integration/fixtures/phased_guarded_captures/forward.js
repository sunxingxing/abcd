export function make() {
  try {
    return function read() {
      try { return value; }
      catch (error) { globalThis.trace.push('caught'); return error.constructor.name; }
    };
  } catch (error) { return function fail() { return 0; }; }
}
export const early = make()();
let value = 42;
export const late = make()();
