export function passthrough(value) {
  try { return value; } catch (error) { return error; }
}
export function split(value, flag) {
  try { if (flag) return value; return flag; }
  catch (error) { return error; }
}
export function active(input) {
  try { return input.value; } catch (error) { return error.name; }
}
