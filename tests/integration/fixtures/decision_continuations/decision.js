export function run(outer, choice, hooks) {
  if (outer) {
    if (hooks.first() && hooks.second()) {
      if (choice) hooks.left();
      else hooks.right();
      hooks.terminal();
      return 'early';
    }
    hooks.continuing();
  }
  hooks.tail();
  return 'tail';
}
export function protectedRun(outer, choice, hooks) {
  try {
    if (outer) {
      if (hooks.first() && hooks.second()) {
        if (choice) hooks.left();
        else hooks.right();
        hooks.terminal();
        return 'early';
      }
      hooks.continuing();
    }
    hooks.tail();
    return 'tail';
  } catch (error) {
    hooks.caught(error.name);
    return 'caught';
  }
}
export async function asyncRun(outer, choice, hooks) {
  if (outer) {
    if (hooks.first() && hooks.second()) {
      if (choice) hooks.left();
      else hooks.right();
      hooks.terminal();
      return 'early';
    }
    hooks.continuing();
  }
  hooks.tail();
  return 'tail';
}
