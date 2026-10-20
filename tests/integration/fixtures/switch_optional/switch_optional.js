export function dispatch(kind, state, hooks) {
  for (let i = 0; i < hooks.count; i++) hooks.item(i);
  switch (kind) {
    case 0:
      state.value?.pause();
      break;
    case 1:
      state.value?.resume();
      break;
    default:
      hooks.other();
      break;
  }
  hooks.end();
}
export function chained(kind, state, hooks) {
  switch (kind) {
    case 0: state.value?.pause(); break;
    case 1: state.value?.resume(); break;
    case 2: state.value?.pause(); break;
    default: hooks.other();
  }
  return hooks.end();
}
