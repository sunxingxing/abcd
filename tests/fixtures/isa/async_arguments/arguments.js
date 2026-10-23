// `arguments` in async functions: GETUNMAPPEDARGS precedes ASYNCFUNCTIONENTER.
export async function forward(o) {
  return await o.call(1, arguments);
}
export function factory(o) {
  return async function () {
    await o.m(arguments.length, arguments[0]);
    return arguments[arguments.length - 1];
  };
}
