export function read() {
  try { value = 7; return value; }
  catch (error) { return error.constructor.name; }
}
export const early = read();
let value = 42;
export const late = read();
