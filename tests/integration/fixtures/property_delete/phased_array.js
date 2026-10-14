export function run(object, key) {
  let state = 1;
  try { const result = delete object[key]; state = 2; return [result, state]; }
  catch (error) { return [error.name, state]; }
}
