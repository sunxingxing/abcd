export function finallyRun(outer, choice, hooks) {
  let result;
  try {
    result = hooks.body(outer, choice);
  } finally {
    hooks.finally();
  }
  return result;
}
